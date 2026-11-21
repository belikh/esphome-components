#include "gaitek.h"

#include <cmath>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace gaitek {

static const char *const TAG = "gaitek";

static const uint8_t MIN_TEMPERATURE = 16;
static const uint8_t MAX_TEMPERATURE = 31;
static const uint8_t MIN_HUMIDITY = 30;
static const uint8_t MAX_HUMIDITY = 90;
static const uint8_t MAX_TIMER_HOURS = 24;

static const uint32_t TX_GAP_MS = 120;           // quiet time between two commands
static const uint32_t KEEPALIVE_MS = 110 * 1000;  // send a keepalive after this long idle
static const uint8_t KEEPALIVE_VALUE = 0x01;

// Wire command for each Slot, in Slot order.
static const uint8_t SLOT_OP[] = {
    frame::OP_POWER, frame::OP_MODE,  frame::OP_FAN,   frame::OP_TARGET_TEMPERATURE,
    frame::OP_TARGET_HUMIDITY, frame::OP_SWING, frame::OP_TIMER,
};

struct ModeMap {
  uint8_t wire;
  climate::ClimateMode mode;
};
static const ModeMap MODES[] = {
    {frame::MODE_COOL, climate::CLIMATE_MODE_COOL},
    {frame::MODE_DRY, climate::CLIMATE_MODE_DRY},
    {frame::MODE_FAN, climate::CLIMATE_MODE_FAN_ONLY},
};

struct FanMap {
  uint8_t wire;
  climate::ClimateFanMode fan;
};
static const FanMap FANS[] = {
    {frame::FAN_LOW, climate::CLIMATE_FAN_LOW},
    {frame::FAN_MEDIUM, climate::CLIMATE_FAN_MEDIUM},
    {frame::FAN_HIGH, climate::CLIMATE_FAN_HIGH},
};

static uint8_t clamp_round(float value, uint8_t low, uint8_t high) {
  if (!(value > low))  // also catches NaN
    return low;
  if (value > high)
    return high;
  return static_cast<uint8_t>(lroundf(value));
}

void GaitekTimer::control(float value) { this->parent_->write_timer(clamp_round(value, 0, MAX_TIMER_HOURS)); }

void GaitekAC::setup() { this->last_tx_ = millis(); }

void GaitekAC::dump_config() {
  LOG_CLIMATE("", "GAITEK air conditioner", this);
  ESP_LOGCONFIG(TAG, "  Humidity: %s", YESNO(this->humidity_));
  ESP_LOGCONFIG(TAG, "  Status received: %s", YESNO(this->have_status_));
}

climate::ClimateTraits GaitekAC::traits() {
  climate::ClimateTraits traits;
  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY,
  });
  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
  });
  traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_VERTICAL,
  });
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(MIN_TEMPERATURE);
  traits.set_visual_max_temperature(MAX_TEMPERATURE);
  traits.set_visual_target_temperature_step(1.0f);
  traits.set_visual_current_temperature_step(1.0f);
  if (this->humidity_) {
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY |
                             climate::CLIMATE_SUPPORTS_TARGET_HUMIDITY);
    traits.set_visual_min_humidity(MIN_HUMIDITY);
    traits.set_visual_max_humidity(MAX_HUMIDITY);
  }
  return traits;
}

void GaitekAC::loop() {
  uint8_t byte;
  while (this->available() && this->read_byte(&byte))
    this->feed_rx_(byte);
  this->pump_tx_();
}

// ---------------------------------------------------------------- unit -> module

void GaitekAC::feed_rx_(uint8_t byte) {
  // Slide the new byte into the window and test the whole window each time. A frame is
  // found wherever it starts, with no state to get out of step after a line glitch.
  memmove(this->window_, this->window_ + 1, frame::STATUS_LEN - 1);
  this->window_[frame::STATUS_LEN - 1] = byte;

  if (!frame::has_magic(this->window_))
    return;

  frame::Status status;
  if (!frame::decode_status(this->window_, &status)) {
    char hex[format_hex_pretty_size(frame::STATUS_LEN)];
    ESP_LOGW(TAG, "Bad status frame: %s", format_hex_pretty_to(hex, this->window_, frame::STATUS_LEN, ' '));
    return;
  }
  memset(this->window_, 0, frame::STATUS_LEN);

  if (this->have_status_ && status == this->last_)
    return;  // the unit repeats its status every few seconds
  this->last_ = status;
  this->have_status_ = true;
  this->apply_(status);
}

void GaitekAC::apply_(const frame::Status &status) {
  ESP_LOGD(TAG, "Status: power %d, mode %u, fan %u, swing %d, room %u C, target %u C, humidity %u/%u %%, timer %u h",
           status.power, status.mode, status.fan, status.swing, status.room_temperature,
           status.target_temperature, status.room_humidity, status.target_humidity, status.timer_hours);

  if (!status.power) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    bool known = false;
    for (const auto &m : MODES) {
      if (m.wire == status.mode) {
        this->mode = m.mode;
        known = true;
      }
    }
    if (!known) {
      ESP_LOGW(TAG, "Unknown mode %u", status.mode);
      if (this->mode == climate::CLIMATE_MODE_OFF)
        this->mode = climate::CLIMATE_MODE_COOL;  // it is on, so do not show it as off
    }
  }

  for (const auto &f : FANS) {
    if (f.wire == status.fan)
      this->fan_mode = f.fan;
  }

  this->swing_mode = status.swing ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;
  this->current_temperature = status.room_temperature;
  if (status.target_temperature >= MIN_TEMPERATURE && status.target_temperature <= MAX_TEMPERATURE)
    this->target_temperature = status.target_temperature;
  if (this->humidity_) {
    this->current_humidity = status.room_humidity;
    this->target_humidity = status.target_humidity;
  }
  this->publish_state();

  if (this->timer_ != nullptr)
    this->timer_->publish_state(status.timer_hours);
}

// ---------------------------------------------------------------- module -> unit

void GaitekAC::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    const climate::ClimateMode mode = *call.get_mode();
    if (mode == climate::CLIMATE_MODE_OFF) {
      this->want_(SLOT_POWER, 0);
    } else {
      for (const auto &m : MODES) {
        if (m.mode != mode)
          continue;
        if (this->mode == climate::CLIMATE_MODE_OFF)
          this->want_(SLOT_POWER, 1);
        this->want_(SLOT_MODE, m.wire);
      }
    }
  }

  if (call.get_fan_mode().has_value()) {
    for (const auto &f : FANS) {
      if (f.fan == *call.get_fan_mode())
        this->want_(SLOT_FAN, f.wire);
    }
  }

  if (call.get_target_temperature().has_value())
    this->want_(SLOT_TARGET_TEMPERATURE, clamp_round(*call.get_target_temperature(), MIN_TEMPERATURE, MAX_TEMPERATURE));

  if (this->humidity_ && call.get_target_humidity().has_value())
    this->want_(SLOT_TARGET_HUMIDITY, clamp_round(*call.get_target_humidity(), MIN_HUMIDITY, MAX_HUMIDITY));

  if (call.get_swing_mode().has_value())
    this->want_(SLOT_SWING, *call.get_swing_mode() == climate::CLIMATE_SWING_OFF ? 0 : 1);
}

void GaitekAC::write_timer(uint8_t hours) { this->want_(SLOT_TIMER, hours); }

void GaitekAC::want_(Slot slot, uint8_t value) {
  this->wanted_[slot] = value;
  this->dirty_ |= 1 << slot;
}

void GaitekAC::pump_tx_() {
  const uint32_t idle = millis() - this->last_tx_;
  if (idle < TX_GAP_MS)
    return;

  for (uint8_t slot = 0; slot < SLOT_COUNT; slot++) {
    if (this->dirty_ & (1 << slot)) {
      this->dirty_ &= ~(1 << slot);
      this->send_(SLOT_OP[slot], this->wanted_[slot]);
      return;
    }
  }

  if (idle >= KEEPALIVE_MS)
    this->send_(frame::OP_KEEPALIVE, KEEPALIVE_VALUE);
}

void GaitekAC::send_(uint8_t op, uint8_t value) {
  uint8_t buf[frame::COMMAND_LEN];
  frame::encode_command(op, value, buf);
  this->write_array(buf, sizeof(buf));
  this->last_tx_ = millis();
  ESP_LOGD(TAG, "Sent command %02X value %02X", op, value);
}

}  // namespace gaitek
}  // namespace esphome

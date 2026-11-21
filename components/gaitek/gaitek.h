#pragma once

#include <cstdint>

#include "esphome/components/climate/climate.h"
#include "esphome/components/number/number.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "frame.h"

namespace esphome {
namespace gaitek {

class GaitekAC;

// Off timer in hours, 0 = no timer.
class GaitekTimer : public number::Number, public Parented<GaitekAC> {
 protected:
  void control(float value) override;
};

// Climate entity for an air conditioner that speaks GAITEK on its Wi-Fi module port.
//
// The unit is the source of truth. A change asked for by Home Assistant is only written to
// the unit; the entity changes when the unit's next status frame reports it.
class GaitekAC : public climate::Climate, public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_humidity(bool on) { this->humidity_ = on; }
  void set_timer_number(GaitekTimer *timer) { this->timer_ = timer; }
  void write_timer(uint8_t hours);

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  // One slot per setting that can be written. A new value for a setting replaces a value
  // that has not gone out yet, and slots are sent in this order.
  enum Slot : uint8_t {
    SLOT_POWER,
    SLOT_MODE,
    SLOT_FAN,
    SLOT_TARGET_TEMPERATURE,
    SLOT_TARGET_HUMIDITY,
    SLOT_SWING,
    SLOT_TIMER,
    SLOT_COUNT,
  };

  void want_(Slot slot, uint8_t value);
  void send_(uint8_t op, uint8_t value);
  void pump_tx_();
  void feed_rx_(uint8_t byte);
  void apply_(const frame::Status &status);

  uint8_t window_[frame::STATUS_LEN]{};  // the last STATUS_LEN bytes received
  frame::Status last_{};
  bool have_status_{false};

  uint8_t wanted_[SLOT_COUNT]{};
  uint8_t dirty_{0};  // bit n set: wanted_[n] is waiting to be sent
  uint32_t last_tx_{0};

  bool humidity_{false};
  GaitekTimer *timer_{nullptr};
};

}  // namespace gaitek
}  // namespace esphome

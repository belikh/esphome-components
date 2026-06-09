#include "bl0939_xiao.h"
#include "esphome/core/log.h"
#include <cinttypes>

namespace esphome {
namespace bl0939_xiao {

static const char *const TAG = "bl0939_xiao";

void BL0939Xiao::setup() {
  // The XIAO PCB has a 1 kΩ pull-up from SCLK (BL0939 pin 13) to 3V3, which
  // keeps the device address at the default value of 5.  If the user provided
  // the SCLK GPIO we drive it HIGH to reinforce the pull-up and remove any
  // doubt about the pin state.
  if (sclk_pin_ != nullptr) {
    sclk_pin_->setup();
    sclk_pin_->digital_write(true);
  }

  for (const auto &frame : BL0939_INIT) {
    this->write_array(frame, sizeof(frame));
    delay(1);
  }
  this->flush();
}

void BL0939Xiao::update() {
  // Discard any leftover bytes from the previous cycle, then request a new
  // full-register packet from the BL0939.
  this->flush();
  this->write_byte(BL0939_READ_COMMAND);
  this->write_byte(BL0939_FULL_PACKET);
}

void BL0939Xiao::loop() {
  // KEY FIX for ESP32-C6 / esp-idf:
  // The original ESPHome BL0939 component checks available() > 0 and then
  // immediately calls read_array(35).  On Arduino/ESP8266 the UART ISR
  // typically fills the ring buffer faster than loop() runs, so this usually
  // succeeds.  On ESP32-C6 with the esp-idf UART driver the main loop can
  // run between individual byte receipts (at 4800 baud a full 35-byte packet
  // takes ~73 ms), so read_array() returns false after discarding the partial
  // data already buffered.  We wait until all 35 bytes have arrived first.
  if (this->available() < static_cast<int>(sizeof(DataPacket))) {
    return;
  }

  DataPacket buffer;
  if (!this->read_array(reinterpret_cast<uint8_t *>(&buffer), sizeof(buffer))) {
    ESP_LOGW(TAG, "Junk on wire. Throwing away partial message.");
    while (this->available()) {
      this->read();
    }
    return;
  }

  if (!validate_checksum(&buffer)) {
    // Packet is corrupt – drain the buffer so the next update() starts clean.
    while (this->available()) {
      this->read();
    }
    return;
  }

  received_package_(&buffer);
}

bool BL0939Xiao::validate_checksum(const DataPacket *data) {
  uint8_t checksum = BL0939_READ_COMMAND;
  // Sum bytes [0..33], i.e. everything except the final checksum byte.
  for (size_t i = 0; i < sizeof(data->raw) - 1; i++) {
    checksum += data->raw[i];
  }
  checksum ^= 0xFF;
  if (checksum != data->checksum) {
    ESP_LOGW(TAG, "BL0939 invalid checksum: computed 0x%02X, got 0x%02X",
             checksum, data->checksum);
    return false;
  }
  return true;
}

void BL0939Xiao::received_package_(const DataPacket *data) const {
  if (data->frame_header != BL0939_PACKET_HEADER) {
    ESP_LOGI(TAG, "Invalid packet: unexpected header byte 0x%02X", data->frame_header);
    return;
  }

  const float v_rms  = static_cast<float>(to_uint32_t(data->v_rms))  / voltage_reference_;
  const float ia_rms = static_cast<float>(to_uint32_t(data->ia_rms)) / current_reference_;
  const float ib_rms = static_cast<float>(to_uint32_t(data->ib_rms)) / current_reference_;
  const float a_watt = static_cast<float>(to_int32_t(data->a_watt))  / power_reference_;
  const float b_watt = static_cast<float>(to_int32_t(data->b_watt))  / power_reference_;

  const int32_t cfa_cnt = to_int32_t(data->cfa_cnt);
  const int32_t cfb_cnt = to_int32_t(data->cfb_cnt);
  const float a_energy  = static_cast<float>(cfa_cnt) / energy_reference_;
  const float b_energy  = static_cast<float>(cfb_cnt) / energy_reference_;

  ESP_LOGV(TAG,
           "U %.1f V  I1 %.2f A  I2 %.2f A  P1 %.0f W  P2 %.0f W"
           "  CntA %" PRId32 "  CntB %" PRId32 "  E1 %.3f kWh  E2 %.3f kWh",
           v_rms, ia_rms, ib_rms, a_watt, b_watt,
           cfa_cnt, cfb_cnt, a_energy, b_energy);

  if (voltage_sensor_)    voltage_sensor_->publish_state(v_rms);
  if (current_sensor_1_)  current_sensor_1_->publish_state(ia_rms);
  if (current_sensor_2_)  current_sensor_2_->publish_state(ib_rms);
  if (power_sensor_1_)    power_sensor_1_->publish_state(a_watt);
  if (power_sensor_2_)    power_sensor_2_->publish_state(b_watt);
  if (energy_sensor_1_)   energy_sensor_1_->publish_state(a_energy);
  if (energy_sensor_2_)   energy_sensor_2_->publish_state(b_energy);
  if (energy_sensor_sum_) energy_sensor_sum_->publish_state(a_energy + b_energy);
}

void BL0939Xiao::dump_config() {
  ESP_LOGCONFIG(TAG, "BL0939 XIAO:");
  ESP_LOGCONFIG(TAG, "  IREF: %.1f  UREF: %.1f  PREF: %.2f  EREF: %.2f",
                current_reference_, voltage_reference_,
                power_reference_, energy_reference_);
  if (sclk_pin_) {
    LOG_PIN("  SCLK pin: ", sclk_pin_);
  }
  LOG_SENSOR("  ", "Voltage",    voltage_sensor_);
  LOG_SENSOR("  ", "Current 1",  current_sensor_1_);
  LOG_SENSOR("  ", "Current 2",  current_sensor_2_);
  LOG_SENSOR("  ", "Power 1",    power_sensor_1_);
  LOG_SENSOR("  ", "Power 2",    power_sensor_2_);
  LOG_SENSOR("  ", "Energy 1",   energy_sensor_1_);
  LOG_SENSOR("  ", "Energy 2",   energy_sensor_2_);
  LOG_SENSOR("  ", "Energy sum", energy_sensor_sum_);
}

uint32_t BL0939Xiao::to_uint32_t(ube24_t input) {
  return static_cast<uint32_t>(input.h) << 16 |
         static_cast<uint32_t>(input.m) << 8  |
         static_cast<uint32_t>(input.l);
}

int32_t BL0939Xiao::to_int32_t(sbe24_t input) {
  return static_cast<int32_t>(input.h) << 16 |
         static_cast<int32_t>(input.m) << 8  |
         static_cast<int32_t>(input.l);
}

}  // namespace bl0939_xiao
}  // namespace esphome

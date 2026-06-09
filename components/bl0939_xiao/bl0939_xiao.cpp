#include "bl0939_xiao.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cinttypes>
#include <cmath>

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
  if (read_state_ != ReadState::IDLE) {
    ESP_LOGW(TAG, "Previous BL0939 read cycle still in progress, skipping update");
    return;
  }

  // Discard any leftover bytes from the previous cycle, then request a new
  // full-register packet from the BL0939.
  this->flush();
  while (this->available()) {
    this->read();
  }
  this->write_byte(BL0939_READ_COMMAND);
  this->write_byte(BL0939_FULL_PACKET);
  read_state_ = ReadState::WAIT_PACKET;
}

void BL0939Xiao::loop() {
  // KEY FIX for ESP32-C6 / esp-idf:
  // The original ESPHome BL0939 component checks available() > 0 and then
  // immediately calls read_array().  On Arduino/ESP8266 the UART ISR
  // typically fills the ring buffer faster than loop() runs, so this usually
  // succeeds.  On ESP32-C6 with the esp-idf UART driver the main loop can
  // run between individual byte receipts (at 4800 baud a full 35-byte packet
  // takes ~73 ms), so read_array() returns false after discarding the partial
  // data already buffered.  We wait until all expected bytes have arrived
  // before reading.
  switch (read_state_) {
    case ReadState::IDLE:
      return;

    case ReadState::WAIT_PACKET: {
      if (this->available() < static_cast<int>(sizeof(DataPacket))) {
        return;
      }

      DataPacket buffer;
      if (!this->read_array(reinterpret_cast<uint8_t *>(&buffer), sizeof(buffer))) {
        ESP_LOGW(TAG, "Junk on wire. Throwing away partial message.");
        while (this->available()) {
          this->read();
        }
        read_state_ = ReadState::IDLE;
        return;
      }

      if (!validate_checksum(&buffer)) {
        // Packet is corrupt – drain the buffer so the next update() starts clean.
        while (this->available()) {
          this->read();
        }
        read_state_ = ReadState::IDLE;
        return;
      }

      received_package_(&buffer);

      // A_CORNER/B_CORNER aren't part of the full packet; only fetch them
      // if a phase-angle sensor is configured.
      if (phase_angle_sensor_1_ != nullptr || phase_angle_sensor_2_ != nullptr) {
        this->write_byte(BL0939_READ_COMMAND);
        this->write_byte(BL0939_REG_A_CORNER);
        read_state_ = ReadState::WAIT_A_CORNER;
      } else {
        read_state_ = ReadState::IDLE;
      }
      return;
    }

    case ReadState::WAIT_A_CORNER: {
      if (this->available() < 4) {
        return;
      }
      uint16_t a_corner = 0;
      if (read_register_(BL0939_REG_A_CORNER, &a_corner) && phase_angle_sensor_1_ != nullptr) {
        publish_phase_angle_(phase_angle_sensor_1_, a_corner);
      }
      this->write_byte(BL0939_READ_COMMAND);
      this->write_byte(BL0939_REG_B_CORNER);
      read_state_ = ReadState::WAIT_B_CORNER;
      return;
    }

    case ReadState::WAIT_B_CORNER: {
      if (this->available() < 4) {
        return;
      }
      uint16_t b_corner = 0;
      if (read_register_(BL0939_REG_B_CORNER, &b_corner) && phase_angle_sensor_2_ != nullptr) {
        publish_phase_angle_(phase_angle_sensor_2_, b_corner);
      }
      read_state_ = ReadState::IDLE;
      return;
    }
  }
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

  // Fast RMS current (half/full AC cycle response, ~10-40 ms) - same
  // conversion factor as the regular (400 ms averaged) RMS current.
  const float fast_ia = static_cast<float>(to_uint32_t(data->ia_fast_rms)) / current_reference_;
  const float fast_ib = static_cast<float>(to_uint32_t(data->ib_fast_rms)) / current_reference_;

  // Internal chip temperature (TPS1), per datasheet section 2.11:
  //   Tx = (170/448) * (TPS1/2 - 32) - 45
  const float chip_temp = (170.0f / 448.0f) * (static_cast<float>(to_uint16_t(data->tps1)) / 2.0f - 32.0f) - 45.0f;

  // --- Derived quantities ---
  // The BL0939 only directly measures effective (RMS) voltage, effective
  // (RMS) current per channel, and active power per channel. Everything
  // below follows from the power triangle S^2 = P^2 + Q^2:
  //   apparent power   S = V_rms * I_rms
  //   reactive power   Q = sqrt(S^2 - P^2)   (magnitude only - the BL0939
  //                                            gives no phase/quadrant info,
  //                                            so inductive vs. capacitive
  //                                            loads cannot be distinguished)
  //   power factor    PF = |P| / S
  //   active current  Ia = P / V_rms
  //   reactive current Ir = Q / V_rms
  //   apparent current   = I_rms (same value as the "effective current"
  //                                 sensors current_1 / current_2)
  const float s1 = v_rms * ia_rms;
  const float s2 = v_rms * ib_rms;
  const float q1 = std::sqrt(std::max(s1 * s1 - a_watt * a_watt, 0.0f));
  const float q2 = std::sqrt(std::max(s2 * s2 - b_watt * b_watt, 0.0f));
  const float pf1 = (s1 > 0.0f) ? std::min(std::fabs(a_watt) / s1, 1.0f) : 0.0f;
  const float pf2 = (s2 > 0.0f) ? std::min(std::fabs(b_watt) / s2, 1.0f) : 0.0f;
  const float active_i1   = (v_rms > 0.0f) ? a_watt / v_rms : 0.0f;
  const float active_i2   = (v_rms > 0.0f) ? b_watt / v_rms : 0.0f;
  const float reactive_i1 = (v_rms > 0.0f) ? q1 / v_rms : 0.0f;
  const float reactive_i2 = (v_rms > 0.0f) ? q2 / v_rms : 0.0f;

  ESP_LOGV(TAG,
           "U %.1f V  I1 %.2f A  I2 %.2f A  P1 %.0f W  P2 %.0f W"
           "  S1 %.0f VA  S2 %.0f VA  Q1 %.0f var  Q2 %.0f var"
           "  CntA %" PRId32 "  CntB %" PRId32 "  E1 %.3f kWh  E2 %.3f kWh",
           v_rms, ia_rms, ib_rms, a_watt, b_watt, s1, s2, q1, q2,
           cfa_cnt, cfb_cnt, a_energy, b_energy);

  if (voltage_sensor_)    voltage_sensor_->publish_state(v_rms);
  if (current_sensor_1_)  current_sensor_1_->publish_state(ia_rms);
  if (current_sensor_2_)  current_sensor_2_->publish_state(ib_rms);
  if (power_sensor_1_)    power_sensor_1_->publish_state(a_watt);
  if (power_sensor_2_)    power_sensor_2_->publish_state(b_watt);
  if (energy_sensor_1_)   energy_sensor_1_->publish_state(a_energy);
  if (energy_sensor_2_)   energy_sensor_2_->publish_state(b_energy);
  if (energy_sensor_sum_) energy_sensor_sum_->publish_state(a_energy + b_energy);

  if (apparent_power_sensor_1_)   apparent_power_sensor_1_->publish_state(s1);
  if (apparent_power_sensor_2_)   apparent_power_sensor_2_->publish_state(s2);
  if (reactive_power_sensor_1_)   reactive_power_sensor_1_->publish_state(q1);
  if (reactive_power_sensor_2_)   reactive_power_sensor_2_->publish_state(q2);
  if (power_factor_sensor_1_)     power_factor_sensor_1_->publish_state(pf1 * 100.0f);
  if (power_factor_sensor_2_)     power_factor_sensor_2_->publish_state(pf2 * 100.0f);
  if (active_current_sensor_1_)   active_current_sensor_1_->publish_state(active_i1);
  if (active_current_sensor_2_)   active_current_sensor_2_->publish_state(active_i2);
  if (reactive_current_sensor_1_) reactive_current_sensor_1_->publish_state(reactive_i1);
  if (reactive_current_sensor_2_) reactive_current_sensor_2_->publish_state(reactive_i2);
  if (apparent_current_sensor_1_) apparent_current_sensor_1_->publish_state(ia_rms);
  if (apparent_current_sensor_2_) apparent_current_sensor_2_->publish_state(ib_rms);

  if (fast_current_sensor_1_)  fast_current_sensor_1_->publish_state(fast_ia);
  if (fast_current_sensor_2_)  fast_current_sensor_2_->publish_state(fast_ib);
  if (chip_temperature_sensor_) chip_temperature_sensor_->publish_state(chip_temp);
}

void BL0939Xiao::dump_config() {
  ESP_LOGCONFIG(TAG, "BL0939 XIAO:");
  ESP_LOGCONFIG(TAG, "  IREF: %.1f  UREF: %.1f  PREF: %.2f  EREF: %.2f",
                current_reference_, voltage_reference_,
                power_reference_, energy_reference_);
  if (sclk_pin_) {
    LOG_PIN("  SCLK pin: ", sclk_pin_);
  }
  LOG_SENSOR("  ", "Voltage",            voltage_sensor_);
  LOG_SENSOR("  ", "Current 1",          current_sensor_1_);
  LOG_SENSOR("  ", "Current 2",          current_sensor_2_);
  LOG_SENSOR("  ", "Power 1",            power_sensor_1_);
  LOG_SENSOR("  ", "Power 2",            power_sensor_2_);
  LOG_SENSOR("  ", "Energy 1",           energy_sensor_1_);
  LOG_SENSOR("  ", "Energy 2",           energy_sensor_2_);
  LOG_SENSOR("  ", "Energy sum",         energy_sensor_sum_);
  LOG_SENSOR("  ", "Apparent power 1",   apparent_power_sensor_1_);
  LOG_SENSOR("  ", "Apparent power 2",   apparent_power_sensor_2_);
  LOG_SENSOR("  ", "Reactive power 1",   reactive_power_sensor_1_);
  LOG_SENSOR("  ", "Reactive power 2",   reactive_power_sensor_2_);
  LOG_SENSOR("  ", "Power factor 1",     power_factor_sensor_1_);
  LOG_SENSOR("  ", "Power factor 2",     power_factor_sensor_2_);
  LOG_SENSOR("  ", "Active current 1",   active_current_sensor_1_);
  LOG_SENSOR("  ", "Active current 2",   active_current_sensor_2_);
  LOG_SENSOR("  ", "Reactive current 1", reactive_current_sensor_1_);
  LOG_SENSOR("  ", "Reactive current 2", reactive_current_sensor_2_);
  LOG_SENSOR("  ", "Apparent current 1", apparent_current_sensor_1_);
  LOG_SENSOR("  ", "Apparent current 2", apparent_current_sensor_2_);
  LOG_SENSOR("  ", "Fast current 1",     fast_current_sensor_1_);
  LOG_SENSOR("  ", "Fast current 2",     fast_current_sensor_2_);
  LOG_SENSOR("  ", "Chip temperature",   chip_temperature_sensor_);
  LOG_SENSOR("  ", "Phase angle 1",      phase_angle_sensor_1_);
  LOG_SENSOR("  ", "Phase angle 2",      phase_angle_sensor_2_);
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

uint16_t BL0939Xiao::to_uint16_t(ube16_t input) {
  return static_cast<uint16_t>(input.h) << 8 | static_cast<uint16_t>(input.l);
}

bool BL0939Xiao::read_register_(uint8_t addr, uint16_t *value) {
  uint8_t buf[4];
  if (!this->read_array(buf, sizeof(buf))) {
    ESP_LOGW(TAG, "BL0939 register 0x%02X: junk on wire, throwing away partial response", addr);
    while (this->available()) {
      this->read();
    }
    return false;
  }

  uint8_t checksum = static_cast<uint8_t>(BL0939_READ_COMMAND + addr + buf[0] + buf[1] + buf[2]);
  checksum ^= 0xFF;
  if (checksum != buf[3]) {
    ESP_LOGW(TAG, "BL0939 register 0x%02X invalid checksum: computed 0x%02X, got 0x%02X",
             addr, checksum, buf[3]);
    return false;
  }

  // Registers are sent low byte first; A_CORNER/B_CORNER are 16-bit so the
  // third (high) byte is always 0.
  *value = static_cast<uint16_t>(buf[1]) << 8 | static_cast<uint16_t>(buf[0]);
  return true;
}

void BL0939Xiao::publish_phase_angle_(sensor::Sensor *s, uint16_t corner) const {
  // Phase angle conversion (datasheet section 2.9):
  //   angle = 2*pi * AC_FREQ * CORNER / CORNER_F0   (radians)
  // Expressed in degrees and wrapped to (-180, 180]: a positive angle means
  // the current zero-crossing trails the voltage zero-crossing (lagging /
  // inductive); values past 180 degrees indicate the current crossing
  // actually preceded the voltage crossing (leading / capacitive).
  float angle = 360.0f * BL0939_XIAO_AC_FREQ * static_cast<float>(corner) / BL0939_XIAO_CORNER_F0;
  if (angle > 180.0f) {
    angle -= 360.0f;
  }
  s->publish_state(angle);
}

}  // namespace bl0939_xiao
}  // namespace esphome

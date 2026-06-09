#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace bl0939_xiao {

// Seeed Studio XIAO 2-Channel Wi-Fi AC Energy Meter V1.0
// Hardware: BL0939 + XIAO ESP32-C6
//
// Current sensing (both channels):
//   CT clamp MTEP43-205AAB-19305 (100 A primary / 50 mA secondary, ratio 2000:1)
//   Burden: R6+R23 (or R7+R24) = 0.3 Ω + 0.3 Ω = 0.6 Ω differential
//   Effective shunt = 0.6 / 2000 = 0.3 mΩ
//   Max differential input = 50 mA × 0.6 Ω = 30 mV  (< 35 mV BL0939 limit)
//
// Voltage sensing:
//   R1+R3+R4+R5+R8 = 5 × 22 kΩ = 110 kΩ  (primary limiting resistors)
//   ZMPT107-1 current transformer (1:1 ratio, 2 mA rated)
//   R13 = 24.9 Ω burden on secondary → VP pin of BL0939
//   At 230 V: primary current ≈ 2.09 mA, V_VP ≈ 52 mV

// Reference values derived from schematic component values (Vref = 1.218 V):
//   IREF: 324004 × R_eff_mΩ / Vref  where R_eff = 0.6 Ω / 2000 = 0.3 mΩ
//   UREF: 79931 × R_lower_Ω / (Vref × R_total_kΩ)
//         R_lower = 24.9 Ω, R_total = 110.0249 kΩ
//   PREF and EREF follow the same pattern.
static const float BL0939_XIAO_IREF =
    324004.0f * 0.3f / 1.218f;
static const float BL0939_XIAO_UREF =
    79931.0f * 0.0249f * 1000.0f / (1.218f * (5.0f * 22.0f + 0.0249f));
static const float BL0939_XIAO_PREF =
    4046.0f * 0.3f * 0.0249f * 1000.0f / (1.218f * 1.218f * (5.0f * 22.0f + 0.0249f));
static const float BL0939_XIAO_EREF =
    3.6e6f * 4046.0f * 0.3f * 0.0249f * 1000.0f /
    (1638.4f * 256.0f * 1.218f * 1.218f * (5.0f * 22.0f + 0.0249f));

// BL0939 UART protocol constants (device address = 5, the default).
static const uint8_t BL0939_READ_COMMAND  = 0x55;  // 0x5{A4,A3,A2,A1}, address=5
static const uint8_t BL0939_FULL_PACKET   = 0xAA;  // request all registers
static const uint8_t BL0939_PACKET_HEADER = 0x55;
static const uint8_t BL0939_WRITE_COMMAND = 0xA5;  // 0xA{A4,A3,A2,A1}, address=5

// Register addresses
static const uint8_t BL0939_REG_IA_FAST_RMS_CTRL = 0x10;
static const uint8_t BL0939_REG_IB_FAST_RMS_CTRL = 0x1E;
static const uint8_t BL0939_REG_MODE              = 0x18;
static const uint8_t BL0939_REG_SOFT_RESET        = 0x19;
static const uint8_t BL0939_REG_USR_WRPROT        = 0x1A;
static const uint8_t BL0939_REG_TPS_CTRL          = 0x1B;

// Initialisation sequence (write frames: cmd, addr, data_l, data_m, data_h, checksum)
static const uint8_t BL0939_INIT[6][6] = {
    // Soft reset
    {BL0939_WRITE_COMMAND, BL0939_REG_SOFT_RESET, 0x5A, 0x5A, 0x5A, 0x33},
    // Enable user writes
    {BL0939_WRITE_COMMAND, BL0939_REG_USR_WRPROT, 0x55, 0x00, 0x00, 0xEB},
    // 50 Hz, 800 ms RMS update
    {BL0939_WRITE_COMMAND, BL0939_REG_MODE, 0x00, 0x10, 0x00, 0x32},
    // Temperature sensor control
    {BL0939_WRITE_COMMAND, BL0939_REG_TPS_CTRL, 0xFF, 0x47, 0x00, 0xF9},
    // Fast RMS thresholds for IA and IB
    {BL0939_WRITE_COMMAND, BL0939_REG_IA_FAST_RMS_CTRL, 0x1C, 0x18, 0x00, 0x16},
    {BL0939_WRITE_COMMAND, BL0939_REG_IB_FAST_RMS_CTRL, 0x1C, 0x18, 0x00, 0x08},
};

// BL0939 uses little-endian order: low byte first, then mid, then high.
struct ube24_t {
  uint8_t l;
  uint8_t m;
  uint8_t h;
} __attribute__((packed));

struct ube16_t {
  uint8_t l;
  uint8_t h;
} __attribute__((packed));

struct sbe24_t {
  uint8_t l;
  uint8_t m;
  int8_t  h;
} __attribute__((packed));

// Full 35-byte response packet layout.
union DataPacket {
  uint8_t raw[35];
  struct {
    uint8_t  frame_header;   // 0x55
    ube24_t  ia_fast_rms;
    ube24_t  ia_rms;
    ube24_t  ib_rms;
    ube24_t  v_rms;
    ube24_t  ib_fast_rms;
    sbe24_t  a_watt;
    sbe24_t  b_watt;
    sbe24_t  cfa_cnt;
    sbe24_t  cfb_cnt;
    ube16_t  tps1;
    uint8_t  reserved1;
    ube16_t  tps2;
    uint8_t  reserved2;
    uint8_t  checksum;
  } __attribute__((packed));
} __attribute__((packed));

class BL0939Xiao : public PollingComponent, public uart::UARTDevice {
 public:
  void set_voltage_sensor(sensor::Sensor *s)    { voltage_sensor_    = s; }
  void set_current_sensor_1(sensor::Sensor *s)  { current_sensor_1_  = s; }
  void set_current_sensor_2(sensor::Sensor *s)  { current_sensor_2_  = s; }
  void set_power_sensor_1(sensor::Sensor *s)    { power_sensor_1_    = s; }
  void set_power_sensor_2(sensor::Sensor *s)    { power_sensor_2_    = s; }
  void set_energy_sensor_1(sensor::Sensor *s)   { energy_sensor_1_   = s; }
  void set_energy_sensor_2(sensor::Sensor *s)   { energy_sensor_2_   = s; }
  void set_energy_sensor_sum(sensor::Sensor *s) { energy_sensor_sum_ = s; }

  // Optional SCLK pin (XIAO D1 / GPIO3).
  // The PCB has a 1 kΩ pull-up to 3V3 keeping SCLK HIGH so BL0939 uses the
  // default address (5). Providing this pin locks the state explicitly.
  void set_sclk_pin(GPIOPin *pin) { sclk_pin_ = pin; }

  // Calibration overrides (computed from schematic values by default).
  void set_current_reference(float r) { current_reference_ = r; }
  void set_voltage_reference(float r) { voltage_reference_ = r; }
  void set_power_reference(float r)   { power_reference_   = r; }
  void set_energy_reference(float r)  { energy_reference_  = r; }

  void setup()       override;
  void loop()        override;
  void update()      override;
  void dump_config() override;

 protected:
  sensor::Sensor *voltage_sensor_    {nullptr};
  sensor::Sensor *current_sensor_1_  {nullptr};
  sensor::Sensor *current_sensor_2_  {nullptr};
  sensor::Sensor *power_sensor_1_    {nullptr};
  sensor::Sensor *power_sensor_2_    {nullptr};
  sensor::Sensor *energy_sensor_1_   {nullptr};
  sensor::Sensor *energy_sensor_2_   {nullptr};
  sensor::Sensor *energy_sensor_sum_ {nullptr};

  GPIOPin *sclk_pin_ {nullptr};

  float current_reference_ {BL0939_XIAO_IREF};
  float voltage_reference_ {BL0939_XIAO_UREF};
  float power_reference_   {BL0939_XIAO_PREF};
  float energy_reference_  {BL0939_XIAO_EREF};

  static uint32_t to_uint32_t(ube24_t input);
  static int32_t  to_int32_t(sbe24_t input);
  static bool     validate_checksum(const DataPacket *data);
  void            received_package_(const DataPacket *data) const;
};

}  // namespace bl0939_xiao
}  // namespace esphome

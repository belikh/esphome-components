<p align="right">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/belikh/design/latest/packages/graphics/logo/jupiter-logo-dark.svg">
    <img alt="jupiter.au" src="https://raw.githubusercontent.com/belikh/design/latest/packages/graphics/logo/jupiter-logo-light.svg" width="260">
  </picture>
</p>

# esphome-components

External ESPHome components. This repo has one: `bl0939_xiao`, a driver for the BL0939 energy metering chip on the Seeed Studio XIAO 2-Channel Wi-Fi AC Energy Meter (XIAO ESP32-C6). The BL0939 talks UART. The component reads the full register packet and publishes voltage, current, power and energy for both channels, plus derived and chip-level sensors.

<p>
  <a href="#install">Install</a> ·
  <a href="#use">Use</a> ·
  <a href="#sensors">Sensors</a> ·
  <a href="#configuration">Configuration</a>
</p>

<br>

---

<br>

## Install

Add the repo as an external component in your ESPHome YAML:

```yaml
external_components:
  - source: github://belikh/esphome-components
    components: [bl0939_xiao]
```

<br>

## Use

The meter's ESP32-C6 only supports the esp-idf framework. The BL0939 runs at 4800 baud, 8 data bits, no parity, 2 stop bits. On the XIAO board its UART is on D6 (GPIO16, TX) and D7 (GPIO17, RX).

```yaml
esp32:
  board: seeed_xiao_esp32c6
  variant: esp32c6
  framework:
    type: esp-idf

uart:
  id: bl0939_uart
  tx_pin: GPIO16
  rx_pin: GPIO17
  baud_rate: 4800
  stop_bits: 2

sensor:
  - platform: bl0939_xiao
    uart_id: bl0939_uart
    update_interval: 10s
    voltage:
      name: "Voltage"
    current_1:
      name: "Current CH1"
    active_power_1:
      name: "Power CH1"
    energy_1:
      name: "Energy CH1"
```

[`examples/xiao_2ch_em.yaml`](examples/xiao_2ch_em.yaml) is a complete device config with every sensor, Wi-Fi, API and OTA. It uses `!secret` for `api_key`, `ota_password`, `wifi_ssid`, `wifi_password` and `fallback_password`.

The default `update_interval` is 60s.

<br>

## Sensors

Every sensor is optional. Channel 1 and channel 2 sensors end in `_1` and `_2`.

| Option | Unit | Source |
|---|---|---|
| `voltage` | V | RMS voltage |
| `current_1`, `current_2` | A | RMS current, averaged over 400 ms |
| `active_power_1`, `active_power_2` | W | active power |
| `energy_1`, `energy_2`, `energy_total` | kWh | energy counters; `energy_total` is the sum of both channels |
| `apparent_power_1`, `apparent_power_2` | VA | derived from voltage and current |
| `reactive_power_1`, `reactive_power_2` | var | derived |
| `power_factor_1`, `power_factor_2` | % | derived |
| `active_current_1`, `active_current_2` | A | derived |
| `reactive_current_1`, `reactive_current_2` | A | derived |
| `apparent_current_1`, `apparent_current_2` | A | same value as `current_1` and `current_2`, kept for naming compatibility |
| `fast_current_1`, `fast_current_2` | A | RMS current refreshed every half or full AC cycle (about 10 to 40 ms), for leakage and overcurrent monitoring |
| `chip_temperature` | °C | BL0939 internal temperature (TPS1 register) |
| `phase_angle_1`, `phase_angle_2` | ° | voltage to current phase angle from the A_CORNER and B_CORNER registers; positive means current lags voltage (inductive), negative means it leads (capacitive) |

The phase angle registers are not in the full packet, so they are read one at a time after it, and only when a phase angle sensor is configured. The angle calculation assumes a 50 Hz line.

<br>

## Configuration

| Option | Meaning |
|---|---|
| `sclk_pin` | Optional output pin wired to the BL0939 SCLK pin (D1, GPIO3 on the XIAO). The board has a 1 kΩ pull-up to 3V3 that sets the chip address to 5, so leaving this out is safe. Set it as `OUTPUT` to drive the pin high as well. |
| `current_reference`, `voltage_reference`, `power_reference`, `energy_reference` | Calibration overrides. The defaults are computed from the board's component values, so set these only after measuring against a known load. |

The example config follows the board's schematic: two 100 A / 50 mA CTs with 0.6 Ω burden resistors on channels 1 and 2, and a voltage divider of five 22 kΩ resistors into a ZMPT107-1 transformer with a 24.9 Ω burden. If your hardware differs, adjust the references.

<br>

<p align="right"><sub>jupiter.au</sub></p>

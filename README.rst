.. raw:: html

   <p align="right">
     <picture>
       <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/belikh/design/latest/packages/graphics/logo/jupiter-logo-dark.svg">
       <img alt="jupiter.au" src="https://raw.githubusercontent.com/belikh/design/latest/packages/graphics/logo/jupiter-logo-light.svg" width="260">
     </picture>
   </p>

   <h1>esphome-components</h1>

.. meta::
   :description: External ESPHome components. bl0939_xiao reads the BL0939 energy metering chip on the Seeed Studio XIAO 2-Channel Wi-Fi AC Energy Meter. ambz2_uart opens UART0 or UART1 with explicit pins on Realtek RTL8720C modules such as the Tuya WBR1.

.. |esp-l| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/esphome-light.svg#gh-light-mode-only
   :width: 20
   :alt:
.. |esp-d| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/esphome-dark.svg#gh-dark-mode-only
   :width: 20
   :alt:
.. |sen-l| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/pulse-light.svg#gh-light-mode-only
   :width: 20
   :alt:
.. |sen-d| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/pulse-dark.svg#gh-dark-mode-only
   :width: 20
   :alt:
.. |cfg-l| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/sliders-light.svg#gh-light-mode-only
   :width: 20
   :alt:
.. |cfg-d| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/sliders-dark.svg#gh-dark-mode-only
   :width: 20
   :alt:

External ESPHome components. This repo has two.

``bl0939_xiao`` is a driver for the BL0939 energy metering chip on the Seeed Studio XIAO 2-Channel Wi-Fi AC Energy Meter (XIAO ESP32-C6). The BL0939 talks UART. The component reads the full register packet and publishes voltage, current, power and energy for both channels, plus derived and chip-level sensors.

``ambz2_uart`` opens UART0 or UART1 on Realtek RTL8720C modules (the Tuya WBR1, for example) with pins you choose, where ESPHome's own ``uart`` refuses them.

`Install`_ · `Use`_ · `Sensors`_ · `Configuration`_ · `ambz2_uart`_

|

.. raw:: html

   <p align="center"><picture><source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/rule/end-dark.svg"><img alt="" src="https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/rule/end-light.svg" width="50%"></picture></p>

|

Install
-------

Add the repo as an external component in your ESPHome YAML:

.. code-block:: yaml

   external_components:
     - source: github://belikh/esphome-components
       components: [bl0939_xiao, ambz2_uart]

|

Use
---

The meter's ESP32-C6 only supports the esp-idf framework. The BL0939 runs at 4800 baud, 8 data bits, no parity, 2 stop bits. On the XIAO board its UART is on D6 (GPIO16, TX) and D7 (GPIO17, RX).

.. code-block:: yaml

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

`examples/xiao_2ch_em.yaml <examples/xiao_2ch_em.yaml>`_ is a complete device config with every sensor, Wi-Fi, API and OTA. It uses ``!secret`` for ``api_key``, ``ota_password``, ``wifi_ssid``, ``wifi_password`` and ``fallback_password``.

The default ``update_interval`` is 60s.

|

Sensors
-------

Every sensor is optional. Channel 1 and channel 2 sensors end in ``_1`` and ``_2``.

.. list-table::
   :header-rows: 1
   :widths: 38 8 54

   * - Option
     - Unit
     - Source
   * - |sen-l| |sen-d| ``voltage``
     - V
     - RMS voltage
   * - |sen-l| |sen-d| ``current_1``, ``current_2``
     - A
     - RMS current, averaged over 400 ms
   * - |sen-l| |sen-d| ``active_power_1``, ``active_power_2``
     - W
     - active power
   * - |sen-l| |sen-d| ``energy_1``, ``energy_2``, ``energy_total``
     - kWh
     - energy counters; ``energy_total`` is the sum of both channels
   * - |sen-l| |sen-d| ``apparent_power_1``, ``apparent_power_2``
     - VA
     - derived from voltage and current
   * - |sen-l| |sen-d| ``reactive_power_1``, ``reactive_power_2``
     - var
     - derived
   * - |sen-l| |sen-d| ``power_factor_1``, ``power_factor_2``
     - %
     - derived
   * - |sen-l| |sen-d| ``active_current_1``, ``active_current_2``
     - A
     - derived
   * - |sen-l| |sen-d| ``reactive_current_1``, ``reactive_current_2``
     - A
     - derived
   * - |sen-l| |sen-d| ``apparent_current_1``, ``apparent_current_2``
     - A
     - same value as ``current_1`` and ``current_2``, kept for naming compatibility
   * - |sen-l| |sen-d| ``fast_current_1``, ``fast_current_2``
     - A
     - RMS current refreshed every half or full AC cycle (about 10 to 40 ms), for leakage and overcurrent monitoring
   * - |sen-l| |sen-d| ``chip_temperature``
     - °C
     - BL0939 internal temperature (TPS1 register)
   * - |sen-l| |sen-d| ``phase_angle_1``, ``phase_angle_2``
     - °
     - voltage to current phase angle from the A_CORNER and B_CORNER registers; positive means current lags voltage (inductive), negative means it leads (capacitive)

The phase angle registers are not in the full packet, so they are read one at a time after it, and only when a phase angle sensor is configured. The angle calculation assumes a 50 Hz line.

|

Configuration
-------------

.. list-table::
   :header-rows: 1
   :widths: 38 62

   * - Option
     - Meaning
   * - |cfg-l| |cfg-d| ``sclk_pin``
     - Optional output pin wired to the BL0939 SCLK pin (D1, GPIO3 on the XIAO). The board has a 1 kΩ pull-up to 3V3 that sets the chip address to 5, so leaving this out is safe. Set it as ``OUTPUT`` to drive the pin high as well.
   * - |cfg-l| |cfg-d| ``current_reference``, ``voltage_reference``, ``power_reference``, ``energy_reference``
     - Calibration overrides. The defaults are computed from the board's component values, so set these only after measuring against a known load.

The example config follows the board's schematic: two 100 A / 50 mA CTs with 0.6 Ω burden resistors on channels 1 and 2, and a voltage divider of five 22 kΩ resistors into a ZMPT107-1 transformer with a 24.9 Ω burden. If your hardware differs, adjust the references.

|

ambz2_uart
----------

On a Realtek RTL8720C chip, UART0 can use RX on PA12 or PA13 and TX on PA11 or PA14, and UART1 can use RX on PA00 or PA02 and TX on PA01 or PA03. LibreTiny lists both choices in the board file, so ESPHome's ``uart`` has no single pin pair to match against, rejects PA13 and PA14 and stops with ``SoftwareSerial is not implemented for this chip``. ``ambz2_uart`` calls ``Serial0.begin()`` or ``Serial1.begin()`` with the pins you set and presents an ordinary ESPHome UART, so other components take it as their ``uart_id``.

.. code-block:: yaml

   rtl87xx:
     board: wbr1

   ambz2_uart:
     id: ac_uart
     port: 0
     rx_pin: 13
     tx_pin: 14
     baud_rate: 9600

.. list-table::
   :header-rows: 1
   :widths: 38 62

   * - Option
     - Meaning
   * - |cfg-l| |cfg-d| ``port``
     - ``0`` (default) or ``1``. UART2 is the log UART on these modules, so ESPHome's own ``uart`` already handles it.
   * - |cfg-l| |cfg-d| ``rx_pin``
     - GPIO number. ``12`` or ``13`` on UART0, ``0`` or ``2`` on UART1.
   * - |cfg-l| |cfg-d| ``tx_pin``
     - GPIO number. ``11`` or ``14`` on UART0, ``1`` or ``3`` on UART1.
   * - |cfg-l| |cfg-d| ``baud_rate``
     - Any rate from 1200 to 1000000. Data bits, parity and stop bits are fixed at 8N1.

Receiving is tested on a Tuya WBR1 at 9600 baud. Sending is not tested yet. `examples/wbr1_gaitek_ac.yaml <examples/wbr1_gaitek_ac.yaml>`_ drives a portable air conditioner through the original Wi-Fi board with the ``gaitek`` component from ``philippe44/esphome-gaitek``.

|

.. raw:: html

   <p align="right"><sub>jupiter.au</sub></p>

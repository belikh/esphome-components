import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart
from esphome.const import (
    CONF_CURRENT,
    CONF_ENERGY,
    CONF_ID,
    CONF_POWER,
    CONF_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_KILOWATT_HOURS,
    UNIT_VOLT,
    UNIT_WATT,
)
from esphome.components.uart import CONF_UART_ID
import esphome.pins as pins

CONF_CURRENT_1 = "current_1"
CONF_CURRENT_2 = "current_2"
CONF_ACTIVE_POWER_1 = "active_power_1"
CONF_ACTIVE_POWER_2 = "active_power_2"
CONF_ENERGY_1 = "energy_1"
CONF_ENERGY_2 = "energy_2"
CONF_ENERGY_TOTAL = "energy_total"
CONF_SCLK_PIN = "sclk_pin"

# Calibration overrides
CONF_CURRENT_REFERENCE = "current_reference"
CONF_VOLTAGE_REFERENCE = "voltage_reference"
CONF_POWER_REFERENCE = "power_reference"
CONF_ENERGY_REFERENCE = "energy_reference"

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor"]

bl0939_xiao_ns = cg.esphome_ns.namespace("bl0939_xiao")
BL0939Xiao = bl0939_xiao_ns.class_("BL0939Xiao", cg.PollingComponent, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BL0939Xiao),
            cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CURRENT_1): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CURRENT_2): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_ACTIVE_POWER_1): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_ACTIVE_POWER_2): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_ENERGY_1): sensor.sensor_schema(
                unit_of_measurement=UNIT_KILOWATT_HOURS,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_ENERGY_2): sensor.sensor_schema(
                unit_of_measurement=UNIT_KILOWATT_HOURS,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_ENERGY_TOTAL): sensor.sensor_schema(
                unit_of_measurement=UNIT_KILOWATT_HOURS,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            # Optional: drive the BL0939 SCLK pin (connected to XIAO D1/GPIO3).
            # The board has a 1kΩ pull-up to 3V3 so leaving it unconfigured is safe.
            # Configure as OUTPUT HIGH to reinforce the pull-up and lock the address.
            cv.Optional(CONF_SCLK_PIN): pins.gpio_output_pin_schema,
            # Calibration overrides — computed from component values at compile time
            # if you need to tweak after real-world measurement.
            cv.Optional(CONF_CURRENT_REFERENCE): cv.positive_float,
            cv.Optional(CONF_VOLTAGE_REFERENCE): cv.positive_float,
            cv.Optional(CONF_POWER_REFERENCE): cv.positive_float,
            cv.Optional(CONF_ENERGY_REFERENCE): cv.positive_float,
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if voltage_config := config.get(CONF_VOLTAGE):
        sens = await sensor.new_sensor(voltage_config)
        cg.add(var.set_voltage_sensor(sens))

    if current_1_config := config.get(CONF_CURRENT_1):
        sens = await sensor.new_sensor(current_1_config)
        cg.add(var.set_current_sensor_1(sens))

    if current_2_config := config.get(CONF_CURRENT_2):
        sens = await sensor.new_sensor(current_2_config)
        cg.add(var.set_current_sensor_2(sens))

    if power_1_config := config.get(CONF_ACTIVE_POWER_1):
        sens = await sensor.new_sensor(power_1_config)
        cg.add(var.set_power_sensor_1(sens))

    if power_2_config := config.get(CONF_ACTIVE_POWER_2):
        sens = await sensor.new_sensor(power_2_config)
        cg.add(var.set_power_sensor_2(sens))

    if energy_1_config := config.get(CONF_ENERGY_1):
        sens = await sensor.new_sensor(energy_1_config)
        cg.add(var.set_energy_sensor_1(sens))

    if energy_2_config := config.get(CONF_ENERGY_2):
        sens = await sensor.new_sensor(energy_2_config)
        cg.add(var.set_energy_sensor_2(sens))

    if energy_total_config := config.get(CONF_ENERGY_TOTAL):
        sens = await sensor.new_sensor(energy_total_config)
        cg.add(var.set_energy_sensor_sum(sens))

    if sclk_pin_config := config.get(CONF_SCLK_PIN):
        sclk_pin = await cg.gpio_pin_expression(sclk_pin_config)
        cg.add(var.set_sclk_pin(sclk_pin))

    if (ref := config.get(CONF_CURRENT_REFERENCE)) is not None:
        cg.add(var.set_current_reference(ref))
    if (ref := config.get(CONF_VOLTAGE_REFERENCE)) is not None:
        cg.add(var.set_voltage_reference(ref))
    if (ref := config.get(CONF_POWER_REFERENCE)) is not None:
        cg.add(var.set_power_reference(ref))
    if (ref := config.get(CONF_ENERGY_REFERENCE)) is not None:
        cg.add(var.set_energy_reference(ref))

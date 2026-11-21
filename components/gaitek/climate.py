import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, number, uart
from esphome.const import CONF_HUMIDITY, CONF_ID, ENTITY_CATEGORY_CONFIG, UNIT_HOUR

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["number"]

CONF_TIMER = "timer"
TIMER_MAX_HOURS = 24

gaitek_ns = cg.esphome_ns.namespace("gaitek")
GaitekAC = gaitek_ns.class_("GaitekAC", climate.Climate, cg.Component, uart.UARTDevice)
GaitekTimer = gaitek_ns.class_("GaitekTimer", number.Number)

CONFIG_SCHEMA = (
    climate.climate_schema(GaitekAC)
    .extend(
        {
            # Units with a humidity sensor report it in the status frame. Leave this off
            # for a unit without one, or Home Assistant shows a meaningless value.
            cv.Optional(CONF_HUMIDITY, default=False): cv.boolean,
            cv.Optional(CONF_TIMER): number.number_schema(
                GaitekTimer,
                unit_of_measurement=UNIT_HOUR,
                icon="mdi:timer-outline",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await climate.register_climate(var, config)
    cg.add(var.set_humidity(config[CONF_HUMIDITY]))

    if CONF_TIMER in config:
        timer = await number.new_number(
            config[CONF_TIMER], min_value=0, max_value=TIMER_MAX_HOURS, step=1
        )
        await cg.register_parented(timer, var)
        cg.add(var.set_timer_number(timer))

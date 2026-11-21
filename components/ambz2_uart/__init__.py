"""UART0 or UART1 on RTL8720CF (LibreTiny) with explicit pins.

ESPHome's built-in `uart` only accepts a LibreTiny UART whose pins are one fixed pair
(PIN_SERIAL<n>_TX/RX). The WBR1 variant defines UART0 with two alternative pairs
(PIN_SERIAL0_RX_0/_1, PIN_SERIAL0_TX_0/_1), so PIN_SERIAL0_RX/TX end up undefined (255) and
`uart:` refuses PA13/PA14. LibreTiny's Serial0.begin(baud, config, rx, tx) accepts the pins
explicitly, so this component does exactly that and exposes an ordinary UARTComponent.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_BAUD_RATE, CONF_ID, CONF_RX_PIN, CONF_TX_PIN

AUTO_LOAD = ["uart"]
MULTI_CONF = True

ambz2_uart_ns = cg.esphome_ns.namespace("ambz2_uart")
Ambz2UART = ambz2_uart_ns.class_("Ambz2UART", uart.UARTComponent, cg.Component)

CONF_PORT = "port"

# GPIO numbers from LibreTiny's wbr1.h: UART0 RX {12,13} TX {11,14}; UART1 RX {0,2} TX {1,3}
PORT_PINS = {0: ((12, 13), (11, 14)), 1: ((0, 2), (1, 3))}


def _validate_pins(config):
    rx_ok, tx_ok = PORT_PINS[config[CONF_PORT]]
    if config[CONF_RX_PIN] not in rx_ok:
        raise cv.Invalid(f"rx_pin must be one of {rx_ok} for UART{config[CONF_PORT]}")
    if config[CONF_TX_PIN] not in tx_ok:
        raise cv.Invalid(f"tx_pin must be one of {tx_ok} for UART{config[CONF_PORT]}")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Ambz2UART),
            cv.Required(CONF_BAUD_RATE): cv.int_range(min=1200, max=1000000),
            cv.Optional(CONF_PORT, default=0): cv.one_of(0, 1, int=True),
            cv.Required(CONF_RX_PIN): cv.positive_int,
            cv.Required(CONF_TX_PIN): cv.positive_int,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_pins,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_baud_rate(config[CONF_BAUD_RATE]))
    cg.add(var.set_data_bits(8))
    cg.add(var.set_stop_bits(1))
    cg.add(var.set_rx_buffer_size(256))
    cg.add(var.set_hw_pins(config[CONF_PORT], config[CONF_RX_PIN], config[CONF_TX_PIN]))

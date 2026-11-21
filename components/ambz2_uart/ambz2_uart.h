#pragma once

#ifdef USE_LIBRETINY

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart_component.h"
#include "Arduino.h"

namespace esphome {
namespace ambz2_uart {

class Ambz2UART : public uart::UARTComponent, public Component {
 public:
  void set_hw_pins(uint8_t port, uint8_t rx, uint8_t tx) {
    this->port_ = port;
    this->hw_rx_ = rx;
    this->hw_tx_ = tx;
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  void write_array(const uint8_t *data, size_t len) override;
  bool peek_byte(uint8_t *data) override;
  bool read_array(uint8_t *data, size_t len) override;
  size_t available() override;
  uart::UARTFlushResult flush() override;

 protected:
  void check_logger_conflict() override {}

  uint8_t port_{0};
  SerialClass *serial_{nullptr};
  uint8_t hw_rx_{255};
  uint8_t hw_tx_{255};
  bool started_{false};
};

}  // namespace ambz2_uart
}  // namespace esphome

#endif  // USE_LIBRETINY

#include "ambz2_uart.h"

#ifdef USE_LIBRETINY

#include "Arduino.h"

namespace esphome {
namespace ambz2_uart {

static const char *const TAG = "ambz2_uart";

void Ambz2UART::setup() {
  // Serial0.begin() validates the pins against PINS_SERIAL0_RX/TX and silently does nothing
  // if they are invalid, so check the result by looking at available() later via dump_config.
  this->serial_ = this->port_ == 1 ? &Serial1 : &Serial0;
  this->serial_->begin(this->baud_rate_, SERIAL_8N1, this->hw_rx_, this->hw_tx_);
  this->started_ = true;
}

void Ambz2UART::dump_config() {
  ESP_LOGCONFIG(TAG,
                "UART%u (explicit pins):\n"
                "  RX GPIO: PA%u\n"
                "  TX GPIO: PA%u\n"
                "  Baud Rate: %" PRIu32 " baud 8N1",
                this->port_, this->hw_rx_, this->hw_tx_, this->baud_rate_);
}

void Ambz2UART::write_array(const uint8_t *data, size_t len) {
  this->serial_->write(data, len);
#ifdef USE_UART_DEBUGGER
  for (size_t i = 0; i < len; i++) {
    this->debug_callback_.call(uart::UART_DIRECTION_TX, data[i]);
  }
#endif
}

bool Ambz2UART::peek_byte(uint8_t *data) {
  if (!this->check_read_timeout_())
    return false;
  *data = this->serial_->peek();
  return true;
}

bool Ambz2UART::read_array(uint8_t *data, size_t len) {
  if (!this->check_read_timeout_(len))
    return false;
  this->serial_->readBytes(data, len);
#ifdef USE_UART_DEBUGGER
  for (size_t i = 0; i < len; i++) {
    this->debug_callback_.call(uart::UART_DIRECTION_RX, data[i]);
  }
#endif
  return true;
}

size_t Ambz2UART::available() { return this->serial_->available(); }

uart::UARTFlushResult Ambz2UART::flush() {
  this->serial_->flush();
  return uart::UARTFlushResult::UART_FLUSH_RESULT_ASSUMED_SUCCESS;
}

}  // namespace ambz2_uart
}  // namespace esphome

#endif  // USE_LIBRETINY

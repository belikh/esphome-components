#pragma once

// GAITEK wire format. No ESPHome includes, so this file also builds on a PC for testing.
//
// Every frame is: "GAITEK", a body that starts with 0xBD, then two check bytes.
// A status frame (unit -> module, 24 bytes) has a 10-byte body and ends with "KETIAG".
// A command frame (module -> unit, 14 bytes) has a 6-byte body and no trailer.
//
// The check bytes split the body by position: the first is the XOR of every second body
// byte starting at the second one, the second is the XOR of the others.

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace gaitek {
namespace frame {

static const uint8_t MAGIC[] = {'G', 'A', 'I', 'T', 'E', 'K'};
static const size_t MAGIC_LEN = sizeof(MAGIC);
static const uint8_t BODY_MARK = 0xBD;

static const size_t STATUS_BODY_LEN = 10;
static const size_t STATUS_LEN = MAGIC_LEN + STATUS_BODY_LEN + 2 + MAGIC_LEN;  // 24
static const size_t COMMAND_BODY_LEN = 6;
static const size_t COMMAND_LEN = MAGIC_LEN + COMMAND_BODY_LEN + 2;  // 14

// Command numbers (first body byte after the mark).
enum Op : uint8_t {
  OP_POWER = 0x00,            // 0 off, 1 on
  OP_SWING = 0x01,            // 0 off, 1 on
  OP_MODE = 0x06,             // Mode
  OP_FAN = 0x07,              // Fan
  OP_TARGET_TEMPERATURE = 0x08,  // degrees C
  OP_TARGET_HUMIDITY = 0x09,  // percent
  OP_TIMER = 0x0A,            // hours
  OP_KEEPALIVE = 0xFF,
};

enum Mode : uint8_t { MODE_COOL = 1, MODE_DRY = 3, MODE_FAN = 6 };
enum Fan : uint8_t { FAN_HIGH = 1, FAN_MEDIUM = 2, FAN_LOW = 3 };

struct Status {
  bool power;
  bool swing;
  uint8_t mode;
  uint8_t fan;
  uint8_t room_temperature;    // degrees C
  uint8_t target_temperature;  // degrees C
  uint8_t room_humidity;       // percent
  uint8_t target_humidity;     // percent
  uint8_t timer_hours;

  bool operator==(const Status &o) const {
    return power == o.power && swing == o.swing && mode == o.mode && fan == o.fan &&
           room_temperature == o.room_temperature && target_temperature == o.target_temperature &&
           room_humidity == o.room_humidity && target_humidity == o.target_humidity &&
           timer_hours == o.timer_hours;
  }
  bool operator!=(const Status &o) const { return !(*this == o); }
};

// Writes the two check bytes for `body` to out[0] and out[1].
inline void checksum(const uint8_t *body, size_t len, uint8_t *out) {
  uint8_t acc[2] = {0, 0};
  for (size_t i = 0; i < len; i++)
    acc[i & 1] ^= body[i];
  out[0] = acc[1];
  out[1] = acc[0];
}

inline bool has_magic(const uint8_t *buf) {
  for (size_t i = 0; i < MAGIC_LEN; i++)
    if (buf[i] != MAGIC[i])
      return false;
  return true;
}

// Decodes STATUS_LEN bytes. Returns false if the framing or the check bytes are wrong.
inline bool decode_status(const uint8_t *buf, Status *out) {
  if (!has_magic(buf))
    return false;
  for (size_t i = 0; i < MAGIC_LEN; i++)  // the trailer is the magic backwards
    if (buf[STATUS_LEN - 1 - i] != MAGIC[i])
      return false;

  const uint8_t *body = buf + MAGIC_LEN;
  uint8_t want[2];
  checksum(body, STATUS_BODY_LEN, want);
  if (body[0] != BODY_MARK || body[STATUS_BODY_LEN] != want[0] || body[STATUS_BODY_LEN + 1] != want[1])
    return false;

  out->power = body[1] & 0x01;
  out->swing = body[1] & 0x04;
  // body[2] is not understood yet
  out->mode = body[3] & 0x0F;
  out->fan = (body[3] >> 4) & 0x03;
  out->room_temperature = body[4];
  out->target_temperature = body[5];
  out->room_humidity = body[6];
  out->target_humidity = body[7];
  out->timer_hours = body[8];
  // body[9] is not understood yet
  return true;
}

// Fills COMMAND_LEN bytes at `buf`.
inline void encode_command(uint8_t op, uint8_t value, uint8_t *buf) {
  for (size_t i = 0; i < MAGIC_LEN; i++)
    buf[i] = MAGIC[i];
  uint8_t *body = buf + MAGIC_LEN;
  body[0] = BODY_MARK;
  body[1] = op;
  body[2] = value;
  body[3] = body[4] = body[5] = 0;
  checksum(body, COMMAND_BODY_LEN, body + COMMAND_BODY_LEN);
}

}  // namespace frame
}  // namespace gaitek
}  // namespace esphome

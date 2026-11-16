#pragma once
// Makita AWS (WUT01) BLE advertisement decoding.
//
// A WUT01 in a powered tool sends ~10 legacy adverts/s whose entire payload is
//   02 01 06            Flags
//   05 FF b0 b1 b2 b3   "Manufacturer specific" (b0 b1 are NOT a real company ID)
// There is no per-device ID in the payload: tools are identified by BLE MAC.
//
// Known states (Wikipedia "Makita AWS"; used by cillian64/makita_aws_rx and
// the-ress/makita-aws-adapter):
//   poweron  FC 33 03 05
//   init     FD 33 06 06
//   waiting  FC 33 03 06   (idle, tool powered)
//   trigger  FD AA 03 06   (trigger pulled)
//   pair     FF 33 0C 07   (pairing button on WUT01)
//   unpair   EF 33 18 08
//
// Observed 2026-11-15 (WUT01 in a track saw, static random address, no Makita receiver):
//   - Fresh WUT01: battery in -> poweron, ~5-10 adverts/s. Trigger sends nothing.
//   - Never paired: short click of the AWS button -> init for ~2 s, then back to poweron.
//   - Holding the AWS button -> pair (LED flashing green) for ~20 s, then waiting.
//     A purely passive scanner was enough: no GATT exchange.
//   - waiting/trigger: ~15-20 adverts/s; trigger while held, waiting again on release.
//   - After pairing once, every battery insert starts in poweron (trigger ignored);
//     one short click -> init, which stays armed: first trigger, then waiting as usual.

#include <cstdint>
#include <cstring>

namespace makita_aws {

enum Msg : uint8_t {
  MSG_NONE = 0,  // not a WUT01-shaped advert
  MSG_POWERON,
  MSG_INIT,
  MSG_WAITING,
  MSG_TRIGGER,
  MSG_PAIR,
  MSG_UNPAIR,
  MSG_UNKNOWN,  // WUT01-shaped (flags + one 4-byte 0xFF record) but unrecognised payload
};

// What the status indicator should show; each device renders these its own way.
enum LedMode : int {
  LED_OFF = 0,
  LED_PRESENT,  // tool seen, idle
  LED_TRIGGER,  // trigger pulled
  LED_PAIR,     // pairing window open
  LED_UNPAIR,   // unpair window open
  LED_BLINK,    // just paired/unpaired/forgot
  LED_HOLD2,    // button held long enough to pair on release
  LED_HOLD5,    // ... to unpair
  LED_HOLD10,   // ... to forget all
};

inline const char *msg_name(uint8_t m) {
  switch (m) {
    case MSG_POWERON: return "poweron";
    case MSG_INIT: return "init";
    case MSG_WAITING: return "waiting";
    case MSG_TRIGGER: return "trigger";
    case MSG_PAIR: return "pair";
    case MSG_UNPAIR: return "unpair";
    case MSG_UNKNOWN: return "unknown";
    default: return "none";
  }
}

struct Pattern {
  uint8_t bytes[4];
  Msg msg;
};

static const Pattern PATTERNS[] = {
    {{0xFC, 0x33, 0x03, 0x05}, MSG_POWERON},
    {{0xFD, 0x33, 0x06, 0x06}, MSG_INIT},
    {{0xFC, 0x33, 0x03, 0x06}, MSG_WAITING},
    {{0xFD, 0xAA, 0x03, 0x06}, MSG_TRIGGER},
    {{0xFF, 0x33, 0x0C, 0x07}, MSG_PAIR},
    {{0xEF, 0x33, 0x18, 0x08}, MSG_UNPAIR},
};

// Walks the AD structures in the advert (not the scan response). Returns
// MSG_NONE unless the advert is exactly {Flags, one 0xFF record with 4 data
// bytes}, which is how a WUT01 looks; `out` receives the 4 payload bytes.
inline Msg parse(const uint8_t *adv, uint8_t len, uint8_t out[4]) {
  bool have_flags = false;
  const uint8_t *mfg = nullptr;
  uint8_t records = 0;
  uint8_t i = 0;
  while (i < len) {
    uint8_t rec_len = adv[i];
    if (rec_len == 0)
      break;  // padding
    if (i + 1 + rec_len > len)
      return MSG_NONE;  // malformed
    uint8_t type = adv[i + 1];
    records++;
    if (type == 0x01) {
      have_flags = true;
    } else if (type == 0xFF && rec_len == 5) {
      mfg = &adv[i + 2];
    } else {
      return MSG_NONE;  // any other record: not a WUT01
    }
    i += 1 + rec_len;
  }
  if (mfg == nullptr || records > 2 || (records == 2 && !have_flags))
    return MSG_NONE;
  memcpy(out, mfg, 4);
  for (const auto &p : PATTERNS) {
    if (memcmp(mfg, p.bytes, 4) == 0)
      return p.msg;
  }
  return MSG_UNKNOWN;
}

}  // namespace makita_aws

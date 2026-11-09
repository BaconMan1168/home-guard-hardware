#include "ir_sender.h"

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

#include "hub_config.h"

static IRsend irsend(hg::kPinIrSend);

void irSenderBegin() { irsend.begin(); }

static void sendOnce(const StoredCode& code) {
  decode_type_t protocol = static_cast<decode_type_t>(code.protocol);
  if (code.kind == CodeKind::Raw) {
    irsend.sendRaw(code.raw.data(), static_cast<uint16_t>(code.raw.size()), 38);
  } else if (!code.state.empty()) {
    irsend.send(protocol, code.state.data(), static_cast<uint16_t>(code.state.size()));
  } else {
    irsend.send(protocol, code.value, code.bits);
  }
}

void irSend(const StoredCode& code, uint8_t times) {
  if (!code.present) return;
  for (uint8_t i = 0; i < times; i++) {
    if (i > 0) delay(hg::kRepeatGapMs);
    sendOnce(code);
  }
}

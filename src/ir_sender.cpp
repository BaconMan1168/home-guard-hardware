#include "ir_sender.h"

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

#include "hub_config.h"

static IRsend irsend(hg::kPinIrSend);

void irSenderBegin() { irsend.begin(); }

static void sendOnce(const StoredCode& code) {
  decode_type_t protocol = static_cast<decode_type_t>(code.protocol);
  switch (code.kind) {
    case CodeKind::AcState:
      irsend.send(protocol, code.state.data(), static_cast<uint16_t>(code.state.size()));
      break;
    case CodeKind::Toggle:
      irsend.send(protocol, code.value, code.bits);
      break;
    case CodeKind::Raw:
      irsend.sendRaw(code.raw.data(), static_cast<uint16_t>(code.raw.size()), 38);
      break;
  }
}

void irSend(const StoredCode& code, uint8_t times) {
  if (!code.present) return;
  for (uint8_t i = 0; i < times; i++) {
    if (i > 0) delay(hg::kRepeatGapMs);
    sendOnce(code);
  }
}

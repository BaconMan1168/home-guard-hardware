#include "ir_learner.h"

#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>

#include "hub_config.h"

static IRrecv irrecv(hg::kPinIrReceive, 1024, 50, true);
static decode_results results;
static bool enabled = false;

void irLearnerBegin() { irrecv.setUnknownThreshold(12); }

void irLearnerEnable(bool on) {
  if (on == enabled) return;
  enabled = on;
  if (on) {
    irrecv.enableIRIn();
  } else {
    irrecv.disableIRIn();
  }
}

static bool hasPowerToggleBit(decode_type_t protocol) {
  switch (protocol) {
    case DAIKIN128:
    case HITACHI_AC1:
    case WHIRLPOOL_AC:
    case YORK:
      return true;
    default:
      return false;
  }
}

bool irLearnerPoll(FrameInfo& frame, StoredCode& code) {
  if (!irrecv.decode(&results)) return false;

  frame = {results.rawlen, results.overflow};
  bool decoded = results.decode_type != UNKNOWN;
  code = StoredCode{};
  bool stateful = hasACState(results.decode_type);
  code.kind = classifyCode(decoded, stateful, hasPowerToggleBit(results.decode_type));
  code.protocol = results.decode_type;
  code.protocolName = typeToString(results.decode_type).c_str();

  if (code.kind == CodeKind::Raw) {
    uint16_t* raw = resultToRawArray(&results);
    code.raw.assign(raw, raw + getCorrectedRawLength(&results));
    delete[] raw;
  } else if (stateful) {
    code.state.assign(results.state, results.state + results.bits / 8);
  } else {
    code.value = results.value;
    code.bits = results.bits;
  }
  return true;
}

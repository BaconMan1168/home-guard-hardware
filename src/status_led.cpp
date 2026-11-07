#include "status_led.h"

#include <Arduino.h>

#include "hub_config.h"

void StatusLed::begin() {
  pinMode(hg::kPinStatusLed, OUTPUT);
  digitalWrite(hg::kPinStatusLed, LOW);
  on_ = false;
}

void StatusLed::setMode(HubMode mode, uint32_t nowMs) {
  if (mode == mode_) return;
  mode_ = mode;
  modeStartMs_ = nowMs;
}

void StatusLed::update(uint32_t nowMs) {
  bool on = ledOnAt(mode_, nowMs - modeStartMs_);
  if (on == on_) return;
  on_ = on;
  digitalWrite(hg::kPinStatusLed, on ? HIGH : LOW);
}

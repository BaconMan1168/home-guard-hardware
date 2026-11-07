#include "temp_sensor.h"

#include <Adafruit_AHTX0.h>
#include <Wire.h>

#include "hub_config.h"

static Adafruit_AHTX0 aht;
static bool begun = false;

bool sensorBegin() {
  Wire.begin(hg::kPinSda, hg::kPinScl);
  begun = aht.begin();
  return begun;
}

bool sensorRead(float& celsius) {
  if (!begun && !sensorBegin()) return false;
  sensors_event_t humidity, temp;
  if (!aht.getEvent(&humidity, &temp)) return false;
  celsius = temp.temperature;
  return true;
}

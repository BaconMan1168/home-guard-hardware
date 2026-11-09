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

static constexpr uint32_t kMeasureTimeoutMs = 200;

static bool readMeasurement(uint8_t (&data)[6]) {
  Wire.beginTransmission(AHTX0_I2CADDR_DEFAULT);
  Wire.write(AHTX0_CMD_TRIGGER);
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;

  uint32_t startMs = millis();
  do {
    delay(10);
    if (Wire.requestFrom(AHTX0_I2CADDR_DEFAULT, sizeof(data)) != sizeof(data)) return false;
    for (uint8_t& b : data) b = Wire.read();
  } while ((data[0] & AHTX0_STATUS_BUSY) && millis() - startMs < kMeasureTimeoutMs);
  return !(data[0] & AHTX0_STATUS_BUSY);
}

bool sensorRead(float& celsius) {
  if (!begun && !sensorBegin()) return false;
  uint8_t data[6];
  if (!readMeasurement(data)) return false;
  uint32_t raw = (static_cast<uint32_t>(data[3] & 0x0F) << 16) | (static_cast<uint32_t>(data[4]) << 8) | data[5];
  celsius = raw * 200.0f / 0x100000 - 50;
  return true;
}

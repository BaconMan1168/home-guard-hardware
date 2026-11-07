# Board checklist

Checks to run once the hub board exists. Each plan adds its own section. Run them in order.

## Hub basics

Before flashing, copy `include/secrets.example.h` to `include/secrets.h` and fill in the Wi-Fi and Firebase values.

1. Flash and open the serial monitor (`pio run -e esp32dev -t upload -t monitor`): "HomeGuard hub" prints and the LED double-blinks.
2. Serial shows a plausible AHT20 temperature, and the reading matches a separate thermometer within ±0.5 °C. Test this with the sensor mounted away from the LEDs and the ESP32.
3. In the Firebase console, `devices/<id>/telemetry` updates every ~60 s, `temperatureReadings` grows by one per minute, and `remote` shows `{"learned":false}`. The `<id>` is printed on serial as "Device ID".
4. In the console, set `commands/latest` to `{"value":"OFF","requestedAt":{".sv":"timestamp"},"source":"app"}`: `commands/result.status` becomes `"no-code"` within a few seconds.
5. Reboot the hub: no new `commands/result` is written (the old command is ignored).
6. Unplug the router for 2 minutes, then plug it back in: the LED double-blinks and then turns solid, and telemetry resumes.

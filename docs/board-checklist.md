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

## Teach remote

1. In the console, set `commands/learnRequest` to `{"requestedAt":{".sv":"timestamp"}}`: `learn/status.state` becomes `listening` and the LED fast-blinks.
2. Point the AC remote at the hub and press OFF: the state becomes `saved`, and `remote` shows `learned:true` with a `kind` and `protocol`. Note the kind for the demo AC.
3. Repeat the request and wait 30 s without pressing anything: `timeout`, and `remote` is unchanged.
4. Reboot: no new `listening` (the leftover request is ignored), and `remote` still shows the saved code.
5. With the AC on, set `commands/latest` to OFF (as in Hub Basics step 4): the AC turns off and `commands/result.status` is `"sent"`. A phone camera shows the IR LEDs flickering 3 times for `ac-state` and once otherwise.

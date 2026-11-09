# Board checklist

Checks to run once the hub board exists. Each plan adds its own section. Run them in order.

## Hub basics

Before flashing, copy `include/secrets.example.h` to `include/secrets.h` and fill in the Firebase values. Wi-Fi is set up over Bluetooth (see "Bluetooth setup and reset"); if your `secrets.h` still has `WIFI_SSID` / `WIFI_PASSWORD`, delete them.

1. Flash and open the serial monitor (`pio run -e esp32dev -t upload -t monitor`): "HomeGuard hub" prints and the LED double-blinks.
2. Serial shows a plausible AHT20 temperature, and the reading matches a separate thermometer within ±0.5 °C. Test this with the sensor mounted away from the LEDs and the ESP32.
3. In the Firebase console, `devices/<id>/telemetry` updates every ~60 s, `temperatureReadings` grows by one per minute, and `remote` shows `{"learned":false}`. The `<id>` is printed on serial as "Device ID".
4. In the console, set `commands/latest` to `{"value":"OFF","requestedAt":{".sv":"timestamp"},"source":"app"}`: `commands/result.status` becomes `"no-code"` within a few seconds.
5. Reboot the hub: no new `commands/result` is written (the old command is ignored).
6. Unplug the router for 2 minutes, then plug it back in: the LED double-blinks and then turns solid, and telemetry resumes.
7. Pull the AHT20 SDA wire while the hub runs: serial logs "Sensor read failed" each minute, and a console `commands/latest` write still gets a `commands/result`. Reconnect it: readings resume.

## Teach remote

1. In the console, set `commands/learnRequest` to `{"requestedAt":{".sv":"timestamp"}}`: `learn/status.state` becomes `listening` and the LED fast-blinks.
2. Point the AC remote at the hub and press OFF: the state becomes `saved`, and `remote` shows `learned:true` with a `kind` and `protocol`. Note the kind for the demo AC.
3. Repeat the request and wait 30 s without pressing anything: `timeout`, and `remote` is unchanged.
4. Reboot: no new `listening` (the leftover request is ignored), and `remote` still shows the saved code.
5. With the AC on, set `commands/latest` to OFF (as in Hub Basics step 4): the AC turns off and `commands/result.status` is `"sent"`. A phone camera shows the IR LEDs flickering 3 times for `ac-state` and once otherwise.

## Bluetooth setup and reset

Use Espressif's free **ESP BLE Provisioning** app (iOS/Android), so this works before the HomeGuard app's Add hub exists. In the app's settings, change the device name prefix from `PROV_` to `HomeGuard-`, or it will not list the hub.

1. Fresh flash: the LED slow-blinks, and the phone sees `HomeGuard-XXXXXX`.
2. Connect with PoP `homeguard-pop-v1` and pick the home network. Enter the wrong password first: the app reports failure and the hub still accepts a retry. Then enter the right password: the LED double-blinks and then turns solid, and telemetry appears under `devices/homeguard-xxxxxx`.
3. Reboot: no Bluetooth advertising, and it reconnects straight to Wi-Fi.
4. Hold BOOT for 10 s: the LED blinks fast, `remote` becomes `{"learned":false}`, and the hub restarts slow-blinking. Wi-Fi and the learned code are gone.
5. Power off the router and reboot the hub: the LED keeps double-blinking with no Bluetooth advertising, and it recovers when the router comes back.
6. Hold BOOT for 5 s and release: nothing happens.

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
7. Pull the AHT20 SDA wire while the hub runs: serial logs "Sensor read failed" each minute, `telemetry` keeps updating `lastSeenAt` with no `temperatureCelsius`, and a console `commands/latest` write still gets a `commands/result`. Reconnect it: readings resume.
8. Leave the hub online for 2 hours: Firebase console > Authentication shows one anonymous user for the hub, not one per hour, and telemetry never stops.
9. Put the hub on a very slow link (a phone hotspot with one bar, or a router speed limit of ~64 kbit/s) for 10 minutes, and drop it to nothing for 30 s a few times. Send OFF and a learn request during it: each still gets its `commands/result` / `learn/status` (late is fine, missing is not), the hub never restarts, and telemetry resumes at full rate once the link recovers.
10. Block NTP at the router (UDP port 123) and reboot the hub: serial prints "clock set from Firebase sign-in time", and telemetry `lastSeenAt` is within a few seconds of real time.
11. Unplug the hub within a second of writing `commands/latest` (OFF). Plug it back in: `commands/result` ends up as `sent` or `interrupted` for that `requestedAt`, never missing, and the AC never gets OFF twice.
12. With Wi-Fi active, send OFF 10 times in a row (a new `requestedAt` each time) on the real power supply: serial never shows "Restarted after a brownout". If it does, add the capacitor in the design doc's hardware list (item 4).
13. Soak for 48 hours: the hub stays online, and the "largest block" in the once-a-minute serial "Heap free" line never trends down. Note the lowest value. If it's near 20000, lower `kMinFreeHeapBlockBytes` before shipping so the hub doesn't restart needlessly.

## Teach remote

1. In the console, set `commands/learnRequest` to `{"requestedAt":{".sv":"timestamp"}}`: `learn/status.state` becomes `listening` and the LED fast-blinks.
2. Point the AC remote at the hub and press OFF: the state becomes `saved`, and `remote` shows `learned:true` with a `kind` and `protocol`. Note the kind for the demo AC.
3. Repeat the request and wait 30 s without pressing anything: `timeout`, and `remote` is unchanged.
4. Reboot: no new `listening` (the leftover request is ignored), and `remote` still shows the saved code.
5. With the AC on, set `commands/latest` to OFF (as in Hub Basics step 4): the AC turns off and `commands/result.status` is `"sent"`. A phone camera shows the IR LEDs flickering 3 times for `ac-state` and once otherwise.
6. Start a learn request, and while `learn/status` is `listening`, unplug the hub. Plug it back in: once online, `learn/status.state` becomes `interrupted` and `remote` is unchanged.
7. Reboot the hub: `commands/result` keeps its old value (the answered command is skipped, not rewritten as `stale`).

## Bluetooth setup and reset

Use Espressif's free **ESP BLE Provisioning** app (iOS/Android), so this works before the HomeGuard app's Add hub exists. In the app's settings, change the device name prefix from `PROV_` to `HomeGuard-`, or it will not list the hub.

1. Fresh flash: the LED slow-blinks, and the phone sees `HomeGuard-XXXXXX`.
2. Connect with PoP `homeguard-pop-v1` and pick the home network. Enter the wrong password first: the app reports failure and the hub still accepts a retry. Then enter the right password: the LED double-blinks and then turns solid, and telemetry appears under `devices/homeguard-xxxxxx`.
3. Reboot: no Bluetooth advertising, and it reconnects straight to Wi-Fi.
4. Hold BOOT for 10 s: the LED blinks fast, `remote` becomes `{"learned":false}`, and the hub restarts slow-blinking. Wi-Fi and the learned code are gone.
5. Power off the router and reboot the hub: the LED keeps double-blinking with no Bluetooth advertising, and it recovers when the router comes back.
6. Hold BOOT for 5 s and release: nothing happens.

# OMOTE-Blaster

A higher-power, always-on IR repeater for the [OMOTE](../OMOTE-Firmware/) remote. The OMOTE finds the blaster on the LAN at wake time and forwards IR sends over WiFi; the blaster fires them through a high-current LED driver. If the blaster isn't reachable, the OMOTE falls back to its built-in IR LED automatically.

> Status: design only — no firmware committed yet. This README is the implementation plan.

---

## Why

The OMOTE remote drives a small onboard IR LED. Range and angle are limited — particularly for AV components inside cabinets or at oblique angles. The blaster sits in line-of-sight of the equipment, mains-powered, with a much beefier driver and (optionally) multiple LEDs. The OMOTE keeps doing all the UI/scene/code-table work; the blaster is a dumb sender.

Goals:
- Zero added latency you can feel on a button press.
- Transparent fallback to local IR when the blaster is off / unreachable.
- Single source of truth for IR codes — the OMOTE's existing device tables. No duplicated tables to maintain.
- OTA firmware updates that work across an IoT VLAN, same way the OMOTE itself does.

---

## Architecture

```
  ┌──────────────────────┐                   ┌──────────────────────┐
  │  OMOTE Remote        │                   │  OMOTE-Blaster       │
  │  (ESP32)             │                   │  (Wemos D1 Mini)     │
  │                      │                   │                      │
  │  Button press        │                   │  HTTP server         │
  │    │                 │   POST /send      │    │                 │
  │    ▼                 │  ───────────────▶ │    ▼                 │
  │  commandHandler IR:  │  {protocol, data, │  IrSender.send()     │
  │    │                 │   nbits, repeat}  │    │                 │
  │    ├─ blaster up? ──▶│                   │    ▼                 │
  │    │  send HTTP      │                   │  High-power IR LED   │
  │    └─ else           │                   │  driver → LEDs       │
  │       local IR LED   │                   │                      │
  └──────────────────────┘                   └──────────────────────┘
            ▲                                          │
            └────────── mDNS browse / advertise ───────┘
                       _omote-blaster._tcp
```

The OMOTE already routes every IR send through a single bottleneck (`commandHandler.cpp` `IR:` case → `sendIRcode_HAL`). We add a check at that bottleneck: if the blaster is reachable, POST `(protocol, data, nbits, repeat)` over HTTP; otherwise drive the local LED as today.

**Discovery:** mDNS — blaster advertises `omote-blaster.local` on `_omote-blaster._tcp`. The OMOTE caches the resolved IP in NVS and tries it first on every wake (mDNS only as fallback) so wake → first-button-press has no perceptible discovery delay.

**WiFi setup:** WiFiManager captive portal on first boot (the blaster has no screen). User joins `OMOTE-Blaster-Setup`, picks home WiFi, enters password, done.

**OTA:** mirrors the OMOTE's existing `ota_hal_esp32.cpp` — port 3232, `POST /update`, multipart firmware upload pushed by `curl` from a PlatformIO `extra_scripts` hook. All connections are dev-machine → device, which is what makes it work across an IoT VLAN.

**Wire format** (`POST /send`, `application/json`):
```json
{ "protocol": 14, "data": "0x4202", "nbits": 15, "repeat": 0 }
```
Response `200 {"ok":true}` on success, `400` on bad input, `500` on send failure.

---

## Hardware (Wemos D1 Mini / ESP8266)

| Function              | Pin            | Notes                                                                      |
|-----------------------|----------------|----------------------------------------------------------------------------|
| IR drive              | D2 / GPIO4     | Into transistor/MOSFET → high-current LED string. 38 kHz carrier in software. |
| WiFi LED              | D5 / GPIO14    | Slow blink in portal/connecting; solid when associated.                    |
| Command-received LED  | D6 / GPIO12    | Lit for 1 s after the most recent HTTP request (`/send`, `/status`, `/update`). |
| Reset-to-portal       | D3 / GPIO0     | Hold low at boot to force the WiFiManager portal even if creds are saved.  |
| Power LED             | (rail)         | No GPIO. Wired to the supply rail through a current-limit resistor — lit whenever powered. |
| IR-active indicator   | (driver line)  | Visible LED in series/parallel with the IR LEDs themselves. Hardware-only — confirms LEDs are actually pulsing. |

Power: USB-C or barrel jack to a 5 V supply. The high-current driver and IR LED selection are hardware-side concerns out of scope for the firmware; the firmware just needs a clean digital signal on D2.

---

## Repo layout

```
OMOTE-Blaster/
├── platformio.ini
├── README.md                  ← this file
├── src/
│   ├── main.cpp               ← setup/loop, wires the modules below
│   ├── wifi_setup.{cpp,h}     ← WiFiManager + reset-to-portal button
│   ├── mdns_service.{cpp,h}   ← advertise _omote-blaster._tcp
│   ├── http_server.{cpp,h}    ← /send, /status, /reset endpoints (port 80)
│   ├── ir_sender.{cpp,h}      ← thin wrapper over IrSender.send + protocol-defaults table
│   ├── ota.{cpp,h}            ← /update endpoint (port 3232) — port of OMOTE ota_hal_esp32.cpp
│   ├── status_led.{cpp,h}     ← non-blocking blink/latch patterns for WiFi + cmd LEDs
│   └── IRremoteProtocols.h    ← copied verbatim from OMOTE-Firmware
└── tools/
    └── ota_upload.py          ← curl-based PlatformIO upload hook (copy from OMOTE)
```

`platformio.ini`:
```ini
[env:d1_mini]
platform = espressif8266
board = d1_mini
framework = arduino
monitor_speed = 115200
lib_deps =
    crankyoldgit/IRremoteESP8266@^2.8.6
    tzapu/WiFiManager@^2.0.16-rc.2
    bblanchon/ArduinoJson@^7
build_flags =
    -DBLASTER_HOSTNAME=\"omote-blaster\"
    -DBLASTER_HTTP_PORT=80
    -DIR_SEND_PIN=4
    -DLED_WIFI_PIN=14
    -DLED_CMD_PIN=12
    -DLED_CMD_HOLD_MS=1000
    -DENABLE_OTA=1

; OTA target — Mac → device only, works across IoT VLAN.
[env:d1_mini-ota]
extends = env:d1_mini
upload_protocol = custom
upload_port = omote-blaster.local
extra_scripts = tools/ota_upload.py
```

---

## Endpoints

**Port 80 (app):**

| Method | Path      | Purpose                                                  |
|--------|-----------|----------------------------------------------------------|
| POST   | `/send`   | Fire IR. Body: `{protocol, data, nbits?, repeat?}`       |
| GET    | `/status` | Health/version: `{ok, version, uptime, rssi}`            |
| POST   | `/reset`  | Reboot (debug aid, build-flag gated).                    |

**Port 3232 (OTA, when `ENABLE_OTA=1`):**

| Method | Path      | Purpose                                                                          |
|--------|-----------|----------------------------------------------------------------------------------|
| POST   | `/update` | Multipart firmware upload (`firmware=@build.bin`). Returns `OK` then reboots.    |

Push a build with `pio run -e d1_mini-ota -t upload`.

`/send` is synchronous — parse JSON, call `IrSender.send((decode_type_t)p, strtoull(data, NULL, 0), nbits, repeat)`, return. IR sends take a few ms to a few hundred ms; the HTTP request stays open until the LED finishes pulsing. Button-press cadence is well under 10/s, so this is fine.

### Implementation notes

- `IrSender` constructed once: `IRsend irsend(IR_SEND_PIN); irsend.begin();`
- Protocol defaults: copy `getProtocolDefaultBitsAndRepeat()` from [OMOTE-Firmware/hardware/ESP32/infrared_sender_hal_esp32.cpp:24-52](../OMOTE-Firmware/hardware/ESP32/infrared_sender_hal_esp32.cpp#L24-L52). Pure data, ~30 entries.
- Reuse the same `IRremoteProtocols.h` enum — protocol numbers must agree byte-for-byte across the wire.
- Cap request body at 256 bytes.
- Log every send: `[IR] proto=14 data=0x4202 nbits=15 rep=0 -> ok`.
- IRremoteESP8266 on ESP8266 is tight on flash if all protocols are enabled. Use the per-protocol `#define`s (`SEND_NEC`, `SEND_SHARP`, …) — enable only the set the OMOTE uses.

### OTA implementation

Direct port of [OMOTE-Firmware/hardware/ESP32/ota_hal_esp32.cpp](../OMOTE-Firmware/hardware/ESP32/ota_hal_esp32.cpp) (~80 lines). Same shape; just swap the includes:

- `WebServer` → `ESP8266WebServer`
- `<ESPmDNS.h>` → `<ESP8266mDNS.h>`
- `<Update.h>` → `<Updater.h>` (the `Update` global has the same `begin/write/end` API on both)
- `MDNS.addService("http", "tcp", 3232);` alongside the `_omote-blaster._tcp` advertisement.
- During upload: WiFi LED solid; on failure: fast blink, no reboot; on success: brief pause, then `ESP.restart()`.
- Wrap the whole module in `#if (ENABLE_OTA == 1)`.

---

## Remote-side changes (in [`OMOTE-Firmware/`](../OMOTE-Firmware/))

### 1. New module: `blaster_client`

New files: `src/applicationInternal/blasterClient.{cpp,h}`.

The OMOTE sleeps and wakes frequently. mDNS browses can take hundreds of ms to a couple of seconds, which would be visible on the first button press after wake. So the client caches the blaster's IP across deep sleep and tries it first; mDNS is only a fallback.

**Wake-time handshake** (called once after WiFi associates, on every wake/boot):

1. Read `blaster_last_ip` from NVS (Preferences).
2. If non-empty: `GET http://<ip>/status` with a 150 ms timeout. If `200 OK` and the body identifies as an OMOTE blaster → mark available, set the GUI icon.
3. Else: mDNS browse for `_omote-blaster._tcp`. On hit, write IP to `blaster_last_ip` in NVS and run the `/status` handshake.
4. Else: mark unavailable, clear the icon, fall back to local IR. Re-check on next wake or in 60 s.

Steady-state cost is ~one fast LAN HTTP round-trip per wake (5–30 ms typical). The blaster's IP is unlikely to change since it's powered on 24/7 and DHCP keeps renewing the same lease — but if it does, one mDNS fallback recovers and re-caches.

**Public API:**
```cpp
void blaster_init();                    // call after WiFi connects on every wake
bool blaster_isAvailable();             // for the GUI icon
bool blaster_send(int protocol,
                  const std::list<std::string>& payloads,
                  const std::string& additionalPayload);
```

**Implementation notes:**
- HTTP client: ESP32's `HTTPClient`. Timeouts: 150 ms for `/status` handshake, 500 ms for `/send`.
- JSON: ArduinoJson (already in the firmware tree).
- `blaster_send` does *not* re-do mDNS on failure — it just flips `available=false`. Next `blaster_init` (next wake / periodic retry) re-resolves.

### 2. Inject the call in `commandHandler.cpp`

[`OMOTE-Firmware/src/applicationInternal/commandHandler.cpp`](../OMOTE-Firmware/src/applicationInternal/commandHandler.cpp), `IR:` case (~line 187):

```cpp
case IR: {
    auto it = commandData.commandPayloads.begin();
    std::string protocol = *it;
    it = commandData.commandPayloads.erase(it);

    int proto = std::stoi(protocol);
    if (blaster_isEnabled() && blaster_isAvailable() &&
        blaster_send(proto, commandData.commandPayloads, additionalPayload)) {
        break;  // sent via blaster, done
    }
    sendIRcode(proto, commandData.commandPayloads, additionalPayload);  // local fallback
    break;
}
```

This is the only behaviour change to the rest of the firmware. Scenes, devices, button mapping, and the IR HAL are untouched — long-press, scene macros, everything funnels through `executeCommand` so they're covered automatically.

### 3. Connected-blaster icon

Add a small icon to the existing LVGL status-bar row (next to the WiFi indicator). Visibility tracks `blaster_isAvailable()`:
- Shown after a successful handshake.
- Hidden immediately if a `blaster_send` returns false (instant feedback that we've fallen back to local IR).

Exact file path TBD at implementation time — lives wherever the WiFi-status icon is updated. Confirm rather than guess.

### 4. NVS settings

Add to the existing `settings` namespace in [`OMOTE-Firmware/hardware/ESP32/preferencesStorage_hal_esp32.cpp`](../OMOTE-Firmware/hardware/ESP32/preferencesStorage_hal_esp32.cpp):

| Key                | Type   | Default | Purpose                                                          |
|--------------------|--------|---------|------------------------------------------------------------------|
| `blaster_enabled`  | bool   | `true`  | Master switch (UI toggle).                                       |
| `blaster_host`     | String | `""`    | User override — pin a hostname/IP if mDNS misbehaves.            |
| `blaster_last_ip`  | String | `""`    | Auto-cached IP from last successful handshake — fast wake path.  |

`blaster_last_ip` is internal to `blaster_client`, not exposed in the settings UI.

### 5. Settings UI (optional, deferable)

A "Blaster" page under settings: enable toggle, last-discovered hostname (read-only), test-send button, force-rediscover. Defaults work fine without it.

### 6. Modified files summary

- `src/applicationInternal/commandHandler.cpp` — inject the blaster check (~line 187)
- `hardware/ESP32/preferencesStorage_hal_esp32.cpp` — add the three keys above
- `src/main.cpp` — call `blaster_init()` after WiFi/MQTT init (~line 233)
- `platformio.ini` — confirm `bblanchon/ArduinoJson` is present (likely is via the MQTT path)
- The status-bar GUI file(s) — add the blaster icon (path TBD)

---

## Verification

End-to-end smoke test, in order:

1. **Blaster standalone.** Flash the D1 Mini. First boot → portal SSID `OMOTE-Blaster-Setup` appears; configure home WiFi. From a laptop on the same LAN:
   ```bash
   curl http://omote-blaster.local/status
   curl -X POST http://omote-blaster.local/send \
        -H 'Content-Type: application/json' \
        -d '{"protocol":14,"data":"0x4202","nbits":15,"repeat":0}'
   ```
   TV should react. Command-received LED lights for 1 s. Serial log shows the send.

2. **mDNS visible.** `dns-sd -B _omote-blaster._tcp` (macOS) lists the blaster.

3. **Remote integration — first-time discovery.** Build/flash modified OMOTE firmware (NVS empty). Serial log: `[blaster] no cached IP, browsing mDNS...` → `[blaster] discovered ... handshake 200`. Status-bar icon appears. Press a TV button → goes via blaster.

4. **Wake performance — cached IP path.** Sleep, wake. Serial log: `[blaster] cached IP ... handshake 200`, round-trip < 100 ms. No mDNS browse. Icon appears without perceptible delay.

5. **Fallback.** Power off the blaster. Press a button → `[blaster] unavailable, falling back`, icon disappears, local IR LED fires (verify with phone camera), TV reacts.

6. **Recovery.** Power blaster back on. Either wait ~60 s or sleep+wake the OMOTE. Cached IP path works, icon reappears.

7. **IP-change recovery.** Force the blaster onto a new DHCP lease. Next wake: cached IP fails fast (~150 ms), mDNS fallback succeeds, new IP is cached.

8. **Status LEDs.**
   - Power LED: lit whenever the unit has power, including before WiFi associates.
   - WiFi LED: slow-blinks during portal/connecting, solid when associated.
   - Command-received LED: 1 s after each request; rapid presses keep it solid.
   - IR-active LED (hardware): visibly flickers in sync with each IR send.

9. **OTA across the IoT VLAN.**
   ```bash
   pio run -e d1_mini-ota -t upload
   ```
   Resolves `omote-blaster.local`, POSTs to `:3232/update`, prints `[OTA] HTTP 200 in N.Ns`, blaster reboots into new firmware. `/status` returns the new version. Repeat from a wired and a wireless dev host to confirm the VLAN crossing.

10. **Stress / edge cases.**
    - Held button (auto-repeat): no request queue blow-up; HTTP timeouts shouldn't stack.
    - Long-press codes: covered automatically (same `executeCommand` path); verify one.
    - Power-cycle the router: both devices reconnect and the OMOTE re-discovers.

---

## Open questions / risks (parking lot)

- **Latency for macros.** HTTP on LAN is 5–15 ms. If a future scene fires a burst of codes, may want pipelining or a WebSocket — easy to add later, same wire format.
- **Multiple blasters.** mDNS browse can return >1 service. v1: pick the first. Revisit if per-device routing is wanted.
- **Security.** Open on LAN. Fine for a home network; if exposed, add a shared-secret header (`X-Blaster-Token`) stored in NVS on both ends.
- **Hardware (out of scope for firmware).** High-current driver, LED selection, heat dissipation. The firmware doesn't care as long as D2 sees a clean digital signal into the driver.

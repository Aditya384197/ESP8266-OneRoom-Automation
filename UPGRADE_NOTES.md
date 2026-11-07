# Upgrade notes — v1.6.0

## Files replaced

Copy every file in this package over the matching path in the repository. It is a patch package, not a complete source ZIP. Keep the existing GitHub Actions workflow unless this patch explicitly supplies a replacement.

## Fixes in this version

1. **Settings navigation:** one shared Back arrow returns exactly one level (detail page → settings menu; settings menu → dashboard). A Home icon remains visible at the opposite side of the header on every settings page and returns directly to the dashboard.
2. **Multiple clients:** the firmware now uses `ESPAsyncWebServer` with `ESPAsyncTCP` instead of the single-client synchronous `ESP8266WebServer`. Multiple phones/browsers can load settings and submit requests without one client blocking the other.
3. **Logo save:** Custom logo uses its own `POST /api/logo` endpoint. It validates and writes only the logo text, responds with a success/error message, and does not reboot or require Wi-Fi settings to be rewritten.
4. **OTA isolation:** a second client cannot change the active client's OTA state while a firmware upload is in progress.
5. **Request memory:** JSON body data uses a bounded per-request buffer allocated/freed with the allocator used by the async request object; oversized bodies are rejected.
6. **Four physical switch inputs:** uses a PCF8574 I/O expander (P0–P3) over I²C. Switch contact closed to GND means ON, open means OFF in maintained mode. In this mode the physical switch position is authoritative and will override a conflicting phone command. Follow-contact-on-change mode sends OFF when the contact opens and ON when it closes, while allowing phone control until the next physical transition. The firmware retries the expander if it is temporarily unavailable.

## Required additional hardware for four physical switches

A PCF8574 module is required; the four inputs are not four extra native ESP8266 pins. The existing relay pins are D1/D2/D5/D6 and factory reset remains D7. The unused native pins include ESP8266 boot-strap/UART pins, so connecting maintained GND switches directly to all four available pins would be unsafe or unreliable.

- PCF8574 VCC → NodeMCU 3V3; PCF8574 GND → NodeMCU GND.
- PCF8574 A0, A1, A2 → GND (address `0x20`).
- PCF8574 SDA → NodeMCU D3/GPIO0.
- PCF8574 SCL → NodeMCU D4/GPIO2.
- Ensure SDA and SCL each have a pull-up to 3.3V (typically 4.7kΩ; verify the module does not pull them to 5V).
- Relay 1 wall switch → P0 and GND; Relay 2 → P1 and GND; Relay 3 → P2 and GND; Relay 4 → P3 and GND.
- Do not connect the wall switches directly to ESP8266 D3/GPIO0 or D4/GPIO2. Do not connect mains voltage to any GPIO or expander pin.

## Build dependencies

The new server requires these pinned PlatformIO libraries (ESP8266-compatible maintained releases):

```ini
ESP32Async/ESPAsyncTCP @ 2.0.0
ESP32Async/ESPAsyncWebServer @ 3.12.1
```

`ArduinoJson @ ^7.4.2` remains required. Run GitHub Actions and confirm that both `firmware.bin` and `littlefs.bin` build successfully before flashing.

## Flash addresses

For the repository's 4 MB NodeMCU v2 and `eagle.flash.4m1m.ld` layout:

- `firmware.bin` → `0x00000`
- `littlefs.bin` → `0x300000`

Use both files from the same successful workflow run. Flashing LittleFS overwrites its contents, which can erase saved settings and passwords. Keep a backup where practical.

## Verification status

The code has received static source checks in this editing environment. PlatformIO and its ESP8266 toolchain were not installed here, so a real compile was not completed locally. The GitHub Actions build is the required compile verification before flashing.

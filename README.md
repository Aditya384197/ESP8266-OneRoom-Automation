# ESP8266 One Room Automation — v1.6.0

A local-first four-relay controller for a NodeMCU ESP8266 with a clean relay-only dashboard, multi-client asynchronous web server and a separate slide-style settings area.

## Hardware pin map (unchanged)

| NodeMCU pin | GPIO | Function |
|---|---:|---|
| D1 | GPIO5 | Relay 1 |
| D2 | GPIO4 | Relay 2 |
| D5 | GPIO14 | Relay 3 |
| D6 | GPIO12 | Relay 4 |
| D7 | GPIO13 | Factory-reset button (hold LOW for 10 seconds) |

Relay output pin mapping is fixed. Each relay independently supports Active-Low or Active-High mode from **Settings → Relay configuration**. The factory default remains Active-Low for all four relays. Verify each relay type with a low-voltage test load before connecting mains equipment.

Power relay coils from a suitable regulated supply. Do not power relay coils from the ESP8266 3.3V output. Mains wiring and enclosure work must be performed safely by a qualified person.

## Dashboard

- Main page shows only the editable logo, one Online/Offline indicator and four large square relay tiles.
- Tap anywhere on a relay tile to toggle it. The ON state uses a green visual treatment; OFF returns to the normal tile appearance.
- Dashboard labels and non-input text are not selectable/copyable. Text fields in Settings remain selectable and editable.
- Online means the ESP8266 is connected to the configured home Wi-Fi (STA). Offline means it is running its local access point without a home Wi-Fi connection.
- The logo text is configurable up to 255 characters; its container grows and wraps to fit.

## Multi-client web access

The dashboard is served by ESPAsyncWebServer with ESPAsyncTCP. The older synchronous ESP8266WebServer handled requests one at a time and could make settings controls appear unresponsive while another phone held a connection. Wi-Fi scanning is also non-blocking: the scan starts in the background and the browser polls for its result.

## Settings menu

Each group opens as an individual slide-style page with a back control:

- Custom logo
- Relay names, individual relay polarity and physical switch mapping
- ESP8266 access-point and home-router Wi-Fi configuration, including network scan
- Countdown timers and weekly schedules
- OTA password creation/change
- OTA firmware upload
- Remote access safety/setup information
- Device status, restart and factory reset

## Physical switch inputs — four extra digital inputs

The four wall-switch inputs are provided by a **PCF8574 I/O expander**. This is intentional: with the four relay outputs fixed to D1/D2/D5/D6 and the factory-reset input on D7, the remaining native ESP8266 pins include boot-strap and UART pins. Connecting four maintained GND switches directly to those pins can prevent boot or interfere with serial operation. The PCF8574 provides four additional switch GPIO inputs without using those pins as direct switch terminals.

| PCF8574 input | Relay | Switch behavior |
|---|---:|---|
| P0 (virtual ID 32) | Relay 1 | Closed to GND = ON; open = OFF |
| P1 (virtual ID 33) | Relay 2 | Closed to GND = ON; open = OFF |
| P2 (virtual ID 34) | Relay 3 | Closed to GND = ON; open = OFF |
| P3 (virtual ID 35) | Relay 4 | Closed to GND = ON; open = OFF |

**PCF8574 wiring:** power VCC from the ESP8266 3.3V pin and GND to GND; tie A0/A1/A2 to GND for I²C address `0x20`; connect SDA to NodeMCU D3/GPIO0 and SCL to D4/GPIO2. Both I²C lines must be pulled up to 3.3V (typically 4.7kΩ; verify whether the module already contains pull-ups). Connect each maintained wall switch between its own P0/P1/P2/P3 input and GND. Do not wire the wall switches directly to D3/D4. Never connect mains voltage to the PCF8574 or ESP8266.

The device retries PCF8574 detection after a temporary I²C failure. If the expander is absent, local phone control and all other firmware features remain available, but physical switch input is inactive until the expander is connected. In **Maintained** mode the physical switch position is authoritative (closed forces ON; open forces OFF), so a phone command that conflicts with the current physical position is immediately overridden. Select **Follow contact on change** when you want each physical opening/closing transition to command OFF/ON while still allowing phone commands to remain effective until the next physical movement.

## Timers and schedules

- One independent countdown timer per relay (from 1 second to 30 days). In Settings, enter hours/minutes and choose the final ON/OFF action.
- Timer state is stored in LittleFS. A five-minute checkpoint limits flash writes.
- When NTP time is available, timers retain an absolute due time and can account for time spent powered off. If the device reboots without a valid clock, there is no battery-backed RTC to measure the outage; the fallback resumes from its last saved checkpoint and may be late by the outage plus up to approximately five minutes.
- Up to eight weekly schedules, using India Standard Time (UTC+05:30). Weekly schedules require NTP synchronization and pause until the clock is synchronized.

## Wi-Fi and OTA

Factory AP defaults:

```text
SSID:     RoomAutomation-ESP
Password: RA8266@Setup2026
IP:       192.168.4.1
```

Change the AP password after first connection. Router credentials are stored on the device and are never returned by the settings API. Leave a password field blank to keep its current saved password.

OTA update is disabled until the owner creates an OTA password in Settings. Previously saved private OTA credentials are retained; the publicly documented v1.3.0 factory OTA credential hash is cleared during upgrade. Only upload a `firmware.bin` built for this board to the OTA form. `littlefs.bin` is a filesystem image and is not an OTA firmware image.

## Remote access — security status

This package does **not** create a public remote tunnel by itself. The ESP8266 serves a local HTTP dashboard; exposing its port 80 directly with router port-forwarding would expose relay control and firmware update operations without TLS. Do not forward port 80/OTA to the public internet.

For safe off-site access, use a VPN-capable home router or an always-on gateway on the same LAN (for example, a device running a private VPN such as Tailscale/WireGuard). Connect the phone to that VPN and then open the ESP8266's private LAN IP. This is a separate gateway configuration; the Remote access menu explains the requirement and clearly reports that no tunnel is configured. Firebase/cloud remote control and user approval are not active in this firmware.

## Storage compatibility and reliability

- Existing DeviceConfig and RelayState records remain compatible.
- The previous automation record is migrated in-place. The v1.5 record keeps relay names, logo text, relay polarity, timers and schedules; its former raw ESP GPIO switch assignments are intentionally remapped to PCF8574 P0–P3. Older v1 records keep relay names, timers and schedules, while newly added logo/polarity fields receive safe defaults.
- The new automation record stores logo text and four independent relay polarity values with CRC validation and a backup file.
- Files are written through temporary-file and backup-rename handling.
- Each relay's OFF electrical level is prepared before its GPIO is switched to output.
- Relay output pins remain D1/D2/D5/D6.
- Wi-Fi reconnection uses backoff and the AP remains available when the router is unavailable.

## Build

```sh
pio run -e nodemcuv2
pio run -e nodemcuv2 -t buildfs
```

`platformio.ini` configures NodeMCU v2, 4 MB flash, LittleFS and the `eagle.flash.4m1m.ld` layout. `prepare_fs.py` copies root web assets to `.piofs` before building the filesystem.

GitHub Actions generates:

- `firmware.bin` — ESP8266 application
- `littlefs.bin` — dashboard/settings filesystem
- A ZIP release bundle with build information and SHA-256 checksums

For the configured 4 MB / 1 MB LittleFS layout, the normal PlatformIO ESP8266 flash offsets are `firmware.bin` at `0x00000` and `littlefs.bin` at `0x300000`. Confirm the board is the 4 MB NodeMCU v2 and use both files from the same workflow run. Flashing LittleFS replaces its contents; it may erase saved settings and credentials.

## v1.6.0 changes

- Replaced the single-client synchronous ESP8266 HTTP server with the maintained ESP32Async asynchronous server and ESP8266 TCP library. This permits multiple phones/browsers to load and use settings routes concurrently.
- Corrected request-body ownership to use a bounded, request-scoped heap buffer that is safely released when the request ends, including aborted requests.
- Fixed OTA upload arbitration so a second client cannot mark the first client's upload as rejected.
- Kept a single one-level Back control and a Home shortcut in the settings header on every settings subpage.
- Kept logo saving on its dedicated `/api/logo` endpoint, which stores only the logo value and does not unnecessarily rewrite Wi-Fi settings or restart the board.
- Added PCF8574-based four-channel switch input support, maintained-switch semantics (closed to GND = ON), boot-safe I²C wiring guidance, and periodic expander reconnect attempts.

## v1.5.0 baseline features retained

- Replaced the busy main page with a minimal logo/status/four-relay layout.
- Made each whole relay tile tappable, with separate ON/OFF styling.
- Disabled long-press text selection/copy across dashboard and settings labels while preserving editable form fields.
- Reorganized all configuration into a compact slide-style settings menu and subpages.
- Added editable dashboard logo text (up to 255 characters).
- Added independent Active-Low/Active-High selection and persistence for each relay output.
- Added in-place migration from the previous automation storage format.
- Moved timers, schedules, Wi-Fi, OTA and recovery controls into their own settings sections.
- Added an explicit remote-access safety page; a secure VPN gateway is still required for off-site access.

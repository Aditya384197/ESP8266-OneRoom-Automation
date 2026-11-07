# ESP8266 One Room Automation — v1.4.0

A local-first four-relay controller for NodeMCU ESP8266. The local dashboard, physical inputs (when configured), countdown timers and weekly schedules run on the device and do not require Firebase or an Internet connection.

## Hardware mapping

| NodeMCU pin | GPIO | Function |
|---|---:|---|
| D1 | GPIO5 | Relay 1 |
| D2 | GPIO4 | Relay 2 |
| D5 | GPIO14 | Relay 3 |
| D6 | GPIO12 | Relay 4 |
| D7 | GPIO13 | Factory-reset button (hold LOW for 10 seconds) |

The four relay outputs are fixed to the mapping above. Relay polarity is configurable as Active-Low or Active-High in Settings. The default remains Active-Low for the relay module used by the original project.

Power the relay coils from a suitable regulated supply. Do not power relay coils from the ESP8266 3.3V output. Mains wiring and enclosure work must be performed safely by a qualified person. Test relay behavior with low-voltage loads before connecting mains equipment.

## Physical switch inputs

Four configurable physical switch inputs are supported. They are **disabled by default** because no physical input wiring was specified and some available ESP8266 pins affect boot mode. Configure each switch under Settings after confirming your board wiring.

Available spare input choices:

| NodeMCU | GPIO | Electrical / boot consideration |
|---|---:|---|
| D0 | GPIO16 | No internal pull-up; an external pull-up is required for an Active-Low switch. |
| D3 | GPIO0 | Boot strap: must be HIGH during reset; a switch held LOW can prevent normal boot. |
| D4 | GPIO2 | Boot strap: must be HIGH during reset; a switch held LOW can prevent normal boot. |
| D8 | GPIO15 | Boot strap: must be LOW during reset; external circuitry must not pull it HIGH at reset. |

D3, D4 and D8 are not ideal for maintained wall switches because their state can prevent boot. Use them only with an interface that guarantees the required reset level. For a robust permanent installation, use an external GPIO expander or dedicated switch interface. D0 requires an external resistor; do not assume its internal pull-up exists.

Each switch can be configured as:

- **Momentary:** a debounced press toggles its paired relay.
- **Maintained:** the stable electrical state directly determines the relay state.

Switch polarity is independently configurable. A switch pin cannot be assigned to more than one relay.

## Local dashboard and settings

- Responsive mobile dashboard for all four relay outputs.
- Live local state polling and device health details.
- Configurable AP/STA Wi-Fi, AP channel and device name.
- Real 2.4 GHz Wi-Fi scan, including hidden network indication.
- Captive DNS redirect and AP fallback at `192.168.4.1`.
- Editable relay names and Active-Low / Active-High relay output logic.
- OTA firmware upload and ArduinoOTA are disabled until an owner creates an OTA password in Settings. On upgrade, a previously configured private password is retained; the publicly documented v1.3.0 factory password hash is automatically cleared and must be replaced.
- Passwords are not returned by the settings API.

Default setup AP (change its password after first connection):

```text
SSID:     RoomAutomation-ESP
Password: RA8266@Setup2026
IP:       192.168.4.1
```

## Timers and schedules

- One independent one-shot countdown timer per relay (1 second to 30 days).
- The timer can set its relay ON or OFF when it expires.
- Timer state is stored in LittleFS. A five-minute checkpoint limits flash writes.
- When NTP time is available, timers retain an absolute due time and can account for the time spent powered off. If the device reboots while it has no valid clock, there is no battery-backed RTC to measure the outage: the fallback resumes from the last saved checkpoint, so the timer may be late by the outage duration plus up to approximately five minutes. This limitation is explicit; the device does not guess elapsed offline time.
- Up to eight weekly schedules, each selecting a relay, ON/OFF action, days and local time.
- Weekly schedules use India Standard Time (UTC+05:30) and require an NTP-synchronized clock. They run on the ESP8266 itself and are paused until time is synchronized. There is no battery-backed RTC, so schedules cannot be guaranteed during a long power outage while the device is offline.

## Storage and reliability

- Existing `DeviceConfig` and `RelayState` binary layouts are preserved for v1.3.0 compatibility.
- New switch/timer/schedule data is stored separately in `/automation.bin` with CRC validation and a backup file.
- Config, relay state and automation files are written through a temporary file and backup rename.
- Relay OFF electrical level is written before enabling GPIO output, based on the selected polarity.
- Relay GPIOs remain D1/D2/D5/D6; no relay mapping change is made.
- Wi-Fi reconnection waits for association attempts to complete and uses backoff.
- AP remains available when the router is unavailable.
- Main loop uses non-blocking switch debounce and automation processing; Wi-Fi scan is a synchronous maintenance action.

## Cloud and security status — important

**Firebase Authentication, Realtime Database synchronization, remote control, Admin Approval, Pending/Approved user workflow and role permissions are not implemented in this v1.4.0 package.** The device has no Firebase project credentials or trusted device identity in the supplied repository, and adding a pretend placeholder would not provide working or secure remote control. Do not treat the local HTTP dashboard as a remote-control security boundary.

The local HTTP API currently has no user-login layer. Anyone who can join the device AP or reach its HTTP server on the home LAN may be able to operate relays or change settings. Change the default AP password, keep the controller on a trusted LAN, and **do not expose port 80 or OTA directly to the public Internet**. A proper Firebase phase must use authenticated user accounts, a separately provisioned device identity, TLS certificate validation, RTDB security rules, and admin UID provisioning; no service-account private key belongs on the ESP8266 or in GitHub.

OTA uses the ESP8266 Arduino core's password-hash mechanism (MD5 compatibility API), not a replacement for TLS or network isolation. Use OTA only on a trusted local network.

## Build with PlatformIO

Firmware and LittleFS are separate targets:

```sh
pio run -e nodemcuv2
pio run -e nodemcuv2 -t buildfs
```

`platformio.ini` sets `src_dir = .` and `data_dir = .piofs`. `prepare_fs.py` copies the four required web assets to `.piofs` and stops the build if any asset is missing.

GitHub Actions builds and verifies both images separately. Workflow artifacts are:

- `ESP8266-firmware` — `firmware.bin`
- `ESP8266-littlefs` — `littlefs.bin`
- `ESP8266-OneRoom-Automation-1.4.0` — release bundle, build information and SHA-256 checksums

The firmware and LittleFS images must both be flashed for the updated dashboard and settings page to appear. Use the exact offsets/layout reported by the build process for your board/environment; do not guess offsets from another ESP8266 board.

## Version 1.4.0 changes

- Added configurable physical switch input handling, debounce and momentary/maintained modes.
- Added one-shot countdown timer and weekly schedule engine.
- Added persistent relay labels and settings UI for relay polarity and switch mapping.
- Fixed PlatformIO source/data directory configuration and LittleFS asset preparation.
- Corrected relay OFF preload for Active-High and Active-Low output modes.
- Improved Wi-Fi reconnect pacing and timer fallback after offline reboot.
- Disabled OTA on factory-new configurations until an owner configures a password; also cleared the previously published v1.3.0 factory OTA credential during migration.
- Firebase and remote user-management work remains a separate, explicitly incomplete phase.

# ESP8266 One Room Automation — v1.5.0

A local-first four-relay controller for a NodeMCU ESP8266 with a clean relay-only dashboard and a separate slide-style settings area.

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

## Physical switch inputs

Four configurable physical switch inputs are supported. They are **disabled by default** because no physical input wiring was specified and some available ESP8266 pins affect boot mode.

| NodeMCU | GPIO | Electrical / boot consideration |
|---|---:|---|
| D0 | GPIO16 | No internal pull-up; an external pull-up is required for an Active-Low switch. |
| D3 | GPIO0 | Boot strap: must be HIGH during reset; a switch held LOW can prevent normal boot. |
| D4 | GPIO2 | Boot strap: must be HIGH during reset; a switch held LOW can prevent normal boot. |
| D8 | GPIO15 | Boot strap: must be LOW during reset; external circuitry must not pull it HIGH at reset. |

Use D3, D4 and D8 only with an interface that guarantees the correct reset level. For a robust permanent installation, use an external GPIO expander or dedicated switch interface. D0 requires an external resistor; do not assume an internal pull-up exists. Never connect mains voltage directly to a GPIO.

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
- The previous automation record version is migrated in-place. Relay names, switch mappings, timers and schedules are preserved; each relay is initialized as Active-Low until the owner selects its real trigger type. The new logo receives the default text.
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

## v1.5.0 changes

- Replaced the busy main page with a minimal logo/status/four-relay layout.
- Made each whole relay tile tappable, with separate ON/OFF styling.
- Disabled long-press text selection/copy across dashboard and settings labels while preserving editable form fields.
- Reorganized all configuration into a compact slide-style settings menu and subpages.
- Added editable dashboard logo text (up to 255 characters).
- Added independent Active-Low/Active-High selection and persistence for each relay output.
- Added in-place migration from the previous automation storage format.
- Moved timers, schedules, Wi-Fi, OTA and recovery controls into their own settings sections.
- Added an explicit remote-access safety page; a secure VPN gateway is still required for off-site access.

# ESP8266 OneRoom Automation — v1.7.2

A local-first four-relay controller for NodeMCU ESP8266. The local web dashboard, physical switch, countdown timers and weekly schedules work without Firebase or internet. Firebase is an optional remote-control channel; it never replaces local control.

## Hardware pin map

| NodeMCU pin | ESP8266 GPIO | Function |
|---|---:|---|
| D1 | GPIO5 | Relay 1 |
| D2 | GPIO4 | Relay 2 |
| D5 | GPIO14 | Relay 3 |
| D6 | GPIO12 | Relay 4 |
| D7 | GPIO13 | One configurable maintained physical switch input |

The four relay output pins are fixed and are not configurable. Every relay independently supports Active-Low or Active-High mode. Verify each relay board with a low-voltage test load before connecting mains equipment. Use a suitable regulated relay supply; do not power relay coils from the ESP8266 3.3V output.

### D7 / GPIO13 switch wiring

Only one physical maintained switch is supported directly by this version. In **Settings → Relay configuration**, select which of the four relays the switch controls.

- Firmware enables the internal `INPUT_PULLUP`; no external pull-up resistor is required.
- Connect a dry-contact switch between D7/GPIO13 and GND.
- Closed to GND = ON; open = OFF.
- Use 3.3V logic only. Never connect mains voltage to D7 or any ESP8266 GPIO.
- The physical factory-reset input has been removed. Factory reset is available from Settings → System and recovery, and remotely through an Admin-only Firebase command from the future app.

The selected relay is owned by the maintained physical switch. Dashboard/manual, countdown timer, weekly schedule and Firebase commands cannot override it. Its timer and schedule entries are disabled when the relay is assigned to the switch. Other relays continue to work normally.

## Dashboard and settings

- Main page: editable logo, Wi-Fi status and four large relay cards.
- Relay taps update optimistically, then reconcile with the device response; a rejected command rolls back the card state.
- Settings retain slide navigation and separate pages for relay configuration, Wi-Fi, timers/schedules, OTA and recovery.
- Relay names and polarity save through `/api/relay-config` without reboot. Wi-Fi settings use `/api/settings` and restart to apply network changes.

## Firebase remote access

Firmware supports Firebase Authentication Email/Password REST sign-in, refresh-token rotation, Realtime Database REST polling, command acknowledgement and actual-state reporting.

- Configure a **dedicated device-only Firebase Authentication account** in Settings → Remote access. Do not use the Admin account.
- Before saving in the device UI, create the single Admin Authentication account, set its immutable UID in `firebase.database.rules.json`, publish the rules, create the device account, and add its `deviceAccounts/{uid}` record. Follow `FIREBASE_SETUP.md` exactly.
- Authentication uses the Firebase `accounts:signInWithPassword` endpoint; the firmware retains a refresh token and refreshes short-lived ID tokens. Realtime Database REST requests send that ID token using the `auth` query parameter.
- TLS certificate verification remains enabled using the bundled Google Trust Services root CA set in `FirebaseCerts.h`. The code does not use `setInsecure()`.
- The device account may read its own device node and write only the actual `state` and `commandAck` for its mapped device. It cannot approve users or send user relay commands. The fixed Admin UID can request a remote factory reset with `commands/factoryReset = {"id":"unique-id","confirm":"RESET"}`; approved non-admin users cannot do this.
- The Android app will later create only `users/{uid}` records in `pending` state. Only the one fixed Admin UID may approve/reject/revoke users or create `userDevices/{uid}/{deviceId}` access links.
- Relay commands are idempotent desired states, for example `commands/relay1 = {"id":"unique-command-id","value":true}`. Always use a new unique ID for each new command. The ESP8266 publishes the last processed ID to `commandAck` and reports its actual relay states, `physicalSwitchRelay`, `online`, `lastSeen`, uptime, RSSI and free heap.
- `lastSeen` is the reliable heartbeat. When the device loses power or Wi-Fi, it cannot immediately publish `online:false`; the app should consider a device online only while `lastSeen` is recent (for example, within 30 seconds).

**Security and performance limits:** Do not forward the ESP8266 local HTTP port or OTA route to the public internet. Firebase API key is a project identifier, while the refresh token is a secret. The refresh token is stored in LittleFS, which is not encrypted by this firmware and can be read by someone with physical flash access. The ESP8266 HTTP/TLS client is synchronous; an HTTPS operation can temporarily delay its cooperative main loop. The physical switch is not a safety-rated emergency stop or mains disconnect. Google Trust Services roots should be reviewed and refreshed periodically as certificate chains and CA policies change.

## Timers and schedules

- One independent countdown per relay, from 1 second to 30 days.
- A five-minute timer checkpoint limits LittleFS writes.
- When NTP is available, a timer keeps an absolute due time and can account for powered-off time. Without a valid clock at reboot, the monotonic fallback resumes from the most recent saved checkpoint; outage time cannot be measured without a battery-backed RTC.
- Up to eight weekly schedules use India Standard Time (UTC+05:30) and remain paused until NTP time is synchronized.

## Wi-Fi and OTA

Factory AP defaults:

```text
SSID:     RoomAutomation-ESP
Password: RA8266@Setup2026
IP:       192.168.4.1
```

Change the AP password after first setup. Router credentials are stored locally and are not returned by the settings API. OTA remains disabled until the owner creates a private OTA password. Upload `firmware.bin` to the OTA page; `littlefs.bin` is a filesystem image, not an OTA firmware image.

## Storage and migration

- DeviceConfig and RelayState remain compatible.
- The automation record is migrated to v5. Existing v4 switch assignment on D0/GPIO16 is automatically remapped to D7/GPIO13. Relay names, logo, relay polarity, timers and schedules are preserved from v1/v2/v3/v4. Old PCF8574/virtual GPIO switch mappings are disabled; select the single D7/GPIO13 switch and wire the dry contact to GND (internal pull-up enabled).
- Firebase configuration is stored separately in `/firebase.bin` with CRC and a backup file. The device password and short-lived ID token are not stored; only the refresh token is retained.
- Configuration writes use a temporary file and backup-rename handling.

## Function audit

See `FUNCTION_AUDIT.md` for the reviewed function list, static checks and explicit runtime-test limitations.

## Build

```sh
pio run -e nodemcuv2
pio run -e nodemcuv2 -t buildfs
```

The PlatformIO environment targets NodeMCU v2, 4 MB flash, LittleFS and `eagle.flash.4m1m.ld`. GitHub Actions produces both images and a ZIP with build information and SHA-256 checksums:

- `firmware.bin` → flash address `0x00000`
- `littlefs.bin` → flash address `0x300000`

Use both files from the same successful workflow run. Flashing `littlefs.bin` overwrites the filesystem and may erase saved Wi-Fi, OTA and Firebase configuration. Back up settings before flashing where possible.

## v1.7.2 changes

- Moved the maintained physical switch to D7/GPIO13 and removed the physical factory-reset input. Dashboard factory reset and Admin-only Firebase remote reset are supported.
- Enforced physical-switch priority in the main automation loop, manual API, timers, weekly schedules and Firebase commands.
- Split relay names/polarity/switch selection into a dedicated no-reboot API and immediately reapplied relay output levels after polarity changes.
- Added Firebase Authentication device-account sign-in, verified TLS REST access, refresh-token rotation, RTDB command polling/acknowledgement and actual-state heartbeat reporting.
- Added a fixed-Admin-UID RTDB rules template, Admin-only remote factory-reset command, and `FIREBASE_SETUP.md` provisioning instructions for the later Android app's pending/approved/rejected/revoked flow.
- Fixed dashboard optimistic rendering so it does not momentarily change Wi-Fi status or overwrite relay names.


## OTA update (v1.7.2)

Open Settings → OTA firmware update. Choose `firmware.bin` and `littlefs.bin` from the **same** GitHub Actions build and enter the OTA password. Press Install once. The browser uploads firmware first, waits for the ESP8266 to restart, then uploads LittleFS and waits for the final restart. The device backs up and restores saved Wi-Fi, relay, automation, OTA and Firebase configuration across the filesystem replacement. Keep stable power connected until the page reports completion.


**First v1.7.2 installation:** flash both `firmware.bin` and `littlefs.bin` once using USB/serial. The combined OTA screen is included in the v1.7.2 LittleFS image. Subsequent releases can be installed together from the dashboard with one button press.

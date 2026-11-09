# v1.7.1 function audit — ESP8266 OneRoom Automation

This is a source-level review and static validation record. It is not a substitute for compiling with the ESP8266 toolchain or testing a physical board and Firebase project.

## Requested GPIO and reset changes

- D1/GPIO5, D2/GPIO4, D5/GPIO14 and D6/GPIO12 remain the four fixed relay outputs.
- D7/GPIO13 is the one maintained physical-switch input. Firmware configures `INPUT_PULLUP`; the switch contact connects D7 to GND. Closed = ON, open = OFF.
- The physical factory-reset GPIO constant, hold timer, polling function and `pinMode` setup have been removed from `main.cpp`.
- Local factory reset remains available through Settings → System and recovery (`POST /api/factory-reset`, explicit `RESET` confirmation).
- The future app can request remote factory reset by writing `devices/{deviceId}/commands/factoryReset` with `{id, confirm:"RESET"}`. Realtime Database rules allow this command only to the fixed Admin UID. The device acknowledges it, resets its local configuration, drives relays OFF using factory-default polarity and restarts.
- Automation storage is now v5. A v4 assignment to D0/GPIO16 is migrated to D7/GPIO13; relay names, logo, polarity, timers and schedules are preserved. Old virtual expander assignments remain disabled.

## Source-level function review

| Area | Reviewed behavior |
|---|---|
| Boot / main loop | LittleFS mount and recovery, config loading, relay initialization, automation initialization, Wi-Fi, Firebase, web server, OTA and periodic diagnostics |
| RelayManager | Fixed GPIO map, per-relay Active-Low/Active-High output, saved-state restore, state persistence, all-off handling |
| Physical switch | INPUT_PULLUP, 10 ms sampling, 45 ms debounce, maintained-level behavior, boot-time state initialization and priority over conflicting commands |
| Manual relay API | Index validation and HTTP 409 rejection for a relay owned by the physical switch |
| Timer API / engine | 0–30 day validation, cancellation, physical-priority rejection, monotonic fallback, optional NTP due time and five-minute persistence checkpoint |
| Schedule API / engine | Slot, relay, day-mask and time validation; NTP gate; duplicate-per-day protection; physical-priority rejection |
| Relay configuration | Four names and four polarity values validated; one optional switch relay; no-reboot persistence; output polarity reapplied; switch config reloaded |
| General settings | AP/STA/device fields validated; Wi-Fi changes restart the device; passwords are not returned by the settings GET response |
| Local factory reset | Requires exact `RESET`; clears Wi-Fi, relay, automation and Firebase settings; reapplies factory-default OFF levels and restarts |
| Firebase setup | Dedicated device account, Email/Password sign-in, device UID mapping check, refresh-token rotation and LittleFS persistence |
| Firebase transport | HTTPS certificate verification, bounded request/response sizes, short timeout, no insecure TLS mode, retry backoff |
| Firebase RTDB | Relay desired-state command polling, command IDs/acknowledgements, physical-priority protection, actual relay state and heartbeat publication |
| Firebase remote reset | Separate factory-reset command; Admin UID only in rules; acknowledgement before local credentials are cleared |
| Dashboard / settings | Relay optimistic update and rollback, settings APIs, timer/schedule forms, Firebase provisioning UI and local reset confirmation |
| Build workflow | Separate firmware and LittleFS build/staging, required-file checks, checksums and artifact packaging |

## Static checks executed

- `node --check app.js`: passed.
- Extracted inline JavaScript from `settings.html` and ran `node --check`: passed.
- `python -m py_compile prepare_fs.py`: passed.
- Parsed `firebase.database.rules.json`: passed as JSON.
- Parsed all four bundled PEM certificates with OpenSSL: passed.
- GPIO/reset/migration/API assertions: passed; no factory-reset GPIO polling remains in `main.cpp`.
- ZIP integrity: checked separately before delivery.

## Not verified in this environment

- PlatformIO / ESP8266 C++ compilation and linker result.
- GitHub Actions workflow execution.
- Actual D7 switch electrical behavior, relay module polarity, reboot behavior or mains-connected operation.
- Firebase Authentication, deployed Realtime Database Rules, device account permissions, cloud command timing or remote reset on a live project.

Do not mark these runtime checks as passed until the GitHub Actions build and hardware/Firebase tests are completed.

# Upgrade notes — v1.7.2

This ZIP is a complete source tree based on the latest source archive supplied for this task, not a patch-only ZIP. It includes the PlatformIO project and GitHub Actions workflow.

## Main changes

1. **One direct physical switch:** select one relay to be controlled by NodeMCU D7 / ESP8266 GPIO13. The physical factory-reset input has been removed. Firmware enables the internal INPUT_PULLUP; connect a dry-contact switch from D7/GPIO13 to GND. Closed = ON, open = OFF.
2. **Priority:** the selected relay is governed by the maintained switch. Manual/API, timer, schedule and Firebase commands cannot alter it. When selected, its countdown and weekly schedules are disabled. The other three relays keep normal behavior.
3. **Relay settings:** relay names, output polarity and switch assignment save through `POST /api/relay-config`; this does not restart the board. Output polarity changes are reapplied immediately.
4. **Firebase device link:** added a separate Firebase device account, Authentication sign-in and refresh, verified HTTPS to Firebase endpoints, Realtime Database command polling, command acknowledgements and actual state publication.
5. **Account approval rules:** added `firebase.database.rules.json` with default-deny rules and one fixed Admin UID. `FIREBASE_SETUP.md` explains provisioning, device account mapping and the later app's pending/approved/rejected/revoked flow.
6. **Dashboard:** keeps optimistic ON/OFF transition and reconciles or rolls back after API response without changing the displayed Wi-Fi status while pending.
7. **Settings:** added Remote access provisioning and corrected factory-reset help to include Firebase configuration.

## Before enabling the GPIO switch

- Connect the dry-contact switch between D7/GPIO13 and GND; internal INPUT_PULLUP is enabled.
- Never place mains voltage on GPIO13.
- Factory reset is available in the local dashboard and as an Admin-only Firebase command for the future app.

## Before enabling Firebase

- Follow `FIREBASE_SETUP.md`.
- Replace the Admin UID placeholder everywhere in `firebase.database.rules.json` and publish the rules.
- Enable Email/Password Authentication.
- Create a separate device-only Authentication account and add its UID under `deviceAccounts` with matching `deviceId` and `enabled: true`.
- Enter the project Database URL, Web API key and dedicated device credentials in Settings → Remote access.

## Build / test

Run the included GitHub Actions workflow and confirm both `firmware.bin` and `littlefs.bin` are generated. The local editing environment did not have PlatformIO/ESP8266 toolchain, so this package has not been claimed as successfully compiled or hardware-tested. Do not flash until the GitHub Actions compile passes.

For this NodeMCU v2 4 MB layout, flash firmware at `0x00000` and LittleFS at `0x300000`. Both files must come from the same Actions run. LittleFS flashing overwrites settings and credentials.

## Known engineering limit

ESP8266 `HTTPClient` HTTPS calls are synchronous even though the local web server is asynchronous. Firebase network operations are bounded and polled at low frequency with retry backoff, but an active HTTPS call can temporarily delay other cooperative loop work. Use this firmware for normal home automation, not as a safety-certified controller or emergency disconnect.


## v1.7.2 OTA update improvements

- Settings now accepts both `firmware.bin` and `littlefs.bin` in one operation.
- When both are selected, firmware is uploaded first; the browser waits for the new firmware to restart and return, then uploads LittleFS.
- Before a LittleFS image is written, the ESP8266 snapshots the Wi-Fi, device/OTA, relay-state, automation and Firebase records into the dedicated 4 KiB EEPROM flash sector. On boot, it restores those records before normal configuration loading.
- Uploads are type-checked by filename and use the ESP8266 core updater commands `U_FLASH` and `U_FS`.
- Keep the device on stable power throughout the filesystem update. Do not disconnect power while LittleFS is being written.


### First installation note
The v1.7.2 dashboard is what provides the combined two-file OTA workflow. Install v1.7.2 firmware and LittleFS images together once using the normal USB/serial flashing method. After that, future updates can be selected together in Settings and installed with one button press. The two images are deliberately sent in sequence with a reboot between them because the new firmware must be running before the filesystem upload can be processed.

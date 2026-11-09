# Upgrade notes — v1.7.0

This ZIP is a complete source tree based on the latest source archive supplied for this task, not a patch-only ZIP. It includes the PlatformIO project and GitHub Actions workflow.

## Main changes

1. **One direct physical switch:** select one relay to be controlled by NodeMCU D0 / ESP8266 GPIO16. D7/GPIO13 remains reserved for the factory-reset button. GPIO16 requires an external 10 kΩ pull-up to 3.3V and a dry-contact switch to GND. Closed = ON, open = OFF.
2. **Priority:** the selected relay is governed by the maintained switch. Manual/API, timer, schedule and Firebase commands cannot alter it. When selected, its countdown and weekly schedules are disabled. The other three relays keep normal behavior.
3. **Relay settings:** relay names, output polarity and switch assignment save through `POST /api/relay-config`; this does not restart the board. Output polarity changes are reapplied immediately.
4. **Firebase device link:** added a separate Firebase device account, Authentication sign-in and refresh, verified HTTPS to Firebase endpoints, Realtime Database command polling, command acknowledgements and actual state publication.
5. **Account approval rules:** added `firebase.database.rules.json` with default-deny rules and one fixed Admin UID. `FIREBASE_SETUP.md` explains provisioning, device account mapping and the later app's pending/approved/rejected/revoked flow.
6. **Dashboard:** keeps optimistic ON/OFF transition and reconciles or rolls back after API response without changing the displayed Wi-Fi status while pending.
7. **Settings:** added Remote access provisioning and corrected factory-reset help to include Firebase configuration.

## Before enabling the GPIO switch

- Fit the external 10 kΩ pull-up from D0/GPIO16 to 3.3V.
- Connect the switch contact between D0/GPIO16 and GND.
- Never place mains voltage on GPIO16.
- Keep factory reset on D7/GPIO13.

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

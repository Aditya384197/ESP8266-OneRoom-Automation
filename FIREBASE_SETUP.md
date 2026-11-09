# Firebase setup — ESP8266 OneRoom Automation v1.7.2

This firmware uses Firebase Authentication (Email/Password) for a dedicated ESP8266 device account and Firebase Realtime Database (REST API) for remote commands and actual-state reporting. The ESP8266 does not create, approve, reject or revoke people. Those administrator functions are reserved for the later Android app and are enforced by the database rules.

## 1. Create the Firebase project

1. Create or open the Firebase project in the Firebase Console.
2. In **Authentication → Sign-in method**, enable **Email/Password**.
3. Create exactly one administrator account manually from the Firebase Console. Do not add an Admin registration option to the public app.
4. Open the Admin user in Authentication and copy its UID.
5. Open `firebase.database.rules.json` and replace every `REPLACE_WITH_ADMIN_UID` with that exact UID. Keep the quotes around the UID.
6. Create a Realtime Database. Copy its database URL exactly, including `https://` and the hostname, but do not add a trailing path.
7. In **Project settings → General**, copy the Firebase Web API key. It identifies the project; it is not the device password.
8. Open Realtime Database → Rules, paste the edited `firebase.database.rules.json` and publish it.

The rules intentionally deny access by default. Admin access is bound to the one fixed UID. New people may create only their own `users/{uid}` record with `role: "user"` and `status: "pending"`. Only the fixed Admin UID can change that record or link a user to a device. Device accounts cannot issue user relay commands.

## 2. Create a separate ESP8266 device account

1. In Authentication → Users, add another Email/Password account dedicated only to this ESP8266. Do not reuse the Admin account or a person's app account.
2. Copy this device account's Authentication UID.
3. In Realtime Database → Data, create this exact record, replacing `DEVICE_UID` with that UID:

```json
{
  "deviceAccounts": {
    "DEVICE_UID": {
      "deviceId": "room8266",
      "enabled": true
    }
  }
}
```

The `deviceId` must match the Device ID field entered in the ESP8266 settings. The database rules allow this account to read its own device-account record and to update only that device's `state` and `commandAck` nodes.

## 3. Configure the ESP8266

1. Flash the firmware and LittleFS image built by GitHub Actions.
2. Connect the ESP8266 to your home Wi-Fi in the local settings page. Wait until the device clock is synchronized; certificate-verified Firebase HTTPS requires valid time.
3. Open **Settings → Remote access**.
4. Enter the Realtime Database URL, Web API key, Device ID (`room8266` unless you deliberately chose another ID), dedicated device-account email and its password.
5. Tap **Verify and save Firebase connection**. The device signs in, checks that `deviceAccounts/{deviceUid}` exists and is enabled for this Device ID, then stores the refresh token in LittleFS. It does not save the password or the short-lived ID token.

The Firebase refresh token is a secret. ESP8266 LittleFS/flash is not encrypted by this firmware, so someone with physical flash access may extract it. Keep the hardware physically protected and do not expose the local HTTP settings page to the public internet.

## 4. Realtime Database data format

The mobile app will write a unique command ID and the desired boolean state to one relay path. Example: turn Relay 1 ON:

```json
{
  "devices": {
    "room8266": {
      "commands": {
        "relay1": {
          "id": "unique-command-id-123",
          "value": true
        }
      }
    }
  }
}
```

Use a new, unique `id` for every new command. The ESP8266 acknowledges the ID at `devices/room8266/commandAck/relay1`. It publishes the actual applied relay state at `devices/room8266/state/relay1` through `relay4`, plus `physicalSwitchRelay` (0–3, or -1 when not assigned), `online`, `lastSeen` (Unix seconds), `uptimeSec`, `rssi` and `freeHeap`. The app can use `physicalSwitchRelay` to disable remote controls for the relay owned by the wall switch.

The app should consider the device online only while `lastSeen` is recent (for example, within 30 seconds). When Wi-Fi or power is lost, the device cannot write an immediate `online: false` event; the last heartbeat will simply become stale.

## 5. User registration and Admin approval model for the later app

- A person registers with Firebase Authentication Email/Password.
- The app creates `users/{uid}` with `role: "user"`, `status: "pending"`, `createdAt` (server timestamp), and optionally `displayName` and `email`.
- Pending, rejected or revoked users cannot read device data or write relay commands.
- The Admin app lists pending requests, then updates `status` to `approved`, `rejected` or `revoked`.
- To grant control, the Admin app also writes `userDevices/{uid}/{deviceId}: true`. Remove that link to revoke device access. Setting the status to `revoked` also blocks access.
- Never let the public app write `role: "admin"`, change its own approval status, or write `userDevices`.

The fixed Admin UID in the rules is the only Admin identity. The ESP8266 has no Admin password or Admin token and performs no approval operations.

## 6. Local operation and limitations

- The local AP dashboard, physical switch, countdown timers and weekly schedules do not require Firebase or internet access.
- Firebase sync is a separate optional loop. It uses verified TLS root certificates and bounded request/response sizes, but `HTTPClient` on ESP8266 is synchronous: an HTTPS operation can temporarily delay the cooperative main loop. Use the local physical switch as a convenience input, not as a safety-rated emergency stop or mains disconnect.
- The firmware polls approximately every 6 seconds when connected and backs off after failures. A command is an idempotent ON/OFF state, not a toggle.
- Google Trust Services root certificates are embedded in `FirebaseCerts.h`. Certificate authorities and server chains can change; review and refresh the trust bundle periodically before its certificates expire.



### Admin remote factory reset (future Android app)

The future app can issue a remote factory reset only when signed in with the single Admin UID configured in the database rules. Write this object to `devices/{deviceId}/commands/factoryReset`: `{"id":"unique-reset-command-id","confirm":"RESET"}`. The firmware acknowledges the command at `commandAck/factoryReset`, switches all relays off, clears Wi-Fi, relay, timer, schedule and Firebase settings, then restarts. After reset the device must be provisioned again locally. Approved non-admin users cannot issue this command. The local dashboard continues to use `POST /api/factory-reset` with the explicit `RESET` confirmation.

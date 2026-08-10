# ESP8266 One Room Automation

Production-oriented local controller for a NodeMCU V3 ESP8266.

## Hardware mapping

| NodeMCU pin | GPIO | Function |
|---|---:|---|
| D1 | GPIO5 | Relay 1 — Ceiling Fan |
| D2 | GPIO4 | Relay 2 — Charging Socket |
| D5 | GPIO14 | Relay 3 — LED 1 |
| D6 | GPIO12 | Relay 4 — LED 2 |
| D7 | GPIO13 | Factory Reset button |

The selected relay board is a 5V, 4-channel, optocoupler-isolated, Active-Low module with a 3.3V-compatible input stage.

**Important:** the relay coils should be powered from a suitable regulated 5V supply, not from the ESP8266 3.3V rail. Mains wiring must be performed safely by a qualified person.

## Core behavior

- Four independent relays.
- Active-Low relay logic by default.
- Relay state is persisted in LittleFS.
- Power loss does not intentionally clear relay state.
- On the next boot, the saved ON/OFF state is restored.
- Dashboard reads the controller's current state; it does not invent a default browser state.
- No dashboard login.
- STA Wi-Fi configuration.
- ESP8266 AP configuration.
- AP DNS captive portal: clients connected to the ESP8266 AP are redirected to the local dashboard without requiring the user to type the IP address in normal captive-portal-capable devices.
- AP fallback remains available when the home router is unavailable.
- mDNS service is enabled when STA is connected.
- OTA is password protected.
- OTA password is never returned by the web API.
- OTA password change requires Old + New + Confirm.
- Factory reset from the web UI or by holding D7/GPIO13 for 10 seconds.
- Configuration/state files contain magic/version/CRC validation and a backup copy.
- Wi-Fi reconnect uses controlled backoff.
- Main loop avoids long blocking waits.

## Captive portal note

The ESP8266 runs a wildcard DNS server on the AP interface and resolves AP-client hostnames to `192.168.4.1`. Common captive-portal detection URLs are redirected to `/`.

This is designed for the normal captive-portal behavior of phones and laptops. Operating systems decide whether to automatically open their captive-portal window; no firmware can force every browser/OS to do so. If an OS does not auto-open the portal, browse to `http://192.168.4.1/`.

## Default AP

```text
SSID:     RoomAutomation-ESP
Password: RA8266@Setup2026
IP:       192.168.4.1
```

## Initial OTA password

```text
R8!vQ2#nL7@xP4$k
```

The password is intentionally not displayed by the settings API or dashboard. Change it from Settings before putting the controller into long-term service.

## Build

This repository uses PlatformIO and GitHub Actions.

Local:

```text
pio run
pio run --target uploadfs
```

The GitHub workflow builds both firmware and the LittleFS filesystem image and publishes them as workflow artifacts.

## Long-term reliability approach

The firmware is designed for recoverability rather than claiming an impossible guarantee of zero failures for 365 days. It keeps relay state persistent, validates stored data, avoids repeated filesystem writes when a state has not changed, uses Wi-Fi backoff, and keeps the AP configuration path available when STA connectivity is lost.

Before connecting mains loads, bench-test all four relay channels at low voltage and verify the exact relay board's input behavior.


## v1.3.0
- Real ESP8266 2.4 GHz Wi-Fi scanning with hidden-network detection.
- Normal SSID selection fills the SSID field and focuses the password field.
- Hidden networks are clearly marked; their SSID must be entered manually because it is not broadcast.
- STA connection remains channel-agnostic so router channel changes are handled automatically.
- Compact relay controls reduce dashboard scrolling.

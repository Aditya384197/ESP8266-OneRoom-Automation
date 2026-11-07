# Upgrade package notes — v1.4.0

## Source baseline

This package was prepared from the uploaded `ESP8266-OneRoom-Automation-main` source archive. Its contents matched the GitHub `main` branch at commit:

`1fe59debb53df53b73ef33945550d7fd20ac0fa7`

Repository: `Aditya384197/ESP8266-OneRoom-Automation`

## What this package changes

- Adds `AutomationManager.cpp/.h` for configurable physical inputs, one-shot countdown timers and up to eight weekly schedules.
- Preserves the existing relay output GPIO map: GPIO5, GPIO4, GPIO14 and GPIO12.
- Adds a separate CRC-protected automation settings record, leaving the v1.3.0 `DeviceConfig` and `RelayState` record layouts unchanged.
- Corrects relay OFF-level preload for either output polarity.
- Improves Wi-Fi association retry pacing and timer fallback handling. Without a battery-backed RTC, powered-off time cannot be deducted when the device reboots without NTP; this is documented rather than estimated.
- Clears the previously published v1.3.0 factory OTA password hash during migration; a private OTA password must be configured again. Previously customized OTA hashes are retained.
- Reworks the dashboard/settings UI and adds relay naming, polarity, switch mapping, countdown and weekly schedule controls.
- Corrects PlatformIO `src_dir`/`data_dir` configuration and prepares/verifies the LittleFS asset directory.
- Updates GitHub Actions to build firmware and LittleFS independently and upload/checksum both artifacts.

## Hardware setup required

Physical switch inputs are deliberately disabled on first boot. The repository did not specify the physical switch wiring. Configure the four inputs in Settings only after confirming the actual circuit. GPIO0/D3, GPIO2/D4 and GPIO15/D8 are ESP8266 boot-strap pins. GPIO16/D0 requires an external pull-up for an Active-Low input. See `README.md` before wiring.

## Explicitly not included

Firebase Authentication, Realtime Database, remote control, user approval, roles and cloud/local command synchronization are not implemented by this package. No Firebase project configuration, device identity, admin UID or TLS trust configuration was supplied. This package must not be described or deployed as a working remote Firebase system.

The local HTTP API also has no login layer. Keep it on a trusted LAN, change the default AP password, and do not port-forward its HTTP or OTA endpoints.

## Verification status

- JavaScript syntax, inline settings JavaScript, HTML structure, Python syntax and workflow YAML/shell syntax were statically checked in the preparation environment.
- PlatformIO firmware compilation and LittleFS image generation were **not run locally** because PlatformIO is not installed and the environment could not reach the package index to install it.
- The included GitHub Actions workflow is configured to run both build targets on the next push/PR/manual workflow run. A successful GitHub Actions run is still required before flashing this firmware to hardware.

# Upgrade notes — v1.5.0

## Files replaced

Copy the files in this package over the matching repository paths. The package contains only changed source/assets and documentation.

## Main changes

1. The main dashboard is now limited to the custom logo, Online/Offline indicator, Settings icon and four large relay tiles.
2. Every relay tile is clickable across its full area. The browser UI sends an explicit ON/OFF request to the existing `/api/relay` route.
3. Timers, schedules, Wi-Fi, OTA and recovery controls are only inside the settings menu.
4. Settings use one category at a time and a back control, with slide transitions.
5. Logo text can be edited up to 255 characters.
6. Relay polarity is stored separately for all four channels. The default for all channels is Active-Low; change each to match the physical relay board.
7. Old automation records are migrated to the new structure, preserving prior names, switches, timers and schedules.

## Build and flashing

Run GitHub Actions after replacing the files. The workflow stages firmware immediately after compilation, then builds/stages LittleFS separately.

For this repository's 4 MB NodeMCU v2 and `eagle.flash.4m1m.ld` configuration:

- `firmware.bin` → `0x00000`
- `littlefs.bin` → `0x300000`

Use both files from the same successful workflow run. LittleFS flashing overwrites the filesystem. Keep a backup if existing saved settings must be preserved.

## Before powering relay loads

- Verify Relay 1–4 polarity individually using a safe low-voltage test.
- The default is Active-Low for every channel, but mixed Active-High/Active-Low boards are supported.
- Do not select a polarity based only on the relay module's appearance; confirm its trigger behavior.
- The output pin map is unchanged: D1/GPIO5, D2/GPIO4, D5/GPIO14, D6/GPIO12.

## Remote access limitation

The page includes a remote-access guidance section, but this firmware does not start a secure public tunnel or implement Firebase remote commands. Do not expose the ESP8266's local HTTP/OTA endpoints using public port forwarding. Off-site access requires a separately secured VPN gateway/router on the same LAN.

## Build verification note

A PlatformIO compiler was not available in the current editing environment, so this package has not been locally compiled here. GitHub Actions must be run on the repository before flashing. The UI scripts and generated asset paths should be reviewed in the Actions result; do not treat static checks as a successful firmware build.

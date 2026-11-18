Kabuki is a native control panel and homebrew overlay hub for PS5.

Version 1.0.0 includes the Plex library pagination control and compact artwork support.

- Configurable shortcut, sounds and sidebar arrangement.
- Built-in System and Settings cards.
- Provider protocol, media and list templates, C SDK and native plugin API.
- Music controls, queue and library cards supplied by a compatible librespot-ps5 receiver.
- Library navigation resets when the receiver changes account or session.

**Firmware 13.60:** Kabuki and Plex playback have been used on the user's console;
the new pagination control still requires on-console visual verification. There is no firmware
version allowlist; runtime method and hook validation still applies.

Copy `kabuki-ps5.elf` to a USB drive and load it manually. The default shortcut is
L3 + R3 held for half a second. When using librespot-ps5, launch Kabuki first.
Settings and logs are stored in `/data/custom-overlays`. No autoload is installed.

`SHA256SUMS.txt` verifies the payload and its accompanying build information.
The release payload excludes development takeover support. Source and build
instructions are available in the repository.

# Kabuki

A control centre for the PS5 that looks and feels like the console's own. Hold a button
shortcut and a panel slides in from the right over whatever is on screen, home or in a game,
drawn with the console's own interface toolkit, fonts, icons, glass focus and sounds.

Kabuki is a host: the System and Settings tiles are its own, and homebrew adds its own tiles
beside them. librespot-ps5, a Spotify Connect receiver for the PS5, adds Now Playing, Queue and
Library.

> **Status: early and experimental (v0.1.0).** Tested only on a jailbroken PS5 on firmware 13.20.
> Kabuki loads code into the console's system interface (SceShellUI). A bug can crash the
> home screen or the console. Save your game first and expect to reboot sometimes.

## What it does

- **Opens anywhere:** hold the shortcut (L3 + R3 for half a second by default) at home or in
  a game. Circle or the shortcut closes it. Opening the console's own control centre closes it too.
- **Looks native:** PUI widgets, SST fonts, the console's icons, UI3 glass focus and
  interface sounds.
- **Tiles on a rail:** up to 7, in any order. Up and Down wrap around the ends.
- **System:** CPU, memory, frame rate and temperatures.
- **Kabuki Settings:**
  - record a new open shortcut and set its hold time
  - turn interface sounds on or off
  - choose and arrange the tiles; the first one opens with the panel
- **With librespot-ps5:**
  - Now Playing: art, scrub bar, controls, volume, shuffle and repeat
  - Queue
  - Library: playlists and albums, opened into their tracks

## Install

1. Copy `kabuki-ps5.elf` to a USB drive and connect it to the console.
2. Load it with your payload loader. Kabuki runs until the console restarts.
3. Hold L3 + R3.

Kabuki doesn't add itself to autoload, and writes its settings and log to
`/data/custom-overlays` on the console.

## Building

On Linux, or WSL on Windows, with the
[PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) and Python 3:

```bash
python3 tools/fetch-deps.py   # pinned third-party headers into .deps
bash tests/run.sh             # host checks
bash build.sh                 # build/kabuki-ps5.elf
```

[docs/DEVELOPING.md](docs/DEVELOPING.md) covers the console development loop and the browser
preview. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) explains how Kabuki works.

## Preview

The panel's layout code also runs on a PC. `preview/native_preview.py` serves a page that draws
it with the console's fonts and icons, and plays it with a keyboard or controller. The fonts
and icons are copied from your own console with `tools/fetch_console_assets.py` and stay on
your machine. They're Sony's and are never part of this repository.

## Credits and license

Made by [stupiditytries](https://github.com/stupiditytries). Licensed under GPL-3.0-or-later
(see [LICENSE](LICENSE)).

- The ShellUI loader is adapted from
  [ps5-dualsense-overlay](https://github.com/erickdavestech/ps5-dualsense-overlay) and John
  Tornblom's [shsrv](https://github.com/ps5-payload-dev/shsrv) (details in
  [src/loader/SOURCES.md](src/loader/SOURCES.md)).
- Uses [JSON for Modern C++](https://github.com/nlohmann/json) (MIT), and
  [stb_truetype](https://github.com/nothings/stb) in the PC preview.

Kabuki isn't affiliated with or endorsed by Sony Interactive Entertainment. PlayStation and
PS5 are trademarks of Sony Interactive Entertainment Inc.

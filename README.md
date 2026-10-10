# gbadual

A Game Boy Advance emulator for dual-screen handhelds (built for the Anbernic RG DS Plus on ROCKNIX). The game runs on one screen, and the game's own map stays up on the other screen and keeps itself current as you explore, the way the DS Castlevanias work.

![Aria of Sorrow](docs/aos.png)
![Metroid: Zero Mission](docs/zm.png)

| Game | Map button | Status |
|---|---|---|
| Castlevania: Aria of Sorrow (USA) | Select | working |
| Castlevania: Harmony of Dissonance (USA) | Select | working |
| Castlevania: Circle of the Moon (USA) | Select | working |
| Metroid: Zero Mission (USA) | Start | working |
| Metroid Fusion (USA) | Start | working |

Any other GBA game also runs; it just gets no map screen.

## How it works

Emulation is [libmgba](https://mgba.io). gbadual runs a second, silent copy of the emulator next to the real one. About every 1.5 seconds, and right after each room transition (detected as a fade to black), it does the following:

1. It copies the live game's state into the silent copy. The game you're playing is never paused or touched.
2. It presses the map button there, waits for the map screen to appear and settle, then records a short loop of frames so the blinking or pulsing player marker still animates.
3. It checks the result two ways before showing it: a memory flag that is set only while the map screen is open, and fixed pixels of the map screen's frame or background. Title screens, cutscenes, menus, and fades are rejected, and the previous map stays up.

You see the real in-game map with every detail the game draws (explored rooms, save and warp rooms, completion %, area names) at 4× integer scale.

## Install on ROCKNIX

1. Unzip `gbadual-rocknix-aarch64.zip` into `/storage/roms/ports/`, so you have `/storage/roms/ports/gbadual/gbadual`.
2. Put your **unzipped** `.gba` ROMs in `/storage/roms/gba/`. Zipped ROMs aren't supported yet.
3. Over SSH, run `bash /storage/roms/ports/gbadual/install-ports.sh`. This adds entries like "Castlevania - Aria of Sorrow (Dual Screen)" under **Ports**.
4. In EmulationStation, update the game lists and launch from Ports.

Saves:
- Battery saves are written next to the ROM as `Game.sav`.
- On first launch, an existing RetroArch save (`Game.srm`) is copied to `Game.sav`, so you keep your progress. The `.srm` itself is left alone.
- Save states are `Game.gbadual.ss0`.

## Controls

| Button | Action |
|---|---|
| D-pad / left stick | D-pad |
| Right face button | GBA A (`ab_swap = 1` flips A/B) |
| Bottom face button | GBA B |
| L1 / R1 | GBA L / R |
| Start / Select | Start / Select |
| L2 | Swap screens (game ↔ map) |
| R2 (hold) | Fast-forward |
| Hotkey + Start | Quit (saves are flushed) |
| Hotkey + R1 / L1 | Save / load state |
| Hotkey + Y | Swap screens |
| Hotkey + X | FPS counter |

The hotkey is the function/home button (`hotkey = guide`). Set `hotkey = back` to use Select instead. A plain Select press still reaches the game when it isn't used in a combo.

## Settings

Settings live in `gbadual.cfg` next to the binary. The file is commented. The options you're most likely to change:

- `swap = 1`: puts the map on the other screen. Use this if the game comes up on the bottom screen. L2 swaps live.
- `layout`:
  - `auto` (the default) works on ROCKNIX's sway setup, which gives the RG DS Plus's two panels as side-by-side 1024×768 outputs. gbadual asks sway to make its window full screen across both outputs (`fullscreen enable global`) and splits the window in half.
  - `dual` uses one window per display instead. This is for systems without sway.
  - `single` puts everything on one screen, and L2 flips between the game and the map.
- `map_interval`: frames between periodic map refreshes. The default is 90 (≈1.5 s). Lower values update faster but use more CPU on the second core.

Logs go to `/storage/roms/ports/gbadual/gbadual.log`.

## Not yet verified on hardware

This build was developed and tested on an x86 Linux host in a headless video mode, with the aarch64 binary cross-compiled. Things to check on the device first:

- **Which screen is which.** If the game is on the bottom, set `swap = 1`.
- **Only one screen is used.** If the window fills one screen instead of spanning both, check the log for `swaymsg global fullscreen failed`. Then try `layout = dual`, or report the output of `swaymsg -t get_outputs`.
- **Button layout.** Face buttons are read through SDL's controller mapping (positional). If A and B feel wrong, set `ab_swap = 1`.
- **Speed.** Host benchmarks suggest the RK3568 has plenty of headroom: one core for the game, part of a second for map captures. If audio crackles, raise `audio_latency` or `map_interval`.

## Building

On an x86_64 Linux host with git, cmake, make, and zip, run:

```sh
scripts/build-rocknix.sh            # -> dist/gbadual-rocknix-aarch64.zip
```

The script fetches a GCC aarch64 toolchain, mGBA (pinned commit), and SDL2 into `deps/`.

To build for the host so you can develop and test:

```sh
make MGBA_BUILD=../mgba/build-host SDL_PREFIX=../sdl-host   # build/host/gbadual
make tools                                                  # build/host/harness (needs libpng)
```

Run it headless and take a screenshot:

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy GBADUAL_WINDOW=2048x768 \
  build/host/gbadual --load-state=x.ss --screenshot=300:out.bmp --quit-after=301 game.gba
```

## Adding a game

Each supported game is one entry in `src/profiles.c`:

- the button press that opens the map
- the map's crop rectangle
- a few pixels that are always the same colour while the map is up
- a memory flag that is set only while the map screen is open
- an optional flag for "in gameplay" on the live core

The tools in `tools/` are for finding these values:

- **`harness`**: runs a ROM headless from a small script. Script commands: `tap A`, `hold RIGHT 60`, `shot name`, `save`/`load` states, `dumpram name`, `map name` (runs the real capture code).
- **`ramdiff.py`**: given RAM dumps taken with the map open and closed, lists the addresses that cleanly separate them.
- **`sheet.py`**: tiles screenshots into a contact sheet.

## Licence

gbadual statically links libmgba, which is © Jeffrey Pfau and licensed under MPL-2.0. The mGBA source is at <https://github.com/mgba-emu/mgba> (pinned commit in `scripts/build-rocknix.sh`).

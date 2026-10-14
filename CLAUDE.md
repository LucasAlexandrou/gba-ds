# GBA DS: notes for Claude sessions

GBA DS is a GBA emulator (libmgba) for dual-screen handhelds, built for the
**Anbernic RG DS Plus on ROCKNIX** (two 1024x768 screens, the bottom one a
touchscreen; the RG DS has two 640x480 ones). The game runs on one screen; the other shows the game's own
map (captured from a hidden second copy of the game) or the game's logo.
The README covers user-facing behaviour; CHANGELOG.md the history.

The owner (kyannon) tests every build on real hardware and reports back
with photos and screenshots; nothing ships untested on the device.

## Conventions

- **Branches:** all work goes to `dev`. `main` only moves at release time,
  fast-forwarded to the release commit. The owner creates GitHub releases
  and tags by hand (sessions get HTTP 403 on releases and can't push tags).
- **Commits:** author `kyannon <341221214+kyannon@users.noreply.github.com>`,
  with the Co-Authored-By / Claude-Session trailers the session asks for.
- **VERSION** holds `X.Y.Z-dev` (or `X.Y-dev`) on `dev`; CHANGELOG.md has a
  `## vX.Y.Z (not yet released)` section on top. Add a line there for every
  user-visible change, written for players (not implementation notes).
- **Release:** merge `android` into `dev` if it has anything new. Set
  VERSION to `X.Y.Z` and date the changelog section, commit
  "Release vX.Y.Z", push `dev`, push that commit to `main`. Two files:
  - the ROCKNIX zip, built from a fresh clone of the release commit
    (`scripts/build-rocknix.sh`);
  - the APK, `gba-ds-vX.Y.Z-android.apk`: the artifact of the Android
    workflow run for the release commit on `dev` (built and signed with the
    release key by CI; the owner downloads it from the run page, the zip
    holds the APK). Check that run shows no "debug-signed" warning.
  Write release notes (What's new / Fixes / Install / upgrade, for both
  platforms, with the SHA-256 of both files; the APK's is the run's "APK
  SHA-256" annotation, readable from the run page or the public API:
  check-runs/<job id>/annotations) and send them with the zip.
  Then bump `dev` to the next `-dev` version with an empty changelog
  section.
- Test builds for the device: `dist/gba-ds-vX.Y.Z-dev-rocknix-aarch64.zip`,
  sent to the owner after each change; Android test APKs are the artifacts
  of the workflow runs on `android` / `dev` (link the run).

## Building

- Desktop (for development and tests): `scripts/setup-host.sh` once (fetches
  and builds mGBA and SDL2 into `deps/`), then `make` and `make tools`.
- Device: `scripts/build-rocknix.sh` (fetches an aarch64 toolchain, mGBA
  and SDL2 into `deps/`) produces the release zip in `dist/`.
- Android: `scripts/build-android.sh` (needs the Android SDK/NDK, so it
  runs on GitHub Actions: `.github/workflows/android.yml`, APK in the run's
  artifacts). See `docs/ANDROID.md`.
- libmgba options live in `scripts/mgba-options.sh` (shared by all builds). It's
  a full mGBA configuration with everything optional off, not
  `LIBMGBA_ONLY` (that forces zlib off; .zip ROMs need it), and LTO off.
- Generated sources: `src/brand_data.c` (`tools/make_brand.py` with the
  Futura Bold Italic and Gamecuben fonts, which aren't in the repo: ask the
  owner for them) and `src/logo_masks.c` (`tools/make_logo_masks.py`).
  New menu text must be added to MENU_TEXTS in make_brand.py and the file
  regenerated.

## Testing without the device

- The app runs headless: `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
  GBADS_WINDOW=1024x1536 build/host/gba-ds --state-dir=DIR [--load-state=X.ss]
  --input-script=SCRIPT ROM`. Script lines: `TICK BUTTONS` (A, B+UP, - for
  none), `TICK SHOT:file.bmp` (both screens), `TICK TAP:x,y` (fractions of
  the whole window), `TICK QUIT`. `GBADS_DEBUG=1` logs map captures,
  snapshot and control-scheme switches, taps.
- `build/host/harness ROM OUTDIR SCRIPT` drives the core frame by frame:
  `load/save NAME`, `wait N`, `hold BUTTONS N`, `shot NAME`, `peek ADDR LEN`,
  `poke ADDR LEN VAL`, `dumpram NAME`, `layershot NAME` (each BG layer and
  OBJ alone), `map NAME` (run a map capture), `logo NAME`.
- ROMs and save files are not in the repo. The owner attaches them; ask.
- Verify visual changes by rendering screenshots and looking at them before
  sending a build.

## Architecture (src/)

- `main.c` start-up; `config.c` settings (`gba-ds.cfg`, `prefs.cfg`, `--key=value`).
- `emu.c` the libmgba wrapper: frames, states, audio, cheats, per-renderer
  sprite hiding (SpriteRule), layer toggles, ROM headers (also inside
  .zip / .7z).
- `profiles.c/.h` everything game-specific (MapProfile): how to open the
  map, screen and memory probes, crops, captureHide/underHide sprite rules,
  logo extraction, regional variants (`variants[]` + patch functions), and
  for logo-only games (Tony Hawk's Pro Skater 2) the `snap` screen copy and
  `controls` four-face layout. Adding a game mostly means a profile.
- `mapcap.c` map capture on a hidden second core in a background thread.
- `mapframe.c` generated map surrounds; `logo.c` logo / frame extraction.
- `game.c` a play session: input, the second screen (map, logo, THPS2
  snapshot and Controls picker), pause menu, cheats screen, screenshots.
- `library.c` the game list and its Settings screen; `esinteg.c` the
  EmulationStation integration; `cheats.c` RetroArch .cht files;
  `ui.c` SDL windows, input, drawing, the pause-menu text images.

## Device facts

- ROCKNIX on the RG DS Plus, sway compositor; the app spans both screens
  with one window ("fullscreen global"). Face buttons arrive by position:
  SDL "B" is the right button (GBA A), "A" the bottom, "X" the top, "Y" the
  left.
- Outputs: DSI-2 is the top panel (ROCKNIX's main output, `$WLR_CON`), DSI-1
  the bottom. **Stock ROCKNIX keeps DSI-1 powered off**, gives it no fixed
  position, and its sway-touch service maps the touchscreen to DSI-2. ROCKNIX's
  udev calibration puts bottom-panel touches in the right half of the layout.
  `dist/screens.sh` (run by launch.sh) powers DSI-1 on, places it right of
  DSI-2, maps touch to the whole layout, and restores all of it on exit.
- ROCKNIXDS (github.com/JorreFog/ROCKNIXDS, no longer developed) is ROCKNIX
  with dual-screen fixes (DSI-1 on, side by side, sway-touch off). The owner
  developed against it at first; GBA DS must work on stock ROCKNIX on its own.
  For how ROCKNIX drives the screens, see its `sway` package (111-sway-init),
  `vertical-check` and the RG DS device quirks.
- Installed at `/storage/roms/ports/gba-ds/`, ROMs in `/storage/roms/gba/`.
- EmulationStation: `/usr/bin/runemu.sh` launches games; the GBA command is
  `/usr/bin/runemu.sh %ROM% -P%SYSTEM% --core=%CORE% --emulator=%EMULATOR%
  --controllers="%CONTROLLERSCONFIG%"`.

## Ongoing work

- Android version: see `docs/ANDROID.md`.
- The next release is **v1.1** (VERSION `1.1-dev`): the first with the Android
  APK. Its release also attaches the APK built by GitHub Actions from the
  release commit (`gba-ds-v1.1-android.apk`), with install notes for it.

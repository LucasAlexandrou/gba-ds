[BananaOS](https://github.com/JorreFog/BananaLinux) is a light Linux made only for the RG DS and RG DS Plus. GBA DS runs on it as a port, from the same zip as on ROCKNIX. *Support is new: report anything that doesn't work (see [Troubleshooting](Troubleshooting)).*

## Install

1. Download the latest `gba-ds-v…-rocknix-aarch64.zip` from the [Releases page](https://github.com/kyannon/gba-ds/releases).
2. Unzip it into the card's `roms/ports` folder (on the device: `/data/roms/ports`). You get a `gba-ds/` folder, a `GBA DS.sh` entry and `images/`. Banana's file sharing (Settings → Network) lets you copy it over Wi-Fi.
3. Keep your Game Boy Advance games in `roms/gba` (`/data/roms/gba`), where Banana's own GBA list finds them too.
4. In Banana's menu, open **Ports** and start **GBA DS**.

## How it fits in

- **Both screens:** Banana has no compositor, so GBA DS opens a window on each panel and draws them with the GPU, straight onto the panels (SDL's KMS/DRM driver); touches on the bottom panel work as on ROCKNIX. Banana's ports launcher normally switches the bottom panel off; GBA DS switches it back on while it runs.
- **Buttons:** the zip has a mapping for the RG DS's built-in controls (`gba-ds/gamecontrollerdb.txt`, ROCKNIX's), since Banana ships none for SDL apps. The buttons work as on ROCKNIX (HOME, or a tap on the bottom touchscreen, opens the pause menu).
- **Where things go:** these replace ROCKNIX's paths unless you've changed them in `gba-ds.cfg`:

| | |
|---|---|
| Games | `/data/roms/gba` |
| Battery saves (`.sav`) | `/data/saves`, beside RetroArch's: a game's RetroArch save (`.srm`, from Banana's gpSP) is carried over the first time you start it in GBA DS |
| Screenshots | `/data/screenshots` |
| Cheats | `roms/ports/gba-ds/cheats` (RetroArch `.cht` files; subfolders are fine), and RetroArch's `/data/retroarch/cheats` |
| Save states, settings from the menu, logos | `roms/ports/gba-ds/states/` |
| Log | `roms/ports/gba-ds/gba-ds.log` |

- **RetroAchievements:** the account from Banana's first-time setup (`banana/accounts.conf`) is used, in casual mode. Banana has no `curl` program, so GBA DS talks to the server through Banana's curl library instead (the log says "using libcurl").
- **Battery and clock** in the pause menu come from the system, as on ROCKNIX.
- **GBA DS in EmulationStation** (the game list's Settings) doesn't apply: Banana has no EmulationStation. Start games from GBA DS's own list.

## If something's wrong

Send `roms/ports/gba-ds/gba-ds.log` (and for a stutter or slowness, a log made with `export GBADS_DEBUG=1` added near the top of `gba-ds/launch.sh`). The log's first lines say it found BananaOS, which paths it used, the window size and the frame pacing.

To compare other ways of reaching the panels, add a line near the top of `gba-ds/launch.sh` (before `if [ -d /etc/banana ]`):
- `export GBADS_BANANA_VIDEO=evdev`: one window over both panels drawn by the CPU, copied onto them each frame (24-30 frames a second on an RG DS Plus).
- `export GBADS_BANANA_VIDEO=offscreen`: the same drawn with OpenGL ES and read back (under 5 frames a second).

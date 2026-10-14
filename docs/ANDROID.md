# Android version: plan

Status: not started. This is the brief for the session that starts it.

## Goal

An APK of GBA DS for Android-based firmware on dual-screen handhelds (first
target: GammaOS on the owner's device; see "Facts from the owner" below),
with the same behaviour as the ROCKNIX build: game on one screen, map or
logo (and the Tony Hawk goal sheet and Controls picker) on the other, the
pause menu and game list on the touchscreen.

The ROCKNIX build stays the main product. Android work happens on an
`android` branch off `dev` and is merged into `dev` once it builds and the
ROCKNIX build is unaffected.

## What carries over

Everything game-side is portable C: `emu.c`, `mapcap.c`, `profiles.c`,
`logo.c`, `mapframe.c`, `cheats.c`, and most of `game.c` / `library.c`.
libmgba builds with the Android NDK (mGBA has its own Android-friendly
CMake), SDL2 runs on Android (SDLActivity), audio and gamepads come through
SDL.

## What needs work

1. **The second screen.** SDL2 on Android gives one window on the main
   display only. The other display needs a small Java layer: an Android
   `Presentation` (a window on a secondary `Display`) holding a SurfaceView,
   whose Surface is passed to native code (ANativeWindow). Plan: keep SDL for
   the main screen; render the second screen into a software
   `SDL_Renderer` (on an SDL_Surface) with the existing ui.c drawing calls,
   and copy it into the ANativeWindow each frame. `ui.c` already treats each
   screen as a "view" with its own renderer, so the second view becomes the
   software one.
2. **Touch on the second screen.** Touches on the Presentation arrive in
   Java; forward them to the native pointer queue (ui.c `queue_pointer`).
3. **Buttons.** Android keeps HOME (and often BACK) for itself. The pause
   menu needs another trigger (tap the touchscreen already works; add a
   combo such as SELECT+START, configurable). Check how GammaOS maps the
   face buttons (SDL's positional names; see CLAUDE.md "Device facts").
4. **Files.** Ask for "all files access" (MANAGE_EXTERNAL_STORAGE; fine for
   a sideloaded app) so the game list can read a ROM folder anywhere, with
   Android-style defaults: ROMs in `/storage/emulated/0/ROMs/gba`, data
   (states, logos, prefs, screenshots) in the app's own folder. Make the ROM
   folder choosable in Settings.
5. **ROCKNIX-only parts** become no-ops on Android: sway window placement,
   `esinteg.c` (EmulationStation), the `launch.sh` wrapper.
6. **Launching from frontends** (optional, later): an intent filter so
   frontends like Daijisho / ES-DE can start GBA DS with a ROM path.

## Build pipeline

The Claude workspace can't download Google's Android SDK/NDK (blocked), so
APKs are built by **GitHub Actions** (its Ubuntu runners include the SDK and
NDK): a workflow at `.github/workflows/android.yml` builds on every push to
`android`/`dev` and uploads the APK as a run artifact. The owner downloads
it from the Actions tab and sideloads it.

- If a session can't push workflow files (GitHub needs the `workflow`
  permission for that), the session gives the owner the file to commit
  through the GitHub web UI.
- **Signing:** use one fixed keystore, stored as repository secrets
  (`ANDROID_KEYSTORE_B64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`,
  `ANDROID_KEY_PASSWORD`). A debug key generated per CI run would differ
  every build, and Android refuses to install an update signed with a
  different key (you'd have to uninstall and lose saves each time). The
  session generates the keystore (keytool is available) and hands it to the
  owner to add under Settings > Secrets and variables > Actions; the
  keystore itself is never committed.

## First milestone (proof of concept)

The APK starts a ROM from a fixed path, plays it on the main screen with
sound and buttons, and shows the game's logo on the second screen, with a
tap on the second screen logged. That settles the second-display question
before the rest is ported. Then: game list, pause menu, map capture,
settings, file access.

## Facts from the owner (fill in before starting)

- Device model and GammaOS version:
- Android version (Settings > About):
- Output of `adb shell dumpsys display | grep -E "mDisplayId|DisplayDeviceInfo"`
  (how the second screen is exposed):
- Which screen has touch, and does touch on it reach apps?
- Does the device have the RG DS Plus's button layout? Anything special
  about HOME / BACK / SELECT+START in GammaOS?
- Where ROMs live on the device:

# Android version: plan

Status: first milestone done and tested on the device (branch `android`,
2026-10-14). See "Progress" at the end.

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

- Device model and GammaOS version: RG DS Plus, GammaOS 1.4.4 (core)
- Android version (Settings > About): Android 14 (20261002)
- Output of `adb shell dumpsys display | grep -E "mDisplayId|DisplayDeviceInfo"`
  (how the second screen is exposed):

1|tdgsi_arm64_ab:/ # dumpsys display | grep -E "mDisplayId|DisplayDeviceInfo"
  DisplayDeviceInfo{"Built-in Screen (port 0)": uniqueId="local:0", 1024 x 768, modeId 1, renderFrameRate 60.000004, defaultModeId 1, userPreferredModeId -1, supportedModes [{id=1, width=1024, height=768, fps=60.000004, vsync=60.000004, alternativeRefreshRates=[], supportedHdrTypes=[]}], colorMode 0, supportedColorModes [0], hdrCapabilities HdrCapabilities{mSupportedHdrTypes=[], mMaxLuminance=500.0, mMaxAverageLuminance=500.0, mMinLuminance=0.0}, allmSupported false, gameContentTypeSupported false, density 240, 218.568 x 136.413 dpi, appVsyncOff 1000000, presDeadline 16666666, touch INTERNAL, rotation 0, type INTERNAL, address {port=0}, deviceProductInfo null, state ON, committedState ON, frameRateOverride , brightnessMinimum 0.0, brightnessMaximum 1.0, brightnessDefault 0.39763778, hdrSdrRatio NaN, FLAG_ALLOWED_TO_BE_DEFAULT_DISPLAY, FLAG_ROTATES_WITH_CONTENT, FLAG_SECURE, FLAG_SUPPORTS_PROTECTED_BUFFERS, FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS, FLAG_TRUSTED, installOrientation 0, displayShape DisplayShape{ spec=-1440493287 displayWidth=1024 displayHeight=768 physicalPixelDisplaySizeRatio=1.0 rotation=0 offsetX=0 offsetY=0 scale=1.0}}
  DisplayDeviceInfo{"Built-in Screen (port 1)": uniqueId="local:1", 1024 x 768, modeId 2, renderFrameRate 60.000004, defaultModeId 2, userPreferredModeId -1, supportedModes [{id=2, width=1024, height=768, fps=60.000004, vsync=60.000004, alternativeRefreshRates=[], supportedHdrTypes=[]}], colorMode 0, supportedColorModes [0], hdrCapabilities HdrCapabilities{mSupportedHdrTypes=[], mMaxLuminance=500.0, mMaxAverageLuminance=500.0, mMinLuminance=0.0}, allmSupported false, gameContentTypeSupported false, density 240, 113.579 x 136.413 dpi, appVsyncOff 1000000, presDeadline 16666666, touch INTERNAL, rotation 0, type INTERNAL, address {port=1}, deviceProductInfo null, state ON, committedState ON, frameRateOverride , brightnessMinimum 0.0, brightnessMaximum 1.0, brightnessDefault 0.5, hdrSdrRatio NaN, FLAG_ALLOWED_TO_BE_DEFAULT_DISPLAY, FLAG_ROTATES_WITH_CONTENT, FLAG_SECURE, FLAG_SUPPORTS_PROTECTED_BUFFERS, FLAG_PRESENTATION, FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS, FLAG_TRUSTED, FLAG_OWN_DISPLAY_GROUP, installOrientation 2, displayShape DisplayShape{ spec=-1440493287 displayWidth=1024 displayHeight=768 physicalPixelDisplaySizeRatio=1.0 rotation=0 offsetX=0 offsetY=0 scale=1.0}}
    mDisplayId=0
    mDisplayId=2
  mDisplayId=0
  mDisplayId=0
  mDisplayId=0
  mDisplayId=: 0
  mDisplayId= 0
  mDisplayId=0
  mDisplayId=0
  mDisplayId=2
  mDisplayId=2
  mDisplayId=2
  mDisplayId=: 2
  mDisplayId= 2
  mDisplayId=2
  mDisplayId=2
  
- Which screen has touch, and does touch on it reach apps? Bottom screen. Yes.
- Does the device have the RG DS Plus's button layout? Anything special
  about HOME / BACK / SELECT+START in GammaOS? It is an RG DS Plus.
- Where ROMs live on the device: Uri is content://com.android.externalstorage.documents/document/primary%3AROMs

## Progress

### First milestone (branch `android`)

How it's put together:

- `android/`: a Gradle project. `GbaDsActivity` (an SDLActivity) runs the
  game on its display; `SecondScreen` (a Presentation) covers the other
  display with a SurfaceView. `LauncherActivity` (the app icon) asks for
  All files access once, then starts the game on display 0. SDL's Java
  files are compiled straight from the SDL source in `deps/SDL2`, so they
  always match libSDL2.so.
- `android/app/src/main/cpp/CMakeLists.txt` builds libSDL2.so from source
  and libmain.so from `src/` (the same files as the ROCKNIX build) plus
  `src/android.c`; libmgba is built first by `scripts/build-android.sh`
  with the ROCKNIX build's options (`scripts/mgba-options.sh`).
- `ui.c`: on Android, view 1 is a software SDL_Renderer on an SDL_Surface
  the size of the second display; `ui_present` copies it to the
  Presentation's ANativeWindow. Second-screen touches are pushed into SDL's
  event queue as finger events with window id `GBADS_SECOND_WINDOW_ID`, so
  `queue_pointer` sees them as view 1. Every touch down/up is logged. The
  touch screen (menus) is whichever screen was touched last. BACK acts as
  HOME (SDL traps it). All Android code is `#ifdef __ANDROID__`.
- `main.c`: config, states and the log live in the app's folder
  (`Android/data/io.github.kyannon.gbads/files`, passed as `GBADS_HOME`);
  stderr goes to logcat (tag `GBADS`) and `gba-ds.log` there. With no game
  given it plays the first game in `/storage/emulated/0/ROMs/gba` (or
  `GBA`) that has a map profile, else the first game. Another game:
  `adb shell am start -n io.github.kyannon.gbads/.LauncherActivity --es rom /path/to/game.gba`.
- CI: `.github/workflows/android.yml` (runs `scripts/build-android.sh`;
  APK in the run's artifacts; on failure the end of the log is posted as
  an annotation, readable through the public API without logs access).
- Signing keystore generated by the session that built this and handed to
  the owner (alias `gbads`, SHA-256 fingerprint
  95:24:1F:DD:11:B5:67:F6:D6:80:C4:C7:67:8E:BB:F6:46:3C:52:A6:E3:C9:50:5B:14:C6:A7:96:2F:04:69:58).

What the device test should answer (`adb logcat -s GBADS`):

- Do both screens show (game on display 0, logo then map on display 2)?
- Which display is the touchscreen: the log says `touch down on screen N
  (game activity's / second display)`. If display 0 is the bottom one,
  the game is on the bottom: L2 or the menu's Swap moves it (the pause
  menu opens on the screen that was touched).
- Sound, buttons (face buttons by position, as on ROCKNIX?), speed.
- Do buttons keep working after touching the second screen? (The
  Presentation is non-focusable so it shouldn't take key focus.)

First device test (2026-10-14): game playable, buttons, menu and touch
work; both screens only with GammaOS's "Run on primary screen"; A and B
swapped; 20-30 fps. Changes since:

- A/B: GammaOS reports face buttons by their Nintendo labels (SDL A =
  right). ui.c swaps SDL A/B back on Android, so everything stays
  positional as on ROCKNIX (X top, Y left already match).
- Speed: the second screen's frame is handed to the display by a thread
  of its own (ANativeWindow_lock waits for that display's refresh; doing it
  on the game loop on top of the game screen's own wait capped it at 30).
  Logged every 5 s: fps from game.c, and from ui.c the time spent per
  frame on the game screen, drawing the second screen (software), and
  handing it over.
- Speed, second round: the log showed ~50 ms a frame with the presents at
  ~3 ms; the software renderer draws during the draw calls (no batching),
  so the second screen's cost wasn't being timed. Now ui.c records the
  draw calls aimed at the software view and replays them at ui_present
  only when they differ from the last frame's (calls + texture versions;
  game.c updates textures through `ui_update_texture` so changes count),
  plus a redraw once a second as a safety net. Logged every 5 s: emulating
  and drawing time per frame (game.c) and how many frames the second
  screen was redrawn and at what cost (ui.c). Checked on the desktop with
  the Android paths and stub glue: the replayed pause menu matches the
  directly drawn one, and it redraws only on changes.
- Speed, third round: a map redraw in software took 41 ms (log), so map
  updates and animations still dropped frames. The second screen is now
  drawn by the GPU: android.c makes SDL's OpenGL ES context current on an
  EGL window surface on the Presentation's window (same EGLConfig, buffers
  the game screen's size so SDL's viewport fits; swap interval 0), and
  ui.c replays the recorded calls through the game screen's renderer
  between `android_second_begin` / `_end`. View 1 shares view 0's
  renderer (`View.offwin`). The software renderer remains the fallback
  (no GLES renderer, or `GBADS_SOFT_SECOND` set). Tested on the desktop
  with the offscreen driver (also GLES/EGL) and a pbuffer standing in for
  the window: menus match the software path, render targets and the LCD
  grid work, the game screen is unaffected. Screenshots read the second
  screen back while it is the current surface.
- Start-up (5-10 s of black reported): the ROM pick reads every ROM's
  header (unpacking archives) until it finds one with a map profile; the
  result is now kept in `last-rom.txt` in the app folder and reused.
  Start-up steps are logged (`startup:` lines; logcat timestamps time them).
- Display: GbaDsActivity has a task affinity of its own and is started
  with FLAG_ACTIVITY_NEW_TASK, so the display-0 launch request isn't
  ignored when the launcher's task sits on the other display.

Device result (2026-10-14, Circle of the Moon, GPU second screen): a
steady 59.6-60 fps, no audio underruns, emulation ~6 ms and drawing ~3 ms
a frame; the second screen redraws only on changes (5 per 5 s with the
map still, ~76 with map animations) at 2-4 ms each. Start-up from the
icon to the first frame ~3.2 s with last-rom.txt (about 0.75 s in the
activity loading libraries, ~0.9 s before SDL starts the native thread,
~1 s loading the game and the map profile). The built-in pad appears as
"Xbox Series X Controller". All touches came from display 0 (the game
activity's display), so display 0 is the touchscreen.

After the first milestone:

- Screens in physical order (ui_layout): screen 0 = top = the second
  display (view 1), screen 1 = bottom = the activity's display 0 (view 0,
  the touchscreen), so the defaults match ROCKNIX: game on top, menus on
  the bottom.
- L2 / R2 (screen swap, fast-forward) didn't work: the triggers send
  digital KEYCODE_BUTTON_L2 / R2, which SDL turns into raw joystick
  buttons 15 / 16 that its Android controller mapping leaves out (it only
  maps analog trigger axes). ui.c reads them directly on Android; raw
  buttons from 15 up are logged.

Not done yet (next): game list and pause menu polish, SELECT+START menu
combo, saving when Android pauses or kills the app, file access settings,
frontend launch intents.

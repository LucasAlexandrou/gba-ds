#!/bin/bash
# Builds the Android APK (arm64-v8a): dist/gba-ds-vX.Y.Z-android.apk.
#
# Needs: the Android SDK (ANDROID_HOME or ANDROID_SDK_ROOT) with an NDK
# (ANDROID_NDK_HOME, or ANDROID_NDK_LATEST_HOME as on GitHub's runners),
# Gradle 8.7+ and JDK 17, git, cmake. mGBA and SDL2 are fetched into ./deps
# (the same versions as the ROCKNIX build). The Claude workspace can't
# download the SDK, so this runs on GitHub Actions (.github/workflows/android.yml).
#
# Release signing: set GBADS_KEYSTORE (path), GBADS_KEYSTORE_PASSWORD,
# GBADS_KEY_ALIAS, GBADS_KEY_PASSWORD; without them the APK is debug-signed.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
DEPS="$ROOT/deps"
mkdir -p "$DEPS"
. scripts/mgba-options.sh   # MGBA_OPTS, MGBA_COMMIT, SDL_BRANCH

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-}}"
if [ -z "$NDK" ] || [ ! -f "$NDK/build/cmake/android.toolchain.cmake" ]; then
	echo "set ANDROID_NDK_HOME to an Android NDK" >&2
	exit 1
fi
export ANDROID_NDK_HOME="$NDK"
ABI=arm64-v8a
API=26

# --- sources ---------------------------------------------------------------
if [ ! -d "$DEPS/mgba" ]; then
	git clone https://github.com/mgba-emu/mgba.git "$DEPS/mgba"
	git -C "$DEPS/mgba" checkout "$MGBA_COMMIT"
fi
if [ ! -d "$DEPS/SDL2" ]; then
	git clone --depth 1 -b "$SDL_BRANCH" https://github.com/libsdl-org/SDL.git "$DEPS/SDL2"
fi

# --- libmgba for Android ----------------------------------------------------
MGBA_BUILD="$DEPS/mgba/build-android-$ABI"
cmake -S "$DEPS/mgba" -B "$MGBA_BUILD" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
	-DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$API "${MGBA_OPTS[@]}"
cmake --build "$MGBA_BUILD" -j"$(nproc)" --target mgba zlibstatic

# --- APK ---------------------------------------------------------------------
gradle -p android --no-daemon assembleRelease \
	-PsdlDir="$DEPS/SDL2" -PmgbaSrc="$DEPS/mgba" -PmgbaBuild="$MGBA_BUILD"

VERSION=$(cat VERSION)
APK="dist/gba-ds-v$VERSION-android.apk"
rm -f dist/gba-ds-*-android.apk
cp android/app/build/outputs/apk/release/app-release.apk "$APK"
echo "built $APK"

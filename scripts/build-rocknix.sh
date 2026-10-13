#!/bin/bash
# Reproducible cross-build of GBA DS for ROCKNIX (aarch64) on an x86_64 Linux host.
# Produces dist/gba-ds-rocknix-aarch64.zip.
#
# Needs: git, cmake, make, zip, an x86_64 host. Everything else is fetched into ./deps.
# Set AARCH64_TOOLCHAIN_BIN to use your own aarch64-linux-gnu-gcc instead.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
DEPS="$ROOT/deps"
mkdir -p "$DEPS"

MGBA_COMMIT=143adf5   # mGBA master, Oct 2026 (libmgba API with mAudioBuffer)
SDL_BRANCH=release-2.30.x

# --- toolchain -------------------------------------------------------------
if [ -z "${AARCH64_TOOLCHAIN_BIN:-}" ]; then
	TC="$DEPS/aarch64-linux-gnu"
	if [ ! -x "$TC/bin/aarch64-linux-gnu-gcc" ]; then
		git clone --depth 1 https://github.com/radcolor/aarch64-linux-gnu "$TC"
		# The prebuilt toolchain's linker scripts hard-code its CI build path.
		for f in "$TC"/aarch64-linux-gnu/lib/*.so; do
			grep -q /home/runner "$f" 2>/dev/null &&
				sed -i "s|/home/runner/work/build-tools-gcc/build-tools-gcc/aarch64-linux-gnu/aarch64-linux-gnu/lib/|$TC/aarch64-linux-gnu/lib/|g" "$f"
		done
		# ...and its limits.h doesn't chain to glibc's (PATH_MAX etc. missing).
		L=$(echo "$TC"/lib/gcc/aarch64-linux-gnu/*/include-fixed/limits.h)
		if ! grep -qE "gbadual|gba-ds" "$L"; then  # already patched (by either name)
			{ printf '/* gba-ds: chain to glibc limits.h */\n#ifndef _GCC_LIMITS_H_\n#define _GCC_LIMITS_H_\n#include_next <limits.h>\n#endif\n'; cat "$L"; } > "$L.tmp" && mv "$L.tmp" "$L"
		fi
	fi
	export AARCH64_TOOLCHAIN_BIN="$TC/bin"
fi
CROSS="$AARCH64_TOOLCHAIN_BIN/aarch64-linux-gnu-"

# --- libmgba ---------------------------------------------------------------
if [ ! -d "$DEPS/mgba" ]; then
	git clone https://github.com/mgba-emu/mgba.git "$DEPS/mgba"
	git -C "$DEPS/mgba" checkout "$MGBA_COMMIT"
fi
MGBA_OPTS=(-DLIBMGBA_ONLY=ON -DBUILD_STATIC=ON -DBUILD_SHARED=OFF -DCMAKE_BUILD_TYPE=Release
	-DUSE_DEBUGGERS=OFF -DUSE_EDITLINE=OFF -DUSE_GDB_STUB=OFF -DUSE_SQLITE3=OFF -DUSE_ELF=OFF
	-DUSE_LZMA=OFF -DUSE_ZLIB=OFF -DUSE_PNG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF -DUSE_FFMPEG=OFF
	-DUSE_LUA=OFF -DUSE_JSON_C=OFF -DUSE_DISCORD_RPC=OFF -DM_CORE_GB=OFF -DENABLE_SCRIPTING=OFF)
cmake -S "$DEPS/mgba" -B "$DEPS/mgba/build-aarch64" -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/aarch64.cmake" "${MGBA_OPTS[@]}"
make -C "$DEPS/mgba/build-aarch64" -j"$(nproc)" mgba

# --- SDL2 (only to link against; the device's own libSDL2 is used at runtime) ---
if [ ! -d "$DEPS/SDL2" ]; then
	git clone --depth 1 -b "$SDL_BRANCH" https://github.com/libsdl-org/SDL.git "$DEPS/SDL2"
fi
cmake -S "$DEPS/SDL2" -B "$DEPS/SDL2/build-aarch64" -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/aarch64.cmake" \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS/sdl-aarch64" -DSDL_STATIC=OFF -DSDL_TEST=OFF
make -C "$DEPS/SDL2/build-aarch64" -j"$(nproc)" install

# --- gba-ds ---------------------------------------------------------------
make CROSS="$CROSS" MGBA_SRC="$DEPS/mgba" MGBA_BUILD="$DEPS/mgba/build-aarch64" \
	SDL_PREFIX="$DEPS/sdl-aarch64" OUT=build/aarch64 OPTFLAGS="-O2 -mcpu=cortex-a55"

# --- package ---------------------------------------------------------------
PKG="$ROOT/build/pkg/gba-ds"
rm -rf "$ROOT/build/pkg"
mkdir -p "$PKG"
"${CROSS}strip" -o "$PKG/gba-ds" build/aarch64/gba-ds
cp dist/launch.sh dist/gba-ds.cfg "$PKG/"
mkdir -p "$PKG/logos"
printf "Optional: your own logo images (<CODE>.png or .bmp) override the ones extracted from the games.\n" > "$PKG/logos/README.txt"
mkdir -p "$PKG/overlays"
printf "Overlay PNGs (1024x768, transparent where the picture shows): primary.png (game screen) / secondary.png (map screen) for every game,\n<CODE>-primary.png / <CODE>-secondary.png for one game, optionally with -integer / -fit / -stretch. See README.\n" > "$PKG/overlays/README.txt"
cp README.md "$PKG/README.md"
cp "dist/GBA DS.sh" "$ROOT/build/pkg/"
# EmulationStation artwork for the Ports entry
mkdir -p "$ROOT/build/pkg/images"
cp assets/brand/gba-ds-icon.png "$ROOT/build/pkg/images/GBA DS.png"
chmod +x "$PKG"/*.sh "$PKG/gba-ds" "$ROOT/build/pkg/GBA DS.sh"
(cd "$ROOT/build/pkg" && rm -f "$ROOT/dist/gba-ds-rocknix-aarch64.zip" && zip -qr "$ROOT/dist/gba-ds-rocknix-aarch64.zip" gba-ds "GBA DS.sh" images)
echo "built dist/gba-ds-rocknix-aarch64.zip"

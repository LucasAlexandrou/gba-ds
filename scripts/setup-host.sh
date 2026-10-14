#!/bin/bash
# Sets up the desktop (x86_64 Linux) build used for development and testing:
# mGBA and SDL2 into ./deps, then the app (build/host/gba-ds) and the
# scripted test runner (build/host/harness).
#
# Needs: git, cmake, make, gcc, libpng-dev (harness only).
#
# Run the app without a screen:
#   SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy GBADS_WINDOW=640x960 \
#     build/host/gba-ds --state-dir=/tmp/st --input-script=script.txt GAME.gba
# (two 640x480 screens stacked, like the RG DS Plus; script lines are
#  "TICK BUTTONS" with buttons like A, B+UP or - for none, "TICK SHOT:file.bmp",
#  "TICK TAP:x,y" with x,y as fractions of the whole window, "TICK QUIT")
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
DEPS="$ROOT/deps"
mkdir -p "$DEPS"
. scripts/mgba-options.sh

if [ ! -d "$DEPS/mgba" ]; then
	git clone https://github.com/mgba-emu/mgba.git "$DEPS/mgba"
	git -C "$DEPS/mgba" checkout "$MGBA_COMMIT"
fi
cmake -S "$DEPS/mgba" -B "$DEPS/mgba/build-host" "${MGBA_OPTS[@]}"
make -C "$DEPS/mgba/build-host" -j"$(nproc)" mgba zlibstatic

if [ ! -f "$DEPS/sdl-host/lib/libSDL2.so" ]; then
	[ -d "$DEPS/SDL2" ] || git clone --depth 1 -b "$SDL_BRANCH" https://github.com/libsdl-org/SDL.git "$DEPS/SDL2"
	cmake -S "$DEPS/SDL2" -B "$DEPS/SDL2/build-host" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$DEPS/sdl-host" -DSDL_TEST=OFF -DSDL_STATIC=OFF
	make -C "$DEPS/SDL2/build-host" -j"$(nproc)" install
fi

make -j"$(nproc)"
make tools
echo "built build/host/gba-ds and build/host/harness"

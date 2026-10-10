#!/bin/sh
# Development build for the host (x86_64) using the libmgba built in ../mgba/build-host
set -e
cd "$(dirname "$0")"
M=../mgba
DEFS=$(sed -n "s/^C_DEFINES = //p" $M/build-host/CMakeFiles/mgba.dir/flags.make)
CFLAGS="$DEFS -O2 -g -Wall -I$M/include -I$M/build-host/include"
LIBS="$M/build-host/libmgba.a -lpng -lpthread -lm"
gcc $CFLAGS -o tools/harness tools/harness.c tools/png.c src/emu.c src/mapcap.c src/profiles.c $LIBS

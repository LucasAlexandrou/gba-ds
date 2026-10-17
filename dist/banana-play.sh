#!/bin/bash
# BananaOS: its Game Boy Advance list runs this for every game once GBA DS is
# switched on there (GBA DS's game list > Settings > GBA DS in BananaOS's GBA
# list, which edits /data/banana/systems.conf). Games GBA DS has second-screen
# support for start in GBA DS; every other game goes to the list's own
# emulator, the run line kept in systems.conf as "# gba-ds: run = ...".
#   banana-play.sh <rom>
DIR="$(cd "$(dirname "$0")" && pwd)"
ROM="$1"
CONF=/data/banana/systems.conf
if "$DIR/gba-ds" --supported "$ROM" >/dev/null 2>&1; then
	# GBA DS runs two copies of the game (the second for the map): all the
	# clock the profile for ports gives
	banana-perf performance >/dev/null 2>&1
	exec bash "$DIR/launch.sh" "$ROM"
fi
ORIG=$(sed -n 's/^# gba-ds: run = //p' "$CONF" 2>/dev/null | head -n 1)
[ -n "$ORIG" ] || ORIG='/usr/libexec/banana/run-retroarch gpsp %ROM%'
export GBADS_ROM="$ROM"
ORIG=${ORIG//%ROM%/\"\$GBADS_ROM\"}
ORIG=${ORIG//%SYSTEM%/gba}
exec sh -c "$ORIG"

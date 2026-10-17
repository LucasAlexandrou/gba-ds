#!/bin/bash
# GBA DS launcher for ROCKNIX.
#   launch.sh                      open the game library
#   launch.sh /path/Game.gba       play one game directly
#   (any --option=value from gba-ds.cfg can be appended)
# Output goes to gba-ds.log next to this script.

. /etc/profile >/dev/null 2>&1

DIR="$(cd "$(dirname "$0")" && pwd)"

# Controller mappings shipped with ROCKNIX
for db in /storage/.config/SDL-GameControllerDB/gamecontrollerdb.txt \
          /usr/config/SDL-GameControllerDB/gamecontrollerdb.txt; do
	if [ -f "$db" ]; then
		export SDL_GAMECONTROLLERCONFIG_FILE="$db"
		break
	fi
done

# Find sway's IPC socket so GBA DS can span both screens.
if [ -z "$SWAYSOCK" ]; then
	for s in "$XDG_RUNTIME_DIR"/sway-ipc.*.sock /var/run/0-runtime-dir/sway-ipc.*.sock /run/user/*/sway-ipc.*.sock; do
		if [ -S "$s" ]; then
			export SWAYSOCK="$s"
			break
		fi
	done
fi

cd "$DIR"

# First run after upgrading from gbadual (the app's old name): bring over
# save states, resume points, menu settings and any custom logos/overlays.
OLD="$DIR/../gbadual"
if [ -d "$OLD/states" ] && [ ! -d "$DIR/states" ]; then
	cp -a "$OLD/states" "$DIR/states"
	for d in logos overlays; do
		[ -d "$OLD/$d" ] && mkdir -p "$DIR/$d" && cp -an "$OLD/$d/." "$DIR/$d/" 2>/dev/null
	done
fi

# BananaOS (github.com/JorreFog/BananaLinux): no compositor and no sway.
# Banana's SDL puts one window over both panels itself (SDL_BANANA_PANELS:
# the top panel on the left, the bottom one on the right, as sway lays them
# out on ROCKNIX; touches on the bottom panel land on its half), drawn with
# the GPU (the offscreen driver: OpenGL ES, each frame read back onto the
# panels). Its paths replace ROCKNIX's for the settings still at ROCKNIX's
# defaults in gba-ds.cfg (anything you changed there is kept).
if [ -d /etc/banana ]; then
	LOG="$DIR/gba-ds.log"
	echo "BananaOS $(cat /etc/banana/VERSION /usr/share/banana/VERSION 2>/dev/null | head -n 1)" > "$LOG"
	export SDL_VIDEODRIVER=${GBADS_BANANA_VIDEO:-offscreen} SDL_BANANA_PANELS=side SDL_BANANA_TOP=${BANANA_TOP:-DSI-2}
	unset SDL_KMSDRM_FIRST
	# the ports launcher switched the bottom panel's backlight off (and
	# bananad keeps it off while /run/banana/panel-off names it): on for us
	if [ -f /run/banana/panel-off ]; then
		BL=$(cat /run/banana/panel-off)
		rm -f /run/banana/panel-off
		[ -w "$BL/bl_power" ] && echo 0 > "$BL/bl_power"
	fi
	cfgval() { sed -n "s/^$1 *= *//p" "$DIR/gba-ds.cfg" 2>/dev/null | tail -n 1; }
	B=()
	[ "$(cfgval rom_dir)" = /storage/roms/gba ] && B+=(--rom-dir=/data/roms/gba)
	[ "$(cfgval screenshot_dir)" = /storage/roms/screenshots ] && B+=(--screenshot-dir=/data/screenshots)
	case "$(cfgval cheat_dir)" in /config/retroarch/cheats*) B+=(--cheat-dir=/data/retroarch/cheats) ;; esac
	# battery saves beside RetroArch's (a gpSP .srm is carried over on a game's first start)
	[ -z "$(cfgval save_dir)" ] && B+=(--save-dir=/data/saves)
	# RetroAchievements: the account from Banana's first-time setup
	A=/data/banana/accounts.conf
	if [ -z "$(cfgval ra_username)" ] && [ -f $A ]; then
		u=$(sed -n 's/^ra_user=//p' $A | head -n 1)
		w=$(sed -n 's/^ra_password=//p' $A | head -n 1)
		[ -n "$u" ] && B+=("--ra-username=$u" "--ra-password=$w")
	fi
	mkdir -p /data/screenshots /data/saves 2>/dev/null
	echo "Banana paths: ${B[*]//--ra-password=*/--ra-password=...}" >> "$LOG"
	exec ./gba-ds "${B[@]}" "$@" >> "$LOG" 2>&1
fi

# Second screen on and in place for the run (stock ROCKNIX keeps it off),
# put back afterwards; see screens.sh. The trap also covers being killed.
"$DIR/screens.sh" on > "$DIR/gba-ds.log" 2>&1
trap '"$DIR/screens.sh" off >> "$DIR/gba-ds.log" 2>&1' EXIT
trap 'exit 143' TERM INT HUP

./gba-ds "$@" >> "$DIR/gba-ds.log" 2>&1

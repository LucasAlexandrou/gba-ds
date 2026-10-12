#!/bin/bash
# ROCKNIX Ports entry for GBA DS. Opens the game library, which lists every
# .gba in /storage/roms/gba (and its subfolders) each time it starts.
exec "$(dirname "$0")/gba-ds/launch.sh" "$@"

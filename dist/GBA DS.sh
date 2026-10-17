#!/bin/bash
# Ports entry for GBA DS (ROCKNIX, BananaOS). Opens the game library, which
# lists every .gba in the GBA ROM folder (and its subfolders) each time it starts.
exec "$(dirname "$0")/gba-ds/launch.sh" "$@"

#!/bin/bash
# Creates a "Ports" menu entry for every supported game found in your GBA
# ROM folder. Run it once after copying gbadual, and again after adding ROMs:
#   bash /storage/roms/ports/gbadual/install-ports.sh [ROM_DIR]
# Then restart EmulationStation (or "Update Gamelists") to see the entries.

ROMDIR="${1:-/storage/roms/gba}"
PORTS="$(cd "$(dirname "$0")/.." && pwd)"
APP="$(cd "$(dirname "$0")" && pwd)"

found=0
shopt -s nullglob nocaseglob
for rom in "$ROMDIR"/*.gba; do
	code=$(dd if="$rom" bs=1 skip=172 count=4 2>/dev/null)
	case "$code" in
		A2CE) title="Castlevania - Aria of Sorrow" ;;
		ACHE) title="Castlevania - Harmony of Dissonance" ;;
		AAME) title="Castlevania - Circle of the Moon" ;;
		BMXE) title="Metroid - Zero Mission" ;;
		AMTE) title="Metroid Fusion" ;;
		*) continue ;;
	esac
	script="$PORTS/$title (Dual Screen).sh"
	cat > "$script" <<EOF
#!/bin/bash
exec "$APP/launch.sh" "$rom"
EOF
	chmod +x "$script"
	echo "added: $title  <-  $(basename "$rom")"
	found=$((found + 1))
done

if [ "$found" = 0 ]; then
	echo "No supported (unzipped, USA) .gba ROMs found in $ROMDIR"
	exit 1
fi
echo "Done. Refresh the game list in EmulationStation to see them under Ports."

# Changelog

## v1.0.3 (not yet released)

- **Tony Hawk's Pro Skater 2** (USA/Europe, France, Germany, and SK8 for Japan) is in the game list. For now the second screen shows its logo, cut out of the title screen (it has no layer of its own).
- Tony Hawk's Pro Skater 2: the goal sheet shown before a run (Free Skate or Single Session rules, or the career level's checklist) is copied to the second screen, without the CAREER / CASH counters and button prompts drawn over it, and stays there for the run, through pauses and Retry. Back at the menus (level select, main menu) the second screen goes back to the logo.
- Tony Hawk's Pro Skater 2: a **Controls** picker on the second screen (touch) chooses the GBA buttons, a four-face-button layout like the console games (top grind, bottom ollie, left flip, right grab), or **Auto**: four buttons while skating, the GBA layout in menus, pause and results. Remembered per game.
- Game list: French and German flags for single-country releases when you have more than one region of a game.

## v1.0.2 (2026-10-13)

- Metroid map screens: the scroll arrows and play-time clock are left out (both games), as is Fusion's row of ticked item icons along the top (the item counters stay), and Zero Mission's corner minimap and its A button give way to the map underneath. Both Metroids are now marked "working".
- **Take Screenshot** on the pause menu's Game page: both screens exactly as displayed (scaling, LCD grid, overlays, screen order), stacked top to bottom in one PNG, saved to `/storage/roms/screenshots`.
- The map no longer dims in the Metroid (and Castlevania) games when a capture misses, e.g. while running through rooms quickly. Only A Link to the Past dims it, where L really has no map to show.
- Zero Mission map: the "SELECT / CHANGE AREAS" prompt (once you've been to more than one area) is covered by a raised piece of the bezel bar, which is now always drawn so the frame looks the same either way.
- Metroid Fusion map: the "A OBJECTIVE" prompt is left out of the capture instead of painted over, so the rooms under it show properly on a well-explored map.
- README: a Roadmap section.

## v1.0.1 (2026-10-13)

- **Cheats**: RetroArch `.cht` files (Code Breaker, GameShark, Action Replay) from RetroArch's cheats folder, switched on and off from a new Cheats screen on the pause menu's Game page.
- Game list: a small USA / Europe / Japan flag in the corner of a game's tile when you have the same game from more than one region.
- **European and Japanese releases** of all six games: map, title logo and Castlevania border. The Japanese Aria of Sorrow, Harmony of Dissonance and Metroid Fusion keep their state at slightly different memory addresses; the European multi-language releases start at a language menu, which the logo and border extraction now gets past.
- Title logos: a fix for games that don't rewrite their display settings every frame (Circle of the Moon JP came out blank).
- New **Map Animations** option (pause menu → Display, or `map_animations` in gba-ds.cfg). Off shows a still map: each blinking or pulsing marker is frozen where it's most visible (Fusion's white Samus square, the Castlevania glows at their brightest, Link and the red X in A Link to the Past), and the still only changes when the map itself does.

## v1.0 (2026-10-13)

First release. GBA DS is a Game Boy Advance emulator for dual-screen handhelds (built for the Anbernic RG DS Plus on ROCKNIX) that keeps the game's own map on the second screen, the way the DS Castlevanias do.

**Games with a live map**
- Castlevania: Aria of Sorrow, Harmony of Dissonance, Circle of the Moon (USA). Circle of the Moon also shows its completion rate, copied from the pause screen.
- Metroid: Zero Mission and Metroid Fusion (USA): the whole pause screen with its frame, lettering and button prompts erased, and the HUD mini-map hidden.
- The Legend of Zelda: A Link to the Past (USA): the zoomed-out world map outdoors, dungeon maps once you have them; dimmed where no map is available. Four Swords is left off the list (it needs linked GBAs).
- Every other GBA game plays normally on one screen.

**App**
- Game list of logo tiles pulled from each ROM; touch or D-pad.
- Pause menu (tap the screen or HOME): save / load state, restart, swap screens, scaling (pixel perfect / full width / stretch), LCD grid, overlays, FPS counter.
- Automatic resume: leaving a game saves where you were.
- Overlays: generated map surrounds, plus your own PNGs for the primary (game) and secondary (map) screens, per game or for every game.
- Everything game-specific (logos, map borders) is extracted from your ROMs; no game art is shipped.

**Install:** unzip `gba-ds-v1.0-rocknix-aarch64.zip` into `/storage/roms/ports/`, keep unzipped `.gba` ROMs in `/storage/roms/gba/`, and launch **GBA DS** from Ports. See the README for details and for upgrading from the old `gbadual` builds.

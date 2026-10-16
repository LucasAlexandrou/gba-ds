The **Overlays** setting (pause menu → Display, or `overlays` in `gba-ds.cfg`) cycles **Primary / Secondary / Both / None**. "Primary" is the game screen and "secondary" the second (map) screen, whichever physical screen they're on: swap screens and each overlay moves with its picture.

## Generated map surrounds

**Secondary** turns on the generated map surround (and your own second-screen overlay). The surround fills the space around the map with graphics from the game itself. Nothing is shipped: like the logos, it's extracted from the ROM once in the background and cached in `states/frames/`.

- **Castlevania (all three):** the map's background colour, plus a border from the game's own menus:
  - Aria of Sorrow and Harmony of Dissonance use their name-entry frame;
  - Circle of the Moon uses the bevelled boxes from its file menu.

  In Pixel Perfect scaling the border goes all the way round the map; in Full Width it spans the top and bottom.
- **Metroid: Zero Mission / Fusion, A Link to the Past:** none. These map screens are shown whole, with the game's own frame, so there's nothing to fill.
- **Tony Hawk's Pro Skater 2, Mario Kart: Super Circuit:** none; their second screens are full GBA screens (Mario Kart's has the course's scenery behind it; see [[Supported games]]).

The surround is assembled in the game's own pixel space and scaled together with the map, so it lines up exactly and the LCD grid covers it too. `border_color` (hex, default `000000`) sets the colour behind the map where there's no surround or overlay.

## Your own overlays

Put PNGs in `gba-ds/overlays/` (Android: `GBA DS/overlays/`). Each one is drawn over the whole screen, stretched to its size, so make it the screen's size (**1024×768** on the RG DS Plus, **640×480** on the RG DS) and leave it transparent where the picture should show. The primary (game screen) overlay is a good place for a console bezel.

For each screen the first file found is used, so a game's own overlay beats a global one:

| | Primary (game screen) | Secondary (second screen) |
|---|---|---|
| this game, this scaling | `<CODE>-primary-<scale>.png` | `<CODE>-secondary-<scale>.png` |
| this game | `<CODE>-primary.png` | `<CODE>-secondary.png` |
| every game, this scaling | `primary-<scale>.png` | `secondary-<scale>.png` |
| every game | `primary.png` | `secondary.png` |

- `<CODE>` is the game's code, e.g. `A2CE` (Aria of Sorrow, USA). Each region has its own code: see the table in [[Supported games]].
- `<scale>` is that screen's scaling: `integer` (Pixel Perfect), `fit` (Full Width), `zoom` or `stretch`.
- Map overlays named the old way (`<CODE>-<scale>.png`, `<CODE>.png`) still work, ahead of the global ones.
- The secondary overlay is only drawn while a map (or HUD) is showing, not over the logo.

Areas to keep transparent, where the game or map picture is (the same for every game, since everything is shown at full GBA size):

| Scaling | RG DS Plus (1024×768) | RG DS (640×480) |
|---|---|---|
| Pixel Perfect (`integer`) | x=32, y=64, 960×640 | x=80, y=80, 480×320 |
| Full Width (`fit`) | x=0, y=43, 1024×682 | x=0, y=27, 640×427 |
| Zoom, Stretch | the whole screen | the whole screen |

`dist/gba-ds-overlay-templates-preview.png` in the repository shows the areas.

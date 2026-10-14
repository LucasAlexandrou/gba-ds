/* Per-game knowledge: how to open the map and how to recognise it. */
#ifndef GBADS_PROFILES_H
#define GBADS_PROFILES_H

#include "emu.h"
#include "logo_masks.h"

typedef struct {
	uint32_t keys;
	int frames;
} MacroStep;

/* A screen pixel that must have a given colour while the map is up. */
typedef struct {
	int x, y;
	uint32_t rgb; /* 0xRRGGBB */
} Probe;

/* A memory location that must hold a given value while the map is up. */
typedef struct {
	uint32_t addr; /* 0 terminates the list */
	uint32_t mask;
	uint32_t value;
	int size; /* 1, 2 or 4 bytes */
	bool hasAlt; /* also accept `alt` (A Link to the Past: overworld OR dungeon) */
	uint32_t alt;
} MemProbe;

typedef struct MapProfile {
	const char* codes[4]; /* 4-char game codes this profile handles */
	const char* title;
	const char* listName; /* name in the game list instead of the file name */

	MacroStep open[8]; /* button sequence that brings up the map */
	/* Optional second step: once the map is up (mem probes), if open2If
	 * matches, wait open2Delay frames and press open2 (A Link to the Past:
	 * L again on the world map zooms out to the whole world). */
	MemProbe open2If[2];
	int open2Delay;
	MacroStep open2[4];
	int settleMax;     /* frames to wait after the macro for the map to settle */
	int openWithin;    /* give up if the memory probes don't match within this many frames (0 = settleMax) */
	/* Dim the last map while the map can't be brought up (A Link to the
	 * Past: L does nothing indoors without a dungeon map). Other games keep
	 * the last map at full brightness: their captures also fail briefly
	 * during room transitions. */
	bool dimWhenUnavailable;
	/* No map yet: the game is listed and the second screen shows its logo,
	 * but nothing is captured. */
	bool logoOnly;
	int stableFrames;  /* consecutive "quiet" frames required */
	int maxDiff;       /* pixels allowed to change between quiet frames (blinking markers) */
	int crop_x, crop_y, crop_w, crop_h;

	/* Optional tighter crop that drops the screen's bezel and title bar
	 * (used when map_crop is on), and rectangles (screen coords) to cover
	 * with empty map-grid tiles, e.g. button prompts drawn over the map.
	 * gridX/gridY/gridCell give the phase and size of the background grid. */
	int tight_x, tight_y, tight_w, tight_h;
	struct { int x, y, w, h; } masks[8];
	int gridX, gridY, gridCell;
	/* (Unused: removing the Metroid scroll arrows glitched once the map grew
	 * under them, so they're left in.) Scroll arrows drawn on top of the map: inside these rects, pixels of the
	 * arrow's colours (arrowKey 1 = magenta, 2 = yellow/orange/red) are
	 * replaced with the same pixel one or two grid cells to the side. */
	struct { int x, y, w, h; } arrows[2];
	int arrowKey;
	/* Lettering to erase (when map_crop is on): inside the rect (screen
	 * coords), pixels of exactly these n colours (0xRRGGBB, n = 0 means 1; n < 0
	 * paints the whole rect in rgb[0], for text that animates) are
	 * filled in from the pixels around them (or, with flat, all set to the most
	 * common other colour in the rect), which leaves the button or plate they
	 * sit on untouched. With grid, every pixel in the rect is rebuilt from the
	 * same spot 1-3 grid cells (gridCell) above and to the sides, outside any
	 * grid rect: for prompts that also clear the map grid behind them. */
	struct { int x, y, w, h; int n; uint32_t rgb[8]; bool flat, grid; } glyphs[10];
	/* Recolouring (when map_crop is on, after glyphs): inside the rect, each
	 * colour from[i] becomes to[i] (0xRRGGBB, n pairs). */
	struct { int x, y, w, h; int n; uint32_t from[16], to[16]; } recolor[6];

	Probe probes[8];
	int minProbes;    /* how many probes must match (0 = all) */
	int tolerance;    /* per-channel colour tolerance */
	uint32_t bgColor; /* map background; >= minForeground pixels must differ from it */
	int minForeground;

	MemProbe mem[4]; /* checked on the shadow core once the map should be up */
	MemProbe live[4]; /* checked on the live core before snapshotting (gameplay) */
	/* Back on the title / file menus (all must match): forget the map and
	 * show the logo again until the next capture. */
	MemProbe menu[2];

	/* Values that change when the player enters a new room; a change
	 * triggers an early capture. Only addr/size are used. */
	MemProbe room[4];

	/* Title-screen logo, pulled out of the running game: boot, wait, optionally
	 * press Start (to skip an intro), wait again, then keep one BG layer
	 * (0-3) above row maxY. blackIsClear: the layer has an opaque black fill. */
	struct {
		int wait, wait2, layer, maxY;
		bool pressStart, blackIsClear;
		MacroStep steps[24]; /* instead of wait/pressStart: button steps from power-on */
		int rev; /* bump when extraction changes, so cached logos are made again */
		/* The logo is painted into the whole title picture: render every
		 * layer and keep the pixels inside this mask (THPS2). */
		const LogoMask* mask;
	} logo;

	/* Border for the map screen, taken from one of the game's own menu screens
	 * (Castlevania): play `steps` from power-on, render BG `layer` alone, and
	 * read a frame of thickness t whose corners are at x0/x1, y0/y1. Edges are
	 * tiled from p-wide slices (the most common one along each edge, which
	 * skips one-off decorations). */
	struct {
		MacroStep steps[24];
		int layer, t, p;
		int x0, y0, x1, y1;
		int minLuma; /* pixels darker than this are dropped (translucent panel fills) */
	} frame;
	/* Metroid: fill the map screen with the map's own empty grid tile. */
	bool gridBackground;

	/* Text to copy from another screen onto the map (Circle of the Moon's map
	 * has no completion rate; its pause screen does): press `open` from the
	 * live state, wait, then take the pixels of colour `text` inside x/y/w/h
	 * (the digits only), outlined in the map's background colour. It goes in
	 * the first corner of the map (top right, then bottom right) with no rooms. */
	struct {
		MacroStep open[4];
		int wait;
		int x, y, w, h;
		uint32_t text;
	} rate;

	/* HUD sprites to hide on the game screen while the map is shown (the
	 * Metroids' corner mini-map). */
	SpriteRule hideSprites[4];
	/* Sprites left out of the map capture (when map_crop is on): prompts the
	 * game draws over the map as sprites (Fusion's "A OBJECTIVE"), so the map
	 * underneath shows instead. */
	SpriteRule captureHide[4];
	/* Parts of the map capture (screen rects, inclusive) taken from a render
	 * of only `underLayers` (bits 0-3 BG0-3, bit 4 sprites), made once per
	 * capture: bezel pieces drawn over the map, like Zero Mission's corner
	 * minimap, give way to the map underneath. */
	struct { int x0, y0, x1, y1; } underRect[2];
	unsigned underLayers;
	SpriteRule underHide[2]; /* also left out of that render (on top of captureHide) */

	/* A raised piece of bezel painted over a prompt that sits in a notch of
	 * the bottom bezel bar (Zero Mission's "SELECT / CHANGE AREAS"), when
	 * map_crop is on. The bar's top edge is at row `base`; its notch spans
	 * n0..n1 and is filled in. Above it the tab's flat top runs x0..x1 at row
	 * `top`, its ends slope down one pixel per row to the bar. Each pixel is
	 * coloured by its depth below that outline: colors[0] (outline),
	 * colors[1], ... the last colour repeats down to row `bottom`. */
	struct { int x0, x1, top, base, n0, n1, bottom, n; uint32_t colors[12]; } tab;

	int animFrames; /* frames to record for the blinking/pulsing marker loop */
	int animStep;   /* emulated frames between recorded frames */
	/* "Map Animations: Off": a marker loop longer than animFrames * animStep
	 * (the Castlevania glows) is recorded over this many frames instead, so
	 * the still frame can always come from the same point of the loop. */
	int animCycle;
	/* How the still frame is chosen for each animated spot: the brightest
	 * state (default), or the state that covers the most pixels (ties: the
	 * most contrast) for markers whose brightest state isn't the marker. */
	enum { STILL_BRIGHT, STILL_COVER } stillPick;
} MapProfile;

const MapProfile* profile_find(const char* gameCode);
/* True for a regional release whose RAM layout differs from the USA one
 * (cheat codes made for other regions would write the wrong addresses). */
bool profile_ram_moved(const char* gameCode);
bool memprobes_match(const MemProbe* list, int n, Emu* e);

#endif

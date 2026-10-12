/* Per-game knowledge: how to open the map and how to recognise it. */
#ifndef GBADS_PROFILES_H
#define GBADS_PROFILES_H

#include "emu.h"

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
} MemProbe;

typedef struct MapProfile {
	const char* codes[4]; /* 4-char game codes this profile handles */
	const char* title;

	MacroStep open[8]; /* button sequence that brings up the map */
	int settleMax;     /* frames to wait after the macro for the map to settle */
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
	/* A letter drawn on top of a button prompt: inside the rect (screen coords),
	 * pixels of exactly this colour are filled in from the pixels around them,
	 * which leaves the button itself (and anything else) untouched. */
	struct { int x, y, w, h; uint32_t rgb; } glyphs[2];

	Probe probes[8];
	int minProbes;    /* how many probes must match (0 = all) */
	int tolerance;    /* per-channel colour tolerance */
	uint32_t bgColor; /* map background; >= minForeground pixels must differ from it */
	int minForeground;

	MemProbe mem[4]; /* checked on the shadow core once the map should be up */
	MemProbe live[4]; /* checked on the live core before snapshotting (gameplay) */

	/* Values that change when the player enters a new room; a change
	 * triggers an early capture. Only addr/size are used. */
	MemProbe room[4];

	/* Title-screen logo, pulled out of the running game: boot, wait, optionally
	 * press Start (to skip an intro), wait again, then keep one BG layer
	 * (0-3) above row maxY. blackIsClear: the layer has an opaque black fill. */
	struct {
		int wait, wait2, layer, maxY;
		bool pressStart, blackIsClear;
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

	int animFrames; /* frames to record for the blinking/pulsing marker loop */
	int animStep;   /* emulated frames between recorded frames */
} MapProfile;

const MapProfile* profile_find(const char* gameCode);
bool memprobes_match(const MemProbe* list, int n, Emu* e);

#endif

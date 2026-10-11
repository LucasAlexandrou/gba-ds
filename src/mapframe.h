/* The generated map-screen surround ("built-in overlay").
 *
 * The map is placed on a canvas in GBA pixel space that has the screen's
 * shape, so the surround is scaled with exactly the same pipeline as the map
 * (sharp at any size, LCD grid included). The canvas is filled with either
 * the map's own background colour plus a border taken from one of the game's
 * menus (Castlevania), or the map's own empty grid tile continued past the
 * edges (Metroid). */
#ifndef GBADUAL_MAPFRAME_H
#define GBADUAL_MAPFRAME_H

#include "profiles.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct MapFrame {
	const MapProfile* prof;
	int mw, mh; /* map image size */

	/* geometry for the current screen and scale mode */
	int sw, sh;
	char mode[16];
	int cw, ch, mx, my;

	uint32_t* base;   /* surround without the map */
	uint32_t* canvas; /* base + current map frame */
	bool baseDirty;

	uint32_t* atlas; /* border pieces (Castlevania), mGBA colour layout + alpha */
	int aw;
	uint32_t tile[16 * 16]; /* empty grid cell (Metroid) */
	int tk, tx, ty;
	bool tileOk;
} MapFrame;

void mapframe_init(MapFrame* f, const MapProfile* prof, int mw, int mh);
void mapframe_free(MapFrame* f);
/* Loads the border atlas; returns false if it isn't there (yet). */
bool mapframe_load_atlas(MapFrame* f, const char* path);
void mapframe_set_tile(MapFrame* f, const uint32_t* tile, int k, int tx, int ty);
/* Whether there's anything to draw around the map. */
bool mapframe_ready(const MapFrame* f);
/* Updates geometry for a screen of sw x sh and a scale mode; true if changed. */
bool mapframe_layout(MapFrame* f, int sw, int sh, const char* mode);
/* Canvas with this map frame on it (cw x ch). */
const uint32_t* mapframe_compose(MapFrame* f, const uint32_t* map);

#endif

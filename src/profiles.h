/* Per-game knowledge: how to open the map and how to recognise it. */
#ifndef GBADUAL_PROFILES_H
#define GBADUAL_PROFILES_H

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

	int animFrames; /* frames to record for the blinking/pulsing marker loop */
	int animStep;   /* emulated frames between recorded frames */
} MapProfile;

const MapProfile* profile_find(const char* gameCode);
bool memprobes_match(const MemProbe* list, int n, Emu* e);

#endif

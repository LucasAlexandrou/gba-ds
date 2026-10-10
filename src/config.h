#ifndef GBADUAL_CONFIG_H
#define GBADUAL_CONFIG_H

#include <stdbool.h>

typedef struct Config {
	char layout[16];   /* auto | span-h | span-v | dual | single */
	bool swap;         /* map on the first (left/top) screen instead of the second */
	char scale[16];    /* game: integer | fit | stretch */
	char mapScale[16]; /* map:  integer | fit | stretch */
	bool smooth;       /* linear filtering when scaling */
	bool vsync;
	bool map;          /* enable the second-screen map */
	int mapInterval;   /* frames between periodic map refreshes */
	bool abSwap;       /* GBA A on the bottom face button instead of the right one */
	char hotkey[16];   /* guide | back */
	bool showFps;
	bool sway;         /* ask sway to fullscreen the window across all outputs */
	int audioLatency;  /* ms */
	int ffSpeed;       /* fast-forward multiplier */
	char saveDir[512]; /* default: next to the ROM */
	char rom[512];

	/* testing / automation */
	char loadState[512];
	int shotFrame;
	char shotPath[512];
	int quitAfter;
	char inputScript[512];
} Config;

void config_defaults(Config* c);
/* Loads key=value lines; missing file is not an error. */
void config_load_file(Config* c, const char* path);
/* Parses --key=value / --flag arguments and the ROM path. Returns false on error. */
bool config_parse_args(Config* c, int argc, char** argv);
void config_usage(const char* argv0);

#endif

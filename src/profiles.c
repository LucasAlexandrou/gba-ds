#include "profiles.h"

#include <string.h>

/* Values below were found with tools/harness + tools/ramdiff.py against the
 * USA releases. Each profile describes how to bring up the in-game map and
 * how to tell, from memory and from the picture, that it is really showing. */
/* Metroid Fusion's SLEEP / STATUS plates cycle through four palettes
 * (green, teal, blue, bright blue): body, edge, lettering. */
#define PLATE_TEXT { 0x10FF84, 0x31FFAD, 0x52FFD6, 0x7BFFFF, 0x10B55A, 0x18B594, 0x29ADCE, 0x39A5FF }
#define PLATE_FROM { 0x086B31, 0x106B73, 0x1863BD, 0x2963FF, 0x10B55A, 0x18B594, 0x29ADCE, 0x39A5FF, \
                     0x10FF84, 0x31FFAD, 0x52FFD6, 0x7BFFFF, 0x21214A }
#define PLATE_TO   { 0xADC6BD, 0xADC6BD, 0xADC6BD, 0xADC6BD, 0xC6DED6, 0xC6DED6, 0xC6DED6, 0xC6DED6, \
                     0xDEF7EF, 0xDEF7EF, 0xDEF7EF, 0xDEF7EF, 0x5A5A5A }

static const MapProfile profiles[] = {
	{
		.codes = { "A2CE" },
		.title = "Castlevania: Aria of Sorrow",
		.open = { { K_SELECT, 4 } },
		.settleMax = 150, .stableFrames = 3, .maxDiff = 160,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		/* dark purple background in the corners (3 of 4, so a room in a corner can't break it) */
		.probes = { { 1, 1, 0x29007B }, { 238, 1, 0x29007B }, { 1, 158, 0x29007B }, { 238, 158, 0x29007B } },
		.minProbes = 3, .tolerance = 8,
		.bgColor = 0x29007B, .minForeground = 12,
		.mem = { { 0x03002C68, 0xFF, 0x02, 1 } },
		.logo = { .wait = 1700, .layer = 1, .maxY = 160 },
		/* name entry screen: power on, Start through the title, A (Game Start), A (new file) */
		.frame = { .steps = { { 0, 900 }, { K_START, 3 }, { 0, 9 }, { K_START, 3 }, { 0, 69 }, { K_A, 3 }, { 0, 49 },
		                      { K_A, 3 }, { 0, 69 } },
		           .layer = 1, .t = 8, .p = 8, .x0 = 0, .y0 = 40, .x1 = 232, .y1 = 152 },
		.animFrames = 8, .animStep = 4,
	},
	{
		.codes = { "ACHE" },
		.title = "Castlevania: Harmony of Dissonance",
		.open = { { K_SELECT, 4 } },
		.settleMax = 150, .stableFrames = 3, .maxDiff = 160,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 1, 1, 0x000000 }, { 238, 158, 0x000000 }, { 1, 158, 0x000000 }, { 120, 1, 0x000000 } },
		.minProbes = 3, .tolerance = 8,
		.bgColor = 0x000000, .minForeground = 12,
		.mem = { { 0x03002B28, 0xFF, 0x02, 1 } },
		.logo = { .wait = 1700, .layer = 1, .maxY = 130 },
		/* name entry screen */
		.frame = { .steps = { { 0, 700 }, { K_START, 3 }, { 0, 9 }, { K_A, 3 }, { 0, 39 }, { K_START, 3 }, { 0, 9 },
		                      { K_A, 3 }, { 0, 39 }, { K_START, 3 }, { 0, 9 }, { K_A, 3 }, { 0, 39 }, { K_START, 3 },
		                      { 0, 9 }, { K_A, 3 }, { 0, 90 } },
		           .layer = 1, .t = 8, .p = 8, .x0 = 0, .y0 = 40, .x1 = 232, .y1 = 152, .minLuma = 90 },
		.animFrames = 8, .animStep = 3,
	},
	{
		.codes = { "AAME" },
		.title = "Castlevania: Circle of the Moon",
		.open = { { K_SELECT, 4 } },
		.settleMax = 150, .stableFrames = 3, .maxDiff = 160,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 1, 1, 0x000000 }, { 238, 1, 0x000000 }, { 1, 158, 0x000000 }, { 238, 158, 0x000000 } },
		.minProbes = 3, .tolerance = 8,
		.bgColor = 0x000000, .minForeground = 8,
		.mem = { { 0x03000C80, 0xFF, 0x01, 1 } },
		.live = { { 0x03000C80, 0xFF, 0x00, 1 } },
		.logo = { .wait = 700, .layer = 0, .maxY = 120, .blackIsClear = true },
		/* the bevelled boxes of the file menu (same style as name entry) */
		.frame = { .steps = { { 0, 600 }, { K_START, 3 }, { 0, 9 }, { K_START, 3 }, { 0, 400 } },
		           .layer = 1, .t = 4, .p = 1, .x0 = 10, .y0 = 10, .x1 = 98, .y1 = 34 },
		.animFrames = 8, .animStep = 4,
	},
	{
		.codes = { "BMXE" },
		.title = "Metroid: Zero Mission",
		.open = { { K_START, 4 } },
		.settleMax = 180, .stableFrames = 3, .maxDiff = 300,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 0, 0, 0xCEDECE }, { 239, 0, 0xCEDECE }, { 2, 80, 0x212921 }, { 237, 80, 0x212921 } },
		.tolerance = 8,
		/* the whole pause screen, frame included; map_crop erases its lettering:
		 * the L and R keys, SLEEP, the area name, STATUS, and the A on the
		 * button beside the minimap (the keys, plates and button stay) */
		.glyphs = { { 3, 3, 6, 5, 2, { 0x000000, 0x314231 }, true },     /* L */
		            { 232, 3, 5, 5, 2, { 0x000000, 0x314231 }, true },   /* R */
		            { 13, 2, 35, 7, 2, { 0xFFFF73, 0xFFBD00 }, true },         /* SLEEP */
		            { 98, 3, 44, 8, -1, { 0xCE21D6 } },              /* area name (animates), */
		            { 99, 3, 1, 8, -1, { 0xE742EF } },               /* with the plate's */
		            { 140, 3, 1, 8, -1, { 0xE742EF } },              /* inner bevel */
		            { 192, 2, 35, 7, 2, { 0xFFFF73, 0xFFBD00 }, true },        /* STATUS */
		            { 176, 140, 14, 12, 1, { 0x001831 } } },       /* A */
		/* the now-blank magenta plates and orange L/R keys, in the grey-green of
		 * the curved pieces beside the area name: edge/shade, body, bevel/highlight */
		.recolor = { { 13, 2, 35, 7, 3, { 0xAD18B5, 0xCE21D6, 0xE742EF }, { 0x212921, 0x314231, 0x637363 } },
		             { 97, 2, 46, 10, 3, { 0xAD18B5, 0xCE21D6, 0xE742EF }, { 0x212921, 0x314231, 0x637363 } },
		             { 192, 2, 35, 7, 3, { 0xAD18B5, 0xCE21D6, 0xE742EF }, { 0x212921, 0x314231, 0x637363 } },
		             { 2, 2, 8, 7, 4, { 0xFF7B00, 0xFFBD00, 0xFFFF73, 0xFFFFFF }, { 0x212921, 0x314231, 0x637363, 0x94A594 } },
		             { 230, 2, 8, 7, 4, { 0xFF7B00, 0xFFBD00, 0xFFFF73, 0xFFFFFF }, { 0x212921, 0x314231, 0x637363, 0x94A594 } } },
		.mem = { { 0x03000C70, 0xFF, 0x05, 1 } },
		.live = { { 0x03000C70, 0xFF, 0x04, 1 } },
		.logo = { .wait = 200, .pressStart = true, .wait2 = 200, .layer = 1, .maxY = 104 },
		/* the HUD mini-map in the top-right corner: a 32x32 frame (tile 220)
		 * and the blinking 8x8 position marker, both drawn on top (priority 0) */
		.hideSprites = { { 200, -16, 239, 24, 160, 255, 0 } },
		.animFrames = 8, .animStep = 4,
	},
	{
		.codes = { "AMTE" },
		.title = "Metroid Fusion",
		.open = { { K_START, 4 } },
		.settleMax = 180, .stableFrames = 3, .maxDiff = 640,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 0, 0, 0x9C9C9C }, { 239, 0, 0x9C9C9C }, { 0, 159, 0x9C9C9C }, { 239, 159, 0x9C9C9C },
		            { 2, 80, 0xFFFFFF }, { 237, 80, 0xFFFFFF } },
		.tolerance = 8,
		/* the whole pause screen, frame included; map_crop erases its lettering
		 * (the L and R key letters, SLEEP, the area name, STATUS) and the
		 * "A OBJECTIVE" prompt, which also blanks the grid behind it */
		.glyphs = { { 4, 3, 5, 6, 1, { 0x21214A }, true },                      /* L */
		            { 231, 3, 5, 6, 1, { 0x21214A }, true },                    /* R */
		            { 13, 4, 22, 5, 8, PLATE_TEXT, true },                      /* SLEEP */
		            { 203, 4, 26, 5, 8, PLATE_TEXT, true },                     /* STATUS */
		            { 97, 0, 46, 7, -1, { 0xADC6BD } },                         /* area name */
		            { 169, 141, 14, 11, .grid = true },                         /* A button */
		            { 172, 143, 62, 7, .grid = true } },                        /* OBJECTIVE */
		/* the blank plates and keys, in the bezel's greys */
		.recolor = { { 2, 1, 42, 10, 13, PLATE_FROM, PLATE_TO },
		             { 196, 1, 42, 10, 13, PLATE_FROM, PLATE_TO },
		             { 96, 0, 48, 8, 1, { 0x21214A }, { 0x5A5A5A } } },
		.gridX = 0, .gridY = 0, .gridCell = 8,
		.mem = { { 0x03000BDE, 0xFF, 0x03, 1 } },
		.live = { { 0x03000BDE, 0xFF, 0x01, 1 } },
		.logo = { .wait = 200, .pressStart = true, .wait2 = 300, .layer = 1, .maxY = 125 },
		/* the HUD mini-map in the top-right corner: a 32x32 frame (tile 220)
		 * and the blinking 8x8 position marker, both drawn on top (priority 0) */
		.hideSprites = { { 200, -16, 239, 24, 160, 255, 0 } },
		.animFrames = 8, .animStep = 4,
	},
};

const MapProfile* profile_find(const char* gameCode) {
	for (size_t i = 0; i < sizeof(profiles) / sizeof(*profiles); ++i) {
		for (int j = 0; j < 4 && profiles[i].codes[j]; ++j) {
			if (!strncmp(profiles[i].codes[j], gameCode, 4)) {
				return &profiles[i];
			}
		}
	}
	return NULL;
}

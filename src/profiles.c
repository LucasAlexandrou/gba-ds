#include "profiles.h"

#include <string.h>

/* Values below were found with tools/harness + tools/ramdiff.py against the
 * USA releases. Each profile describes how to bring up the in-game map and
 * how to tell, from memory and from the picture, that it is really showing. */
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
		.mem = { { 0x03000C70, 0xFF, 0x05, 1 } },
		.live = { { 0x03000C70, 0xFF, 0x04, 1 } },
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
		.mem = { { 0x03000BDE, 0xFF, 0x03, 1 } },
		.live = { { 0x03000BDE, 0xFF, 0x01, 1 } },
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

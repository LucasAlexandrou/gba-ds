#include "profiles.h"

#include <pthread.h>
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
		.animFrames = 8, .animStep = 4, .animCycle = 90, /* glow pulse: 84 frames */
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
		.animFrames = 8, .animStep = 3, .animCycle = 64, /* glow pulse: 64 frames */
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
		/* the "x.x%" after RATE on the pause screen (fully drawn ~30 frames after
		 * Start); digits only, so it fits beside a fully explored castle */
		.rate = { .open = { { K_START, 4 } }, .wait = 40, .x = 183, .y = 25, .w = 56, .h = 7, .text = 0xEFFFEF },
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
		 * the L and R keys, SLEEP, the area name and STATUS (the keys and
		 * plates stay) */
		.glyphs = { { 3, 3, 6, 5, 2, { 0x000000, 0x314231 }, true },     /* L */
		            { 232, 3, 5, 5, 2, { 0x000000, 0x314231 }, true },   /* R */
		            { 13, 2, 35, 7, 2, { 0xFFFF73, 0xFFBD00 }, true },         /* SLEEP */
		            { 98, 3, 44, 8, -1, { 0xCE21D6 } },              /* area name (animates), */
		            { 99, 3, 1, 8, -1, { 0xE742EF } },               /* with the plate's */
		            { 140, 3, 1, 8, -1, { 0xE742EF } },              /* inner bevel */
		            { 192, 2, 35, 7, 2, { 0xFFFF73, 0xFFBD00 }, true } },      /* STATUS */
		/* left out of the capture: the purple scroll arrows (tiles 512-515),
		 * the play-time clock (916-927) and the corner minimap's own markers
		 * (priority 1, bottom right) */
		.captureHide = { { -256, -128, 255, 159, 512, 515, 3 }, { -256, 130, 255, 159, 916, 927, 3 } },
		/* the corner minimap and its A button are part of the bezel layers:
		 * there the capture shows the map layer (and sprites) on their own */
		.underRect = { { 160, 90, 231, 151 } }, .underLayers = 0x18,
		/* without the minimap's own sprites (priority 1, bottom right; hiding
		 * them all the time stops the map screen from ever settling early on) */
		.underHide = { { 150, 80, 255, 159, 540, 700, 1 } },
		/* the now-blank magenta plates and orange L/R keys, in the grey-green of
		 * the curved pieces beside the area name: edge/shade, body, bevel/highlight */
		.recolor = { { 13, 2, 35, 7, 3, { 0xAD18B5, 0xCE21D6, 0xE742EF }, { 0x212921, 0x314231, 0x637363 } },
		             { 97, 2, 46, 10, 3, { 0xAD18B5, 0xCE21D6, 0xE742EF }, { 0x212921, 0x314231, 0x637363 } },
		             { 192, 2, 35, 7, 3, { 0xAD18B5, 0xCE21D6, 0xE742EF }, { 0x212921, 0x314231, 0x637363 } },
		             { 2, 2, 8, 7, 4, { 0xFF7B00, 0xFFBD00, 0xFFFF73, 0xFFFFFF }, { 0x212921, 0x314231, 0x637363, 0x94A594 } },
		             { 230, 2, 8, 7, 4, { 0xFF7B00, 0xFFBD00, 0xFFFF73, 0xFFFFFF }, { 0x212921, 0x314231, 0x637363, 0x94A594 } } },
		/* SELECT / CHANGE AREAS (when the map has more than one area):
		 * covered by a raised piece of the bezel bar */
		.tab = { .x0 = 101, .x1 = 136, .top = 146, .base = 152, .n0 = 89, .n1 = 150, .bottom = 159, .n = 9,
		         .colors = { 0x000000, 0xFFFFFF, 0xCEDECE, 0xCEDECE, 0x94A594, 0x637363, 0x637363, 0x314231, 0x000000 } },
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
		 * (the L and R key letters, SLEEP, the area name, STATUS); the
		 * "A OBJECTIVE" prompt is left out (captureHide below) */
		.glyphs = { { 4, 3, 5, 6, 1, { 0x21214A }, true },                      /* L */
		            { 231, 3, 5, 6, 1, { 0x21214A }, true },                    /* R */
		            { 13, 4, 22, 5, 8, PLATE_TEXT, true },                      /* SLEEP */
		            { 203, 4, 26, 5, 8, PLATE_TEXT, true },                     /* STATUS */
		            { 97, 0, 46, 7, -1, { 0xADC6BD } } },                       /* area name */
		/* "A OBJECTIVE" is sprites (A button tile 942, OBJECTIVE 976/980,
		 * priority 2): left out of the capture, so the map under it shows */
		.captureHide = { { 160, 130, 239, 159, 940, 1023, 2 },
		                 /* the yellow scroll arrows (tiles 512-515) */
		                 { -256, -128, 255, 159, 512, 515, 2 },
		                 /* the play-time clock, bottom left (408-415) */
		                 { -256, 130, 255, 159, 408, 415, 3 },
		                 /* and the row of ticked item icons along the top (the
		                  * counters down the left stay) */
		                 { 30, 0, 120, 20, 400, 439, 3 } },
		/* the blank plates and keys, in the bezel's greys */
		.recolor = { { 2, 1, 42, 10, 13, PLATE_FROM, PLATE_TO },
		             { 196, 1, 42, 10, 13, PLATE_FROM, PLATE_TO },
		             { 96, 0, 48, 8, 1, { 0x21214A }, { 0x5A5A5A } } },
		.gridX = 0, .gridY = 0, .gridCell = 8,
		.mem = { { 0x03000BDE, 0xFF, 0x03, 1 } },
		.live = { { 0x03000BDE, 0xFF, 0x01, 1 } },
		.logo = { .wait = 200, .pressStart = true, .wait2 = 300, .layer = 1, .maxY = 125, .blackIsClear = true,
		          .rev = 1 /* was saved with its black fill */ },
		/* the HUD mini-map in the top-right corner: a 32x32 frame (tile 220)
		 * and the blinking 8x8 position marker, both drawn on top (priority 0) */
		.hideSprites = { { 200, -16, 239, 24, 160, 255, 0 } },
		.animFrames = 8, .animStep = 4,
	},
	{
		.codes = { "AZLE" },
		.title = "The Legend of Zelda: A Link to the Past",
		/* the cartridge also holds Four Swords (multiplayer only), so list it
		 * by the game that has the map */
		.listName = "Legend of Zelda, The - A Link to the Past (USA)",
		.open = { { K_L, 4 } },
		/* on the world map, L again: the zoomed-out whole world, which doesn't
		 * scroll along with Link like the close-up does */
		.open2If = { { 0x03002BDD, 0xFF, 0x07, 1 } }, .open2Delay = 30, .open2 = { { K_L, 4 } },
		.settleMax = 150, .openWithin = 30, .stableFrames = 3, .maxDiff = 300,
		.dimWhenUnavailable = true,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		/* not still black: dungeon maps can sit on a black screen for a few
		 * frames before fading in (dark rooms), which otherwise looks "settled" */
		.bgColor = 0x000000, .minForeground = 2000, .tolerance = 8,
		/* the game's main mode byte (as on the SNES): 0x0E with sub-mode 7 =
		 * world map, 3 = dungeon map */
		.mem = { { 0x03002BDC, 0xFF, 0x0E, 1 }, { 0x03002BDD, 0xFF, 0x07, 1, true, 0x03 } },
		/* A Link to the Past is running (0x03000010: 2; 1 on the shared menus
		 * and in Four Swords) and in normal play: 9 outdoors, 7 indoors, with
		 * sub-mode 0 (not mid screen transition or closing the map) */
		.live = { { 0x03000010, 0xFF, 0x02, 1 }, { 0x03002BDC, 0xFF, 0x09, 1, true, 0x07 }, { 0x03002BDD, 0xFF, 0x00, 1 } },
		/* save & quit returns to the cartridge's title and file select */
		.menu = { { 0x03000010, 0xFF, 0x01, 1 } },
		/* A Link to the Past's own title (the cartridge's shows Four Swords too):
		 * name a file, pick it, choose the game, wait for the title */
		.logo = { .steps = { { 0, 400 }, { K_A, 4 }, { 0, 60 }, { K_A, 4 }, { 0, 20 }, { K_A, 4 }, { 0, 20 }, { K_A, 4 },
		                     { 0, 20 }, { K_START, 4 }, { 0, 20 }, { K_A, 4 }, { 0, 120 }, { K_A, 4 }, { 0, 120 },
		                     { K_A, 4 }, { 0, 120 }, { K_A, 4 }, { 0, 200 }, { K_A, 4 }, { 0, 900 } },
		          .layer = 1, .maxY = 130, .blackIsClear = true },
		.animFrames = 8, .animStep = 4,
		/* the brightest state of the X is a pale flash; the red X covers more */
		.stillPick = STILL_COVER,
	},
};

/* ---------------------------------------------------------------- regions
 *
 * European and Japanese releases reuse the USA profile. Where their RAM
 * layout moved, memShift is added to every memory probe address (found by
 * matching the code that reads them in the two ROMs); patch
 * adjusts anything else that differs (button sequences through extra
 * screens, lettering positions). */
typedef struct {
	const char* code;
	const char* base;
	int memShift;
	void (*patch)(MapProfile*);
} Variant;

/* Inserts n steps at position at (arrays hold max entries). */
static void insert_steps(MacroStep* steps, int max, int at, const MacroStep* add, int n) {
	int len = 0;
	while (len < max && steps[len].frames) ++len;
	if (len + n > max) return;
	memmove(&steps[at + n], &steps[at], (size_t) (len - at) * sizeof(*steps));
	memcpy(&steps[at], add, (size_t) n * sizeof(*steps));
}

/* European multi-language releases start with a language menu (English
 * selected): wait for it and press A, then boot as the USA release does. */
static const MacroStep LANGUAGE_A[] = { { 0, 60 }, { K_A, 4 } };

static void lang_menu(MapProfile* p) {
	insert_steps(p->logo.steps, 24, 0, LANGUAGE_A, 2);
	if (p->frame.steps[0].frames) insert_steps(p->frame.steps, 24, 0, LANGUAGE_A, 2);
}

static void zelda_eur(MapProfile* p) {
	p->listName = "Legend of Zelda, The - A Link to the Past (Europe)";
	/* the language menu comes after the title, where USA has the file menu */
	static const MacroStep lang[] = { { K_A, 4 }, { 0, 60 } };
	insert_steps(p->logo.steps, 24, 1, lang, 2);
}
static void zelda_jpn(MapProfile* p) {
	p->listName = "Zelda no Densetsu - Kamigami no Triforce (Japan)";
}


static const Variant variants[] = {
	{ "A2CP", "A2CE", 0, lang_menu },
	{ "A2CJ", "A2CE", -0x30, NULL },
	{ "ACHP", "ACHE", 0, NULL },
	{ "ACHJ", "ACHE", -0x30, NULL },
	{ "AAMP", "AAME", 0, NULL },
	{ "AAMJ", "AAME", 0, NULL },
	{ "BMXP", "BMXE", 0, lang_menu },
	{ "BMXJ", "BMXE", 0, NULL },
	{ "AMTP", "AMTE", 0, lang_menu },
	{ "AMTJ", "AMTE", 0x34, NULL },
	{ "AZLP", "AZLE", 0, zelda_eur },
	{ "AZLJ", "AZLE", 0, zelda_jpn },
};
#define N_VARIANTS (sizeof(variants) / sizeof(*variants))
static MapProfile variantProfiles[N_VARIANTS];
static pthread_once_t variantsOnce = PTHREAD_ONCE_INIT;

static const MapProfile* base_find(const char* gameCode) {
	for (size_t i = 0; i < sizeof(profiles) / sizeof(*profiles); ++i) {
		for (int j = 0; j < 4 && profiles[i].codes[j]; ++j) {
			if (!strncmp(profiles[i].codes[j], gameCode, 4)) {
				return &profiles[i];
			}
		}
	}
	return NULL;
}

static void shift(MemProbe* list, int n, int by) {
	for (int i = 0; i < n && list[i].addr; ++i) list[i].addr += by;
}

static void build_variants(void) {
	for (size_t i = 0; i < N_VARIANTS; ++i) {
		const Variant* v = &variants[i];
		MapProfile* p = &variantProfiles[i];
		const MapProfile* b = base_find(v->base);
		if (!b) continue;
		*p = *b;
		memset(p->codes, 0, sizeof(p->codes));
		p->codes[0] = v->code;
		shift(p->mem, 4, v->memShift);
		shift(p->live, 4, v->memShift);
		shift(p->menu, 2, v->memShift);
		shift(p->room, 4, v->memShift);
		shift(p->open2If, 2, v->memShift);
		if (v->patch) v->patch(p);
	}
}

bool profile_ram_moved(const char* gameCode) {
	for (size_t i = 0; i < N_VARIANTS; ++i) {
		if (!strncmp(variants[i].code, gameCode, 4)) return variants[i].memShift != 0;
	}
	return false;
}

const MapProfile* profile_find(const char* gameCode) {
	const MapProfile* p = base_find(gameCode);
	if (p) return p;
	pthread_once(&variantsOnce, build_variants);
	for (size_t i = 0; i < N_VARIANTS; ++i) {
		if (!strncmp(variants[i].code, gameCode, 4) && variantProfiles[i].codes[0]) return &variantProfiles[i];
	}
	return NULL;
}

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(Config* c) {
	memset(c, 0, sizeof(*c));
	strcpy(c->layout, "auto");
	strcpy(c->scale, "integer");
	strcpy(c->mapScale, "integer");
	strcpy(c->hotkey, "guide");
	c->map = true;
	c->mapInterval = 90;
	c->sway = true;
	c->audioLatency = 64;
	c->ffSpeed = 3;
	c->shotFrame = -1;
}

static bool parse_bool(const char* v) {
	return !strcmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on");
}

static bool set_option(Config* c, const char* key, const char* val) {
#define STR(name, field) \
	if (!strcmp(key, name)) { \
		snprintf(c->field, sizeof(c->field), "%s", val); \
		return true; \
	}
#define BOOL(name, field) \
	if (!strcmp(key, name)) { \
		c->field = parse_bool(val); \
		return true; \
	}
#define INT(name, field) \
	if (!strcmp(key, name)) { \
		c->field = atoi(val); \
		return true; \
	}
	STR("layout", layout)
	BOOL("swap", swap)
	STR("scale", scale)
	STR("map_scale", mapScale)
	BOOL("smooth", smooth)
	BOOL("vsync", vsync)
	BOOL("map", map)
	INT("map_interval", mapInterval)
	BOOL("ab_swap", abSwap)
	STR("hotkey", hotkey)
	BOOL("show_fps", showFps)
	BOOL("sway", sway)
	INT("audio_latency", audioLatency)
	INT("ff_speed", ffSpeed)
	STR("save_dir", saveDir)
	STR("load_state", loadState)
	INT("quit_after", quitAfter)
	STR("input_script", inputScript)
	if (!strcmp(key, "screenshot")) {
		/* FRAME:PATH */
		const char* colon = strchr(val, ':');
		if (!colon) {
			return false;
		}
		c->shotFrame = atoi(val);
		snprintf(c->shotPath, sizeof(c->shotPath), "%s", colon + 1);
		return true;
	}
	return false;
#undef STR
#undef BOOL
#undef INT
}

void config_load_file(Config* c, const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) {
		return;
	}
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char* p = line;
		while (isspace((unsigned char) *p)) ++p;
		if (!*p || *p == '#' || *p == ';') continue;
		char* eq = strchr(p, '=');
		if (!eq) continue;
		*eq = 0;
		char* key = p;
		char* val = eq + 1;
		char* end = eq - 1;
		while (end >= key && isspace((unsigned char) *end)) *end-- = 0;
		while (isspace((unsigned char) *val)) ++val;
		end = val + strlen(val) - 1;
		while (end >= val && isspace((unsigned char) *end)) *end-- = 0;
		if (!set_option(c, key, val)) {
			fprintf(stderr, "%s: unknown option '%s'\n", path, key);
		}
	}
	fclose(f);
}

bool config_parse_args(Config* c, int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		const char* a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			return false;
		}
		if (!strncmp(a, "--", 2)) {
			char key[64];
			const char* eq = strchr(a, '=');
			const char* val = "1";
			size_t len = eq ? (size_t) (eq - a - 2) : strlen(a + 2);
			if (len >= sizeof(key)) len = sizeof(key) - 1;
			memcpy(key, a + 2, len);
			key[len] = 0;
			for (char* k = key; *k; ++k) {
				if (*k == '-') *k = '_';
			}
			if (eq) {
				val = eq + 1;
			} else if (!strncmp(key, "no_", 3)) {
				memmove(key, key + 3, strlen(key + 3) + 1);
				val = "0";
			}
			if (!set_option(c, key, val)) {
				fprintf(stderr, "unknown option %s\n", a);
				return false;
			}
		} else {
			snprintf(c->rom, sizeof(c->rom), "%s", a);
		}
	}
	return c->rom[0] != 0;
}

void config_usage(const char* argv0) {
	fprintf(stderr,
	        "usage: %s [options] game.gba\n"
	        "\n"
	        "Options (also accepted as key=value lines in gbadual.cfg):\n"
	        "  --layout=auto|span-h|span-v|dual|single   screen arrangement (auto)\n"
	        "  --swap                 show the map on the first screen\n"
	        "  --scale=integer|fit|stretch      game scaling (integer)\n"
	        "  --map-scale=integer|fit|stretch  map scaling (integer)\n"
	        "  --smooth               linear filtering\n"
	        "  --vsync                sync presentation to the display\n"
	        "  --no-map               disable the map screen\n"
	        "  --map-interval=N       frames between map refreshes (90)\n"
	        "  --ab-swap              GBA A on the bottom face button\n"
	        "  --hotkey=guide|back    hotkey modifier button (guide)\n"
	        "  --show-fps\n"
	        "  --no-sway              don't ask sway to span both outputs\n"
	        "  --audio-latency=MS     (64)\n"
	        "  --ff-speed=N           fast-forward multiplier (3)\n"
	        "  --save-dir=DIR         where .sav and states go (next to ROM)\n"
	        "\n"
	        "Testing: --load-state=FILE --screenshot=FRAME:PATH.bmp --quit-after=FRAMES\n"
	        "         --input-script=FILE (lines: FRAME KEYS, e.g. '120 START')\n",
	        argv0);
}

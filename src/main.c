/* gbadual: a GBA emulator frontend for dual-screen handhelds that keeps the
 * game's own map on the second screen.
 *
 * Emulation is libmgba. The map is produced by mapcap.c (a shadow core that
 * opens the in-game map off-screen); see profiles.c for per-game details. */
#include "config.h"
#include "emu.h"
#include "font.h"
#include "mapcap.h"

#include <SDL.h>

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define GBA_FPS (16777216.0 / 280896.0)

enum Content { C_GAME, C_MAP };

typedef struct {
	SDL_Rect rect;
	enum Content content;
} Region;

typedef struct {
	SDL_Window* win;
	SDL_Renderer* ren;
	SDL_Texture* gameTex;
	SDL_Texture* mapTex;
	Region regions[2];
	int nRegions;
} View;

static Config cfg;
static Emu emu;
static MapCapture* mc;
static View views[2];
static int nViews;
static bool singleShowsMap; /* single-screen mode: currently showing the map */
static volatile sig_atomic_t quitRequested;

static MapResult mapRes;
static int mapW = GBA_W, mapH = GBA_H;
static int mapShownIdx = -1;
static uint64_t mapAnimStart;
static uint64_t frameCount;

static char osdText[96];
static int osdFrames;
static double fps;

static char savPath[1024], statePath[1024];

/* ---------------------------------------------------------------- utils */

static void osd(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(osdText, sizeof(osdText), fmt, ap);
	va_end(ap);
	osdFrames = 120;
}

static void on_signal(int sig) {
	(void) sig;
	quitRequested = 1;
}

static void make_paths(void) {
	char romCopy[1024], romCopy2[1024];
	snprintf(romCopy, sizeof(romCopy), "%s", cfg.rom);
	snprintf(romCopy2, sizeof(romCopy2), "%s", cfg.rom);
	const char* dir = cfg.saveDir[0] ? cfg.saveDir : dirname(romCopy);
	char* base = basename(romCopy2);
	char* dot = strrchr(base, '.');
	if (dot) {
		*dot = 0;
	}
	mkdir(dir, 0755);
	snprintf(savPath, sizeof(savPath), "%s/%s.sav", dir, base);
	snprintf(statePath, sizeof(statePath), "%s/%s.gbadual.ss0", dir, base);

	/* First run: carry over a RetroArch (mGBA/gpSP) battery save if there is one. */
	struct stat st;
	if (stat(savPath, &st) != 0) {
		char srm[1024];
		snprintf(srm, sizeof(srm), "%s/%s.srm", dir, base);
		FILE* in = fopen(srm, "rb");
		if (in) {
			FILE* out = fopen(savPath, "wb");
			if (out) {
				char buf[8192];
				size_t n;
				while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
					fwrite(buf, 1, n, out);
				}
				fclose(out);
				fprintf(stderr, "imported %s -> %s\n", srm, savPath);
			}
			fclose(in);
		}
	}
}

/* --------------------------------------------------------------- layout */

static void fit_rect(const SDL_Rect* area, int w, int h, const char* mode, SDL_Rect* out) {
	if (!strcmp(mode, "stretch")) {
		*out = *area;
		return;
	}
	double s = SDL_min((double) area->w / w, (double) area->h / h);
	if (!strcmp(mode, "integer") && s >= 1.0) {
		s = (int) s;
	}
	out->w = (int) (w * s);
	out->h = (int) (h * s);
	out->x = area->x + (area->w - out->w) / 2;
	out->y = area->y + (area->h - out->h) / 2;
}

/* Decide which content goes in which part of each window. */
static void compute_layout(void) {
	enum Content first = cfg.swap ? C_MAP : C_GAME;
	enum Content second = cfg.swap ? C_GAME : C_MAP;
	if (!mc || !cfg.map) {
		first = C_GAME;
	}
	if (nViews == 2) {
		for (int i = 0; i < 2; ++i) {
			int w, h;
			SDL_GetRendererOutputSize(views[i].ren, &w, &h);
			views[i].nRegions = 1;
			views[i].regions[0].rect = (SDL_Rect) { 0, 0, w, h };
			views[i].regions[0].content = i == 0 ? first : second;
		}
		return;
	}
	View* v = &views[0];
	int w, h;
	SDL_GetRendererOutputSize(v->ren, &w, &h);
	const char* lay = cfg.layout;
	if (!strcmp(lay, "auto") || !strcmp(lay, "dual")) {
		if (w >= h * 18 / 10) {
			lay = "span-h";
		} else if (h >= w * 12 / 10) {
			lay = "span-v";
		} else {
			lay = "single";
		}
	}
	if (!mc || !cfg.map) {
		lay = "single";
		singleShowsMap = false;
	}
	if (!strcmp(lay, "span-h")) {
		v->nRegions = 2;
		v->regions[0] = (Region) { { 0, 0, w / 2, h }, first };
		v->regions[1] = (Region) { { w / 2, 0, w - w / 2, h }, second };
	} else if (!strcmp(lay, "span-v")) {
		v->nRegions = 2;
		v->regions[0] = (Region) { { 0, 0, w, h / 2 }, first };
		v->regions[1] = (Region) { { 0, h / 2, w, h - h / 2 }, second };
	} else {
		v->nRegions = 1;
		v->regions[0] = (Region) { { 0, 0, w, h }, singleShowsMap ? C_MAP : C_GAME };
	}
}

static bool create_view(View* v, int display, const char* title, bool fullscreen) {
	Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	if (fullscreen) {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	int ww = GBA_W * 3 * (nViews == 1 && strcmp(cfg.layout, "single") ? 2 : 1);
	int wh = GBA_H * 3;
	if (getenv("GBADUAL_WINDOW")) {
		sscanf(getenv("GBADUAL_WINDOW"), "%dx%d", &ww, &wh);
	}
	v->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display),
	                          ww, wh, flags);
	if (!v->win) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return false;
	}
	v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | (cfg.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
	if (!v->ren) {
		v->ren = SDL_CreateRenderer(v->win, -1, 0);
	}
	if (!v->ren) {
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, cfg.smooth ? "linear" : "nearest");
	v->gameTex = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
	v->mapTex = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, mapW, mapH);
	SDL_SetTextureBlendMode(v->gameTex, SDL_BLENDMODE_NONE);
	SDL_SetTextureBlendMode(v->mapTex, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(v->ren, 0, 0, 0, 255);
	SDL_RenderClear(v->ren);
	SDL_RenderPresent(v->ren);
	return true;
}

static void sway_span(void) {
	if (!cfg.sway || !getenv("SWAYSOCK") || nViews != 1 || !strcmp(cfg.layout, "single")) {
		return;
	}
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "swaymsg '[pid=%d] fullscreen enable global' >/dev/null 2>&1", (int) getpid());
	if (system(cmd) != 0) {
		fprintf(stderr, "note: swaymsg global fullscreen failed\n");
	}
}

/* ---------------------------------------------------------------- draw */

static void draw_text(SDL_Renderer* r, int x, int y, int px, const char* s, Uint8 cr, Uint8 cg, Uint8 cb) {
	SDL_Rect rects[512];
	int n = 0;
	for (; *s; ++s, x += 6 * px) {
		const uint8_t* g = font_glyph(*s);
		for (int row = 0; row < 7; ++row) {
			for (int col = 0; col < 5; ++col) {
				if ((g[row] >> (4 - col)) & 1) {
					rects[n++] = (SDL_Rect) { x + col * px, y + row * px, px, px };
					if (n == 512) {
						SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
						SDL_RenderFillRects(r, rects, n);
						n = 0;
					}
				}
			}
		}
	}
	SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
	SDL_RenderFillRects(r, rects, n);
}

static void draw_text_shadow(SDL_Renderer* r, int x, int y, int px, const char* s) {
	draw_text(r, x + px / 2 + 1, y + px / 2 + 1, px, s, 0, 0, 0);
	draw_text(r, x, y, px, s, 255, 255, 255);
}

static void render_view(View* v) {
	SDL_SetRenderDrawColor(v->ren, 0, 0, 0, 255);
	SDL_RenderClear(v->ren);
	for (int i = 0; i < v->nRegions; ++i) {
		Region* rg = &v->regions[i];
		SDL_Rect dst;
		int px = SDL_max(2, rg->rect.h / 160);
		if (rg->content == C_GAME) {
			fit_rect(&rg->rect, GBA_W, GBA_H, cfg.scale, &dst);
			SDL_RenderCopy(v->ren, v->gameTex, NULL, &dst);
			if (osdFrames > 0) {
				draw_text_shadow(v->ren, dst.x + px * 2, dst.y + dst.h - px * 10, px, osdText);
			}
			if (cfg.showFps) {
				char buf[32];
				snprintf(buf, sizeof(buf), "%.1f", fps);
				draw_text_shadow(v->ren, dst.x + px * 2, dst.y + px * 2, px, buf);
			}
		} else {
			if (mapShownIdx >= 0) {
				fit_rect(&rg->rect, mapW, mapH, cfg.mapScale, &dst);
				SDL_RenderCopy(v->ren, v->mapTex, NULL, &dst);
			} else {
				const char* msg = mc ? "MAP APPEARS ONCE YOU'RE IN GAME" : "NO MAP SUPPORT FOR THIS GAME";
				int tw = (int) strlen(msg) * 6 * px;
				while (tw > rg->rect.w - 8 && px > 1) {
					--px;
					tw = (int) strlen(msg) * 6 * px;
				}
				draw_text(v->ren, rg->rect.x + (rg->rect.w - tw) / 2, rg->rect.y + rg->rect.h / 2 - 4 * px, px, msg,
				          90, 90, 110);
			}
		}
	}
	SDL_RenderPresent(v->ren);
}

static void upload_map_frame(int idx) {
	if (idx == mapShownIdx || !mapRes.frames[idx]) {
		return;
	}
	for (int i = 0; i < nViews; ++i) {
		SDL_UpdateTexture(views[i].mapTex, NULL, mapRes.frames[idx], mapW * 4);
	}
	mapShownIdx = idx;
}

static void screenshot(const char* path) {
	View* v = &views[0];
	int w, h;
	SDL_GetRendererOutputSize(v->ren, &w, &h);
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	/* re-render so the back buffer holds this frame */
	render_view(v);
	if (SDL_RenderReadPixels(v->ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
		SDL_SaveBMP(s, path);
		fprintf(stderr, "screenshot %dx%d -> %s\n", w, h, path);
	} else {
		fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
	}
	SDL_FreeSurface(s);
}

/* --------------------------------------------------------------- input */

enum {
	PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, PAD_GUIDE, PAD_START, PAD_L1, PAD_R1,
	PAD_L2, PAD_R2, PAD_L3, PAD_R3, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_COUNT
};

static SDL_GameController* pads[8];
static bool padNow[PAD_COUNT], padPrev[PAD_COUNT];

static void open_pads(void) {
	for (int i = 0; i < SDL_NumJoysticks() && i < 8; ++i) {
		if (!pads[i] && SDL_IsGameController(i)) {
			pads[i] = SDL_GameControllerOpen(i);
			if (pads[i]) {
				fprintf(stderr, "controller: %s\n", SDL_GameControllerName(pads[i]));
			}
		}
	}
}

static void read_pads(void) {
	memcpy(padPrev, padNow, sizeof(padNow));
	memset(padNow, 0, sizeof(padNow));
	static const SDL_GameControllerButton map[] = {
		[PAD_A] = SDL_CONTROLLER_BUTTON_A, [PAD_B] = SDL_CONTROLLER_BUTTON_B,
		[PAD_X] = SDL_CONTROLLER_BUTTON_X, [PAD_Y] = SDL_CONTROLLER_BUTTON_Y,
		[PAD_BACK] = SDL_CONTROLLER_BUTTON_BACK, [PAD_GUIDE] = SDL_CONTROLLER_BUTTON_GUIDE,
		[PAD_START] = SDL_CONTROLLER_BUTTON_START, [PAD_L1] = SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
		[PAD_R1] = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, [PAD_L3] = SDL_CONTROLLER_BUTTON_LEFTSTICK,
		[PAD_R3] = SDL_CONTROLLER_BUTTON_RIGHTSTICK, [PAD_UP] = SDL_CONTROLLER_BUTTON_DPAD_UP,
		[PAD_DOWN] = SDL_CONTROLLER_BUTTON_DPAD_DOWN, [PAD_LEFT] = SDL_CONTROLLER_BUTTON_DPAD_LEFT,
		[PAD_RIGHT] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
	};
	for (int p = 0; p < 8; ++p) {
		SDL_GameController* gc = pads[p];
		if (!gc) {
			continue;
		}
		for (int b = 0; b < PAD_COUNT; ++b) {
			if (b == PAD_L2 || b == PAD_R2) {
				continue;
			}
			if (SDL_GameControllerGetButton(gc, map[b])) {
				padNow[b] = true;
			}
		}
		if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) padNow[PAD_L2] = true;
		if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) padNow[PAD_R2] = true;
		int lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
		int ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
		if (lx < -16000) padNow[PAD_LEFT] = true;
		if (lx > 16000) padNow[PAD_RIGHT] = true;
		if (ly < -16000) padNow[PAD_UP] = true;
		if (ly > 16000) padNow[PAD_DOWN] = true;
	}
	/* keyboard, for desktop testing */
	const Uint8* k = SDL_GetKeyboardState(NULL);
	if (k[SDL_SCANCODE_X]) padNow[PAD_B] = true; /* GBA A (east) */
	if (k[SDL_SCANCODE_Z]) padNow[PAD_A] = true; /* GBA B (south) */
	if (k[SDL_SCANCODE_A]) padNow[PAD_L1] = true;
	if (k[SDL_SCANCODE_S]) padNow[PAD_R1] = true;
	if (k[SDL_SCANCODE_RETURN]) padNow[PAD_START] = true;
	if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT]) padNow[PAD_BACK] = true;
	if (k[SDL_SCANCODE_UP]) padNow[PAD_UP] = true;
	if (k[SDL_SCANCODE_DOWN]) padNow[PAD_DOWN] = true;
	if (k[SDL_SCANCODE_LEFT]) padNow[PAD_LEFT] = true;
	if (k[SDL_SCANCODE_RIGHT]) padNow[PAD_RIGHT] = true;
	if (k[SDL_SCANCODE_TAB]) padNow[PAD_L2] = true;
	if (k[SDL_SCANCODE_SPACE]) padNow[PAD_R2] = true;
	if (k[SDL_SCANCODE_F1]) padNow[PAD_GUIDE] = true;
}

static bool pressed(int b) {
	return padNow[b] && !padPrev[b];
}

/* --------------------------------------------------------- input script */

typedef struct {
	long frame;
	uint32_t keys;
	char action[128];
} ScriptLine;

static ScriptLine* script;
static int scriptLen, scriptPos;
static uint32_t scriptKeys;

static uint32_t parse_keys(const char* s) {
	uint32_t k = 0;
	char buf[128];
	snprintf(buf, sizeof(buf), "%s", s);
	for (char* t = strtok(buf, "+"); t; t = strtok(NULL, "+")) {
		if (!strcmp(t, "A")) k |= K_A;
		else if (!strcmp(t, "B")) k |= K_B;
		else if (!strcmp(t, "SELECT")) k |= K_SELECT;
		else if (!strcmp(t, "START")) k |= K_START;
		else if (!strcmp(t, "RIGHT")) k |= K_RIGHT;
		else if (!strcmp(t, "LEFT")) k |= K_LEFT;
		else if (!strcmp(t, "UP")) k |= K_UP;
		else if (!strcmp(t, "DOWN")) k |= K_DOWN;
		else if (!strcmp(t, "R")) k |= K_R;
		else if (!strcmp(t, "L")) k |= K_L;
	}
	return k;
}

static void load_script(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "cannot open input script %s\n", path);
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		long fr;
		char what[128];
		if (line[0] == '#' || sscanf(line, "%ld %127s", &fr, what) != 2) {
			continue;
		}
		script = realloc(script, sizeof(*script) * (scriptLen + 1));
		ScriptLine* s = &script[scriptLen++];
		s->frame = fr;
		s->keys = 0;
		s->action[0] = 0;
		if (isupper((unsigned char) what[0]) && (!strcmp(what, "SWAP") || !strcmp(what, "SAVESTATE") ||
		                                         !strcmp(what, "LOADSTATE") || !strcmp(what, "QUIT"))) {
			snprintf(s->action, sizeof(s->action), "%s", what);
		} else {
			s->keys = parse_keys(what);
		}
	}
	fclose(f);
}

/* True when the live frame is (nearly) black: a fade between rooms. */
static bool frame_is_dark(const uint32_t* fb) {
	int bright = 0, total = 0;
	for (int i = 0; i < GBA_W * GBA_H; i += 7) {
		uint32_t c = fb[i];
		int lum = ((c & 0xFF) + ((c >> 8) & 0xFF) * 2 + ((c >> 16) & 0xFF)) >> 2;
		bright += lum > 40;
		++total;
	}
	return bright * 40 < total; /* < 2.5% lit pixels */
}

/* ---------------------------------------------------------------- main */

static void do_save_state(void) {
	osd(emu_state_save_file(&emu, statePath) ? "STATE SAVED" : "STATE SAVE FAILED");
}

static void do_load_state(void) {
	if (emu_state_load_file(&emu, statePath)) {
		osd("STATE LOADED");
	} else {
		osd("NO STATE TO LOAD");
	}
}

static void do_swap(void) {
	if (nViews == 1 && views[0].nRegions == 1) {
		singleShowsMap = !singleShowsMap;
	} else {
		cfg.swap = !cfg.swap;
	}
	compute_layout();
}

int main(int argc, char** argv) {
	config_defaults(&cfg);
	/* config search: $GBADUAL_CONFIG, next to the binary, ~/.config/gbadual */
	char cfgPath[1024];
	const char* envCfg = getenv("GBADUAL_CONFIG");
	if (envCfg) {
		config_load_file(&cfg, envCfg);
	} else {
		char exe[1024];
		ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		if (n > 0) {
			exe[n] = 0;
			snprintf(cfgPath, sizeof(cfgPath), "%s/gbadual.cfg", dirname(exe));
			config_load_file(&cfg, cfgPath);
		}
		if (getenv("HOME")) {
			snprintf(cfgPath, sizeof(cfgPath), "%s/.config/gbadual/gbadual.cfg", getenv("HOME"));
			config_load_file(&cfg, cfgPath);
		}
	}
	if (!config_parse_args(&cfg, argc, argv)) {
		config_usage(argv[0]);
		return 1;
	}
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);

	make_paths();
	if (!emu_open(&emu, cfg.rom, savPath, true)) {
		return 1;
	}
	fprintf(stderr, "game code %s, save %s\n", emu.gameCode, savPath);
	if (cfg.map) {
		mc = mapcap_create(cfg.rom, emu.gameCode);
		if (mc) {
			fprintf(stderr, "map profile: %s\n", mapcap_title(mc));
			mapcap_size(mc, &mapW, &mapH);
		} else {
			fprintf(stderr, "no map profile for game code %s\n", emu.gameCode);
		}
	}
	if (cfg.inputScript[0]) {
		load_script(cfg.inputScript);
	}
	if (cfg.loadState[0] && !emu_state_load_file(&emu, cfg.loadState)) {
		fprintf(stderr, "could not load state %s\n", cfg.loadState);
	}

	SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
	SDL_SetHint(SDL_HINT_APP_NAME, "gbadual");
	setenv("SDL_VIDEO_WAYLAND_WMCLASS", "gbadual", 0);
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_ShowCursor(SDL_DISABLE);
	SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
	if (getenv("SDL_GAMECONTROLLERCONFIG_FILE")) {
		SDL_GameControllerAddMappingsFromFile(getenv("SDL_GAMECONTROLLERCONFIG_FILE"));
	}
	open_pads();

	int displays = SDL_GetNumVideoDisplays();
	const char* driver = SDL_GetCurrentVideoDriver();
	bool underSway = getenv("SWAYSOCK") != NULL;
	bool wantDual = !strcmp(cfg.layout, "dual") || (!strcmp(cfg.layout, "auto") && displays >= 2 && !underSway);
	if (!mc) {
		wantDual = false;
	}
	bool fullscreen = strcmp(driver, "offscreen") && strcmp(driver, "dummy") && strcmp(driver, "x11") &&
	                  strcmp(driver, "windows") && strcmp(driver, "cocoa");
	if (getenv("GBADUAL_WINDOWED")) {
		fullscreen = false;
	}
	fprintf(stderr, "video driver %s, %d display(s)%s -> %s\n", driver, displays, underSway ? ", sway" : "",
	        wantDual && displays >= 2 ? "two windows" : "one window");
	if (wantDual && displays >= 2) {
		nViews = 2;
		if (!create_view(&views[0], 0, "gbadual", true) || !create_view(&views[1], 1, "gbadual map", true)) {
			return 1;
		}
	} else {
		nViews = 1;
		if (!create_view(&views[0], 0, "gbadual", fullscreen)) {
			return 1;
		}
	}
	compute_layout();

	SDL_AudioSpec want = { 0 }, have;
	want.freq = (int) emu_audio_rate(&emu);
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 512;
	SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!audio) {
		fprintf(stderr, "no audio: %s\n", SDL_GetError());
	} else {
		SDL_PauseAudioDevice(audio, 0);
	}
	const int targetQueued = want.freq * cfg.audioLatency / 1000; /* stereo frames */

	int16_t samples[4096 * 2];
	uint32_t lastRoomKey = 0;
	int sinceMapRequest = 1 << 20;
	bool forceMap = true;
	bool hotkeyUsed = false;
	int hotkeyBtn = !strcmp(cfg.hotkey, "back") ? PAD_BACK : PAD_GUIDE;

	const double frameSec = 1.0 / GBA_FPS;
	const double freq = (double) SDL_GetPerformanceFrequency();
	double next = SDL_GetPerformanceCounter() / freq;
	double fpsT0 = next;
	int fpsFrames = 0;
	int swayTries = 0;

	while (!quitRequested) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			switch (ev.type) {
			case SDL_QUIT:
				quitRequested = 1;
				break;
			case SDL_CONTROLLERDEVICEADDED:
				open_pads();
				break;
			case SDL_WINDOWEVENT:
				if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || ev.window.event == SDL_WINDOWEVENT_RESIZED) {
					compute_layout();
				}
				break;
			case SDL_KEYDOWN:
				if (ev.key.keysym.sym == SDLK_ESCAPE) quitRequested = 1;
				if (ev.key.keysym.sym == SDLK_F2) do_save_state();
				if (ev.key.keysym.sym == SDLK_F4) do_load_state(), forceMap = true;
				break;
			}
		}
		if (swayTries < 3 && (frameCount == 2 || frameCount == 30 || frameCount == 120)) {
			sway_span();
			++swayTries;
		}

		read_pads();

		/* hotkey combos; the modifier itself never reaches the game while held with another button */
		bool hk = padNow[hotkeyBtn];
		if (hk) {
			if (pressed(PAD_START)) quitRequested = 1, hotkeyUsed = true;
			if (pressed(PAD_R1)) do_save_state(), hotkeyUsed = true;
			if (pressed(PAD_L1)) do_load_state(), forceMap = true, hotkeyUsed = true;
			if (pressed(PAD_X)) cfg.showFps = !cfg.showFps, hotkeyUsed = true;
			if (pressed(PAD_Y)) do_swap(), hotkeyUsed = true;
		} else {
			if (pressed(PAD_L2)) do_swap();
		}
		uint32_t keys = 0;
		if (!hk) {
			bool aPressed = cfg.abSwap ? padNow[PAD_A] : padNow[PAD_B];
			bool bPressed = cfg.abSwap ? padNow[PAD_B] : padNow[PAD_A];
			if (aPressed) keys |= K_A;
			if (bPressed) keys |= K_B;
			if (padNow[PAD_START]) keys |= K_START;
			if (padNow[PAD_BACK] && hotkeyBtn != PAD_BACK) keys |= K_SELECT;
			if (padNow[PAD_L1]) keys |= K_L;
			if (padNow[PAD_R1]) keys |= K_R;
			if (padNow[PAD_UP]) keys |= K_UP;
			if (padNow[PAD_DOWN]) keys |= K_DOWN;
			if (padNow[PAD_LEFT]) keys |= K_LEFT;
			if (padNow[PAD_RIGHT]) keys |= K_RIGHT;
		}
		/* With hotkey=back, Select is sent to the game when released without a combo. */
		static int selectPulse;
		if (hotkeyBtn == PAD_BACK) {
			if (padPrev[PAD_BACK] && !padNow[PAD_BACK] && !hotkeyUsed) selectPulse = 4;
			if (!padNow[PAD_BACK]) hotkeyUsed = false;
			if (selectPulse > 0) keys |= K_SELECT, --selectPulse;
		} else if (!hk) {
			hotkeyUsed = false;
		}

		while (scriptPos < scriptLen && script[scriptPos].frame <= (long) frameCount) {
			ScriptLine* s = &script[scriptPos++];
			if (!strcmp(s->action, "SWAP")) do_swap();
			else if (!strcmp(s->action, "SAVESTATE")) do_save_state();
			else if (!strcmp(s->action, "LOADSTATE")) do_load_state(), forceMap = true;
			else if (!strcmp(s->action, "QUIT")) quitRequested = 1;
			else scriptKeys = s->keys;
		}
		keys |= scriptKeys;

		bool ff = padNow[PAD_R2];
		int runs = ff ? SDL_max(1, cfg.ffSpeed) : 1;
		for (int i = 0; i < runs; ++i) {
			emu_frame(&emu, keys);
			size_t got;
			while ((got = emu_audio_read(&emu, samples, 4096)) > 0) {
				if (audio && !ff) {
					SDL_QueueAudio(audio, samples, got * 4);
				}
			}
		}
		++frameCount;

		/* map refresh */
		if (mc) {
			++sinceMapRequest;
			/* A fade-to-black followed by a picture usually means a new room:
			 * refresh shortly after the screen comes back. */
			static bool wasDark;
			static long refreshAt = -1;
			bool dark = frame_is_dark(emu.fb);
			if (wasDark && !dark) {
				refreshAt = (long) frameCount + 6;
			}
			wasDark = dark;
			if (refreshAt >= 0 && (long) frameCount >= refreshAt) {
				forceMap = true;
				refreshAt = -1;
				if (getenv("GBADUAL_DEBUG")) {
					fprintf(stderr, "frame %llu: room transition\n", (unsigned long long) frameCount);
				}
			}
			uint32_t rk = mapcap_room_key(mc, &emu);
			bool roomChanged = rk && rk != lastRoomKey;
			if ((forceMap || roomChanged || sinceMapRequest >= cfg.mapInterval) && mapcap_live_ok(mc, &emu)) {
				if (mapcap_request(mc, &emu)) {
					sinceMapRequest = 0;
					lastRoomKey = rk;
					forceMap = false;
				}
			}
			if (mapcap_poll(mc, mapRes.serial, &mapRes)) {
				mapShownIdx = -1;
				mapAnimStart = frameCount;
				if (getenv("GBADUAL_DEBUG")) {
					fprintf(stderr, "frame %llu: map updated (%d anim frames)\n", (unsigned long long) frameCount, mapRes.nFrames);
				}
				upload_map_frame(0);
			} else if (mapRes.nFrames > 1 && mapShownIdx >= 0) {
				int idx = (int) ((frameCount - mapAnimStart) / SDL_max(1, mapRes.frameStep)) % mapRes.nFrames;
				upload_map_frame(idx);
			}
		}

		for (int i = 0; i < nViews; ++i) {
			SDL_UpdateTexture(views[i].gameTex, NULL, emu.fb, GBA_W * 4);
			render_view(&views[i]);
		}
		if (osdFrames > 0) {
			--osdFrames;
		}
		if ((long) frameCount == cfg.shotFrame) {
			screenshot(cfg.shotPath);
		}
		if (cfg.quitAfter && (long) frameCount >= cfg.quitAfter) {
			quitRequested = 1;
		}

		/* pacing: real-time clock, nudged by the audio queue depth */
		double now = SDL_GetPerformanceCounter() / freq;
		++fpsFrames;
		if (now - fpsT0 >= 1.0) {
			fps = fpsFrames / (now - fpsT0);
			fpsFrames = 0;
			fpsT0 = now;
		}
		if (ff) {
			next = now;
			continue;
		}
		next += frameSec;
		if (audio) {
			int queued = (int) (SDL_GetQueuedAudioSize(audio) / 4);
			if (queued > targetQueued * 3 / 2) next += frameSec * 0.05;
			else if (queued < targetQueued / 2) next -= frameSec * 0.05;
		}
		if (next < now - 4 * frameSec) {
			next = now; /* fell far behind; don't try to catch up */
		}
		double wait = next - now;
		if (wait > 0) {
			struct timespec ts = { 0, (long) (wait * 1e9) };
			nanosleep(&ts, NULL);
		}
	}

	fprintf(stderr, "exiting after %llu frames\n", (unsigned long long) frameCount);
	if (audio) {
		SDL_CloseAudioDevice(audio);
	}
	mapcap_destroy(mc);
	emu_close(&emu); /* flushes the battery save */
	for (int i = 0; i < nViews; ++i) {
		SDL_DestroyRenderer(views[i].ren);
		SDL_DestroyWindow(views[i].win);
	}
	SDL_Quit();
	return 0;
}

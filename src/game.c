/* One play session: the emulator, the second-screen map, and the pause menu. */
#include "game.h"
#include "emu.h"
#include "mapcap.h"
#include "paths.h"
#include "ui.h"
#include "logo.h"
#include "mapframe.h"
#include "profiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GBA_FPS (16777216.0 / 280896.0)

static Config* cfg;
static Emu emu;
static MapCapture* mc;
static GamePaths gp;

static SDL_Texture* gameTex[2];
static SDL_Texture* mapTex[2];
static SDL_Texture* logoTex[2];
static SDL_Texture* overlayTex[2];
static int overlayW, overlayH;
static char overlayMode[16];
static int logoW, logoH;
static bool logoPixel;
static MapResult mapRes;
static int mapW = GBA_W, mapH = GBA_H;
static int mapShownIdx = -1;
static uint64_t mapAnimStart, frameCount;
static bool singleShowsMap;
static double fps, speedPct;
static bool debug;

/* ---------------------------------------------------------------- menu */

enum MenuItem { M_RESUME, M_SWAP, M_SCALE, M_LCD, M_OVERLAY, M_FPS, M_SAVE, M_LOAD, M_RESTART, M_LIBRARY, M_EXIT };
static const char* menuLabels[] = {
	[M_RESUME] = "RESUME", [M_SWAP] = "SWAP SCREENS", [M_SAVE] = "SAVE STATE", [M_SCALE] = "", [M_LCD] = "", [M_FPS] = "", [M_OVERLAY] = "", [M_RESTART] = "RESTART GAME",
	[M_LOAD] = "LOAD STATE", [M_LIBRARY] = "EXIT TO EMULATOR", [M_EXIT] = "EXIT TO MAIN MENU",
};
static int menuItems[9], nMenuItems;
/* two pages, switched with L1/R1 or the tabs */
static int pages[2][8], pageLen[2], curPage;
/* Buttons still held when the menu closes (or after a HOME combo) are kept
 * from the game until released, so e.g. the A that picked "Save state"
 * doesn't make Samus jump. */
static bool suppress[B_COUNT];
#define HELD(b) (btnNow[b] && !suppress[b])
static void suppress_held(void) {
	for (int b = 0; b < B_COUNT; ++b) suppress[b] = btnNow[b];
}
static const char* pageNames[2] = { "GAME", "DISPLAY" };
static bool restartArmed;
static bool menuOpen;
static int menuSel;
static int menuPressed = -1;      /* item under a finger that went down in the menu */
static bool ignorePointerUp;      /* the tap that opened the menu */
static char menuNote[64];

static const SDL_Color C_BTN = { 34, 30, 58, 240 };
static const SDL_Color C_BTN_SEL = { 92, 52, 140, 250 };
static const SDL_Color C_BTN_EDGE = { 150, 120, 210, 255 };
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIS = { 100, 100, 120, 255 };
static const SDL_Color C_GOLD = { 232, 200, 120, 255 };

static bool item_enabled(int item) {
	return item != M_LOAD || file_exists(gp.state);
}

static int menu_width(int scr) {
	int px = ui_px(scr);
	return SDL_min(screens[scr].rect.w - px * 16, ui_text_width("EXIT TO MAIN MENU", px) + px * 48);
}

static SDL_Rect item_rect(int scr, int i) {
	SDL_Rect r = screens[scr].rect;
	int px = ui_px(scr);
	int h = px * 14, gap = px * 3;
	int w = menu_width(scr);
	int top = px * 28; /* below the page tabs; same on both pages */
	(void) r;
	return (SDL_Rect) { (r.w - w) / 2, top + i * (h + gap), w, h };
}

static SDL_Rect tab_rect(int scr, int t) {
	SDL_Rect r = screens[scr].rect;
	int px = ui_px(scr);
	int w = menu_width(scr);
	int tw = (w - px * 3) / 2;
	return (SDL_Rect) { (r.w - w) / 2 + t * (tw + px * 3), px * 7, tw, px * 14 };
}

static void set_page(int page) {
	curPage = page;
	nMenuItems = pageLen[page];
	memcpy(menuItems, pages[page], sizeof(int) * nMenuItems);
	menuSel = 0;
	menuPressed = -1;
	menuNote[0] = 0;
	restartArmed = false;
}

static int tab_at(int scr, int x, int y) {
	for (int t = 0; t < 2; ++t) {
		SDL_Rect r = tab_rect(scr, t);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return t;
	}
	return -1;
}

static int item_at(int scr, int x, int y) {
	for (int i = 0; i < nMenuItems; ++i) {
		SDL_Rect r = item_rect(scr, i);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
	}
	return -1;
}

static void draw_menu(void) {
	for (int s = 0; s < nScreens; ++s) ui_dim(s, 170);
	int scr = ui_touch_screen();
	int px = ui_px(scr);
	for (int t = 0; t < 2; ++t) {
		SDL_Rect r = tab_rect(scr, t);
		bool on = t == curPage;
		ui_fill(scr, r, on ? C_BTN_EDGE : C_BTN);
		ui_text_center(scr, r.x + r.w / 2, r.y + (r.h - px * 7) / 2, px, pageNames[t], on ? (SDL_Color) { 20, 16, 36, 255 } : C_DIS);
	}
	SDL_Rect t0 = tab_rect(scr, 0), t1 = tab_rect(scr, 1);
	ui_text(scr, t0.x - px * 14, t0.y + (t0.h - px * 7) / 2, px, "L1", C_DIS);
	ui_text(scr, t1.x + t1.w + px * 3, t1.y + (t1.h - px * 7) / 2, px, "R1", C_DIS);
	for (int i = 0; i < nMenuItems; ++i) {
		SDL_Rect r = item_rect(scr, i);
		bool sel = i == menuSel;
		ui_fill(scr, (SDL_Rect) { r.x - px / 2, r.y - px / 2, r.w + px, r.h + px }, sel ? C_BTN_EDGE : C_BTN);
		ui_fill(scr, r, sel ? C_BTN_SEL : C_BTN);
		bool en = item_enabled(menuItems[i]);
		const char* label = menuLabels[menuItems[i]];
		if (menuItems[i] == M_SCALE) label = !strcmp(cfg->scale, "fit") ? "SCALE: FULL WIDTH" : !strcmp(cfg->scale, "stretch") ? "SCALE: STRETCH" : "SCALE: PIXEL PERFECT";
		if (menuItems[i] == M_LCD) label = cfg->lcdGrid ? "LCD GRID: ON" : "LCD GRID: OFF";
		if (menuItems[i] == M_FPS) label = cfg->showFps ? "SHOW FPS: ON" : "SHOW FPS: OFF";
		if (menuItems[i] == M_OVERLAY) label = cfg->overlays ? "OVERLAYS: ON" : "OVERLAYS: OFF";
		if (menuItems[i] == M_RESTART && restartArmed) label = "TAP AGAIN TO RESTART";
		ui_text_center(scr, r.x + r.w / 2, r.y + (r.h - px * 7) / 2, px, label, en ? C_TEXT : C_DIS);
	}
	if (menuNote[0]) {
		SDL_Rect last = item_rect(scr, nMenuItems - 1);
		ui_text_center(scr, screens[scr].rect.w / 2, last.y + last.h + px * 6, px, menuNote, C_GOLD);
	}
	if (nScreens > 1) {
		int o = ui_other_screen();
		ui_text_center(o, screens[o].rect.w / 2, screens[o].rect.h / 2 - px * 4, ui_px(o) + 1, "PAUSED", C_GOLD);
	}
}

/* ---------------------------------------------------------------- helpers */

static void save_thumbs(void) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(emu.fb, GBA_W, GBA_H, 32, GBA_W * 4, SDL_PIXELFORMAT_XBGR8888);
	if (s) {
		SDL_SaveBMP(s, gp.thumb);
		SDL_FreeSurface(s);
	}
	if (mapShownIdx >= 0 && mapRes.frames[0]) {
		s = SDL_CreateRGBSurfaceWithFormatFrom(mapRes.frames[0], mapW, mapH, 32, mapW * 4, SDL_PIXELFORMAT_XBGR8888);
		if (s) {
			SDL_SaveBMP(s, gp.mapThumb);
			SDL_FreeSurface(s);
		}
	}
}

static void auto_save(void) {
	if (!cfg->autoResume) return;
	if (emu_state_save_file(&emu, gp.autoState)) {
		save_thumbs();
		fprintf(stderr, "auto-saved %s\n", gp.autoState);
	} else {
		fprintf(stderr, "auto-save failed\n");
	}
}

/* generated surround (built-in overlay), see mapframe.c */
static MapFrame mf;
static SDL_Texture* canvasTex[2];
static int canvasTexW, canvasTexH;
static bool canvasDirty;
static char framePath[1200];

static void upload_map_frame(int idx) {
	if (idx == mapShownIdx || !mapRes.frames[idx]) return;
	for (int i = 0; i < nViews; ++i) SDL_UpdateTexture(mapTex[i], NULL, mapRes.frames[idx], mapW * 4);
	mapShownIdx = idx;
	canvasDirty = true;
}

/* Draws the map with its generated surround; false if there's none to draw. */
static bool draw_framed_map(int s) {
	if (!cfg->overlays || !mapframe_ready(&mf) || mapShownIdx < 0) return false;
	SDL_Rect r = screens[s].rect;
	if (mapframe_layout(&mf, r.w, r.h, cfg->mapScale) || mf.baseDirty) canvasDirty = true;
	if (canvasTexW != mf.cw || canvasTexH != mf.ch) {
		for (int i = 0; i < nViews; ++i) {
			if (canvasTex[i]) SDL_DestroyTexture(canvasTex[i]);
			canvasTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, mf.cw, mf.ch);
			SDL_SetTextureBlendMode(canvasTex[i], SDL_BLENDMODE_NONE);
		}
		canvasTexW = mf.cw;
		canvasTexH = mf.ch;
		canvasDirty = true;
	}
	if (canvasDirty) {
		const uint32_t* px = mapframe_compose(&mf, mapRes.frames[mapShownIdx]);
		for (int i = 0; i < nViews; ++i) SDL_UpdateTexture(canvasTex[i], NULL, px, mf.cw * 4);
		canvasDirty = false;
	}
	ui_image_pixels(s, canvasTex, mf.cw, mf.ch, "stretch", cfg->lcdGrid);
	return true;
}

static bool frame_is_dark(const uint32_t* fb) {
	int bright = 0, total = 0;
	for (int i = 0; i < GBA_W * GBA_H; i += 7) {
		uint32_t c = fb[i];
		int lum = ((c & 0xFF) + ((c >> 8) & 0xFF) * 2 + ((c >> 16) & 0xFF)) >> 2;
		bright += lum > 40;
		++total;
	}
	return bright * 40 < total;
}

static int game_screen(void) {
	if (nScreens < 2) return 0;
	return cfg->swap ? 1 : 0;
}

/* Display choices made in the menu are remembered for next time. */
static void save_prefs(void) {
	char prefs[1100];
	snprintf(prefs, sizeof(prefs), "%s/prefs.cfg", cfg->stateDir);
	FILE* f = fopen(prefs, "w");
	if (f) {
		fprintf(f, "# written by gba-ds's pause menu; overrides gba-ds.cfg\n");
		fprintf(f, "swap = %d\nscale = %s\nmap_scale = %s\nlcd_grid = %d\nshow_fps = %d\noverlays = %d\n",
		        cfg->swap ? 1 : 0, cfg->scale, cfg->mapScale, cfg->lcdGrid ? 1 : 0, cfg->showFps ? 1 : 0,
		        cfg->overlays ? 1 : 0);
		fclose(f);
	}
}

static void swap_screens(void) {
	if (nScreens < 2) {
		singleShowsMap = !singleShowsMap;
		return;
	}
	cfg->swap = !cfg->swap;
	save_prefs();
}

static SDL_Color borderColor;

/* <asset_dir>/overlays/<CODE>-<scale>.png, else <CODE>.png: drawn over the
 * whole map screen (transparent where the map should show). */
static void load_overlay(void) {
	if (!strcmp(overlayMode, cfg->mapScale)) return;
	snprintf(overlayMode, sizeof(overlayMode), "%s", cfg->mapScale);
	ui_free_image(overlayTex);
	char path[1200];
	snprintf(path, sizeof(path), "%s/overlays/%.4s-%s.png", cfg->assetDir, emu.gameCode, cfg->mapScale);
	if (!ui_load_image(path, overlayTex, &overlayW, &overlayH, true)) {
		snprintf(path, sizeof(path), "%s/overlays/%.4s.png", cfg->assetDir, emu.gameCode);
		ui_load_image(path, overlayTex, &overlayW, &overlayH, true);
	}
}

static void draw_game(void) {
	bool mapOn = mc && cfg->map;
	for (int s = 0; s < nScreens; ++s) {
		bool showGame = nScreens == 1 ? !(singleShowsMap && mapOn) : (s == game_screen() || !mapOn);
		int px = ui_px(s);
		SDL_Rect r = screens[s].rect;
		if (showGame) {
			if (nScreens == 2 && !mapOn && s != game_screen()) continue; /* no map: leave other screen black */
			ui_image_pixels(s, gameTex, GBA_W, GBA_H, cfg->scale, cfg->lcdGrid);
			const char* osd = ui_osd_text();
			if (osd && !menuOpen) {
				ui_text(s, px * 4 + 2, r.h - px * 12 + 2, px, osd, (SDL_Color) { 0, 0, 0, 255 });
				ui_text(s, px * 4, r.h - px * 12, px, osd, C_TEXT);
			}
			if (cfg->showFps) {
				/* sized to sit in the empty band above the picture */
				char buf[48];
				snprintf(buf, sizeof(buf), "%.1f FPS  %d%%", fps, (int) (speedPct + 0.5));
				SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, cfg->scale);
				int gap = img.y;
				int fpx = SDL_max(1, SDL_min(SDL_min(px - 1, 3), (gap - 6) / 7)); /* 7 rows tall, 3px margin each side */
				int fy = gap >= 7 * fpx + 2 ? (gap - 7 * fpx) / 2 : fpx * 2;
				int fx = img.x + fpx * 2;
				ui_text(s, fx + 1, fy + 1, fpx, buf, (SDL_Color) { 0, 0, 0, 255 });
				ui_text(s, fx, fy, fpx, buf, C_TEXT);
			}
			continue;
		}
		/* the map screen */
		if (mapShownIdx >= 0 && draw_framed_map(s)) {
			/* map + generated surround */
		} else if (mapShownIdx >= 0) {
			ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, borderColor);
			ui_image_pixels(s, mapTex, mapW, mapH, cfg->mapScale, cfg->lcdGrid);
		} else if (logoTex[0] || logoTex[1]) {
			/* no map yet (title screen, intro...): the game's logo on black */
			SDL_Rect area = { r.w / 12, r.h / 6, r.w - r.w / 6, r.h - r.h / 3 };
			ui_image(s, logoTex, logoW, logoH, logoPixel ? "integer" : "fit", &area);
		}
		if (mapShownIdx >= 0 && cfg->overlays) { /* overlays frame the map only, never the logo */
			load_overlay();
			if (overlayTex[0] || overlayTex[1]) ui_image(s, overlayTex, overlayW, overlayH, "stretch", NULL);
		}
	}
}

static void open_menu(void) {
	set_page(0);
	menuOpen = true;
	menuSel = 0;
	menuPressed = -1;
	menuNote[0] = 0;
	restartArmed = false;
}

/* Returns true if the session should end (*exitTo set). */
static bool activate(int item, enum GameExit* exitTo) {
	if (item != M_RESTART) restartArmed = false;
	switch (item) {
	case M_RESUME:
		menuOpen = false;
		break;
	case M_SWAP:
		swap_screens();
		snprintf(menuNote, sizeof(menuNote), "SCREENS SWAPPED");
		break;
	case M_SCALE: {
		const char* next = !strcmp(cfg->scale, "integer") ? "fit" : "integer";
		snprintf(cfg->scale, sizeof(cfg->scale), "%s", next);
		snprintf(cfg->mapScale, sizeof(cfg->mapScale), "%s", next);
		save_prefs();
		break;
	}
	case M_LCD:
		cfg->lcdGrid = !cfg->lcdGrid;
		save_prefs();
		break;
	case M_SAVE:
		if (emu_state_save_file(&emu, gp.state)) {
			ui_osd("STATE SAVED");
			menuOpen = false;
		} else {
			snprintf(menuNote, sizeof(menuNote), "SAVE FAILED");
		}
		break;
	case M_LOAD:
		if (!item_enabled(M_LOAD)) {
			snprintf(menuNote, sizeof(menuNote), "NO SAVED STATE YET");
		} else if (emu_state_load_file(&emu, gp.state)) {
			ui_osd("STATE LOADED");
			menuOpen = false;
			return false;
		} else {
			snprintf(menuNote, sizeof(menuNote), "LOAD FAILED");
		}
		break;
	case M_OVERLAY:
		cfg->overlays = !cfg->overlays;
		save_prefs();
		break;
	case M_FPS:
		cfg->showFps = !cfg->showFps;
		save_prefs();
		break;
	case M_RESTART:
		if (!restartArmed) {
			restartArmed = true;
			snprintf(menuNote, sizeof(menuNote), "UNSAVED PROGRESS WILL BE LOST");
			break;
		}
		emu_reset(&emu);
		mapShownIdx = -1; /* back to the logo until the next map */
		restartArmed = false;
		menuOpen = false;
		ui_osd("GAME RESTARTED");
		break;
	case M_LIBRARY:
		*exitTo = GAME_TO_LIBRARY;
		return true;
	case M_EXIT:
		*exitTo = GAME_QUIT_APP;
		return true;
	}
	return false;
}

/* ---------------------------------------------------------------- session */

enum GameExit game_run(Config* c, const char* rom, bool fromLibrary) {
	cfg = c;
	debug = getenv("GBADS_DEBUG") != NULL;
	game_paths(cfg, rom, &gp);
	if (!emu_open(&emu, rom, gp.sav, true)) {
		ui_osd("COULD NOT LOAD GAME");
		return fromLibrary ? GAME_TO_LIBRARY : GAME_QUIT_APP;
	}
	fprintf(stderr, "game %s (%s), save %s\n", gp.base, emu.gameCode, gp.sav);

	mc = NULL;
	mapW = GBA_W;
	mapH = GBA_H;
	memset(&mapRes, 0, sizeof(mapRes));
	mapShownIdx = -1;
	frameCount = 0;
	singleShowsMap = false;
	menuOpen = false;
	if (cfg->map) {
		mc = mapcap_create(rom, emu.gameCode, cfg->mapCrop);
		if (mc) {
			fprintf(stderr, "map profile: %s\n", mapcap_title(mc));
			mapcap_size(mc, &mapW, &mapH);
		} else {
			fprintf(stderr, "no map profile for %s\n", emu.gameCode);
		}
	}
	for (int i = 0; i < nViews; ++i) {
		gameTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
		mapTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, mapW, mapH);
		SDL_SetTextureBlendMode(gameTex[i], SDL_BLENDMODE_NONE);
		SDL_SetTextureBlendMode(mapTex[i], SDL_BLENDMODE_NONE);
	}

	unsigned bc = (unsigned) strtoul(cfg->borderColor, NULL, 16);
	borderColor = (SDL_Color) { (bc >> 16) & 0xFF, (bc >> 8) & 0xFF, bc & 0xFF, 255 };
	overlayMode[0] = 0;
	uint32_t logoGen = logo_generation();
	ui_load_logo(cfg, rom, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel);
	mapframe_init(&mf, mc ? profile_find(emu.gameCode) : NULL, mapW, mapH);
	canvasTexW = canvasTexH = 0;
	snprintf(framePath, sizeof(framePath), "%s/frames", cfg->stateDir);
	mkdirs(framePath);
	snprintf(framePath, sizeof(framePath), "%s/frames/%.4s.png", cfg->stateDir, emu.gameCode);
	if (mf.prof && mf.prof->frame.t && !mapframe_load_atlas(&mf, framePath)) frame_request(rom, emu.gameCode, framePath);
	uint32_t frameGen = logo_generation();

	pageLen[0] = pageLen[1] = 0;
	pages[0][pageLen[0]++] = M_RESUME;
	pages[0][pageLen[0]++] = M_RESTART;
	pages[0][pageLen[0]++] = M_SAVE;
	pages[0][pageLen[0]++] = M_LOAD;
	if (fromLibrary) pages[0][pageLen[0]++] = M_LIBRARY;
	pages[0][pageLen[0]++] = M_EXIT;
	pages[1][pageLen[1]++] = M_SWAP;
	pages[1][pageLen[1]++] = M_SCALE;
	pages[1][pageLen[1]++] = M_LCD;
	pages[1][pageLen[1]++] = M_OVERLAY;
	pages[1][pageLen[1]++] = M_FPS;
	set_page(0);

	if (cfg->loadState[0]) {
		if (!emu_state_load_file(&emu, cfg->loadState)) fprintf(stderr, "could not load %s\n", cfg->loadState);
		cfg->loadState[0] = 0;
	} else if (cfg->autoResume && file_exists(gp.autoState)) {
		if (emu_state_load_file(&emu, gp.autoState)) {
			ui_osd("RESUMED  -  TAP OR HOME FOR MENU");
		}
	} else {
		ui_osd("TAP THE SCREEN OR PRESS HOME FOR MENU");
	}

	SDL_AudioSpec want = { 0 }, have;
	want.freq = (int) emu_audio_rate(&emu);
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 512;
	SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audio) SDL_PauseAudioDevice(audio, 0);
	else fprintf(stderr, "no audio: %s\n", SDL_GetError());
	const int targetQueued = want.freq * cfg->audioLatency / 1000;

	static int16_t samples[4096 * 2];
	uint32_t lastRoomKey = 0;
	int sinceMapRequest = 1 << 20;
	bool forceMap = true, wasDark = false;
	long refreshAt = -1;
	bool hotkeyBack = !strcmp(cfg->hotkey, "back");
	int modBtn = hotkeyBack ? B_SELECT : B_HOME;
	bool comboUsed = false;
	int selectPulse = 0;
	unsigned ffPhase = 0;
	bool noPace = getenv("GBADS_NOPACE") != NULL;
	enum GameExit exitTo = GAME_QUIT_APP;
	bool done = false;
	bool wasMenu = false;

	const double frameSec = 1.0 / GBA_FPS;
	const double freq = (double) SDL_GetPerformanceFrequency();
	double next = SDL_GetPerformanceCounter() / freq;
	double fpsT0 = next;
	int fpsFrames = 0;

	while (!done) {
		if (mf.prof && mf.prof->frame.t && !mf.atlas && logo_generation() != frameGen) {
			frameGen = logo_generation();
			if (mapframe_load_atlas(&mf, framePath)) canvasDirty = true;
		}
		if (!logoTex[0] && !logoTex[1] && logo_generation() != logoGen) {
			logoGen = logo_generation();
			ui_load_logo(cfg, NULL, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel);
		}
		if (!ui_events()) {
			exitTo = GAME_QUIT_APP;
			break;
		}
		ui_tick();
		ui_read_buttons();

		/* ---- pointer: any tap opens the menu; taps in the menu press buttons */
		Pointer p;
		while (ui_next_pointer(&p)) {
			if (!menuOpen) {
				if (p.kind == 1) {
					open_menu();
					ignorePointerUp = true;
				}
				continue;
			}
			if (p.screen != ui_touch_screen()) continue;
			if (p.kind == 1) {
				int t = tab_at(p.screen, p.x, p.y);
				if (t >= 0) {
					if (t != curPage) set_page(t);
					ignorePointerUp = true;
					continue;
				}
			}
			int it = item_at(p.screen, p.x, p.y);
			if (p.kind == 1) {
				ignorePointerUp = false;
				menuPressed = it;
				if (it >= 0) menuSel = it;
			} else if (p.kind == 3 && menuPressed >= 0 && it >= 0) {
				menuSel = it;
			} else if (p.kind == 2) {
				if (!ignorePointerUp && it >= 0 && it == menuPressed && activate(menuItems[it], &exitTo)) done = true;
				menuPressed = -1;
				ignorePointerUp = false;
			}
		}
		if (done) break;

		/* ---- HOME (or the hotkey modifier) alone toggles the menu; with a button it's a hotkey */
		bool mod = btnNow[modBtn];
		if (mod && !menuOpen) {
			if (btn_pressed(B_START)) exitTo = GAME_QUIT_APP, done = true, comboUsed = true;
			if (btn_pressed(B_R)) ui_osd(emu_state_save_file(&emu, gp.state) ? "STATE SAVED" : "SAVE FAILED"), comboUsed = true;
			if (btn_pressed(B_L) && file_exists(gp.state)) emu_state_load_file(&emu, gp.state), ui_osd("STATE LOADED"), forceMap = true, comboUsed = true;
			if (btn_pressed(B_X)) cfg->showFps = !cfg->showFps, comboUsed = true;
			if (btn_pressed(B_Y)) swap_screens(), comboUsed = true;
		}
		if (btn_released(B_HOME) && !(modBtn == B_HOME && comboUsed)) {
			if (menuOpen) menuOpen = false;
			else open_menu();
		}
		if (btn_released(modBtn)) {
			if (comboUsed) suppress_held();
			if (hotkeyBack && !comboUsed && !menuOpen) selectPulse = 4; /* plain Select goes to the game */
			comboUsed = false;
		}
		if (done) break;

		if (menuOpen) {
			if (btn_pressed(B_L) && curPage > 0) set_page(curPage - 1);
			if (btn_pressed(B_R) && curPage < 1) set_page(curPage + 1);
			if (btn_pressed(B_DOWN)) menuSel = (menuSel + 1) % nMenuItems;
			if (btn_pressed(B_UP)) menuSel = (menuSel + nMenuItems - 1) % nMenuItems;
			if (btn_pressed(B_B) || btn_pressed(B_START)) menuOpen = false;
			else if (btn_pressed(B_A) && activate(menuItems[menuSel], &exitTo)) break;
			if (menuOpen && !wasMenu && audio) {
				SDL_PauseAudioDevice(audio, 1);
				SDL_ClearQueuedAudio(audio);
			}
		}
		if (wasMenu && !menuOpen) {
			suppress_held();
			if (audio) SDL_PauseAudioDevice(audio, 0);
			next = SDL_GetPerformanceCounter() / freq;
			fpsT0 = next;
			fpsFrames = 0;
			forceMap = true;
		}
		wasMenu = menuOpen;

		if (menuOpen) {
			ui_clear();
			draw_game();
			draw_menu();
			ui_present();
			SDL_Delay(16);
			next = SDL_GetPerformanceCounter() / freq;
			continue;
		}

		if (btn_pressed(B_L2)) swap_screens();

		/* ---- emulate */
		uint32_t keys = 0;
		for (int b = 0; b < B_COUNT; ++b) {
			if (!btnNow[b]) suppress[b] = false; /* released: counts again */
		}
		if (!btnNow[modBtn] || hotkeyBack) {
			if (HELD(B_A)) keys |= K_A;
			if (HELD(B_B)) keys |= K_B;
			if (HELD(B_START) && !(hotkeyBack && btnNow[B_SELECT])) keys |= K_START;
			if (HELD(B_SELECT) && !hotkeyBack) keys |= K_SELECT;
			if (HELD(B_L)) keys |= K_L;
			if (HELD(B_R)) keys |= K_R;
			if (HELD(B_UP)) keys |= K_UP;
			if (HELD(B_DOWN)) keys |= K_DOWN;
			if (HELD(B_LEFT)) keys |= K_LEFT;
			if (HELD(B_RIGHT)) keys |= K_RIGHT;
			if (hotkeyBack && btnNow[B_SELECT]) keys &= ~(K_L | K_R | K_START);
		}
		if (selectPulse > 0) keys |= K_SELECT, --selectPulse;
		{
			static uint32_t lastKeys;
			if (debug && keys != lastKeys) fprintf(stderr, "frame %llu: game keys %03x\n", (unsigned long long) frameCount, keys);
			lastKeys = keys;
		}

		bool ff = btnNow[B_R2];
		int runs = ff ? SDL_max(1, cfg->ffSpeed) : 1;
		/* Audio keeps flowing during fast-forward: every runs-th sample is kept,
		 * so the device gets real-time worth of (sped-up) sound. Starving it
		 * makes PulseAudio/PipeWire grow their buffers after each underrun,
		 * which is what made the lag worse every time. */
		int queuedNow = audio ? (int) (SDL_GetQueuedAudioSize(audio) / 4) : 0;
		bool dropAudio = queuedNow > targetQueued * 3; /* hard cap on drift */
		if (debug && audio && frameCount % 30 == 0) {
			fprintf(stderr, "audio queue %d ms%s\n", queuedNow * 1000 / want.freq, ff ? " (ff)" : "");
		}
		for (int i = 0; i < runs; ++i) {
			emu_frame(&emu, keys);
			size_t got;
			while ((got = emu_audio_read(&emu, samples, 4096)) > 0) {
				if (!audio || dropAudio) continue;
				if (runs > 1) {
					size_t outN = 0;
					for (size_t f = 0; f < got; ++f) {
						if (ffPhase++ % runs == 0) {
							samples[outN * 2] = samples[f * 2];
							samples[outN * 2 + 1] = samples[f * 2 + 1];
							++outN;
						}
					}
					got = outN;
				}
				if (got) SDL_QueueAudio(audio, samples, got * 4);
			}
		}
		++frameCount;

		/* ---- map refresh */
		if (mc) {
			++sinceMapRequest;
			bool dark = frame_is_dark(emu.fb);
			if (wasDark && !dark) refreshAt = (long) frameCount + 6;
			wasDark = dark;
			if (refreshAt >= 0 && (long) frameCount >= refreshAt) {
				forceMap = true;
				refreshAt = -1;
				if (debug) fprintf(stderr, "frame %llu: room transition\n", (unsigned long long) frameCount);
			}
			uint32_t rk = mapcap_room_key(mc, &emu);
			bool roomChanged = rk && rk != lastRoomKey;
			if ((forceMap || roomChanged || sinceMapRequest >= cfg->mapInterval) && mapcap_live_ok(mc, &emu)) {
				if (mapcap_request(mc, &emu)) {
					sinceMapRequest = 0;
					lastRoomKey = rk;
					forceMap = false;
				}
			}
			if (mapcap_poll(mc, mapRes.serial, &mapRes)) {
				if (mapRes.hasTile) mapframe_set_tile(&mf, mapRes.tile, mapRes.tileK, mapRes.tileX, mapRes.tileY);
				mapShownIdx = -1;
				mapAnimStart = frameCount;
				upload_map_frame(0);
				if (debug) fprintf(stderr, "frame %llu: map updated\n", (unsigned long long) frameCount);
			} else if (mapRes.nFrames > 1 && mapShownIdx >= 0) {
				int idx = (int) ((frameCount - mapAnimStart) / SDL_max(1, mapRes.frameStep)) % mapRes.nFrames;
				upload_map_frame(idx);
			}
		}

		for (int i = 0; i < nViews; ++i) SDL_UpdateTexture(gameTex[i], NULL, emu.fb, GBA_W * 4);
		ui_clear();
		draw_game();
		ui_present();

		/* ---- pacing: real-time clock nudged by audio queue depth */
		double now = SDL_GetPerformanceCounter() / freq;
		fpsFrames += runs; /* emulated frames */
		if (now - fpsT0 >= 1.0) {
			fps = fpsFrames / (now - fpsT0);
			speedPct = fps / GBA_FPS * 100.0;
			if (debug) fprintf(stderr, "speed %.1f fps (%.0f%%)\n", fps, speedPct);
			fpsFrames = 0;
			fpsT0 = now;
		}
		/* fast-forward is paced too: ff_speed frames per real frame */
		next += frameSec;
		if (audio) {
			int queued = (int) (SDL_GetQueuedAudioSize(audio) / 4);
			if (queued > targetQueued * 3 / 2) next += frameSec * 0.015;
			else if (queued < targetQueued / 2) next -= frameSec * 0.015;
		}
		if (next < now - 4 * frameSec) next = now;
		if (noPace) next = now; /* benchmarking: run as fast as possible */
		double wait = next - now;
		if (wait > 0) {
			struct timespec ts = { 0, (long) (wait * 1e9) };
			nanosleep(&ts, NULL);
		}
	}

	fprintf(stderr, "leaving game after %llu frames\n", (unsigned long long) frameCount);
	auto_save();
	if (audio) SDL_CloseAudioDevice(audio);
	mapcap_destroy(mc);
	mc = NULL;
	emu_close(&emu); /* flushes the battery save */
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		free(mapRes.frames[i]);
		mapRes.frames[i] = NULL;
	}
	for (int i = 0; i < nViews; ++i) {
		SDL_DestroyTexture(gameTex[i]);
		SDL_DestroyTexture(mapTex[i]);
		gameTex[i] = mapTex[i] = NULL;
	}
	ui_free_image(logoTex);
	ui_free_image(overlayTex);
	overlayMode[0] = 0;
	mapframe_free(&mf);
	for (int i = 0; i < 2; ++i) {
		if (canvasTex[i]) SDL_DestroyTexture(canvasTex[i]);
		canvasTex[i] = NULL;
	}
	return exitTo;
}

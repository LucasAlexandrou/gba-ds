/* One play session: the emulator, the second-screen map, and the pause menu. */
#include "game.h"
#include "emu.h"
#include "mapcap.h"
#include "paths.h"
#include "ui.h"

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
static MapResult mapRes;
static int mapW = GBA_W, mapH = GBA_H;
static int mapShownIdx = -1;
static uint64_t mapAnimStart, frameCount;
static bool singleShowsMap;
static double fps;
static bool debug;

/* ---------------------------------------------------------------- menu */

enum MenuItem { M_RESUME, M_SWAP, M_SAVE, M_LOAD, M_LIBRARY, M_EXIT };
static const char* menuLabels[] = {
	[M_RESUME] = "RESUME", [M_SWAP] = "SWAP SCREENS", [M_SAVE] = "SAVE STATE",
	[M_LOAD] = "LOAD STATE", [M_LIBRARY] = "GAME LIST", [M_EXIT] = "EXIT EMULATOR",
};
static int menuItems[6], nMenuItems;
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

static SDL_Rect item_rect(int scr, int i) {
	SDL_Rect r = screens[scr].rect;
	int px = ui_px(scr);
	int h = px * 17, gap = px * 4;
	int w = SDL_min(r.w - px * 16, ui_text_width("EXIT EMULATOR", px) + px * 60);
	int total = nMenuItems * h + (nMenuItems - 1) * gap;
	int top = (r.h - total) / 2 + px * 6;
	return (SDL_Rect) { (r.w - w) / 2, top + i * (h + gap), w, h };
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
	SDL_Rect first = item_rect(scr, 0);
	ui_text_center(scr, screens[scr].rect.w / 2, first.y - px * 16, px + 1, "PAUSED", C_GOLD);
	for (int i = 0; i < nMenuItems; ++i) {
		SDL_Rect r = item_rect(scr, i);
		bool sel = i == menuSel;
		ui_fill(scr, (SDL_Rect) { r.x - px / 2, r.y - px / 2, r.w + px, r.h + px }, sel ? C_BTN_EDGE : C_BTN);
		ui_fill(scr, r, sel ? C_BTN_SEL : C_BTN);
		bool en = item_enabled(menuItems[i]);
		ui_text_center(scr, r.x + r.w / 2, r.y + (r.h - px * 7) / 2, px, menuLabels[menuItems[i]], en ? C_TEXT : C_DIS);
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

static void upload_map_frame(int idx) {
	if (idx == mapShownIdx || !mapRes.frames[idx]) return;
	for (int i = 0; i < nViews; ++i) SDL_UpdateTexture(mapTex[i], NULL, mapRes.frames[idx], mapW * 4);
	mapShownIdx = idx;
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

static void swap_screens(void) {
	if (nScreens < 2) {
		singleShowsMap = !singleShowsMap;
		return;
	}
	cfg->swap = !cfg->swap;
	/* remember the choice for next time */
	char prefs[1100];
	snprintf(prefs, sizeof(prefs), "%s/prefs.cfg", cfg->stateDir);
	FILE* f = fopen(prefs, "w");
	if (f) {
		fprintf(f, "# written by gbadual when you swap screens\nswap = %d\n", cfg->swap ? 1 : 0);
		fclose(f);
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
			ui_image(s, gameTex, GBA_W, GBA_H, cfg->scale, NULL);
			const char* osd = ui_osd_text();
			if (osd && !menuOpen) {
				ui_text(s, px * 4 + 2, r.h - px * 12 + 2, px, osd, (SDL_Color) { 0, 0, 0, 255 });
				ui_text(s, px * 4, r.h - px * 12, px, osd, C_TEXT);
			}
			if (cfg->showFps) {
				char buf[32];
				snprintf(buf, sizeof(buf), "%.1f", fps);
				ui_text(s, px * 4, px * 4, px, buf, C_TEXT);
			}
		} else if (mapShownIdx >= 0) {
			ui_image(s, mapTex, mapW, mapH, cfg->mapScale, NULL);
		} else {
			ui_text_center(s, r.w / 2, r.h / 2 - px * 4, px, "MAP APPEARS ONCE YOU'RE IN GAME",
			               (SDL_Color) { 90, 90, 110, 255 });
		}
	}
}

static void open_menu(void) {
	menuOpen = true;
	menuSel = 0;
	menuPressed = -1;
	menuNote[0] = 0;
}

/* Returns true if the session should end (*exitTo set). */
static bool activate(int item, enum GameExit* exitTo) {
	switch (item) {
	case M_RESUME:
		menuOpen = false;
		break;
	case M_SWAP:
		swap_screens();
		snprintf(menuNote, sizeof(menuNote), "SCREENS SWAPPED");
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
	debug = getenv("GBADUAL_DEBUG") != NULL;
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
		mc = mapcap_create(rom, emu.gameCode);
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

	nMenuItems = 0;
	menuItems[nMenuItems++] = M_RESUME;
	menuItems[nMenuItems++] = M_SWAP;
	menuItems[nMenuItems++] = M_SAVE;
	menuItems[nMenuItems++] = M_LOAD;
	if (fromLibrary) menuItems[nMenuItems++] = M_LIBRARY;
	menuItems[nMenuItems++] = M_EXIT;

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
	enum GameExit exitTo = GAME_QUIT_APP;
	bool done = false;
	bool wasMenu = false;

	const double frameSec = 1.0 / GBA_FPS;
	const double freq = (double) SDL_GetPerformanceFrequency();
	double next = SDL_GetPerformanceCounter() / freq;
	double fpsT0 = next;
	int fpsFrames = 0;

	while (!done) {
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
			if (hotkeyBack && !comboUsed && !menuOpen) selectPulse = 4; /* plain Select goes to the game */
			comboUsed = false;
		}
		if (done) break;

		if (menuOpen) {
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
			if (audio) SDL_PauseAudioDevice(audio, 0);
			next = SDL_GetPerformanceCounter() / freq;
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
		if (!btnNow[modBtn] || hotkeyBack) {
			if (btnNow[B_A]) keys |= K_A;
			if (btnNow[B_B]) keys |= K_B;
			if (btnNow[B_START] && !(hotkeyBack && btnNow[B_SELECT])) keys |= K_START;
			if (btnNow[B_SELECT] && !hotkeyBack) keys |= K_SELECT;
			if (btnNow[B_L]) keys |= K_L;
			if (btnNow[B_R]) keys |= K_R;
			if (btnNow[B_UP]) keys |= K_UP;
			if (btnNow[B_DOWN]) keys |= K_DOWN;
			if (btnNow[B_LEFT]) keys |= K_LEFT;
			if (btnNow[B_RIGHT]) keys |= K_RIGHT;
			if (hotkeyBack && btnNow[B_SELECT]) keys &= ~(K_L | K_R | K_START);
		}
		if (selectPulse > 0) keys |= K_SELECT, --selectPulse;

		bool ff = btnNow[B_R2];
		int runs = ff ? SDL_max(1, cfg->ffSpeed) : 1;
		for (int i = 0; i < runs; ++i) {
			emu_frame(&emu, keys);
			size_t got;
			while ((got = emu_audio_read(&emu, samples, 4096)) > 0) {
				if (audio && !ff) SDL_QueueAudio(audio, samples, got * 4);
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
		if (next < now - 4 * frameSec) next = now;
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
	return exitTo;
}

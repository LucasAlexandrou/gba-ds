/* One play session: the emulator, the second-screen map, and the pause menu. */
#include "game.h"
#include "android.h"
#include "brand.h"
#include "emu.h"
#include "mapcap.h"
#include "paths.h"
#include "ui.h"
#include "ra.h"
#include "logo.h"
#include <dirent.h>
#include <math.h>
#include "mapframe.h"
#include "profiles.h"
#include "cheats.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* Times the game thread was taken off its core (involuntary context
 * switches): another thread got it, so the frame ran late through no work
 * of ours. Linux only; 0 elsewhere. */
static long thread_preemptions(void) {
#if defined(__linux__) && defined(RUSAGE_THREAD)
	struct rusage ru;
	if (getrusage(RUSAGE_THREAD, &ru) == 0) return ru.ru_nivcsw;
#endif
	return 0;
}
static double thread_cpu_ms(void) {
	struct timespec t;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) return 0;
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
static int cpu_mhz(int cpu) {
#ifdef __linux__
	if (cpu < 0) return 0;
	char path[96];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
	FILE* f = fopen(path, "r");
	long k = 0;
	if (f) {
		if (fscanf(f, "%ld", &k) != 1) k = 0;
		fclose(f);
	}
	return (int) (k / 1000);
#else
	(void) cpu;
	return 0;
#endif
}
static int current_cpu(void) {
#if defined(__linux__) && defined(SYS_getcpu)
	unsigned cpu = 0;
	return syscall(SYS_getcpu, &cpu, NULL, NULL) == 0 ? (int) cpu : -1;
#else
	return -1;
#endif
}

#define GBA_FPS (16777216.0 / 280896.0)

static Config* cfg;
static Emu emu;

/* ------------------------------------------------- frame buffer (option)
 * With frame_buffer on, the next frame is emulated on this thread while the
 * game loop draws and shows the one before: a frame that runs long (the
 * game thread kept off the CPU for a few ms, common on Android) no longer
 * misses the screen's refresh, at the cost of one frame of input delay.
 * The worker only runs emu_frame; everything else that touches the core
 * (sound, RetroAchievements, map, probes, states, cheats, the menu) stays
 * on the game loop and runs while the worker is idle: the loop waits for
 * it at the top of each iteration and starts it again just before drawing. */
static struct {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, busy, quit;
	uint32_t keys;
} ahead = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };
static bool aheadInFlight; /* the worker is emulating the next frame */
static bool aheadReady;    /* it has: that frame is emulated but not yet used */
static uint32_t aheadFb[GBA_W * GBA_H]; /* the frame on screen, while the worker writes the next */

static void* ahead_worker(void* arg) {
	(void) arg;
#ifdef __ANDROID__
	int nice = android_raise_thread();
#else
	int nice = 0;
#endif
	fprintf(stderr, "frame buffer: emulation thread started%s\n", nice ? " (priority raised)" : "");
	pthread_mutex_lock(&ahead.lock);
	for (;;) {
		while (!ahead.busy && !ahead.quit) pthread_cond_wait(&ahead.cond, &ahead.lock);
		if (ahead.quit) break;
		uint32_t keys = ahead.keys;
		pthread_mutex_unlock(&ahead.lock);
		emu_frame(&emu, keys);
		pthread_mutex_lock(&ahead.lock);
		ahead.busy = false;
		pthread_cond_broadcast(&ahead.cond);
	}
	pthread_mutex_unlock(&ahead.lock);
	return NULL;
}

/* false: no thread (emulate inline instead) */
static bool ahead_start(uint32_t keys) {
	pthread_mutex_lock(&ahead.lock);
	if (!ahead.started) {
		ahead.quit = false;
		ahead.started = pthread_create(&ahead.thread, NULL, ahead_worker, NULL) == 0;
	}
	bool ok = ahead.started;
	if (ok) {
		ahead.keys = keys;
		ahead.busy = true;
		pthread_cond_broadcast(&ahead.cond);
	}
	pthread_mutex_unlock(&ahead.lock);
	return ok;
}

/* Wait for the frame being emulated, if any (then the core is ours again). */
static void ahead_sync(void) {
	if (!aheadInFlight) return;
	pthread_mutex_lock(&ahead.lock);
	while (ahead.busy) pthread_cond_wait(&ahead.cond, &ahead.lock);
	pthread_mutex_unlock(&ahead.lock);
	aheadInFlight = false;
	aheadReady = true;
}

static void ahead_stop(void) {
	ahead_sync();
	aheadReady = false;
	pthread_mutex_lock(&ahead.lock);
	bool started = ahead.started;
	ahead.quit = true;
	pthread_cond_broadcast(&ahead.cond);
	pthread_mutex_unlock(&ahead.lock);
	if (started) pthread_join(ahead.thread, NULL);
	ahead.started = false;
}
static MapCapture* mc;
static GamePaths gp;

static SDL_Texture* gameTex[2];
static SDL_Texture* mapTex[2];
static SDL_Texture* logoTex[2];
/* the logo centred in a GBA-sized picture, so it's scaled (and gridded)
 * like the game; NULL for a user's high-resolution logo */
static SDL_Texture* logoCanvasTex[2];
/* user overlay PNGs: [0] game screen ("primary"), [1] map screen ("secondary") */
static SDL_Texture* overlayTex[2][2];
static int overlayW[2], overlayH[2];
static char overlayMode[2][16];
static int logoW, logoH;
static bool logoPixel;
static MapResult mapRes;
static int mapW = GBA_W, mapH = GBA_H;
static int mapShownIdx = -1;
/* "Map Animations: Off" shows this composed still instead of mapRes.frames[] */
#define STILL_IDX MAP_MAX_FRAMES
static uint32_t* stillPx;        /* mapW * mapH */
static int stillW, stillH;
static bool stillValid;
/* The map button did nothing at the last attempt (A Link to the Past indoors
 * without a map): keep showing the last map, dimmed. */
static bool mapInactive;
static bool logoOnly; /* the game has a profile but no map: logo on the second screen */
static const MapProfile* snapProf; /* logo-only game with screens copied to the second screen */
/* Mario Kart: the race HUD on the second screen (profile .hud) */
static const MapProfile* hudProf;
static uint32_t hudPx[GBA_W * GBA_H];
static bool hudOn; /* the frame being emulated is split */
/* the second screen's picture: the HUD at 2x over the backdrop, its map
 * larger; the backdrop, made from the grabbed layers as each race starts */
#define HUD_CW (GBA_W * 2)
#define HUD_CH (GBA_H * 2)
static uint32_t hudCanvas[HUD_CW * HUD_CH], hudBack[HUD_CW * HUD_CH], hudGrab[GBA_W * GBA_H];
static bool hudBackValid;
static int hudFrames; /* split frames since the race (re)started */
static int mapX0, mapY0, mapX1, mapY1; /* the map's box on the HUD, measured over the first frames */
static bool mapFound;
static bool hudScene; /* the profile's other scene (Mario Kart's podium): logo backdrop, its own sprites */
static uint32_t hudLogoBack[HUD_CW * HUD_CH];
static bool hudLogoDone;
static int snapShow, snapRevert;   /* consecutive frames matching show / a revert screen */

static uint64_t mapAnimStart, frameCount;
static bool singleShowsMap;
static double fps, speedPct;
static Uint64 emuTicks, drawTicks; /* time spent emulating / drawing, for the 5 s report */

/* Slow frames (work over a frame's time): the worst each second, broken down,
 * logged on Android (where frame time is tight) or with GBADS_DEBUG. */
typedef struct {
	double total, input, emulate, ra, map, draw, top, bottom;
	int redraws;

	unsigned long long frame;
	long preempted; /* times the thread was put off its core during the frame */
	int cpu, mhz;   /* where it ended, and that core's clock then */
	double cpuMs;   /* time the thread actually ran (the rest it waited) */
	double waitEmu, waitDraw; /* of the waiting: during emulation, during drawing */
	double audio;   /* of emulating: handing the sound to SDL (its lock is shared with the audio thread) */
} SlowFrame;
static SlowFrame slowWorst;
static int slowCount;

static void slow_frame_note(SlowFrame f) {
	++slowCount;
	if (f.total > slowWorst.total) slowWorst = f;
}

static void slow_frame_report(bool debug) {
#ifndef __ANDROID__
	if (!debug) {
		slowCount = 0;
		slowWorst.total = 0;
		return;
	}
#endif
	if (!slowCount) return;
	const SlowFrame* w = &slowWorst;
	fprintf(stderr,
	        "slow frames: %d in the last second; worst (frame %llu) %.1f ms: input/menu %.1f, emulating %.1f (sound hand-off %.1f), "
	        "RetroAchievements %.1f, map %.1f, drawing %.1f (top screen %s %.1f, bottom %s %.1f); "
	        "ran %.1f ms of it (waited %.1f: %.1f while emulating, %.1f while drawing), preempted %ld time(s), on cpu %d at %d MHz\n",
	        slowCount, w->frame, w->total, w->input, w->emulate, w->audio, w->ra, w->map, w->draw,
	        w->redraws & 1 ? "redrawn" : "kept", w->top, w->redraws & 2 ? "redrawn" : "kept", w->bottom, w->cpuMs, w->total - w->cpuMs, w->waitEmu, w->waitDraw, w->preempted, w->cpu, w->mhz);
	slowCount = 0;
	slowWorst.total = 0;
}
static bool debug;

/* ---------------------------------------------------------------- menu */

enum MenuItem { M_RESUME, M_CHEATS, M_ACH, M_SHOT, M_SWAP, M_SCALE, M_LCD, M_OVERLAY, M_MAPANIM, M_MAPRATE, M_FPS, M_SAVE, M_LOAD, M_RESTART, M_LIBRARY, M_EXIT, M_FRAMEBUF, M_LOGO, M_MATCH, M_REWIND };
static const char* menuLabels[] = {
	/* title case: drawn in Futura from the built-in images (tools/make_brand.py) */
	[M_RESUME] = "Resume", [M_SWAP] = "Swap Screens", [M_SAVE] = "Save State", [M_SCALE] = "", [M_LCD] = "", [M_FPS] = "", [M_FRAMEBUF] = "", [M_OVERLAY] = "", [M_MAPANIM] = "", [M_LOGO] = "", [M_MATCH] = "", [M_REWIND] = "", [M_MAPRATE] = "", [M_RESTART] = "Restart Game",
	[M_LOAD] = "Load State", [M_CHEATS] = "Cheats", [M_ACH] = "Achievements", [M_SHOT] = "Take Screenshot", [M_LIBRARY] = "Close Game", [M_EXIT] = "Close GBA DS",
};
static int menuItems[12], nMenuItems;
/* two pages, switched with L1/R1 or the tabs */
#define N_PAGES 4
static int pages[N_PAGES][12], pageLen[N_PAGES], curPage;
/* Buttons still held when the menu closes (or after a HOME combo) are kept
 * from the game until released, so e.g. the A that picked "Save state"
 * doesn't make Samus jump. */
static bool suppress[B_COUNT];
#define HELD(b) (btnNow[b] && !suppress[b])
static void suppress_held(void) {
	for (int b = 0; b < B_COUNT; ++b) suppress[b] = btnNow[b];
}
static const char* pageNames[N_PAGES] = { "Game", "Display", "Performance", "Extras" };
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

static Cheats cheats;
static SDL_Rect tab_rect(int scr, int t);
static SDL_Rect ach_area(int scr);

static bool item_enabled(int item) {
	if (item == M_CHEATS) return cheats.n > 0;
	if (item == M_MAPRATE) return mc != NULL; /* games with a live map */
	return item != M_LOAD || file_exists(gp.state);
}

/* ---------------------------------------------------------------- cheats screen
 *
 * "Cheats" on the Game page opens a list of the game's cheats with an On/Off
 * pill each: D-pad / A to toggle, L/R a page at a time, B (or the Back box)
 * back to the menu. Touch: tap a cheat to toggle it, drag to scroll. */
static bool cheatsOpen;
/* "Achievements" (RetroAchievements signed in): the list from ra.c, under
 * the same Back / title boxes. D-pad scrolls, L/R a page, B back. */
static bool achOpen;
static int achPressY, achPressScroll;
static bool achPressed;
static int cheatSel;                 /* row index */
static int cheatScroll;              /* pixels */
static int cheatRows[CHEAT_MAX_FILES + 1024], nCheatRows; /* >= 0 cheat index, < 0 file header (-1 - file) */
static int cheatPressRow = -1, cheatPressY, cheatPressScroll;
static bool cheatDragging;

static void build_cheat_rows(void) {
	nCheatRows = 0;
	for (int f = 0; f < cheats.nFiles; ++f) {
		if (cheats.nFiles > 1) cheatRows[nCheatRows++] = -1 - f;
		for (int i = 0; i < cheats.n && nCheatRows < (int) SDL_arraysize(cheatRows); ++i) {
			if (cheats.items[i].file == f) cheatRows[nCheatRows++] = i;
		}
	}
}

typedef struct {
	float px;
	int tpx, top, rowH, gap, x, w, listH, footY;
} CheatGeom;

static CheatGeom cheat_geom(int scr) {
	CheatGeom g;
	SDL_Rect r = screens[scr].rect;
	g.px = ui_px(scr);
	g.tpx = SDL_max(2, g.px * 3 / 4);
	g.top = g.px * 26;
	g.rowH = g.tpx * 7 + g.px * 5;
	g.gap = g.px * 2;
	g.x = g.px * 6;
	g.w = r.w - g.px * 12;
	g.footY = r.h - g.tpx * 9 * 2 - g.px * 5;
	g.listH = g.footY - g.px * 6 - g.top;
	return g;
}

static int cheat_max_scroll(const CheatGeom* g) {
	return SDL_max(0, nCheatRows * (g->rowH + g->gap) - g->gap - g->listH);
}

static void cheat_keep_visible(int scr) {
	CheatGeom g = cheat_geom(scr);
	int y = cheatSel * (g.rowH + g.gap);
	if (y < cheatScroll) cheatScroll = y;
	if (y + g.rowH > cheatScroll + g.listH) cheatScroll = y + g.rowH - g.listH;
	cheatScroll = SDL_clamp(cheatScroll, 0, cheat_max_scroll(&g));
}

static void cheat_move(int dir) {
	int i = cheatSel;
	for (int k = 0; k < nCheatRows; ++k) {
		i += dir;
		if (i < 0 || i >= nCheatRows) return;
		if (cheatRows[i] >= 0) {
			cheatSel = i;
			cheat_keep_visible(ui_touch_screen());
			return;
		}
	}
}

static void open_cheats(void) {
	build_cheat_rows();
	cheatsOpen = true;
	cheatSel = 0;
	cheatScroll = 0;
	if (nCheatRows && cheatRows[0] < 0) cheat_move(1);
	cheatPressRow = -1;
}

static void toggle_cheat(int row) {
	if (row < 0 || row >= nCheatRows || cheatRows[row] < 0) return;
	Cheat* c = &cheats.items[cheatRows[row]];
	c->on = !c->on;
	cheats_apply(&cheats, &emu);
	cheats_save(&cheats);
}

/* row under screen-local y, or -1 */
static int cheat_row_at(int scr, int x, int y) {
	CheatGeom g = cheat_geom(scr);
	if (y < g.top || y >= g.top + g.listH || x < g.x || x >= g.x + g.w) return -1;
	int ly = y - g.top + cheatScroll;
	int row = ly / (g.rowH + g.gap);
	if (ly % (g.rowH + g.gap) >= g.rowH || row >= nCheatRows) return -1;
	return row;
}

/* fits s into maxChars, ending in "..." when cut */
/* s in Jost, cut short with "..." if it's wider than maxW. */
static void fit_text(char* out, size_t size, int font, int capPx, const char* s, int maxW) {
	snprintf(out, size, "%s", s);
	if (ui_font_width(font, capPx, out) <= maxW) return;
	size_t n = strlen(out);
	while (n > 0) {
		do --n; while (n > 0 && ((unsigned char) out[n] & 0xC0) == 0x80); /* whole UTF-8 characters */
		snprintf(out, size, "%.*s...", (int) n, s);
		if (ui_font_width(font, capPx, out) <= maxW) return;
	}
}

static void draw_cheats(int scr) {
	CheatGeom g = cheat_geom(scr);
	SDL_Rect r = screens[scr].rect;
	float px = g.px;
	int rad = px * 3, cap = px * 11 / 2;
	/* top: Back box (where the Game tab is) and the title */
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Cheats", (SDL_Color) { 20, 16, 36, 255 });
	SDL_Rect clip = { screens[scr].rect.x, screens[scr].rect.y + g.top, r.w, g.listH };
	ui_clip(scr, &clip);
	int tcap = g.tpx * 5; /* Jost capitals, about the pixel font's height */
	int pillW = ui_label_width(scr, tcap, "Off") + px * 8;
	int nameW = g.w - pillW - px * 10;
	for (int i = 0; i < nCheatRows; ++i) {
		int y = g.top + i * (g.rowH + g.gap) - cheatScroll;
		if (y + g.rowH <= g.top || y >= g.top + g.listH) continue;
		char line[300];
		if (cheatRows[i] < 0) {
			const CheatFile* f = &cheats.files[-1 - cheatRows[i]];
			fit_text(line, sizeof(line), TF_BOLD, tcap, f->label, g.w - px * 4);
			ui_font_text(scr, TF_BOLD, g.x + px * 2, y + g.rowH / 2, tcap, line, C_GOLD, -1);
			continue;
		}
		const Cheat* c = &cheats.items[cheatRows[i]];
		bool sel = i == cheatSel;
		SDL_Rect row = { g.x, y, g.w, g.rowH };
		if (sel) ui_fill_round(scr, (SDL_Rect) { row.x - px / 2, row.y - px / 2, row.w + px, row.h + px }, C_BTN_EDGE, rad);
		ui_fill_round(scr, row, sel ? C_BTN_SEL : C_BTN, rad);
		fit_text(line, sizeof(line), TF_BODY, tcap, c->desc, nameW);
		ui_font_text(scr, TF_BODY, g.x + px * 4, y + g.rowH / 2, tcap, line, C_TEXT, -1);
		SDL_Rect pill = { g.x + g.w - pillW - px * 3, y + px * 2, pillW, g.rowH - px * 4 };
		ui_fill_round(scr, pill, c->on ? (SDL_Color) { 64, 168, 112, 255 } : (SDL_Color) { 60, 56, 84, 255 }, px * 2);
		ui_label(scr, pill.x + pill.w / 2, pill.y + pill.h / 2, tcap, c->on ? "On" : "Off",
		         c->on ? C_TEXT : (SDL_Color) { 160, 156, 184, 255 });
	}
	ui_clip(scr, NULL);
	/* scroll bar */
	int maxS = cheat_max_scroll(&g);
	if (maxS > 0) {
		int total = g.listH + maxS;
		int bh = SDL_max(px * 6, g.listH * g.listH / total);
		int by = g.top + (g.listH - bh) * cheatScroll / maxS;
		ui_fill_round(scr, (SDL_Rect) { r.w - px * 4, by, px * 2, bh }, C_BTN_EDGE, px);
	}
	/* footer: the selected cheat's whole name when the row had to cut it */
	if (cheatSel >= 0 && cheatSel < nCheatRows && cheatRows[cheatSel] >= 0 &&
	    ui_font_width(TF_BODY, tcap, cheats.items[cheatRows[cheatSel]].desc) > nameW) {
		ui_font_wrap(scr, TF_BODY, px * 6, g.footY - tcap / 2, r.w - px * 12, tcap, cheats.items[cheatRows[cheatSel]].desc,
		             C_GOLD);
	}
	char count[48];
	snprintf(count, sizeof(count), "%d on", cheats_on(&cheats));
	ui_font_text(scr, TF_BODY, r.w - px * 4, r.h - g.tpx * 5, tcap, count, C_DIS, 1);
}

static int menu_width(int scr) {
	float px = ui_px(scr);
	return SDL_min(screens[scr].rect.w - px * 16, px * (17 * 6 - 1) /* menu width: 17 characters of the old pixel font */ + px * 48);
}

static SDL_Rect item_rect(int scr, int i) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	int h = px * 14, gap = px * 3;
	int w = menu_width(scr);
	int top = px * 28; /* below the page tabs; same on both pages */
	(void) r;
	return (SDL_Rect) { (r.w - w) / 2, top + i * (h + gap), w, h };
}

/* box t of n across the top of the menu: the page tabs (n = N_PAGES), or
 * Back and the title on the cheats / achievements screens (n = 2) */
static SDL_Rect top_box(int scr, int t, int n) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	int w = menu_width(scr);
	int gap = px * 3, avail = w - gap * (n - 1);
	if (n == N_PAGES) {
		/* the page tabs: each as wide as its name needs, sharing the width */
		int cap = px * 11 / 2, need[N_PAGES], sum = 0;
		for (int i = 0; i < n; ++i) sum += need[i] = ui_font_width(TF_BOLD, cap, pageNames[i]) + (int) (px * 6);
		int x = (r.w - w) / 2, used = 0;
		for (int i = 0; i < t; ++i) used += need[i];
		int x0 = x + used * avail / sum + t * gap, x1 = x + (used + need[t]) * avail / sum + t * gap;
		return (SDL_Rect) { x0, px * 7, x1 - x0, px * 14 };
	}
	int tw = avail / n;
	return (SDL_Rect) { (r.w - w) / 2 + t * (tw + gap), px * 7, tw, px * 14 };
}

static SDL_Rect tab_rect(int scr, int t) {
	return top_box(scr, t, 2);
}

/* The device's battery, 0-100, or -1 if unknown: the battery in
 * /sys/class/power_supply (ROCKNIX), else SDL's (Android). Read at most
 * every 5 seconds. */
static int battery_percent(void) {
	static int pct = -1;
	static Uint32 last;
	static bool read;
	Uint32 now = SDL_GetTicks();
	if (read && now - last < 5000) return pct;
	read = true;
	last = now;
	pct = -1;
#ifndef __ANDROID__
	DIR* d = opendir("/sys/class/power_supply");
	if (d) {
		struct dirent* de;
		while (pct < 0 && (de = readdir(d))) {
			if (de->d_name[0] == '.') continue;
			char path[512], type[32] = "";
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", de->d_name);
			FILE* f = fopen(path, "r");
			if (!f) continue;
			if (!fgets(type, sizeof(type), f)) type[0] = 0;
			fclose(f);
			if (strncmp(type, "Battery", 7)) continue;
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", de->d_name);
			f = fopen(path, "r");
			if (!f) continue;
			int v;
			if (fscanf(f, "%d", &v) == 1 && v >= 0 && v <= 100) pct = v;
			fclose(f);
		}
		closedir(d);
	}
#endif
	if (pct < 0) {
		int p = -1;
		SDL_PowerState st = SDL_GetPowerInfo(NULL, &p);
		if (st != SDL_POWERSTATE_UNKNOWN && st != SDL_POWERSTATE_NO_BATTERY && p >= 0) pct = p;
	}
	return pct;
}

/* Status text on the top (first) screen while paused, with a drop shadow:
 * align -1 at the top left, 1 at the top right. */
static void draw_status(const char* text, int align) {
	int s = 0; /* the top screen, whatever is on it */
	float px = ui_px(s);
	int cap = px * 5;
	int x = align < 0 ? (int) (px * 5) : screens[s].rect.w - (int) (px * 5), cy = px * 7;
	int sh = SDL_max(1, (int) (px / 2));
	ui_font_text(s, TF_BOLD, x + sh, cy + sh, cap, text, (SDL_Color) { 0, 0, 0, 255 }, align);
	ui_font_text(s, TF_BOLD, x, cy, cap, text, C_TEXT, align);
}

/* While paused: the time (24-hour HH:MM) at the top left of the top screen
 * and the battery level at the top right. */
static void draw_battery(void) {
	char buf[16];
	time_t now = time(NULL);
	struct tm tm;
	if (localtime_r(&now, &tm)) {
		snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
		draw_status(buf, -1);
	}
	int pct = battery_percent();
	if (pct >= 0) {
		snprintf(buf, sizeof(buf), "%d%%", pct);
		draw_status(buf, 1);
	}
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

static int tab_at(int scr, int x, int y, int n) {
	for (int t = 0; t < n; ++t) {
		SDL_Rect r = top_box(scr, t, n);
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

static const char* scale_label(const char* scale) {
	return !strcmp(scale, "fit")       ? "Scale: Full Width" :
	       !strcmp(scale, "zoom")      ? "Scale: Zoom" :
	       !strcmp(scale, "stretch")   ? "Scale: Stretch" : "Scale: Pixel Perfect";
}

/* Map refresh presets, in frames (60 a second), slowest first; the pause
 * menu steps through them. map_interval in gba-ds.cfg can be anything. */
static const int mapRates[] = { 180, 90, 60, 30 };
#define N_MAP_RATES ((int) (sizeof(mapRates) / sizeof(*mapRates)))

static const char* map_rate_label(int frames) {
	switch (frames) {
	case 180: return "Map Refresh: Every 3 s";
	case 90: return "Map Refresh: Every 1.5 s";
	case 60: return "Map Refresh: Every 1 s";
	case 30: return "Map Refresh: Every 0.5 s";
	default: return "Map Refresh: Custom";
	}
}

static void draw_menu(void) {
	static SDL_Texture* shoulderTex[2][2]; /* L1, R1 prompts, per view */
	static int shoulderW, shoulderH;
	if (!shoulderTex[0][0] && !shoulderTex[0][1]) {
		ui_load_brand(BRAND_BTN_L1, shoulderTex[0], &shoulderW, &shoulderH);
		ui_load_brand(BRAND_BTN_R1, shoulderTex[1], &shoulderW, &shoulderH);
	}
	for (int s = 0; s < nScreens; ++s) ui_dim(s, 170);
	draw_battery();
	int scr = ui_touch_screen();
	float px = ui_px(scr);
	int cap = px * 11 / 2;   /* Futura capital height */
	int rad = px * 3;        /* box corner radius */
	if (achOpen) {
		SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
		ui_fill_round(scr, b, C_BTN, rad);
		ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
		ui_fill_round(scr, t, C_BTN_EDGE, rad);
		ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Achievements", (SDL_Color) { 20, 16, 36, 255 });
		ra_list_draw(scr, ach_area(scr));
		if (nScreens > 1) {
			int o = ui_other_screen();
			ui_label(o, screens[o].rect.w / 2, screens[o].rect.h / 2, (ui_px(o) + 1) * 7 * 34 / 35, "Paused", C_GOLD);
		}
		return;
	}
	if (cheatsOpen) {
		draw_cheats(scr);
		if (nScreens > 1) {
			int o = ui_other_screen();
			ui_label(o, screens[o].rect.w / 2, screens[o].rect.h / 2, (ui_px(o) + 1) * 7 * 34 / 35, "Paused", C_GOLD);
		}
		return;
	}
	for (int t = 0; t < N_PAGES; ++t) {
		SDL_Rect r = top_box(scr, t, N_PAGES);
		bool on = t == curPage;
		ui_fill_round(scr, r, on ? C_BTN_EDGE : C_BTN, rad);
		ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, cap, pageNames[t], on ? (SDL_Color) { 20, 16, 36, 255 } : C_DIS);
	}
	SDL_Rect t0 = top_box(scr, 0, N_PAGES), t1 = top_box(scr, N_PAGES - 1, N_PAGES);
	/* L1 / R1 button prompts beside the tabs */
	int gw = px * 25 / 2, gh = px * 8;
	SDL_Rect g0 = { t0.x - px * 9 - gw / 2, t0.y + (t0.h - gh) / 2, gw, gh };
	SDL_Rect g1 = { t1.x + t1.w + px * 9 - gw / 2, t1.y + (t1.h - gh) / 2, gw, gh };
	if (shoulderTex[0][0] || shoulderTex[0][1]) {
		ui_image(scr, shoulderTex[0], shoulderW, shoulderH, "stretch", &g0);
		ui_image(scr, shoulderTex[1], shoulderW, shoulderH, "stretch", &g1);
	} else {
		ui_text(scr, t0.x - px * 14, t0.y + (t0.h - px * 7) / 2, px, "L1", C_DIS);
		ui_text(scr, t1.x + t1.w + px * 3, t1.y + (t1.h - px * 7) / 2, px, "R1", C_DIS);
	}
	for (int i = 0; i < nMenuItems; ++i) {
		SDL_Rect r = item_rect(scr, i);
		bool sel = i == menuSel;
		if (sel) ui_fill_round(scr, (SDL_Rect) { r.x - px / 2, r.y - px / 2, r.w + px, r.h + px }, C_BTN_EDGE, rad + px / 2);
		ui_fill_round(scr, r, sel ? C_BTN_SEL : C_BTN, rad);
		bool en = item_enabled(menuItems[i]);
		const char* label = menuLabels[menuItems[i]];
		if (menuItems[i] == M_SCALE) label = scale_label(cfg->scale);
		if (menuItems[i] == M_MAPRATE) label = map_rate_label(cfg->mapInterval);
		if (menuItems[i] == M_LCD) label = cfg->lcdGrid ? "LCD Grid: On" : "LCD Grid: Off";
		if (menuItems[i] == M_FPS) label = cfg->showFps ? "Show FPS: On" : "Show FPS: Off";
		if (menuItems[i] == M_FRAMEBUF) label = cfg->frameBuffer ? "Frame Buffer: On" : "Frame Buffer: Off";
		if (menuItems[i] == M_MAPANIM) label = cfg->mapAnim ? "Map Animations: On" : "Map Animations: Off";
		if (menuItems[i] == M_REWIND) label = cfg->rewind ? "Rewind: On (Hold L2)" : "Rewind: Off";
		if (menuItems[i] == M_MATCH) label = cfg->matchDisplay ? "Match Display Rate: On" : "Match Display Rate: Off";
		if (menuItems[i] == M_LOGO) label = cfg->showLogo ? "No Second Screen: Show Logo" : "No Second Screen: Keep Blank";
		if (menuItems[i] == M_OVERLAY) {
			static const char* const ovl[4] = { "Overlays: None", "Overlays: Primary", "Overlays: Secondary", "Overlays: Both" };
			label = ovl[cfg->overlays & OVL_BOTH];
		}
		if (menuItems[i] == M_RESTART && restartArmed) label = "Tap Again to Restart";
		ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, cap, label, en ? C_TEXT : C_DIS);
	}
	if (menuNote[0]) {
		SDL_Rect last = item_rect(scr, nMenuItems - 1);
		ui_font_text(scr, TF_BOLD, screens[scr].rect.w / 2, last.y + last.h + px * 9, px * 5, menuNote, C_GOLD, 0);
	}
	if (nScreens > 1) {
		int o = ui_other_screen();
		ui_label(o, screens[o].rect.w / 2, screens[o].rect.h / 2, (ui_px(o) + 1) * 7 * 34 / 35, "Paused", C_GOLD);
	}
}

/* ---------------------------------------------------------------- helpers */

static int probes_hit(const uint32_t* fb, const Probe* p, int tol) {
	int n = 0;
	for (int i = 0; i < 8 && (p[i].x || p[i].y || p[i].rgb); ++i) {
		uint32_t c = fb[p[i].y * GBA_W + p[i].x];
		int dr = (int) (c & 0xFF) - (int) ((p[i].rgb >> 16) & 0xFF);
		int dg = (int) ((c >> 8) & 0xFF) - (int) ((p[i].rgb >> 8) & 0xFF);
		int db = (int) ((c >> 16) & 0xFF) - (int) (p[i].rgb & 0xFF);
		n += abs(dr) <= tol && abs(dg) <= tol && abs(db) <= tol;
	}
	return n;
}

/* Renders the current frame again without the profile's snap.hide sprites
 * into out, then puts the game back exactly as it was (state, picture,
 * sound). */
static void snapshot_hiding_sprites(uint32_t* out) {
	static void* st;
	static size_t stSize;
	static uint32_t raw[GBA_W * GBA_H];
	size_t sz = emu_state_size(&emu);
	if (sz > stSize) {
		free(st);
		st = malloc(sz);
		stSize = st ? sz : 0;
	}
	if (!st || !emu_state_save(&emu, st)) {
		memcpy(out, emu.fb, sizeof(raw));
		return;
	}
	memcpy(raw, emu.fb, sizeof(raw));
	const SpriteRule* hide = snapProf->snap.hide;
	int n = 0;
	while (n < 4 && hide[n].x1) ++n;
	emu_hide_sprites(&emu, hide, n);
	emu_state_load(&emu, st);
	emu_frame(&emu, 0);
	memcpy(out, emu.fb, sizeof(raw));
	emu_hide_sprites(&emu, NULL, 0);
	emu_state_load(&emu, st);
	int16_t drop[2048];
	while (emu_audio_read(&emu, drop, 1024) > 0) {}
	memcpy(emu.fb, raw, sizeof(raw));
}

/* Logo-only games: copy a recognised screen (THPS2 goal sheet) to the second
 * screen; back to the logo at the menus. */
/* The grabbed sky and scenery (the rows above the horizon), scaled up to
 * cover the canvas (centred), and dimmed well back. */
static void hud_make_backdrop(void) {
	int h = SDL_clamp(hudProf->hud.backdropRows ? hudProf->hud.backdropRows : GBA_H, 1, GBA_H);
	hudBackValid = true;
	if (debug) {
		char path[1200];
		snprintf(path, sizeof(path), "%s/hud-backdrop.bmp", cfg->stateDir);
		SDL_Surface* bs = SDL_CreateRGBSurfaceWithFormatFrom(hudGrab, GBA_W, GBA_H, 32, GBA_W * 4, SDL_PIXELFORMAT_XBGR8888);
		if (bs) SDL_SaveBMP(bs, path), SDL_FreeSurface(bs);
		fprintf(stderr, "frame %llu: backdrop from %d rows (%s)\n", (unsigned long long) frameCount, h, path);
	}
	if (!hudBackValid) return;
	float sc = SDL_max((float) HUD_CW / GBA_W, (float) HUD_CH / h);
	float ox = (GBA_W - HUD_CW / sc) / 2, oy = (h - HUD_CH / sc) / 2;
	for (int y = 0; y < HUD_CH; ++y) {
		float fy = oy + (y + 0.5f) / sc - 0.5f;
		int y0 = SDL_clamp((int) fy, 0, h - 1), y1 = SDL_min(y0 + 1, h - 1);
		float wy = SDL_clamp(fy - y0, 0.0f, 1.0f);
		for (int x = 0; x < HUD_CW; ++x) {
			float fx = ox + (x + 0.5f) / sc - 0.5f;
			int x0 = SDL_clamp((int) fx, 0, GBA_W - 1), x1 = SDL_min(x0 + 1, GBA_W - 1);
			float wx = SDL_clamp(fx - x0, 0.0f, 1.0f);
			uint32_t a = hudGrab[y0 * GBA_W + x0], b = hudGrab[y0 * GBA_W + x1];
			uint32_t c = hudGrab[y1 * GBA_W + x0], d = hudGrab[y1 * GBA_W + x1];
			uint32_t out = 0xFF000000;
			for (int sh = 0; sh < 24; sh += 8) {
				float top = ((a >> sh) & 0xFF) * (1 - wx) + ((b >> sh) & 0xFF) * wx;
				float bot = ((c >> sh) & 0xFF) * (1 - wx) + ((d >> sh) & 0xFF) * wx;
				float v = (top * (1 - wy) + bot * wy) * 0.32f; /* dimmed */
				out |= (uint32_t) v << sh;
			}
			hudBack[y * HUD_CW + x] = out;
		}
	}
}

/* The scene's backdrop: the game's logo on dark blue, as large as fits
 * (whole pixels for the extracted one), dimmed like the race backdrop. */
static void hud_make_logo_back(void) {
	hudLogoDone = true;
	for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudLogoBack[i] = 0xFF1A1010;
	bool pixelArt;
	SDL_Surface* lg = ui_logo_surface(cfg, emu.gameCode, &pixelArt);
	if (!lg) return;
	int mw = HUD_CW - 40, mh = HUD_CH - 40;
	int w = lg->w, h = lg->h;
	if (pixelArt) {
		int k = SDL_max(1, SDL_min(mw / lg->w, mh / lg->h));
		w = lg->w * k, h = lg->h * k;
	} else {
		float sc = SDL_min((float) mw / lg->w, (float) mh / lg->h);
		w = SDL_max(1, (int) (lg->w * sc)), h = SDL_max(1, (int) (lg->h * sc));
	}
	int x0 = (HUD_CW - w) / 2, y0 = (HUD_CH - h) / 2;
	for (int y = 0; y < h; ++y) {
		const uint8_t* row = (const uint8_t*) lg->pixels + (size_t) (y * lg->h / h) * lg->pitch;
		for (int x = 0; x < w; ++x) {
			const uint8_t* p = row + (size_t) (x * lg->w / w) * 4;
			uint32_t* o = &hudLogoBack[(y0 + y) * HUD_CW + x0 + x];
			uint32_t out = 0xFF000000;
			for (int c = 0; c < 3; ++c) {
				int bg = (*o >> (c * 8)) & 0xFF;
				int v = bg + (int) ((p[c] * 0.32f - bg) * p[3] / 255.0f);
				out |= (uint32_t) SDL_clamp(v, 0, 255) << (c * 8);
			}
			*o = out;
		}
	}
	SDL_FreeSurface(lg);
}

static int rule_count(const SpriteRule* r, int max) {
	int n = 0;
	while (n < max && (r[n].x1 || r[n].tileMax)) ++n;
	return n;
}

/* The second screen from this frame's HUD (hudPx). While racing, the turn
 * sign goes back onto the game picture and the map is drawn larger. */
static void hud_compose(void) {
	const typeof(hudProf->hud)* h = &hudProf->hud;
	uint32_t clear = emu_hud_clear();
	bool racing = !hudScene && memprobes_match(h->layout, 4, &emu);
	{
		static int lastRacing = -1;
		if (debug && racing != lastRacing) fprintf(stderr, "frame %llu: HUD layout %s\n", (unsigned long long) frameCount, racing ? "racing" : "plain");
		lastRacing = racing;
	}
	/* the map's box: grown over the race's first frames, then kept */
	if (racing && h->map.x1 && hudFrames <= 8) {
		for (int y = h->map.y0; y <= h->map.y1; ++y) {
			for (int x = h->map.x0; x <= h->map.x1; ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				if (!mapFound) mapX0 = mapX1 = x, mapY0 = mapY1 = y, mapFound = true;
				mapX0 = SDL_min(mapX0, x), mapX1 = SDL_max(mapX1, x);
				mapY0 = SDL_min(mapY0, y), mapY1 = SDL_max(mapY1, y);
			}
		}
	}
	bool bigMap = racing && mapFound;
	int bx0 = SDL_max(mapX0 - 2, h->map.x0), by0 = SDL_max(mapY0 - 2, h->map.y0);
	int bx1 = SDL_min(mapX1 + 2, h->map.x1), by1 = SDL_min(mapY1 + 2, h->map.y1);
	/* the sign: only when nothing else of the HUD is on its rows nearby */
	bool sign = false;
	if (racing && h->sign.x1) {
		bool inside = false, beside = false;
		for (int y = h->sign.y0; y <= h->sign.y1 && !beside; ++y) {
			for (int x = SDL_max(0, h->sign.x0 - 40); x <= SDL_min(GBA_W - 1, h->sign.x1 + 40); ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				if (x >= h->sign.x0 && x <= h->sign.x1) inside = true;
				else beside = true;
			}
		}
		sign = inside && !beside;
	}
	if (sign) {
		for (int y = h->sign.y0; y <= h->sign.y1; ++y) {
			for (int x = h->sign.x0; x <= h->sign.x1; ++x) {
				uint32_t c = hudPx[y * GBA_W + x];
				if ((c & 0xFFFFFF) != clear) emu.fb[y * GBA_W + x] = c;
			}
		}
	}
	if (hudScene) {
		if (!hudLogoDone) hud_make_logo_back();
		memcpy(hudCanvas, hudLogoBack, sizeof(hudCanvas));
	} else if (hudBackValid) {
		memcpy(hudCanvas, hudBack, sizeof(hudCanvas));
	} else {
		for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudCanvas[i] = 0xFF1A1010;
	}
	/* moved pieces (the scene's, or racing's): how far each goes this frame */
	const HudMove* moves = hudScene ? h->scene.moves : racing ? h->moves : NULL;
	int moveDx[4] = { 0 }, moveDy[4] = { 0 };
	bool moving[4] = { false };
	for (int m = 0; moves && m < 4; ++m) {
		const HudMove* mv = &moves[m];
		if (!mv->how && !mv->vhow) continue;
		int lo = GBA_W, hi = -1, top = GBA_H, bot = -1;
		for (int y = SDL_max(mv->y0, 0); y <= SDL_min(mv->y1, GBA_H - 1); ++y) {
			for (int x = SDL_max(mv->x0, 0); x <= SDL_min(mv->x1, GBA_W - 1); ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				lo = SDL_min(lo, x), hi = SDL_max(hi, x), top = SDL_min(top, y), bot = SDL_max(bot, y);
			}
		}
		if (hi < 0) continue;
		moving[m] = true;
		moveDx[m] = mv->how == 'C' ? mv->x - (lo + hi) / 2 : mv->how == 'R' ? mv->x - hi : mv->how == 'L' ? mv->x - lo : 0;
		moveDy[m] = mv->vhow == 'M' ? mv->y - (top + bot) / 2 : mv->vhow == 'B' ? mv->y - bot : mv->vhow == 'T' ? mv->y - top : 0;
	}
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = hudPx[y * GBA_W + x];
			if ((c & 0xFFFFFF) == clear) continue;
			int nx = x, ny = y;
			bool toGame = false;
			for (int m = 0; m < 4; ++m) {
				const HudMove* mv = &moves[m];
				if (moving[m] && x >= mv->x0 && x <= mv->x1 && y >= mv->y0 && y <= mv->y1) {
					nx = x + moveDx[m], ny = y + moveDy[m];
					toGame = mv->toGame;
					break;
				}
			}
			if (nx < 0 || nx >= GBA_W || ny < 0 || ny >= GBA_H) continue;
			if (toGame) {
				emu.fb[ny * GBA_W + nx] = c; /* back onto the game picture */
				continue;
			}
			if (sign && x >= h->sign.x0 && x <= h->sign.x1 && y >= h->sign.y0 && y <= h->sign.y1) continue;
			if (bigMap && x >= bx0 && x <= bx1 && y >= by0 && y <= by1) continue;
			uint32_t* o = &hudCanvas[ny * 2 * HUD_CW + nx * 2];
			o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = c;
		}
	}
	if (bigMap) {
		/* 2x if it fits comfortably right of the faces and below the top
		 * row (about 200 x 128 HUD pixels), else 1.5x; centred there */
		int w = bx1 - bx0 + 1, hh = by1 - by0 + 1;
		int q = (w * 2 <= 180 && hh * 2 <= 120) ? 4 : 3; /* canvas pixels per HUD pixel */
		int cx = 139 * 2, cy = 94 * 2;
		int tx = SDL_clamp(cx - w * q / 2, 0, HUD_CW - w * q), ty = SDL_clamp(cy - hh * q / 2, 0, HUD_CH - hh * q);
		for (int y = by0; y <= by1; ++y) {
			for (int x = bx0; x <= bx1; ++x) {
				uint32_t c = hudPx[y * GBA_W + x];
				if ((c & 0xFFFFFF) == clear) continue;
				for (int j = 0; j < q; ++j) {
					uint32_t* o = &hudCanvas[(ty + (y - by0) * q + j) * HUD_CW + tx + (x - bx0) * q];
					for (int i = 0; i < q; ++i) o[i] = c;
				}
			}
		}
	}
}

/* After each frame: show the HUD it drew, and decide whether the next
 * one is split. A frame is shown only if it was split and still looks like
 * racing (the first pause-menu frame is split too, and is dropped). */
static void update_hud(void) {
	const typeof(hudProf->hud)* h = &hudProf->hud;
	bool race = memprobes_match(h->on, 4, &emu);
	if (hudOn && race) {
		if (emu_hud_grabbed()) hud_make_backdrop(); /* (that frame drew no HUD: the last one is reused) */
		hud_compose();
		for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], hudCanvas, HUD_CW * 4);
		if (mapShownIdx < 0 && debug) fprintf(stderr, "frame %llu: HUD on the second screen\n", (unsigned long long) frameCount);
		mapShownIdx = 0;
	}
	bool scene = race && h->scene.when[0].addr && memprobes_match(h->scene.when, 2, &emu);
	if (race != hudOn || scene != hudScene) {
		const SpriteRule* objs = scene ? h->scene.objs : h->objs;
		emu_hud_split(&emu, race, h->bgMask, objs, rule_count(objs, 4), hudPx);
		if (debug && race && scene != hudScene) fprintf(stderr, "frame %llu: HUD scene %s\n", (unsigned long long) frameCount, scene ? "on" : "off");
		hudOn = race;
		hudScene = scene;
		hudFrames = 0;
		mapFound = false;
	}
	/* the course's backdrop, as each race starts (not for the scene) */
	if (hudOn && ++hudFrames == 2 && h->backdrop && !hudScene) emu_hud_grab(&emu, h->backdrop, hudGrab);
	if (!race && mapShownIdx >= 0 && !memprobes_match(h->keep, 4, &emu)) {
		mapShownIdx = -1; /* menus: the logo */
		if (debug) fprintf(stderr, "frame %llu: HUD gone, logo\n", (unsigned long long) frameCount);
	}
}

static void update_snapshot(void) {
	const typeof(snapProf->snap)* sn = &snapProf->snap;
	int tol = sn->tol ? sn->tol : 12;
	if (probes_hit(emu.fb, sn->show, tol) >= sn->showMin) {
		snapRevert = 0;
		if (++snapShow >= SDL_max(1, sn->stable)) {
			const uint32_t* px = emu.fb;
			if (sn->hide[0].x1) {
				/* only when the picture changes (the sheets are still) */
				static uint32_t last[GBA_W * GBA_H], clean[GBA_W * GBA_H];
				if (mapShownIdx < 0 || memcmp(last, emu.fb, sizeof(last))) {
					memcpy(last, emu.fb, sizeof(last));
					snapshot_hiding_sprites(clean);
					for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], clean, GBA_W * 4);
				}
				px = NULL;
			}
			if (px) for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], px, GBA_W * 4);
			if (mapShownIdx < 0 && debug) fprintf(stderr, "frame %llu: screen copied to the second screen\n", (unsigned long long) frameCount);
			mapShownIdx = 0;
		}
		return;
	}
	snapShow = 0;
	if (mapShownIdx < 0) return;
	bool menu = false;
	for (int k = 0; k < 2; ++k) {
		if (sn->revertMin[k] && probes_hit(emu.fb, sn->revert[k], tol) >= sn->revertMin[k]) menu = true;
	}
	if (!menu) {
		snapRevert = 0;
	} else if (++snapRevert >= SDL_max(1, sn->stable)) {
		mapShownIdx = -1;
		snapRevert = 0;
		if (debug) fprintf(stderr, "frame %llu: back at the menus, logo\n", (unsigned long long) frameCount);
	}
}

/* ---------------------------------------------------------- control scheme
 * Games with a four-face-button layout (profile `controls`): a picker on the
 * second screen (touch only) chooses the GBA layout, the four-face one, or
 * Auto (four-face while the player controls the character). Saved per game
 * in <state_dir>/<game>.controls. */
static int game_screen(void);
enum { CTL_GBA, CTL_FOUR, CTL_AUTO };
static const char* const ctlNames[] = { "gba", "four", "auto" };
static const MapProfile* ctlProf; /* NULL: no alternate layout for this game */
static int ctlMode;
static bool ctlPlaying; /* Auto: the player is controlling the character */
static char ctlPath[1400];

/* The game's own HUD is up (display mode and a sprite at each position). */
static bool ctl_playing_now(void) {
	const typeof(ctlProf->controls)* c = &ctlProf->controls;
	if ((emu_read16(&emu, 0x04000000) & 7) != c->mode) return false;
	int want = 0, found = 0;
	while (want < 4 && (c->hud[want].x || c->hud[want].y)) ++want;
	for (int i = 0; i < 128 && found != (1 << want) - 1; ++i) {
		uint16_t a0 = emu_read16(&emu, 0x07000000 + i * 8);
		if ((a0 & 0x0300) == 0x0200) continue; /* disabled */
		int y = a0 & 0xFF, x = emu_read16(&emu, 0x07000002 + i * 8) & 0x1FF;
		for (int k = 0; k < want; ++k) {
			if (x == c->hud[k].x && y == c->hud[k].y) found |= 1 << k;
		}
	}
	return want && found == (1 << want) - 1;
}

/* tiles in the picker: Auto only where the profile can tell when the player
 * is skating (its display mode and HUD sprites) */
static int ctl_tiles(void) {
	return ctlProf && (ctlProf->controls.hud[0].x || ctlProf->controls.hud[0].y) ? 3 : 2;
}

static bool ctl_four(void) {
	return ctlProf && (ctlMode == CTL_FOUR || (ctlMode == CTL_AUTO && ctlPlaying));
}

static void ctl_load(void) {
	ctlMode = CTL_GBA;
	ctlPlaying = false;
	snprintf(ctlPath, sizeof(ctlPath), "%s/%s.controls", cfg->stateDir, gp.base);
	FILE* f = fopen(ctlPath, "r");
	if (!f) return;
	char w[16] = "";
	if (fscanf(f, "%15s", w) == 1) {
		for (int i = 0; i < 3; ++i) {
			if (!strcmp(w, ctlNames[i])) ctlMode = i;
		}
	}
	fclose(f);
	if (ctlMode >= ctl_tiles()) ctlMode = CTL_GBA;
}

static void ctl_save(void) {
	FILE* f = fopen(ctlPath, "w");
	if (!f) return;
	fprintf(f, "%s\n", ctlNames[ctlMode]);
	fclose(f);
}

/* the picker is on the second screen, when that's the touchscreen */
static bool ctl_shown(int s) {
	return ctlProf && cfg->map && nScreens == 2 && s == ui_touch_screen() && s != game_screen();
}

typedef struct {
	SDL_Rect tile[3];
	SDL_Rect all; /* label and tiles: taps here never open the menu */
	int labelX, labelY, cap;
} CtlGeom;

/* A row along the bottom: "Controls" and three square tiles, in the band
 * below the picture when there's room for it, else over the picture. */
static CtlGeom ctl_geom(int s) {
	CtlGeom g;
	SDL_Rect r = screens[s].rect;
	float u = ui_px(s);
	int t = r.h * 23 / 200, gap = u * 3;
	g.cap = t / 5;
	int lw = ui_label_width(s, g.cap, "Controls");
	int n = ctl_tiles();
	int total = lw + gap * 3 + t * n + gap * (n - 1);
	int x = (r.w - total) / 2;
	SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, cfg->mapScale);
	int below = r.h - (img.y + img.h);
	int y = below >= t + u * 2 ? img.y + img.h + (below - t) / 2 : r.h - t - u * 2;
	g.all = (SDL_Rect) { x - gap, y - u, total + gap * 2, t + u * 2 };
	g.labelX = x + lw / 2;
	g.labelY = y + t / 2;
	x += lw + gap * 3;
	for (int i = 0; i < 3; ++i) g.tile[i] = (SDL_Rect) { x + i * (t + gap), y, i < n ? t : 0, i < n ? t : 0 };
	return g;
}

static void ctl_button(int s, int id, const SDL_Rect* t, int cx, int cy, int w) {
	static SDL_Texture* tex[BRAND_COUNT][2];
	static int tw[BRAND_COUNT], th[BRAND_COUNT];
	if (!tex[id][0] && !tex[id][1]) ui_load_brand(id, tex[id], &tw[id], &th[id]);
	if (!tw[id]) return;
	int h = w * th[id] / tw[id];
	SDL_Rect a = { t->x + t->w * cx / 100 - w / 2, t->y + t->h * cy / 100 - h / 2, w, h };
	ui_image(s, tex[id], tw[id], th[id], "fit", &a);
}

static void ctl_draw(int s) {
	CtlGeom g = ctl_geom(s);
	float u = ui_px(s);
	int active = ctl_four() ? CTL_FOUR : CTL_GBA;
	ui_fill_round(s, g.all, (SDL_Color) { 0, 0, 0, 150 }, u * 4);
	ui_label(s, g.labelX, g.labelY, g.cap, "Controls", C_TEXT);
	for (int i = 0; i < ctl_tiles(); ++i) {
		SDL_Rect t = g.tile[i];
		int rad = t.w / 6;
		bool sel = ctlMode == i;
		/* chosen: filled; in Auto, the layout in use right now: outlined */
		if (sel || (ctlMode == CTL_AUTO && i == active)) {
			ui_fill_round(s, t, C_BTN_EDGE, rad);
			int e = sel ? u : SDL_max(1, u / 2);
			t = (SDL_Rect) { t.x + e, t.y + e, t.w - e * 2, t.h - e * 2 };
		}
		ui_fill_round(s, t, sel ? C_BTN_SEL : C_BTN, rad);
		t = g.tile[i];
		int d = t.w * 30 / 100;
		if (i == CTL_GBA) {
			ctl_button(s, BRAND_PILL_L, &t, 32, 27, t.w * 34 / 100);
			ctl_button(s, BRAND_PILL_R, &t, 68, 27, t.w * 34 / 100);
			d = t.w * 28 / 100;
			ctl_button(s, BRAND_BTN_B, &t, 34, 69, d);
			ctl_button(s, BRAND_BTN_A, &t, 66, 60, d);
		} else if (i == CTL_FOUR) {
			d = t.w * 28 / 100;
			ctl_button(s, BRAND_BTN_A, &t, 50, 27, d);
			ctl_button(s, BRAND_BTN_L, &t, 27, 50, d);
			ctl_button(s, BRAND_BTN_R, &t, 73, 50, d);
			ctl_button(s, BRAND_BTN_B, &t, 50, 73, d);
		} else {
			ui_label(s, t.x + t.w / 2, t.y + t.h / 2, g.cap, "Auto", C_TEXT);
		}
	}
}

/* A tap on the picker: true if it was there (it then doesn't open the menu). */
static bool ctl_tap(int s, int x, int y) {
	if (!ctl_shown(s)) return false;
	CtlGeom g = ctl_geom(s);
	SDL_Point pt = { x, y };
	if (!SDL_PointInRect(&pt, &g.all)) return false;
	for (int i = 0; i < ctl_tiles(); ++i) {
		if (SDL_PointInRect(&pt, &g.tile[i]) && ctlMode != i) {
			ctlMode = i;
			ctl_save();
			if (debug) fprintf(stderr, "frame %llu: controls %s\n", (unsigned long long) frameCount, ctlNames[i]);
		}
	}
	return true;
}

static void save_thumbs(void) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(emu.fb, GBA_W, GBA_H, 32, GBA_W * 4, SDL_PIXELFORMAT_XBGR8888);
	if (s) {
		SDL_SaveBMP(s, gp.thumb);
		SDL_FreeSurface(s);
	}
	const uint32_t* still = stillValid ? stillPx : mapRes.frames[0];
	if (mapShownIdx >= 0 && still) {
		s = SDL_CreateRGBSurfaceWithFormatFrom((void*) still, mapW, mapH, 32, mapW * 4, SDL_PIXELFORMAT_XBGR8888);
		if (s) {
			SDL_SaveBMP(s, gp.mapThumb);
			SDL_FreeSurface(s);
		}
	}
}

/* Save states, with the RetroAchievements progress beside them. */
static bool state_save(const char* path) {
	bool ok = emu_state_save_file(&emu, path);
	if (ok) ra_state_saved(path);
	return ok;
}

static bool state_load(const char* path) {
	bool ok = emu_state_load_file(&emu, path);
	if (ok) ra_state_loaded(path);
	if (ok) aheadReady = false; /* a frame emulated before the load: emulate afresh */
	return ok;
}

static void auto_save(void) {
	if (!cfg->autoResume) return;
	if (state_save(gp.autoState)) {
		save_thumbs();
		fprintf(stderr, "auto-saved %s\n", gp.autoState);
	} else {
		fprintf(stderr, "auto-save failed\n");
	}
}

/* The app is being put in the background (Android may kill it there):
 * keep a resume point. Called between frames, from ui_events. */
static void on_background(void) {
	auto_save();
}

static void open_menu(void);

/* Back from Home / the app switcher: paused, rather than straight into the action. */
static void on_foreground(void) {
	if (!menuOpen) open_menu();
}

/* generated surround (built-in overlay), see mapframe.c */
static MapFrame mf;
static SDL_Texture* canvasTex[2];
static int canvasTexW, canvasTexH;
static bool canvasDirty;
static char framePath[1200];


static const uint32_t* map_px(int idx) {
	return idx == STILL_IDX ? (stillValid ? stillPx : NULL) : mapRes.frames[idx];
}

static void upload_map_frame(int idx) {
	const uint32_t* px = map_px(idx);
	if (idx == mapShownIdx || !px) return;
	for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], px, mapW * 4);
	mapShownIdx = idx;
	canvasDirty = true;
}

static int cmp_u32(const void* a, const void* b) {
	uint32_t x = *(const uint32_t*) a, y = *(const uint32_t*) b;
	return x < y ? -1 : x > y;
}

/* Builds the still map for "Map Animations: Off" into out (w * h), and marks
 * the pixels that animate in mask. Each animated spot (pixels that change,
 * grouped when they're within a few pixels of each other) is taken from the
 * frame where it is most visible: its brightest state (a marker that blinks
 * on and off, a pulsing glow at its peak), or with STILL_COVER the state that
 * covers the most pixels unlike the spot's surroundings, ties going to the
 * one with the most contrast. */
static void compose_still(const MapResult* r, int pick, uint32_t* out, unsigned char* mask) {
	int n = r->nFrames, w = r->w, h = r->h, wh = w * h;
	memcpy(out, r->frames[0], (size_t) wh * 4);
	memset(mask, 0, (size_t) wh);
	if (n < 2) return;
	for (int i = 0; i < wh; ++i) {
		uint32_t p = r->frames[0][i] & 0xFFFFFF;
		for (int f = 1; f < n && !mask[i]; ++f) mask[i] = (r->frames[f][i] & 0xFFFFFF) != p;
	}
	int* label = calloc((size_t) wh, sizeof(int));
	int* queue = malloc((size_t) wh * sizeof(int));
	if (!label || !queue) {
		free(label);
		free(queue);
		return;
	}
	static uint32_t sample[4096];
	const int R = 3; /* animated pixels this close belong to the same spot */
	int spots = 0;
	for (int seed = 0; seed < wh; ++seed) {
		if (!mask[seed] || label[seed]) continue;
		/* flood-fill one spot; list its pixels in queue[0..len) */
		int id = ++spots, len = 0, head = 0;
		int x0 = w, y0 = h, x1 = 0, y1 = 0;
		label[seed] = id;
		queue[len++] = seed;
		while (head < len) {
			int p = queue[head++], px = p % w, py = p / w;
			x0 = SDL_min(x0, px), x1 = SDL_max(x1, px), y0 = SDL_min(y0, py), y1 = SDL_max(y1, py);
			for (int dy = -R; dy <= R; ++dy) {
				for (int dx = -R; dx <= R; ++dx) {
					int nx = px + dx, ny = py + dy;
					if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
					int q = ny * w + nx;
					if (mask[q] && !label[q]) label[q] = id, queue[len++] = q;
				}
			}
		}
		x0 = SDL_max(x0 - 2, 0), y0 = SDL_max(y0 - 2, 0), x1 = SDL_min(x1 + 2, w - 1), y1 = SDL_min(y1 + 2, h - 1);
		/* the surroundings' colour: most common still pixel around the spot */
		uint32_t bg = 0;
		if (pick == STILL_COVER) {
			int ns = 0;
			for (int y = y0; y <= y1; ++y) {
				for (int x = x0; x <= x1 && ns < (int) SDL_arraysize(sample); ++x) {
					if (!mask[y * w + x]) sample[ns++] = r->frames[0][y * w + x] & 0xFFFFFF;
				}
			}
			qsort(sample, ns, sizeof(sample[0]), cmp_u32);
			for (int i = 0, run = 0, top = 0; i < ns; ++i) {
				run = i && sample[i] == sample[i - 1] ? run + 1 : 1;
				if (run > top) top = run, bg = sample[i];
			}
		}
		int best = 0;
		long bestA = -1, bestB = -1;
		for (int f = 0; f < n; ++f) {
			const uint32_t* fr = r->frames[f];
			long a = 0, b = 0;
			for (int i = 0; i < len; ++i) {
				uint32_t p = fr[queue[i]];
				if (pick == STILL_COVER) a += (p & 0xFFFFFF) != bg;
				else a += (p & 0xFF) + ((p >> 8) & 0xFF) + ((p >> 16) & 0xFF);
			}
			if (pick == STILL_COVER) {
				for (int y = y0; y <= y1; ++y) {
					for (int x = x0; x <= x1; ++x) {
						uint32_t p = fr[y * w + x];
						if (x < x1) {
							uint32_t q = fr[y * w + x + 1];
							for (int c = 0; c < 24; c += 8) b += abs((int) ((p >> c) & 0xFF) - (int) ((q >> c) & 0xFF));
						}
						if (y < y1) {
							uint32_t q = fr[(y + 1) * w + x];
							for (int c = 0; c < 24; c += 8) b += abs((int) ((p >> c) & 0xFF) - (int) ((q >> c) & 0xFF));
						}
					}
				}
			}
			if (a > bestA || (a == bestA && b > bestB)) best = f, bestA = a, bestB = b;
		}
		for (int i = 0; i < len; ++i) out[queue[i]] = r->frames[best][queue[i]];
	}
	free(label);
	free(queue);
}

/* After a fresh capture: rebuild the still map. A capture that only differs
 * from the one on screen where the markers animate (a pulse caught at
 * another point of its loop) keeps the old still, so it doesn't jump around
 * every time the map is refreshed. Returns true if the still changed. */
static bool stillStale; /* stillPx is from an older map (built only when it's shown) */

static bool update_still(void) {
	int w = mapRes.w, h = mapRes.h, wh = w * h;
	uint32_t* px = malloc((size_t) wh * 4);
	unsigned char* mask = malloc((size_t) wh);
	if (!px || !mask) {
		free(px);
		free(mask);
		return false;
	}
	const MapProfile* prof = mc ? mapcap_profile(mc) : NULL;
	compose_still(&mapRes, prof ? (int) prof->stillPick : STILL_BRIGHT, px, mask);
	if (stillValid && stillW == w && stillH == h) {
		bool same = true;
		for (int i = 0; i < wh && same; ++i) same = px[i] == stillPx[i] || mask[i];
		if (same) {
			free(px);
			free(mask);
			return false;
		}
	}
	free(stillPx);
	free(mask);
	stillPx = px, stillW = w, stillH = h, stillValid = true;
	return true;
}

/* Draws the map with its generated surround; false if there's none to draw. */
static bool draw_framed_map(int s) {
	if (!(cfg->overlays & OVL_SECONDARY) || !mapframe_ready(&mf) || mapShownIdx < 0) return false;
	if (!strcmp(cfg->mapScale, "zoom")) return false; /* the map fills the height: no room for a surround */
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
		const uint32_t* px = mapframe_compose(&mf, map_px(mapShownIdx));
		for (int i = 0; i < nViews; ++i) ui_update_texture(canvasTex[i], px, mf.cw * 4);
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

/* Frame pacing. The GBA runs at 59.73 frames a second; a 60 Hz screen then
 * shows one frame twice about every 3.7 s (a small, regular stutter).
 * "Match Display Rate" runs the game at the display's rate instead when it's
 * within 1.5% (60 Hz: 0.46% faster, sound resampled to match, a pitch change
 * nobody hears), and presents the game's screen in step with the display
 * (vsync), so each frame gets exactly one refresh. */
static double frameSec = 1.0 / GBA_FPS;
static double pacedHz;    /* the display's rate when matched, else 0 */
static double audioSpeed = 1.0; /* emulated time per real time */

static void pacing_update(void) {
	double hz = nScreens > 0 ? ui_display_hz(screens[game_screen()].view) : 0;
	pacedHz = 0;
	if (cfg->matchDisplay && hz > 0 && fabs(hz / GBA_FPS - 1) <= 0.015) pacedHz = hz;
	frameSec = 1.0 / (pacedHz > 0 ? pacedHz : GBA_FPS);
	audioSpeed = pacedHz > 0 ? pacedHz / GBA_FPS : 1.0;
	fprintf(stderr, "frame pacing: display %.3f Hz, game %.3f frames a second (%s)\n", hz, 1.0 / frameSec,
	        pacedHz > 0 ? "matched to the display" : cfg->matchDisplay ? "display too far off to match" : "the GBA's rate");
}

/* Rewind (hold L2): mGBA's rewind buffer, a state every second frame,
 * rewind_seconds long (default 30 s: 900 states, each kept as its
 * difference from the next, a few MB). */
static int rewindTick;
static bool wasRewinding;

static void rewind_setup(void) {
	emu_rewind_deinit();
	rewindTick = 0;
	if (!cfg->rewind || !emu.core) return;
	int secs = cfg->rewindSeconds < 5 ? 5 : cfg->rewindSeconds > 300 ? 300 : cfg->rewindSeconds;
	emu_rewind_init(&emu, (size_t) secs * 30);
	fprintf(stderr, "rewind: %d s (%d states)\n", secs, secs * 30);
}

/* Display choices made in the menu are remembered for next time. */
static void save_prefs(void) {
	char prefs[1100];
	snprintf(prefs, sizeof(prefs), "%s/prefs.cfg", cfg->stateDir);
	FILE* f = fopen(prefs, "w");
	if (f) {
		fprintf(f, "# written by gba-ds's pause menu; overrides gba-ds.cfg\n");
		fprintf(f, "swap = %d\nscale = %s\nmap_scale = %s\nlcd_grid = %d\nshow_fps = %d\noverlays = %s\nmap_animations = %d\nmap_interval = %d\nframe_buffer = %d\nshow_logo = %d\nmatch_display = %d\nrewind = %d\n",
		        cfg->swap ? 1 : 0, cfg->scale, cfg->mapScale, cfg->lcdGrid ? 1 : 0, cfg->showFps ? 1 : 0,
		        (const char* const[]) { "none", "primary", "secondary", "both" }[cfg->overlays & OVL_BOTH],
		        cfg->mapAnim ? 1 : 0, cfg->mapInterval, cfg->frameBuffer ? 1 : 0, cfg->showLogo ? 1 : 0, cfg->matchDisplay ? 1 : 0, cfg->rewind ? 1 : 0);
		fclose(f);
	}
}

static bool gameTexAll = true; /* upload the game's frame to every view next time */

static void swap_screens(void) {
	/* the game moves to the other view, whose copy of the frame may be old
	 * (frames go only where the game is shown; the menu uploads none) */
	if (emu.fb) {
		for (int i = 0; i < nViews; ++i) ui_update_texture(gameTex[i], emu.fb, GBA_W * 4);
	}
	if (nScreens < 2) {
		singleShowsMap = !singleShowsMap;
		return;
	}
	cfg->swap = !cfg->swap;
	save_prefs();
}

static SDL_Color borderColor;

/* User overlay PNGs in <asset_dir>/overlays/, drawn over a whole screen
 * (transparent where the picture should show). First found wins:
 *   primary (game screen):  <CODE>-primary-<scale>, <CODE>-primary,
 *                           primary-<scale>, primary
 *   secondary (map screen): <CODE>-secondary-<scale>, <CODE>-secondary,
 *                           <CODE>-<scale>, <CODE>, secondary-<scale>, secondary
 * <scale> is that screen's scaling (integer / fit / stretch). */
static void load_overlay(int which) {
	const char* scale = which ? cfg->mapScale : cfg->scale;
	if (!strcmp(overlayMode[which], scale)) return;
	snprintf(overlayMode[which], sizeof(overlayMode[which]), "%s", scale);
	ui_free_image(overlayTex[which]);
	const char* side = which ? "secondary" : "primary";
	char names[6][64];
	int n = 0;
	snprintf(names[n++], 64, "%.4s-%s-%s", emu.gameCode, side, scale);
	snprintf(names[n++], 64, "%.4s-%s", emu.gameCode, side);
	if (which) {
		snprintf(names[n++], 64, "%.4s-%s", emu.gameCode, scale); /* older names */
		snprintf(names[n++], 64, "%.4s", emu.gameCode);
	}
	snprintf(names[n++], 64, "%s-%s", side, scale);
	snprintf(names[n++], 64, "%s", side);
	char path[1200];
	for (int i = 0; i < n; ++i) {
		snprintf(path, sizeof(path), "%s/overlays/%s.png", cfg->assetDir, names[i]);
		if (ui_load_image(path, overlayTex[which], &overlayW[which], &overlayH[which], true)) {
			if (debug) fprintf(stderr, "overlay (%s): %s\n", side, path);
			return;
		}
	}
}

static void draw_overlay(int s, int which) {
	load_overlay(which);
	if (overlayTex[which][0] || overlayTex[which][1]) {
		ui_image(s, overlayTex[which], overlayW[which], overlayH[which], "stretch", NULL);
	}
}


/* Text size for the FPS counter and messages: fits the empty band (gap px tall)
 * above or below the picture, even in full-width scaling. */
static int small_text_px(int s, int gap) {
	return SDL_max(1, SDL_min(SDL_min(ui_px(s) - 1, 3), (gap - 6) / 7)); /* 7 rows tall, 3px margin each side */
}

/* Messages ("RESUMED", "STATE SAVED"...): bottom left of the touchscreen (the
 * bottom screen), in the band below the picture, at the FPS counter's size. */
static bool shotPending; /* Take Screenshot: capture the next frame drawn */

static void draw_osd(void) {
	const char* osd = ui_osd_text();
	if (!osd || menuOpen || shotPending) return;
	int s = ui_touch_screen();
	SDL_Rect r = screens[s].rect;
	bool gameHere = nScreens == 1 || s == game_screen() || !((mc || logoOnly) && cfg->map);
	SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, gameHere ? cfg->scale : cfg->mapScale);
	int gap = r.h - (img.y + img.h);
	int fpx = small_text_px(s, img.y < gap ? img.y : gap);
	int fy = gap >= 7 * fpx + 2 ? img.y + img.h + (gap - 7 * fpx) / 2 : r.h - fpx * 9;
	int fx = img.x + fpx * 2, cy = fy + fpx * 7 / 2, cap = fpx * 6;
	ui_font_text(s, TF_BOLD, fx + SDL_max(1, fpx / 2), cy + SDL_max(1, fpx / 2), cap, osd, (SDL_Color) { 0, 0, 0, 255 }, -1);
	ui_font_text(s, TF_BOLD, fx, cy, cap, osd, C_TEXT, -1);
}

/* The game's logo as a GBA screen would show it: centred on black in a
 * 240x160 picture, drawn with the second screen's scaling and the LCD grid.
 * Logos extracted from the ROM are at GBA resolution; a user's own logo
 * that's bigger stays a high-resolution image (logoCanvasTex NULL). */
static void make_logo_canvas(void) {
	ui_free_image(logoCanvasTex);
	bool pixelArt = false;
	SDL_Surface* lg = ui_logo_surface(cfg, emu.gameCode, &pixelArt);
	if (!lg) return;
	if (lg->w > GBA_W || lg->h > GBA_H) {
		SDL_FreeSurface(lg);
		return;
	}
	static uint32_t px[GBA_W * GBA_H];
	memset(px, 0, sizeof(px));
	int ox = (GBA_W - lg->w) / 2, oy = (GBA_H - lg->h) / 2;
	SDL_LockSurface(lg);
	for (int y = 0; y < lg->h; ++y) {
		const uint8_t* row = (const uint8_t*) lg->pixels + y * lg->pitch;
		for (int x = 0; x < lg->w; ++x) {
			const uint8_t* p = row + x * 4; /* RGBA32: bytes R, G, B, A */
			unsigned a = p[3];
			unsigned r = p[0] * a / 255, gc = p[1] * a / 255, b = p[2] * a / 255;
			px[(oy + y) * GBA_W + ox + x] = 0xFF000000u | b << 16 | gc << 8 | r; /* ABGR8888 */
		}
	}
	SDL_UnlockSurface(lg);
	SDL_FreeSurface(lg);
	for (int i = 0; i < nViews; ++i) {
		logoCanvasTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
		if (!logoCanvasTex[i]) continue;
		SDL_SetTextureBlendMode(logoCanvasTex[i], SDL_BLENDMODE_NONE);
		ui_update_texture(logoCanvasTex[i], px, GBA_W * 4);
	}
}

static void draw_game(void) {
	bool mapOn = (mc || logoOnly) && cfg->map; /* logo-only games: the logo stands in for the map */
	for (int s = 0; s < nScreens; ++s) {
		bool showGame = nScreens == 1 ? !(singleShowsMap && mapOn) : (s == game_screen() || !mapOn);
		SDL_Rect r = screens[s].rect;
		if (showGame) {
			if (nScreens == 2 && !mapOn && s != game_screen()) continue; /* no map: leave other screen black */
			ui_image_pixels(s, gameTex, GBA_W, GBA_H, cfg->scale, cfg->lcdGrid);
			if (cfg->overlays & OVL_PRIMARY) draw_overlay(s, 0);
			if (cfg->showFps) {
				/* sized to sit in the empty band above the picture */
				char buf[48];
				snprintf(buf, sizeof(buf), "%.1f FPS  %d%%", fps, (int) (speedPct + 0.5));
				SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, cfg->scale);
				int gap = img.y;
				int fpx = small_text_px(s, gap);
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
			ui_image_pixels(s, mapTex, mapW, mapH, cfg->mapScale, cfg->lcdGrid ? (hudProf ? 2 : 1) : 0); /* the HUD picture is at 2x: a cell per GBA pixel */
		} else if (!cfg->showLogo) {
			/* nothing to show: the screen stays blank */
		} else if (logoCanvasTex[0] || logoCanvasTex[1]) {
			/* no map yet (title screen, intro...): the game's logo, as if the
			 * game drew it on a second GBA screen */
			ui_image_pixels(s, logoCanvasTex, GBA_W, GBA_H, cfg->mapScale, cfg->lcdGrid ? 1 : 0);
		} else if (logoTex[0] || logoTex[1]) {
			/* a user's high-resolution logo: fitted to where the picture goes */
			SDL_Rect area = ui_image_rect(s, GBA_W, GBA_H, cfg->mapScale);
			ui_image(s, logoTex, logoW, logoH, logoPixel ? "integer" : "fit", &area);
		}
		if (mapShownIdx >= 0 && (cfg->overlays & OVL_SECONDARY)) { /* the map only, never the logo */
			draw_overlay(s, 1);
		}
		if (mapShownIdx >= 0 && mapInactive) {
			ui_dim(s, 120);
		}
		if (ctl_shown(s)) ctl_draw(s);
	}
	draw_osd();
}

static void close_ach(void) {
	if (achOpen) ra_list_close();
	achOpen = false;
}

/* the list's area: below the Back / title boxes */
static SDL_Rect ach_area(int scr) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	return (SDL_Rect) { px * 6, px * 25, r.w - px * 12, r.h - px * 27 };
}

static void open_menu(void) {
	cheatsOpen = false;
	close_ach();
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
	case M_SHOT:
		menuOpen = false; /* taken from the next frame, without the menu */
		shotPending = true;
		break;
	case M_ACH:
		ra_list_open();
		achOpen = true;
		achPressed = false;
		break;
	case M_CHEATS:
		if (!cheats.n) snprintf(menuNote, sizeof(menuNote), "No cheats found (see the README)");
		else open_cheats();
		break;
	case M_SWAP:
		swap_screens();
		snprintf(menuNote, sizeof(menuNote), "Screens swapped");
		break;
	case M_SCALE: {
		/* Pixel Perfect, Full Width, Zoom, Stretch */
		const char* next = !strcmp(cfg->scale, "integer") ? "fit" : !strcmp(cfg->scale, "fit") ? "zoom" :
		                   !strcmp(cfg->scale, "zoom")    ? "stretch" : "integer";
		snprintf(cfg->scale, sizeof(cfg->scale), "%s", next);
		snprintf(cfg->mapScale, sizeof(cfg->mapScale), "%s", next);
		save_prefs();
		break;
	}
	case M_MAPRATE: {
		/* the next preset up the list (a custom map_interval joins it in order) */
		int next = mapRates[0];
		for (int i = 0; i < N_MAP_RATES; ++i) {
			if (mapRates[i] < cfg->mapInterval) {
				next = mapRates[i];
				break;
			}
		}
		cfg->mapInterval = next;
		save_prefs();
		break;
	}
	case M_LCD:
		cfg->lcdGrid = !cfg->lcdGrid;
		save_prefs();
		break;
	case M_SAVE:
		if (state_save(gp.state)) {
			ui_osd("State saved");
			menuOpen = false;
		} else {
			snprintf(menuNote, sizeof(menuNote), "Save failed");
		}
		break;
	case M_LOAD:
		if (!item_enabled(M_LOAD)) {
			snprintf(menuNote, sizeof(menuNote), "No saved state yet");
		} else if (state_load(gp.state)) {
			ui_osd("State loaded");
			menuOpen = false;
			return false;
		} else {
			snprintf(menuNote, sizeof(menuNote), "Load failed");
		}
		break;
	case M_OVERLAY:
		/* PRIMARY -> SECONDARY -> BOTH -> NONE -> PRIMARY */
		cfg->overlays = cfg->overlays == OVL_PRIMARY ? OVL_SECONDARY : cfg->overlays == OVL_SECONDARY ? OVL_BOTH :
		                cfg->overlays == OVL_BOTH ? 0 : OVL_PRIMARY;
		save_prefs();
		break;
	case M_FPS:
		cfg->showFps = !cfg->showFps;
		save_prefs();
		break;
	case M_FRAMEBUF:
		cfg->frameBuffer = !cfg->frameBuffer;
		snprintf(menuNote, sizeof(menuNote), "%s", cfg->frameBuffer ? "Smoother, one frame more input delay" : "Least input delay");
		save_prefs();
		break;
	case M_REWIND:
		cfg->rewind = !cfg->rewind;
		rewind_setup();
		snprintf(menuNote, sizeof(menuNote), "%s", cfg->rewind ? "Hold L2 to go back in time" : "No rewind buffer: a little less work");
		save_prefs();
		break;
	case M_MATCH:
		cfg->matchDisplay = !cfg->matchDisplay;
		pacing_update();
		if (!cfg->matchDisplay) snprintf(menuNote, sizeof(menuNote), "The GBA's own %.2f frames a second", GBA_FPS);
		else if (pacedHz > 0) snprintf(menuNote, sizeof(menuNote), "%.2f frames a second, %.1f%% faster", pacedHz, (pacedHz / GBA_FPS - 1) * 100);
		else snprintf(menuNote, sizeof(menuNote), "The display's rate isn't close enough to match");
		save_prefs();
		break;
	case M_LOGO:
		cfg->showLogo = !cfg->showLogo;
		save_prefs();
		break;
	case M_MAPANIM:
		cfg->mapAnim = !cfg->mapAnim;
		if (!cfg->mapAnim && stillStale && mapRes.nFrames > 0) {
			update_still(); /* skipped while animations were on */
			stillStale = false;
		}
		if (mc && mapShownIdx >= 0) {
			mapAnimStart = frameCount;
			upload_map_frame(cfg->mapAnim ? 0 : STILL_IDX);
		}
		if (mc) mapcap_set_still(mc, !cfg->mapAnim);
		save_prefs();
		break;
	case M_RESTART:
		if (!restartArmed) {
			restartArmed = true;
			snprintf(menuNote, sizeof(menuNote), "Unsaved progress will be lost");
			break;
		}
		emu_reset(&emu);
		aheadReady = false;
		ra_reset();
		mapShownIdx = -1; /* back to the logo until the next map */
		restartArmed = false;
		menuOpen = false;
		ui_osd("Game restarted");
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

/* Saves what's on the screens now (before ui_present) as
 * <screenshot_dir>/<game>-YYYYMMDD-HHMMSS.png, screens stacked top to bottom. */
static void take_screenshot(void) {
	char dir[600], path[1400];
	snprintf(dir, sizeof(dir), "%s", cfg->screenshotDir);
	mkdirs(dir);
	struct stat st;
	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		snprintf(dir, sizeof(dir), "%s/screenshots", cfg->stateDir);
		mkdirs(dir);
	}
	time_t now = time(NULL);
	struct tm tmv;
	localtime_r(&now, &tmv);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
	snprintf(path, sizeof(path), "%s/%s-%s.png", dir, gp.base, stamp);
	for (int n = 2; file_exists(path) && n < 100; ++n) snprintf(path, sizeof(path), "%s/%s-%s-%d.png", dir, gp.base, stamp, n);
	ui_osd(ui_screenshot_stacked(path) ? "Screenshot saved" : "Screenshot failed");
}

/* ---------------------------------------------------------------- session */

enum GameExit game_run(Config* c, const char* rom, bool fromLibrary) {
	cfg = c;
	debug = getenv("GBADS_DEBUG") != NULL;
	game_paths(cfg, rom, &gp);
	if (!emu_open(&emu, rom, gp.sav, true)) {
		ui_osd("Couldn't load the game");
		return fromLibrary ? GAME_TO_LIBRARY : GAME_QUIT_APP;
	}
	fprintf(stderr, "game %s (%s), save %s\n", gp.base, emu.gameCode, gp.sav);
	ra_game_start(&emu, rom);
	cheats_load(&cheats, cfg, rom, emu.gameCode, gp.cheats);
	if (cheats_on(&cheats)) cheats_apply(&cheats, &emu);
	cheatsOpen = false;
	ui_on_background(on_background);
	ui_on_foreground(on_foreground);

	mc = NULL;
	gameTexAll = true;
	mapW = GBA_W;
	mapH = GBA_H;
	memset(&mapRes, 0, sizeof(mapRes));
	mapShownIdx = -1;
	stillValid = false;
	mapInactive = false;
	frameCount = 0;
	singleShowsMap = false;
	menuOpen = false;
	logoOnly = false;
	snapProf = NULL;
	hudProf = NULL;
	hudOn = false;
	{
		const MapProfile* cp = profile_find(emu.gameCode);
		ctlProf = cp && cp->controls.fourFace ? cp : NULL;
		if (ctlProf) ctl_load();
	}
	if (cfg->map) {
#ifdef __ANDROID__
		android_set_pinning(cfg->pinCores);
#endif
		mc = mapcap_create(rom, emu.gameCode, cfg->mapCrop);
		const MapProfile* lp = profile_find(emu.gameCode);
		logoOnly = lp && lp->logoOnly;
		snapProf = logoOnly && lp->snap.showMin ? lp : NULL;
		/* the HUD moves only when there's another screen to put it on */
		hudProf = logoOnly && lp->hud.on[0].addr && nScreens > 1 ? lp : NULL;
		hudOn = false;
		hudScene = false;
		hudLogoDone = false;
		hudBackValid = false;
		if (hudProf) mapW = HUD_CW, mapH = HUD_CH; /* the second screen's picture */
		snapShow = snapRevert = 0;
		if (mc) mapcap_set_still(mc, !cfg->mapAnim);
		if (logoOnly) fprintf(stderr, "logo only: %s\n", lp->title);
		if (mc) {
			fprintf(stderr, "map profile: %s\n", mapcap_title(mc));
			mapcap_size(mc, &mapW, &mapH);
			/* the full map is always up, so hide the game's own corner mini-map */
			const MapProfile* prof = profile_find(emu.gameCode);
			int nRules = 0;
			while (prof && nRules < 4 && prof->hideSprites[nRules].x1) ++nRules;
			emu_hide_sprites(&emu, prof ? prof->hideSprites : NULL, nRules);
		} else if (!logoOnly) {
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
	overlayMode[0][0] = overlayMode[1][0] = 0;
	uint32_t logoGen = logo_generation();
	if (ui_load_logo(cfg, rom, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel)) make_logo_canvas();
	mapframe_init(&mf, mc ? profile_find(emu.gameCode) : NULL, mapW, mapH);
	canvasTexW = canvasTexH = 0;
	snprintf(framePath, sizeof(framePath), "%s/frames", cfg->stateDir);
	mkdirs(framePath);
	snprintf(framePath, sizeof(framePath), "%s/frames/%.4s.png", cfg->stateDir, emu.gameCode);
	if (mf.prof && mf.prof->frame.t && !mapframe_load_atlas(&mf, framePath)) frame_request(rom, emu.gameCode, framePath);
	uint32_t frameGen = logo_generation();

	memset(pageLen, 0, sizeof(pageLen));
	pages[0][pageLen[0]++] = M_RESUME;
	pages[0][pageLen[0]++] = M_RESTART;
	pages[0][pageLen[0]++] = M_SAVE;
	pages[0][pageLen[0]++] = M_LOAD;
	pages[3][pageLen[3]++] = M_CHEATS;
	if (ra_enabled()) pages[3][pageLen[3]++] = M_ACH;
	pages[3][pageLen[3]++] = M_SHOT;
	if (fromLibrary) pages[0][pageLen[0]++] = M_LIBRARY;
	pages[0][pageLen[0]++] = M_EXIT;
	pages[1][pageLen[1]++] = M_SWAP;
	pages[1][pageLen[1]++] = M_SCALE;
	pages[1][pageLen[1]++] = M_LCD;
	pages[1][pageLen[1]++] = M_OVERLAY;
	pages[1][pageLen[1]++] = M_LOGO;
	pages[2][pageLen[2]++] = M_MAPANIM;
	pages[2][pageLen[2]++] = M_MAPRATE;
	pages[2][pageLen[2]++] = M_FRAMEBUF;
	pages[2][pageLen[2]++] = M_MATCH;
	pages[2][pageLen[2]++] = M_REWIND;
	pages[2][pageLen[2]++] = M_FPS;
	set_page(0);

	if (cfg->loadState[0]) {
		if (!state_load(cfg->loadState)) fprintf(stderr, "could not load %s\n", cfg->loadState);
		cfg->loadState[0] = 0;
	} else if (cfg->autoResume && file_exists(gp.autoState)) {
		if (state_load(gp.autoState)) {
			ui_osd("Resumed. Tap or HOME for the menu");
		}
	} else {
		ui_osd("Tap the screen or press HOME for the menu");
	}
	rewind_setup();

	/* The device runs at a fixed rate. The GBA's own rate follows the game's
	 * sound settings (32768 Hz at power-on, 65536 Hz once Zero Mission is
	 * running...) and can change at any moment, so samples go through a
	 * converter that is rebuilt whenever it does. Opening the device at the
	 * rate of the moment made resumed games starve and fresh boots overflow. */
	SDL_AudioSpec want = { 0 }, have;
	want.freq = 48000;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	/* GBADS_AUDIO_SAMPLES: testing only (SDL's dummy driver keeps exact time
	 * only when the buffer is a whole number of milliseconds, e.g. 480) */
	want.samples = getenv("GBADS_AUDIO_SAMPLES") ? atoi(getenv("GBADS_AUDIO_SAMPLES")) : 512;
	SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audio) SDL_PauseAudioDevice(audio, 0);
	else fprintf(stderr, "no audio: %s\n", SDL_GetError());
	SDL_AudioStream* audioConv = NULL;
	int convFrom = 0;
	const int targetQueued = want.freq * cfg->audioLatency / 1000;

	static int16_t samples[4096 * 2];
	uint32_t lastRoomKey = 0;
	int sinceMapRequest = 1 << 20;
	int lastFails = 0;
	bool forceMap = true, wasDark = false;
	long refreshAt = -1;
	bool hotkeyBack = !strcmp(cfg->hotkey, "back");
	int modBtn = hotkeyBack ? B_SELECT : B_HOME;
	bool comboUsed = false;
	int comboHold = -1, comboFrames = 0; /* menu combo: the button held back, frames since */
	int comboPulse[2] = { 0, 0 };        /* SELECT, START handed to the game late */
	int selectPulse = 0;
	unsigned ffPhase = 0;
	bool noPace = getenv("GBADS_NOPACE") != NULL;
	enum GameExit exitTo = GAME_QUIT_APP;
	bool done = false;
	bool wasMenu = false;

	pacing_update();
	const double freq = (double) SDL_GetPerformanceFrequency();
	double next = SDL_GetPerformanceCounter() / freq;
	double fpsT0 = next;
	int fpsFrames = 0;

#ifdef __ANDROID__
	/* a target well under the frame time: the system clocks the CPU so each
	 * frame's work takes about this long, leaving room for heavier frames
	 * (with the full 16.7 ms it clocked down until frames barely fit; with
	 * 65% the average was 11 ms and uneven frames went to 17-21 ms) */
	android_perf_init((int64_t) (frameSec * 0.5 * 1e9));
	android_set_pinning(cfg->pinCores);
	android_pin_game_thread();
#endif
	while (!done) {
		ahead_sync(); /* the core is the loop's again */
		Uint64 workT0 = SDL_GetPerformanceCounter(); /* this frame's work, without the wait at the end */
		long preempt0 = thread_preemptions();
		double cpuMs0 = thread_cpu_ms();
		if (mf.prof && mf.prof->frame.t && !mf.atlas && logo_generation() != frameGen) {
			frameGen = logo_generation();
			if (mapframe_load_atlas(&mf, framePath)) canvasDirty = true;
		}
		if (!logoTex[0] && !logoTex[1] && logo_generation() != logoGen) {
			logoGen = logo_generation();
			if (ui_load_logo(cfg, NULL, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel)) make_logo_canvas();
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
				if (p.kind == 1 && ctl_tap(p.screen, p.x, p.y)) continue;
				if (p.kind == 1) {
					open_menu();
					ignorePointerUp = true;
				}
				continue;
			}
			if (p.screen != ui_touch_screen()) continue;
			if (achOpen) {
				if (p.kind == 1) {
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						close_ach(); /* Back */
						ignorePointerUp = true;
						continue;
					}
					achPressed = true;
					achPressY = p.y;
					achPressScroll = ra_list_scroll_pos();
				} else if (p.kind == 3 && achPressed) {
					ra_list_set_scroll(p.screen, achPressScroll - (p.y - achPressY));
				} else if (p.kind == 2) {
					achPressed = false;
					ignorePointerUp = false;
				}
				continue;
			}
			if (cheatsOpen) {
				float px = ui_px(p.screen);
				if (p.kind == 1) {
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						cheatsOpen = false; /* Back */
						ignorePointerUp = true;
						continue;
					}
					cheatPressRow = cheat_row_at(p.screen, p.x, p.y);
					cheatPressY = p.y;
					cheatPressScroll = cheatScroll;
					cheatDragging = false;
					ignorePointerUp = false;
				} else if (p.kind == 3 && cheatPressRow != -2) {
					if (abs(p.y - cheatPressY) > px * 4) cheatDragging = true;
					if (cheatDragging) {
						CheatGeom g = cheat_geom(p.screen);
						cheatScroll = SDL_clamp(cheatPressScroll - (p.y - cheatPressY), 0, cheat_max_scroll(&g));
					}
				} else if (p.kind == 2) {
					int row = cheat_row_at(p.screen, p.x, p.y);
					if (!ignorePointerUp && !cheatDragging && row >= 0 && row == cheatPressRow && cheatRows[row] >= 0) {
						cheatSel = row;
						toggle_cheat(row);
					}
					cheatPressRow = -2;
					cheatDragging = false;
					ignorePointerUp = false;
				}
				continue;
			}
			if (p.kind == 1) {
				int t = tab_at(p.screen, p.x, p.y, N_PAGES);
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

		/* ---- the menu combo (menu_combo = select+start; on Android, where HOME
		 * belongs to the system): pressing both opens the menu. The first of
		 * the two is held back from the game for a few frames, so that a combo
		 * doesn't also press SELECT (a map in some games) or START (pause);
		 * if the other doesn't follow, it goes to the game after all. */
		bool comboOpened = false;
		if (!menuOpen && !strcmp(cfg->menuCombo, "select+start")) {
			if (comboHold < 0) {
				if (btn_pressed(B_SELECT) && btn_pressed(B_START)) comboHold = B_START, comboFrames = 0;
				else if (btn_pressed(B_SELECT) && !btnNow[B_START]) comboHold = B_SELECT, comboFrames = 0;
				else if (btn_pressed(B_START) && !btnNow[B_SELECT]) comboHold = B_START, comboFrames = 0;
			}
			if (comboHold >= 0) {
				int other = comboHold == B_SELECT ? B_START : B_SELECT;
				if (btnNow[comboHold] && btnNow[other]) {
					open_menu();
					comboOpened = true;
					comboUsed = true; /* not a hotkey, nor a plain SELECT for the game */
					comboHold = -1;
				} else if (!btnNow[comboHold]) {
					comboPulse[comboHold == B_START] = 4; /* a quick tap: give it to the game now */
					comboHold = -1;
				} else if (++comboFrames > 5) {
					comboHold = -1; /* just held: the game gets it from here */
				}
			}
		} else {
			comboHold = -1;
		}

		/* ---- HOME (or the hotkey modifier) alone toggles the menu; with a button it's a hotkey */
		bool mod = btnNow[modBtn];
		if (mod && !menuOpen) {
			if (btn_pressed(B_START)) exitTo = GAME_QUIT_APP, done = true, comboUsed = true;
			if (btn_pressed(B_R)) ui_osd(state_save(gp.state) ? "State saved" : "Save failed"), comboUsed = true;
			if (btn_pressed(B_L) && file_exists(gp.state)) state_load(gp.state), ui_osd("State loaded"), forceMap = true, comboUsed = true;
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

		if (menuOpen && achOpen) {
			int scr = ui_touch_screen();
			static int held[2];
			for (int k = 0; k < 2; ++k) {
				int b = k ? B_DOWN : B_UP;
				held[k] = btnNow[b] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) ra_list_scroll(scr, (k ? 1 : -1) * ra_list_step(scr));
			}
			int page = ach_area(scr).h * 3 / 4;
			if (btn_pressed(B_L)) ra_list_scroll(scr, -page);
			if (btn_pressed(B_R)) ra_list_scroll(scr, page);
			if (btn_pressed(B_B)) close_ach();
			if (btn_pressed(B_START)) close_ach(), menuOpen = false;
		} else if (menuOpen && cheatsOpen) {
			/* D-pad repeats while held */
			static int held[2];
			for (int k = 0; k < 2; ++k) {
				int b = k ? B_DOWN : B_UP;
				held[k] = btnNow[b] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) cheat_move(k ? 1 : -1);
			}
			CheatGeom g = cheat_geom(ui_touch_screen());
			int page = SDL_max(1, g.listH / (g.rowH + g.gap) - 1);
			if (btn_pressed(B_L)) for (int k = 0; k < page; ++k) cheat_move(-1);
			if (btn_pressed(B_R)) for (int k = 0; k < page; ++k) cheat_move(1);
			if (btn_pressed(B_A)) toggle_cheat(cheatSel);
			if (btn_pressed(B_B)) cheatsOpen = false;
			if (btn_pressed(B_START)) cheatsOpen = false, menuOpen = false;
		} else if (menuOpen) {
			if (!comboOpened) { /* (START just opened it: not also a close) */
				if (btn_pressed(B_L) && curPage > 0) set_page(curPage - 1);
				if (btn_pressed(B_R) && curPage < N_PAGES - 1) set_page(curPage + 1);
				if (btn_pressed(B_DOWN)) menuSel = (menuSel + 1) % nMenuItems;
				if (btn_pressed(B_UP)) menuSel = (menuSel + nMenuItems - 1) % nMenuItems;
				if (btn_pressed(B_B) || btn_pressed(B_START)) menuOpen = false;
				else if (btn_pressed(B_A) && activate(menuItems[menuSel], &exitTo)) break;
			}
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
			ra_idle();
			ui_clear();
			draw_game();
			draw_menu();
			ui_present();
			SDL_Delay(16);
			next = SDL_GetPerformanceCounter() / freq;
			continue;
		}

		if (btn_pressed(B_L3)) swap_screens();

		/* ---- emulate */
		uint32_t keys = 0;
		for (int b = 0; b < B_COUNT; ++b) {
			if (!btnNow[b]) suppress[b] = false; /* released: counts again */
		}
		if (!btnNow[modBtn] || hotkeyBack) {
			if (ctl_four()) {
				/* by position: top A, bottom B, left L, right R (on the RG DS
				 * the top face button arrives as X, the left one as Y) */
				int right = cfg->abSwap ? B_B : B_A, bottom = cfg->abSwap ? B_A : B_B;
				if (HELD(B_X)) keys |= K_A;
				if (HELD(bottom)) keys |= K_B;
				if (HELD(B_Y)) keys |= K_L;
				if (HELD(right)) keys |= K_R;
			} else {
				if (HELD(B_A)) keys |= K_A;
				if (HELD(B_B)) keys |= K_B;
			}
			if (HELD(B_START) && !(hotkeyBack && btnNow[B_SELECT]) && comboHold != B_START) keys |= K_START;
			if (HELD(B_SELECT) && !hotkeyBack && comboHold != B_SELECT) keys |= K_SELECT;
			if (HELD(B_L)) keys |= K_L;
			if (HELD(B_R)) keys |= K_R;
			if (HELD(B_UP)) keys |= K_UP;
			if (HELD(B_DOWN)) keys |= K_DOWN;
			if (HELD(B_LEFT)) keys |= K_LEFT;
			if (HELD(B_RIGHT)) keys |= K_RIGHT;
			if (hotkeyBack && btnNow[B_SELECT]) keys &= ~(K_L | K_R | K_START);
		}
		if (selectPulse > 0) keys |= K_SELECT, --selectPulse;
		if (comboPulse[0] > 0) keys |= K_SELECT, --comboPulse[0];
		if (comboPulse[1] > 0) keys |= K_START, --comboPulse[1];
		{
			static uint32_t lastKeys;
			if (debug && keys != lastKeys) fprintf(stderr, "frame %llu: game keys %03x\n", (unsigned long long) frameCount, keys);
			lastKeys = keys;
		}

		bool rewinding = cfg->rewind && btnNow[B_L2];
		bool ff = btnNow[B_R2] && !rewinding;
		int runs = ff ? SDL_max(1, cfg->ffSpeed) : 1;
		/* Audio keeps flowing during fast-forward: every runs-th sample is kept,
		 * so the device gets real-time worth of (sped-up) sound. Starving it
		 * makes PulseAudio/PipeWire grow their buffers after each underrun,
		 * which is what made the lag worse every time. */
		int queuedNow = audio ? (int) (SDL_GetQueuedAudioSize(audio) / 4) : 0;
		bool dropAudio = queuedNow > targetQueued * 3; /* hard cap on drift */
		/* Health report (always on, every 5 s when something went wrong): audio
		 * underruns (the queue ran dry = a crackle), slow loop iterations, and
		 * how often the background map capture was running. */
		{
			static int underruns, slow, captureBusy, n;
			static long preemptT0 = -1;
			if (preemptT0 < 0) preemptT0 = thread_preemptions();
			static Uint64 lastIter;
			Uint64 nowT = SDL_GetPerformanceCounter();
			if (lastIter && !ff && (nowT - lastIter) * 1000 / SDL_GetPerformanceFrequency() > 25) ++slow;
			lastIter = nowT;
			if (audio && frameCount > 120 && queuedNow == 0 && !ff) ++underruns;
			if (mc && mapcap_busy(mc)) ++captureBusy;
			if (++n >= 300) {
#ifdef __ANDROID__
				const bool report = true; /* performance on Android devices is still being measured */
#else
				const bool report = underruns || slow || debug;
#endif
				if (report) {
					double ms = 1000.0 / SDL_GetPerformanceFrequency() / n;
					fprintf(stderr, "frame %llu: last 5 s: %.1f fps, %d audio underrun(s), %d slow frame(s), map capture busy %d%%, "
					                "audio queue %d ms; per frame: emulating %.2f ms, drawing %.2f ms; game thread preempted %ld time(s)\n",
					        (unsigned long long) frameCount, fps, underruns, slow, captureBusy * 100 / n,
					        queuedNow * 1000 / want.freq, emuTicks * ms, drawTicks * ms, thread_preemptions() - preemptT0);
				}
				underruns = slow = captureBusy = n = 0;
				preemptT0 = thread_preemptions();
				emuTicks = drawTicks = 0;
			}
		}
		if (debug && audio && frameCount % 30 == 0) {
			fprintf(stderr, "audio queue %d ms%s\n", queuedNow * 1000 / want.freq, ff ? " (ff)" : "");
		}
		Uint64 emuT0 = SDL_GetPerformanceCounter(), raT = 0, audT = 0;
		double emuCpu0 = thread_cpu_ms();
		if (rewinding) {
			/* Rewinding: each frame goes back one state of the buffer (two
			 * frames) and runs one frame to draw it, so it plays backwards at
			 * double speed; silent, no achievements. At the buffer's start
			 * the picture stays. */
			aheadReady = false; /* (a frame emulated ahead belongs to the present) */
			if (emu_rewind_step(&emu)) {
				emu_frame(&emu, 0);
				ui_osd("Rewinding");
			} else {
				ui_osd("Can't rewind further");
			}
			while (emu_audio_read(&emu, samples, 4096) > 0) {}
			rewindTick = 0;
			wasRewinding = true;
		} else if (wasRewinding) {
			wasRewinding = false;
			forceMap = true; /* the map, as of where the rewind stopped */
		}
		for (int i = 0; i < (rewinding ? 0 : runs); ++i) {
			if (i == 0 && aheadReady) aheadReady = false; /* emulated by the worker while the last frame was shown */
			else emu_frame(&emu, keys);
			Uint64 raT0 = SDL_GetPerformanceCounter();
			ra_frame();
			raT += SDL_GetPerformanceCounter() - raT0;
			Uint64 audT0 = SDL_GetPerformanceCounter();
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
				if (!got) continue;
				/* matched to the display, the game makes its sound audioSpeed times
				 * faster than real time: resample it from that much higher a rate */
				int rate = (int) lround(emu_audio_rate(&emu) * audioSpeed);
				if (rate != convFrom || !audioConv) {
					if (audioConv) SDL_FreeAudioStream(audioConv);
					audioConv = SDL_NewAudioStream(AUDIO_S16SYS, 2, rate, AUDIO_S16SYS, 2, want.freq);
					if (debug || convFrom) fprintf(stderr, "frame %llu: game audio rate %d Hz\n", (unsigned long long) frameCount, rate);
					convFrom = rate;
				}
				if (!audioConv) continue;
				SDL_AudioStreamPut(audioConv, samples, (int) got * 4);
				static int16_t conv[8192 * 2];
				int bytes;
				while ((bytes = SDL_AudioStreamGet(audioConv, conv, sizeof(conv))) > 0) {
					/* Paced by the display, the loop can't slow down or speed up to
					 * keep the sound queue level, so the sound gives: a sample frame
					 * dropped or repeated per chunk (at most ~0.1%, inaudible) while
					 * the queue is a quarter off its target. */
					if (pacedHz > 0 && bytes >= 8) {
						int q = (int) (SDL_GetQueuedAudioSize(audio) / 4);
						if (q > targetQueued * 5 / 4) bytes -= 4;
						else if (q < targetQueued * 3 / 4 && bytes + 4 <= (int) sizeof(conv)) {
							memcpy((char*) conv + bytes, (char*) conv + bytes - 4, 4);
							bytes += 4;
						}
					}
					SDL_QueueAudio(audio, conv, bytes);
				}
			}
			audT += SDL_GetPerformanceCounter() - audT0;
		}
		Uint64 emuT1 = SDL_GetPerformanceCounter();
		double emuCpu1 = thread_cpu_ms();
		emuTicks += emuT1 - emuT0;
		++frameCount;
		/* the rewind buffer: a state every second emulated frame */
		if (cfg->rewind && !rewinding && (rewindTick += runs) >= 2) {
			emu_rewind_append(&emu);
			rewindTick = 0;
		}

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
			if (mapShownIdx >= 0 && frameCount % 30 == 0 && mapcap_in_menu(mc, &emu)) {
				/* quit to the title: the map belongs to the old session */
				mapShownIdx = -1;
				mapInactive = false;
				forceMap = true;
				if (debug) fprintf(stderr, "frame %llu: back at the menus, map cleared\n", (unsigned long long) frameCount);
			}
			uint32_t rk = mapcap_room_key(mc, &emu);
			bool roomChanged = rk && rk != lastRoomKey;
			/* Back off while captures keep failing (e.g. the game says "playing"
			 * but the map can't open: intro cutscenes, briefings). Each failed
			 * attempt emulates a few seconds in the background, which starves
			 * the game and its audio on slower devices. */
			const char* why = NULL;
			int fails = mapcap_failures(mc, &why);
			if (fails != lastFails) {
				if (debug && fails) fprintf(stderr, "frame %llu: map capture failed (%d in a row): %s\n",
				                            (unsigned long long) frameCount, fails, why ? why : "?");
				const MapProfile* dp = mapcap_profile(mc);
				if (fails && why && dp && dp->dimWhenUnavailable &&
				    (!strcmp(why, "map screen did not appear") || !strcmp(why, "map button did nothing"))) {
					mapInactive = true;
				}
				lastFails = fails;
			}
			/* quick "no map here" failures (A Link to the Past indoors) cost little
			 * and are expected, so they don't back off */
			bool cheapFail = why && !strcmp(why, "map button did nothing");
			int interval = cheapFail ? cfg->mapInterval : cfg->mapInterval << SDL_min(fails, 3);
			/* after failures, a room change still triggers a try, but no more
			 * often than the normal interval */
			bool due = sinceMapRequest >= interval ||
			           ((forceMap || roomChanged) && (!fails || sinceMapRequest >= cfg->mapInterval));
			if (due && !mapcap_busy(mc) && mapcap_live_ok(mc, &emu)) {
				if (mapcap_request(mc, &emu)) {
					sinceMapRequest = 0;
					lastRoomKey = rk;
					forceMap = false;
				}
			}
			bool fresh = mapcap_poll(mc, mapRes.serial, &mapRes);
			if (fresh && mapcap_in_menu(mc, &emu)) {
				/* a capture that finished after quitting to the title: drop it */
			} else if (fresh) {
				mapInactive = false;
				if (mapRes.hasTile) mapframe_set_tile(&mf, mapRes.tile, mapRes.tileK, mapRes.tileX, mapRes.tileY);
				mapShownIdx = -1;
				mapAnimStart = frameCount;
				/* the still (each marker frozen) is only shown with map animations
				 * off, and building it is real work: only then */
				bool stillChanged = false;
				stillStale = cfg->mapAnim;
				if (!stillStale) stillChanged = update_still();
				upload_map_frame(cfg->mapAnim ? 0 : STILL_IDX);
				if (debug) fprintf(stderr, "frame %llu: map updated (%d frame(s) %d apart%s)\n", (unsigned long long) frameCount,
				                   mapRes.nFrames, mapRes.frameStep, stillStale ? "" : stillChanged ? ", still changed" : ", still unchanged");
			} else if (cfg->mapAnim && mapRes.nFrames > 1 && mapShownIdx >= 0) {
				int idx = (int) ((frameCount - mapAnimStart) / SDL_max(1, mapRes.frameStep)) % mapRes.nFrames;
				upload_map_frame(idx);
			}
		}

		if (snapProf) update_snapshot();
		if (hudProf) update_hud();
		if (ctlProf) {
			/* switch after a few frames in the new state, not on one odd frame */
			static int ctlStreak;
			ctlStreak = ctl_playing_now() != ctlPlaying ? ctlStreak + 1 : 0;
			if (ctlStreak >= 3) {
				ctlPlaying = !ctlPlaying;
				ctlStreak = 0;
				if (debug && ctlMode == CTL_AUTO) {
					fprintf(stderr, "frame %llu: auto controls: %s\n", (unsigned long long) frameCount, ctlPlaying ? "four-face" : "GBA");
				}
			}
		}

		Uint64 drawT0 = SDL_GetPerformanceCounter();
		double drawCpu0 = thread_cpu_ms();
		/* frame buffer: start the next frame now; this one is drawn from a copy */
		const uint32_t* shownFb = emu.fb;
		if (cfg->frameBuffer && !ff && !rewinding) {
			memcpy(aheadFb, emu.fb, sizeof(aheadFb));
			if (ahead_start(keys)) aheadInFlight = true, shownFb = aheadFb;
		}
		/* only the view the game is on needs it (all of them right after a swap,
		 * since the menu draws the game without uploading) */
		for (int i = 0; i < nViews; ++i) {
			if (gameTexAll || i == screens[game_screen()].view) ui_update_texture(gameTex[i], shownFb, GBA_W * 4);
		}
		gameTexAll = false;
		ui_sync_view(cfg->vsync || pacedHz > 0 ? screens[game_screen()].view : -1); /* Match Display Rate; vsync: testing (docs/ANDROID.md) */
		{
			/* the other screen's redraw waits a frame if this one is past 70% of its time */
			static double budget = -1;
			if (budget < 0) budget = getenv("GBADS_DRAW_BUDGET") ? atof(getenv("GBADS_DRAW_BUDGET")) : frameSec * 1000.0 * 0.7;
			ui_present_budget(ff ? -1 : screens[game_screen()].view, workT0, budget);
		}
		ui_clear();
		draw_game();
		if (shotPending) {
			shotPending = false;
			take_screenshot();
		}
		ra_draw(nScreens > 1 ? 1 - game_screen() : 0); /* after the screenshot: not in it */
		ui_present();
		Uint64 drawT1 = SDL_GetPerformanceCounter();
		drawTicks += drawT1 - drawT0;
		{
			double msPer = 1000.0 / (double) SDL_GetPerformanceFrequency();
			double total = (double) (drawT1 - workT0) * msPer;
			/* matched, each frame includes the wait for the display's refresh:
			 * only one that missed its refresh (about two frames long) is slow */
			if (!ff && total > frameSec * 1000.0 * (pacedHz > 0 ? 1.4 : 1.0)) {
				SlowFrame f = { total, (double) (emuT0 - workT0) * msPer, (double) (emuT1 - emuT0 - raT) * msPer, (double) raT * msPer,
					            (double) (drawT0 - emuT1) * msPer, (double) (drawT1 - drawT0) * msPer, 0, 0, 0,
					            (unsigned long long) frameCount };
				/* by screen (top first, as they're stacked), not by view */
				int sTop = 0, sBot = nScreens > 1 ? 1 : 0;
				int vTop = screens[sTop].view, vBot = screens[sBot].view;
				f.top = ui_last_present_ms(vTop);
				f.bottom = vBot != vTop ? ui_last_present_ms(vBot) : 0;
				int red = ui_last_redraws();
				f.redraws = ((red >> vTop) & 1) | (vBot != vTop ? ((red >> vBot) & 1) << 1 : 0);
				f.preempted = thread_preemptions() - preempt0;
				f.cpu = current_cpu();
				f.mhz = cpu_mhz(f.cpu);
				double cpuNow = thread_cpu_ms();
				f.cpuMs = cpuNow - cpuMs0;
				f.waitEmu = SDL_max(0.0, (double) (emuT1 - emuT0) * msPer - (emuCpu1 - emuCpu0));
				f.waitDraw = SDL_max(0.0, (double) (drawT1 - drawT0) * msPer - (cpuNow - drawCpu0));
				f.audio = (double) audT * msPer;
				slow_frame_note(f);
			}
			static Uint64 lastReport;
			if (drawT1 - lastReport > SDL_GetPerformanceFrequency()) {
				slow_frame_report(debug);
				lastReport = drawT1;
			}
		}
#ifdef __ANDROID__
		if (frameCount == 1) fprintf(stderr, "startup: first frame shown\n");
#endif

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
		/* fast-forward is paced too: ff_speed frames per real frame. Matched to
		 * the display, presenting in step with it sets the pace; the clock runs
		 * a little faster, as a limit for when the display doesn't make us wait. */
		next += pacedHz > 0 ? frameSec / 1.005 : frameSec;
		if (audio) {
			int queued = (int) (SDL_GetQueuedAudioSize(audio) / 4);
			if (queued > targetQueued * 3 / 2) next += frameSec * 0.015;
			else if (queued < targetQueued / 2) next -= frameSec * 0.015;
		}
		if (next < now - 4 * frameSec) next = now;
		if (noPace) next = now; /* benchmarking: run as fast as possible */
#ifdef __ANDROID__
		android_perf_report((int64_t) ((SDL_GetPerformanceCounter() - workT0) * 1e9 / freq));
#endif
		double wait = next - now;
		if (wait > 0) {
			struct timespec ts = { 0, (long) (wait * 1e9) };
			nanosleep(&ts, NULL);
		}
	}

	ahead_stop(); /* before anything else touches the core */
	ui_on_background(NULL);
	ui_on_foreground(NULL);
	ui_sync_view(-1);
	fprintf(stderr, "leaving game after %llu frames\n", (unsigned long long) frameCount);
	auto_save();
	ra_game_end();
	if (audio) SDL_CloseAudioDevice(audio);
	if (audioConv) SDL_FreeAudioStream(audioConv);
	mapcap_destroy(mc);
	mc = NULL;
	emu_rewind_deinit();
	emu_close(&emu); /* flushes the battery save */
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		free(mapRes.frames[i]);
		mapRes.frames[i] = NULL;
	}
	free(stillPx);
	stillPx = NULL;
	stillValid = false;
	for (int i = 0; i < nViews; ++i) {
		SDL_DestroyTexture(gameTex[i]);
		SDL_DestroyTexture(mapTex[i]);
		gameTex[i] = mapTex[i] = NULL;
	}
	ui_free_image(logoTex);
	ui_free_image(logoCanvasTex);
	ui_free_image(overlayTex[0]);
	ui_free_image(overlayTex[1]);
	overlayMode[0][0] = overlayMode[1][0] = 0;
	mapframe_free(&mf);
	for (int i = 0; i < 2; ++i) {
		if (canvasTex[i]) SDL_DestroyTexture(canvasTex[i]);
		canvasTex[i] = NULL;
	}
	return exitTo;
}

#include "library.h"
#include "paths.h"
#include "profiles.h"
#include "ui.h"
#include "logo.h"
#include "brand.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef struct {
	char path[1024];
	char name[160];
	char code[5];
	bool supported;
} GameEntry;

static GameEntry* games;
static int nGames;

static const SDL_Color C_BG = { 14, 14, 26, 255 };
static const SDL_Color C_HEADER = { 30, 26, 52, 255 };
static const SDL_Color C_ROW = { 22, 22, 38, 255 };
static const SDL_Color C_ROW_ALT = { 18, 18, 31, 255 };
static const SDL_Color C_SEL = { 92, 52, 140, 255 };
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIM = { 140, 140, 172, 255 };
static const SDL_Color C_GOLD = { 232, 200, 120, 255 };
static const SDL_Color C_TAG = { 52, 120, 96, 255 };

static int cmp_games(const void* a, const void* b) {
	return strcasecmp(((const GameEntry*) a)->name, ((const GameEntry*) b)->name);
}

static void scan_dir(Config* cfg, const char* d, int depth) {
	DIR* dir = opendir(d);
	if (!dir) {
		if (depth == 0) fprintf(stderr, "library: cannot open %s\n", d);
		return;
	}
	struct dirent* de;
	while ((de = readdir(dir))) {
		if (de->d_name[0] == '.') continue;
		char full[1024];
		snprintf(full, sizeof(full), "%s/%s", d, de->d_name);
		struct stat st;
		if (stat(full, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth < 3 && strcasecmp(de->d_name, "images") && strcasecmp(de->d_name, "videos") &&
			    strcasecmp(de->d_name, "manuals")) {
				scan_dir(cfg, full, depth + 1);
			}
			continue;
		}
		const char* ext = strrchr(de->d_name, '.');
		if (!ext || strcasecmp(ext, ".gba")) continue;
		GameEntry g;
		memset(&g, 0, sizeof(g));
		snprintf(g.path, sizeof(g.path), "%s", full);
		FILE* f = fopen(g.path, "rb");
		if (!f) continue;
		unsigned char hdr[16] = { 0 };
		if (fseek(f, 0xA0, SEEK_SET) == 0 && fread(hdr, 1, 16, f) == 16) memcpy(g.code, hdr + 12, 4);
		fclose(f);
		g.supported = profile_find(g.code) != NULL;
		if (!g.supported && !cfg->showAll) continue;
		int n = (int) (ext - de->d_name);
		if (n >= (int) sizeof(g.name)) n = sizeof(g.name) - 1;
		memcpy(g.name, de->d_name, n);
		for (char* c = g.name; *c; ++c) {
			if (*c == '_') *c = ' ';
		}
		games = realloc(games, sizeof(*games) * (nGames + 1));
		games[nGames++] = g;
	}
	closedir(dir);
}

static void scan(Config* cfg) {
	free(games);
	games = NULL;
	nGames = 0;
	char dirs[1024];
	snprintf(dirs, sizeof(dirs), "%s", cfg->romDir);
	char* save;
	for (char* d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save)) scan_dir(cfg, d, 0);
	qsort(games, nGames, sizeof(*games), cmp_games);
	for (int i = 0; i < nGames; ++i) {
		SDL_Texture* t[2];
		int w, h;
		bool px;
		if (ui_load_logo(cfg, games[i].path, games[i].code, t, &w, &h, &px)) ui_free_image(t);
		char fp[1200];
		snprintf(fp, sizeof(fp), "%s/frames", cfg->stateDir);
		mkdirs(fp);
		snprintf(fp, sizeof(fp), "%s/frames/%.4s.png", cfg->stateDir, games[i].code);
		frame_request(games[i].path, games[i].code, fp);
	}
	fprintf(stderr, "library: %d game(s)\n", nGames);
}

/* -------------------------------------------------------------- preview */

static SDL_Texture* thumbTex[2]; /* resume screenshot, per view */
static int thumbFor = -2;
static SDL_Texture* logoTex[2];
static int logoW, logoH;
static bool logoPixel;
static uint32_t logoGen;

/* built-in images: our logo, button glyphs, "no save states yet!" */
static SDL_Texture* brandTex[BRAND_COUNT][2];
static int brandW[BRAND_COUNT], brandH[BRAND_COUNT];

static void load_brand(void) {
	for (int i = 0; i < BRAND_COUNT; ++i) {
		if (!brandTex[i][0] && !brandTex[i][1]) ui_load_brand(i, brandTex[i], &brandW[i], &brandH[i]);
	}
}

static void free_brand(void) {
	for (int i = 0; i < BRAND_COUNT; ++i) ui_free_image(brandTex[i]);
}

/* Draws built-in image `id` at height h with its top-left at x, y; returns the width drawn. */
static int draw_brand(int scr, int id, int x, int y, int h) {
	if (!brandTex[id][0] && !brandTex[id][1]) return 0;
	int w = brandW[id] * h / brandH[id];
	SDL_Rect a = { x, y, w, h };
	ui_image(scr, brandTex[id], brandW[id], brandH[id], "stretch", &a);
	return w;
}

static void truncate_to(char* out, size_t outSize, const char* s, int maxChars) {
	if (maxChars < 3) maxChars = 3;
	if ((int) strlen(s) <= maxChars) {
		snprintf(out, outSize, "%s", s);
	} else {
		snprintf(out, outSize, "%.*s..", maxChars - 2, s);
	}
}

static void free_thumbs(void) {
	ui_free_image(logoTex);
	ui_free_image(thumbTex);
}

static void load_thumbs(Config* cfg, int idx) {
	if (idx == thumbFor) return;
	thumbFor = idx;
	free_thumbs();
	if (idx < 0 || idx >= nGames) return;
	GamePaths gp;
	game_paths(cfg, games[idx].path, &gp);
	if (ui_load_logo(cfg, games[idx].path, games[idx].code, logoTex, &logoW, &logoH, &logoPixel)) {
		/* drawn at a fixed height rather than a whole-number scale, so filter it */
		for (int v = 0; v < 2; ++v) {
			if (logoTex[v]) SDL_SetTextureScaleMode(logoTex[v], SDL_ScaleModeLinear);
		}
	}
	SDL_Surface* s = SDL_LoadBMP(gp.thumb);
	if (s) {
		for (int v = 0; v < nViews; ++v) thumbTex[v] = SDL_CreateTextureFromSurface(views[v].ren, s);
		SDL_FreeSurface(s);
	}
}

static void draw_preview(Config* cfg, int scr, int sel) {
	SDL_Rect r = screens[scr].rect;
	int px = ui_px(scr);
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, (SDL_Color) { 0, 0, 0, 255 }); /* black, like the logos */
	load_brand();

	/* our logo top left, the game's own logo top right, both the same height */
	int m = px * 6;
	int headH = px * 21;
	int ourW = draw_brand(scr, BRAND_LOGO, m, m, headH);

	/* button hints along the bottom */
	int ty = r.h - px * 11, d = px * 10, bx = m;
	int gy = ty + px * 7 / 2 - d / 2;
	bx += draw_brand(scr, BRAND_BTN_A, bx, gy, d) + px * 3;
	ui_text(scr, bx, ty, px, "PLAY", C_TEXT);
	bx += ui_text_width("PLAY", px) + px * 10;
	bx += draw_brand(scr, BRAND_BTN_B, bx, gy, d) + px * 3;
	ui_text(scr, bx, ty, px, "QUIT", C_TEXT);

	if (nGames == 0 || sel < 0) {
		ui_text_center(scr, r.w / 2, r.h / 2 - px * 4, px, "NO GAMES FOUND", C_TEXT);
		return;
	}
	GameEntry* g = &games[sel];
	load_thumbs(cfg, sel);
	if (logoTex[0] || logoTex[1]) {
		int maxW = r.w - ourW - m * 3;
		int w = logoW * headH / logoH, h = headH;
		if (w > maxW) w = maxW, h = logoH * maxW / logoW;
		SDL_Rect a = { r.w - m - w, m + (headH - h) / 2, w, h };
		ui_image(scr, logoTex, logoW, logoH, "stretch", &a);
	}
	if (!g->supported) ui_text(scr, m, m + headH + px * 4, px, "NO MAP SCREEN - PLAYS NORMALLY", C_DIM);

	/* the resume point (the game screen only), or a note that there isn't one */
	int y = px * 50;
	SDL_Rect area = { m, y, r.w - m * 2, gy - px * 6 - y };
	if (thumbTex[0] || thumbTex[1]) {
		ui_image(scr, thumbTex, GBA_W, GBA_H, "fit", &area);
	} else if (brandTex[BRAND_NO_SAVES][0] || brandTex[BRAND_NO_SAVES][1]) {
		int h = px * 14;
		int w = brandW[BRAND_NO_SAVES] * h / brandH[BRAND_NO_SAVES];
		draw_brand(scr, BRAND_NO_SAVES, (r.w - w) / 2, area.y + (area.h - h) / 2, h);
	}
}

/* -------------------------------------------------------------- list */

typedef struct {
	int rowH, headerH, listH, visible;
	SDL_Rect exitBtn;
} ListGeom;

static ListGeom list_geom(int scr) {
	ListGeom g;
	SDL_Rect r = screens[scr].rect;
	int px = ui_px(scr);
	g.rowH = px * 22;
	g.headerH = px * 20;
	g.listH = r.h - g.headerH;
	g.visible = g.listH / g.rowH;
	int bw = ui_text_width("QUIT", px) + px * 10;
	g.exitBtn = (SDL_Rect) { r.w - bw - px * 4, px * 4, bw, g.headerH - px * 8 };
	return g;
}

static void draw_list(Config* cfg, int scr, int sel, int scroll) {
	SDL_Rect r = screens[scr].rect;
	int px = ui_px(scr);
	ListGeom g = list_geom(scr);
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, C_BG);
	if (nGames == 0) {
		ui_text_center(scr, r.w / 2, r.h / 2 - px * 12, px, "NO SUPPORTED GAMES IN", C_TEXT);
		char dir[200];
		truncate_to(dir, sizeof(dir), cfg->romDir, r.w / (px * 6) - 2);
		ui_text_center(scr, r.w / 2, r.h / 2, px, dir, C_GOLD);
		ui_text_center(scr, r.w / 2, r.h / 2 + px * 12, px, "(SUPPORTED GAMES ONLY, UNZIPPED)", C_DIM);
	}
	for (int i = 0; i < nGames; ++i) {
		int y = g.headerH + i * g.rowH - scroll;
		if (y + g.rowH <= g.headerH || y >= r.h) continue;
		SDL_Color bg = i == sel ? C_SEL : (i & 1) ? C_ROW_ALT : C_ROW;
		ui_fill(scr, (SDL_Rect) { 0, y, r.w, g.rowH - px }, bg);
		int tagW = games[i].supported || cfg->showAll == false ? 0 : ui_text_width("NO MAP", px) + px * 6;
		char line[200];
		truncate_to(line, sizeof(line), games[i].name, (r.w - px * 16 - tagW) / (px * 6));
		ui_text(scr, px * 6, y + (g.rowH - px * 8) / 2, px, line, C_TEXT);
		if (tagW) {
			SDL_Rect t = { r.w - tagW - px * 6, y + (g.rowH - px * 12) / 2, tagW, px * 11 };
			ui_fill(scr, t, C_TAG);
			ui_text(scr, t.x + px * 3, t.y + px * 2, px, "NO MAP", C_TEXT);
		}
	}
	/* header drawn last so rows scroll underneath it */
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, g.headerH }, C_HEADER);
	char hdr[64];
	snprintf(hdr, sizeof(hdr), "GAMES (%d)", nGames);
	ui_text(scr, px * 6, (g.headerH - px * 7) / 2, px, hdr, C_GOLD);
	ui_fill(scr, g.exitBtn, (SDL_Color) { 70, 40, 60, 255 });
	ui_text(scr, g.exitBtn.x + px * 5, g.exitBtn.y + (g.exitBtn.h - px * 7) / 2, px, "QUIT", C_TEXT);
}

static int clamp_scroll(int scr, int scroll) {
	ListGeom g = list_geom(scr);
	int max = nGames * g.rowH - g.listH;
	if (max < 0) max = 0;
	return scroll < 0 ? 0 : scroll > max ? max : scroll;
}

static int keep_visible(int scr, int sel, int scroll) {
	ListGeom g = list_geom(scr);
	int top = sel * g.rowH;
	if (top < scroll) scroll = top;
	if (top + g.rowH > scroll + g.listH) scroll = top + g.rowH - g.listH;
	return clamp_scroll(scr, scroll);
}

static bool in_rect(int x, int y, SDL_Rect r) {
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

bool library_run(Config* cfg, char* outPath, size_t outSize) {
	scan(cfg);
	thumbFor = -2;

	/* reselect the last game played */
	char lastFile[1100], last[1024] = "";
	snprintf(lastFile, sizeof(lastFile), "%s/last_game.txt", cfg->stateDir);
	FILE* f = fopen(lastFile, "r");
	if (f) {
		if (fgets(last, sizeof(last), f)) last[strcspn(last, "\n")] = 0;
		fclose(f);
	}
	int sel = nGames ? 0 : -1;
	for (int i = 0; i < nGames; ++i) {
		if (!strcmp(games[i].path, last)) sel = i;
	}
	int ls = ui_touch_screen();
	int scroll = sel > 0 ? keep_visible(ls, sel, 0) : 0;
	bool dragging = false, pressed = false;
	int pressY = 0, pressScroll = 0;
	int chosen = -1;
	bool quit = false;
	int repeat = 0;

	while (!quit && chosen < 0) {
		if (logo_generation() != logoGen) { /* a logo finished extracting */
			logoGen = logo_generation();
			thumbFor = -2;
		}
		if (!ui_events()) {
			quit = true;
			break;
		}
		ui_tick();
		ui_read_buttons();
		ls = ui_touch_screen();
		ListGeom g = list_geom(ls);

		if (nGames) {
			int dir = 0;
			if (btn_pressed(B_DOWN)) dir = 1, repeat = 0;
			if (btn_pressed(B_UP)) dir = -1, repeat = 0;
			if ((btnNow[B_DOWN] || btnNow[B_UP]) && ++repeat > 18 && repeat % 4 == 0) dir = btnNow[B_DOWN] ? 1 : -1;
			if (btn_pressed(B_R) || btn_pressed(B_RIGHT)) dir = g.visible;
			if (btn_pressed(B_L) || btn_pressed(B_LEFT)) dir = -g.visible;
			if (dir) {
				sel += dir;
				if (sel < 0) sel = 0;
				if (sel >= nGames) sel = nGames - 1;
				scroll = keep_visible(ls, sel, scroll);
			}
			if (btn_pressed(B_A) || btn_pressed(B_START)) chosen = sel;
		}
		if (btn_pressed(B_B) || btn_released(B_HOME)) quit = true;

		Pointer p;
		while (ui_next_pointer(&p)) {
			if (p.screen != ls) continue;
			if (p.kind == 1) {
				pressed = true;
				dragging = false;
				pressY = p.y;
				pressScroll = scroll;
			} else if (p.kind == 3 && pressed) {
				if (abs(p.y - pressY) > g.rowH / 4) dragging = true;
				if (dragging) scroll = clamp_scroll(ls, pressScroll - (p.y - pressY));
			} else if (p.kind == 2 && pressed) {
				pressed = false;
				if (dragging) continue;
				if (in_rect(p.x, p.y, g.exitBtn)) {
					quit = true;
				} else if (p.y >= g.headerH) {
					int i = (p.y - g.headerH + scroll) / g.rowH;
					if (i >= 0 && i < nGames) {
						sel = i;
						chosen = i;
					}
				}
			}
		}

		ui_clear();
		draw_list(cfg, ls, sel, scroll);
		if (nScreens > 1) draw_preview(cfg, ui_other_screen(), sel);
		ui_present();
		SDL_Delay(15);
	}
	free_thumbs();
	free_brand();
	if (chosen < 0) return false;
	snprintf(outPath, outSize, "%s", games[chosen].path);
	f = fopen(lastFile, "w");
	if (f) {
		fprintf(f, "%s\n", outPath);
		fclose(f);
	}
	return true;
}

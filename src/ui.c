#include "ui.h"
#include "brand.h"
#include "font.h"
#include "logo.h"
#include "paths.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

View views[2];
int nViews;
Screen screens[2];
int nScreens;
bool btnNow[B_COUNT], btnPrev[B_COUNT];
volatile int uiQuit;

static Config* cfg;
static uint64_t ticks;
static SDL_GameController* pads[8];
static SDL_Joystick* rawJoys[8];
static bool debug;

/* Per-view caches for the pixel pipeline. Several entries, because the game
 * and the map can differ in size (e.g. the cropped Metroid maps); a single
 * slot made them evict each other twice per frame. */
#define PIPE_CACHE 4
typedef struct {
	SDL_Texture* tex;
	int w, h, k, strength;
	uint64_t used;
} PipeEntry;
static PipeEntry pipeTargets[2][PIPE_CACHE], pipeGrids[2][PIPE_CACHE];
static uint64_t pipeClock;

#define PQ 64
static Pointer pq[PQ];
static int pqHead, pqTail;

static char osdText[96];
static uint64_t osdUntil;

/* ------------------------------------------------------------- script */

typedef struct {
	long tick;
	uint32_t btns;
	char action[160];
} ScriptLine;
static ScriptLine* script;
static int scriptLen, scriptPos;
static uint32_t scriptBtns;
static long scriptTapUp = -1;
static float tapX, tapY;
static char pendingShot[160];

static uint32_t parse_btns(const char* s) {
	static const char* names[B_COUNT] = { "A", "B", "X", "Y", "START", "SELECT", "HOME", "L", "R", "L2", "R2",
		                                  "UP", "DOWN", "LEFT", "RIGHT" };
	uint32_t m = 0;
	char buf[128];
	snprintf(buf, sizeof(buf), "%s", s);
	for (char* t = strtok(buf, "+"); t; t = strtok(NULL, "+")) {
		for (int i = 0; i < B_COUNT; ++i) {
			if (!strcmp(t, names[i])) m |= 1u << i;
		}
	}
	return m;
}

void ui_load_script(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "cannot open input script %s\n", path);
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		long t;
		char what[160];
		if (line[0] == '#' || sscanf(line, "%ld %159s", &t, what) != 2) continue;
		script = realloc(script, sizeof(*script) * (scriptLen + 1));
		ScriptLine* s = &script[scriptLen++];
		s->tick = t;
		s->btns = 0;
		s->action[0] = 0;
		if (!strncmp(what, "TAP:", 4) || !strncmp(what, "SHOT:", 5) || !strcmp(what, "QUIT")) {
			snprintf(s->action, sizeof(s->action), "%s", what);
		} else {
			s->btns = parse_btns(what);
		}
	}
	fclose(f);
}

static void push_finger(Uint32 type, float x, float y) {
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.tfinger.touchId = 1;
	ev.tfinger.fingerId = 1;
	ev.tfinger.x = x;
	ev.tfinger.y = y;
	ev.tfinger.windowID = SDL_GetWindowID(views[0].win);
	SDL_PushEvent(&ev);
}

static void run_script(void) {
	while (scriptPos < scriptLen && script[scriptPos].tick <= (long) ticks) {
		ScriptLine* s = &script[scriptPos++];
		if (!strncmp(s->action, "TAP:", 4)) {
			sscanf(s->action + 4, "%f,%f", &tapX, &tapY);
			push_finger(SDL_FINGERDOWN, tapX, tapY);
			scriptTapUp = (long) ticks + 2;
		} else if (!strncmp(s->action, "SHOT:", 5)) {
			snprintf(pendingShot, sizeof(pendingShot), "%s", s->action + 5);
		} else if (!strcmp(s->action, "QUIT")) {
			uiQuit = 1;
		} else {
			scriptBtns = s->btns;
		}
	}
	if (scriptTapUp >= 0 && (long) ticks >= scriptTapUp) {
		push_finger(SDL_FINGERUP, tapX, tapY);
		scriptTapUp = -1;
	}
}

/* ------------------------------------------------------------- setup */

static bool create_view(View* v, int display, const char* title, bool fullscreen, int ww, int wh) {
	Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	if (getenv("GBADUAL_WINDOW")) sscanf(getenv("GBADUAL_WINDOW"), "%dx%d", &ww, &wh);
	v->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display),
	                          ww, wh, flags);
	if (!v->win) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return false;
	}
	v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE |
	                                            (cfg->vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
	if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, 0);
	if (!v->ren) {
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(v->ren, 0, 0, 0, 255);
	SDL_RenderClear(v->ren);
	SDL_RenderPresent(v->ren);
	return true;
}

static void open_pads(void) {
	for (int i = 0; i < SDL_NumJoysticks() && i < 8; ++i) {
		if (!pads[i] && SDL_IsGameController(i)) {
			pads[i] = SDL_GameControllerOpen(i);
			if (pads[i]) fprintf(stderr, "controller: %s\n", SDL_GameControllerName(pads[i]));
		} else if (!pads[i] && !rawJoys[i]) {
			rawJoys[i] = SDL_JoystickOpen(i);
			if (rawJoys[i]) fprintf(stderr, "joystick without mapping: %s\n", SDL_JoystickName(rawJoys[i]));
		}
	}
}

bool ui_init(Config* c, bool wantDual) {
	cfg = c;
	debug = getenv("GBADUAL_DEBUG") != NULL;
	SDL_SetHint(SDL_HINT_APP_NAME, "gbadual");
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, cfg->smooth ? "linear" : "nearest");
	setenv("SDL_VIDEO_WAYLAND_WMCLASS", "gbadual", 0);
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return false;
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
	bool dual = !strcmp(cfg->layout, "dual") ||
	            (wantDual && !strcmp(cfg->layout, "auto") && displays >= 2 && !underSway);
	bool fullscreen = strcmp(driver, "offscreen") && strcmp(driver, "dummy") && strcmp(driver, "x11") &&
	                  strcmp(driver, "windows") && strcmp(driver, "cocoa") && !getenv("GBADUAL_WINDOWED");
	fprintf(stderr, "video driver %s, %d display(s)%s -> %s\n", driver, displays, underSway ? ", sway" : "",
	        dual && displays >= 2 ? "two windows" : "one window");
	if (dual && displays >= 2) {
		nViews = 2;
		if (!create_view(&views[0], 0, "gbadual", true, 720, 480) ||
		    !create_view(&views[1], 1, "gbadual map", true, 720, 480)) {
			return false;
		}
	} else {
		nViews = 1;
		bool single = !strcmp(cfg->layout, "single");
		if (!create_view(&views[0], 0, "gbadual", fullscreen, single ? 720 : 1440, 480)) return false;
	}
	ui_layout();
	return true;
}

void ui_shutdown(void) {
	for (int v = 0; v < 2; ++v) {
		for (int i = 0; i < PIPE_CACHE; ++i) {
			if (pipeTargets[v][i].tex) SDL_DestroyTexture(pipeTargets[v][i].tex);
			if (pipeGrids[v][i].tex) SDL_DestroyTexture(pipeGrids[v][i].tex);
		}
	}
	for (int i = 0; i < nViews; ++i) {
		SDL_DestroyRenderer(views[i].ren);
		SDL_DestroyWindow(views[i].win);
	}
	SDL_Quit();
}

void ui_layout(void) {
	if (nViews == 2) {
		nScreens = 2;
		for (int i = 0; i < 2; ++i) {
			int w, h;
			SDL_GetRendererOutputSize(views[i].ren, &w, &h);
			screens[i] = (Screen) { i, { 0, 0, w, h } };
		}
		return;
	}
	int w, h;
	SDL_GetRendererOutputSize(views[0].ren, &w, &h);
	const char* lay = cfg->layout;
	if (!strcmp(lay, "auto") || !strcmp(lay, "dual")) {
		lay = w >= h * 18 / 10 ? "span-h" : h >= w * 12 / 10 ? "span-v" : "single";
	}
	if (!strcmp(lay, "span-h")) {
		nScreens = 2;
		screens[0] = (Screen) { 0, { 0, 0, w / 2, h } };
		screens[1] = (Screen) { 0, { w / 2, 0, w - w / 2, h } };
	} else if (!strcmp(lay, "span-v")) {
		nScreens = 2;
		screens[0] = (Screen) { 0, { 0, 0, w, h / 2 } };
		screens[1] = (Screen) { 0, { 0, h / 2, w, h - h / 2 } };
	} else {
		nScreens = 1;
		screens[0] = (Screen) { 0, { 0, 0, w, h } };
	}
}

static void sway_span(void) {
	if (!cfg->sway || !getenv("SWAYSOCK") || nViews != 1 || !strcmp(cfg->layout, "single")) return;
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "swaymsg '[pid=%d] fullscreen enable global' >/dev/null 2>&1", (int) getpid());
	if (system(cmd) != 0) fprintf(stderr, "note: swaymsg global fullscreen failed\n");
}

void ui_tick(void) {
	++ticks;
	if (ticks == 2 || ticks == 30 || ticks == 120) sway_span();
	run_script();
	if (cfg->quitAfter && (long) ticks >= cfg->quitAfter) uiQuit = 1;
}

uint64_t ui_ticks(void) {
	return ticks;
}

int ui_touch_screen(void) {
	if (nScreens < 2) return 0;
	return !strcmp(cfg->touchScreen, "first") ? 0 : 1;
}

int ui_other_screen(void) {
	if (nScreens < 2) return 0;
	return 1 - ui_touch_screen();
}

/* ------------------------------------------------------------- input */

static int view_of_window(Uint32 id) {
	for (int i = 0; i < nViews; ++i) {
		if (SDL_GetWindowID(views[i].win) == id) return i;
	}
	return -1;
}

static void queue_pointer(int kind, int view, float px, float py, bool normalized) {
	Pointer p = { kind, 0, 0, 0 };
	int ts = ui_touch_screen();
	if (normalized && !strcmp(cfg->touchMap, "screen")) {
		/* touch panel coordinates cover just the touch screen */
		p.screen = ts;
		p.x = (int) (px * screens[ts].rect.w);
		p.y = (int) (py * screens[ts].rect.h);
	} else {
		if (view < 0) view = screens[ts].view;
		int w, h;
		SDL_GetRendererOutputSize(views[view].ren, &w, &h);
		int x, y;
		if (normalized) {
			x = (int) (px * w);
			y = (int) (py * h);
		} else {
			int ww, wh;
			SDL_GetWindowSize(views[view].win, &ww, &wh);
			x = (int) (px * w / (ww ? ww : 1));
			y = (int) (py * h / (wh ? wh : 1));
		}
		p.screen = -1;
		for (int i = 0; i < nScreens; ++i) {
			SDL_Rect* r = &screens[i].rect;
			if (screens[i].view == view && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
				p.screen = i;
				p.x = x - r->x;
				p.y = y - r->y;
			}
		}
		if (p.screen < 0) return;
	}
	if (debug) fprintf(stderr, "pointer kind %d screen %d at %d,%d\n", p.kind, p.screen, p.x, p.y);
	int next = (pqTail + 1) % PQ;
	if (next != pqHead) {
		pq[pqTail] = p;
		pqTail = next;
	}
}

bool ui_next_pointer(Pointer* p) {
	if (pqHead == pqTail) return false;
	*p = pq[pqHead];
	pqHead = (pqHead + 1) % PQ;
	return true;
}

static bool mouseDown;

bool ui_events(void) {
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_QUIT:
			uiQuit = 1;
			break;
		case SDL_CONTROLLERDEVICEADDED:
		case SDL_JOYDEVICEADDED:
			open_pads();
			break;
		case SDL_WINDOWEVENT:
			if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || ev.window.event == SDL_WINDOWEVENT_RESIZED) {
				ui_layout();
			}
			break;
		case SDL_JOYBUTTONDOWN:
			if (debug) fprintf(stderr, "raw joystick %d button %d (use home_button = %d to make it HOME)\n",
			                   (int) ev.jbutton.which, ev.jbutton.button, ev.jbutton.button);
			break;
		case SDL_CONTROLLERBUTTONDOWN:
			if (debug) fprintf(stderr, "controller button %s\n", SDL_GameControllerGetStringForButton(ev.cbutton.button));
			break;
		case SDL_KEYDOWN:
			if (debug) fprintf(stderr, "key %s\n", SDL_GetKeyName(ev.key.keysym.sym));
			break;
		case SDL_FINGERDOWN:
		case SDL_FINGERUP:
		case SDL_FINGERMOTION:
			if (debug && ev.type != SDL_FINGERMOTION) {
				fprintf(stderr, "finger %s %.3f,%.3f window %u\n", ev.type == SDL_FINGERDOWN ? "down" : "up",
				        ev.tfinger.x, ev.tfinger.y, ev.tfinger.windowID);
			}
			queue_pointer(ev.type == SDL_FINGERDOWN ? 1 : ev.type == SDL_FINGERUP ? 2 : 3,
			              view_of_window(ev.tfinger.windowID), ev.tfinger.x, ev.tfinger.y, true);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			if (ev.button.which == SDL_TOUCH_MOUSEID) break;
			mouseDown = ev.type == SDL_MOUSEBUTTONDOWN;
			queue_pointer(mouseDown ? 1 : 2, view_of_window(ev.button.windowID), ev.button.x, ev.button.y, false);
			break;
		case SDL_MOUSEMOTION:
			if (ev.motion.which == SDL_TOUCH_MOUSEID || !mouseDown) break;
			queue_pointer(3, view_of_window(ev.motion.windowID), ev.motion.x, ev.motion.y, false);
			break;
		}
	}
	return !uiQuit;
}

void ui_read_buttons(void) {
	memcpy(btnPrev, btnNow, sizeof(btnNow));
	memset(btnNow, 0, sizeof(btnNow));
	bool phys[SDL_CONTROLLER_BUTTON_MAX] = { 0 };
	bool l2 = false, r2 = false;
	for (int p = 0; p < 8; ++p) {
		SDL_GameController* gc = pads[p];
		if (!gc) continue;
		for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
			if (SDL_GameControllerGetButton(gc, b)) phys[b] = true;
		}
		if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) l2 = true;
		if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) r2 = true;
		int lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
		int ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
		if (lx < -16000) phys[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = true;
		if (lx > 16000) phys[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = true;
		if (ly < -16000) phys[SDL_CONTROLLER_BUTTON_DPAD_UP] = true;
		if (ly > 16000) phys[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = true;
	}
	bool rawHome = false;
	if (cfg->homeButton >= 0) {
		for (int p = 0; p < 8; ++p) {
			SDL_Joystick* j = pads[p] ? SDL_GameControllerGetJoystick(pads[p]) : rawJoys[p];
			if (j && cfg->homeButton < SDL_JoystickNumButtons(j) && SDL_JoystickGetButton(j, cfg->homeButton)) rawHome = true;
		}
	}
	/* GBA-style: A is the right face button, B the bottom one */
	btnNow[B_A] = cfg->abSwap ? phys[SDL_CONTROLLER_BUTTON_A] : phys[SDL_CONTROLLER_BUTTON_B];
	btnNow[B_B] = cfg->abSwap ? phys[SDL_CONTROLLER_BUTTON_B] : phys[SDL_CONTROLLER_BUTTON_A];
	btnNow[B_X] = phys[SDL_CONTROLLER_BUTTON_X];
	btnNow[B_Y] = phys[SDL_CONTROLLER_BUTTON_Y];
	btnNow[B_START] = phys[SDL_CONTROLLER_BUTTON_START];
	btnNow[B_SELECT] = phys[SDL_CONTROLLER_BUTTON_BACK];
	btnNow[B_HOME] = phys[SDL_CONTROLLER_BUTTON_GUIDE] || rawHome;
	btnNow[B_L] = phys[SDL_CONTROLLER_BUTTON_LEFTSHOULDER];
	btnNow[B_R] = phys[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER];
	btnNow[B_L2] = l2;
	btnNow[B_R2] = r2;
	btnNow[B_UP] = phys[SDL_CONTROLLER_BUTTON_DPAD_UP];
	btnNow[B_DOWN] = phys[SDL_CONTROLLER_BUTTON_DPAD_DOWN];
	btnNow[B_LEFT] = phys[SDL_CONTROLLER_BUTTON_DPAD_LEFT];
	btnNow[B_RIGHT] = phys[SDL_CONTROLLER_BUTTON_DPAD_RIGHT];

	/* keyboard (desktop testing, and devices whose HOME key arrives as a key) */
	const Uint8* k = SDL_GetKeyboardState(NULL);
	if (k[SDL_SCANCODE_X]) btnNow[B_A] = true;
	if (k[SDL_SCANCODE_Z]) btnNow[B_B] = true;
	if (k[SDL_SCANCODE_A]) btnNow[B_L] = true;
	if (k[SDL_SCANCODE_S]) btnNow[B_R] = true;
	if (k[SDL_SCANCODE_RETURN]) btnNow[B_START] = true;
	if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT]) btnNow[B_SELECT] = true;
	if (k[SDL_SCANCODE_UP]) btnNow[B_UP] = true;
	if (k[SDL_SCANCODE_DOWN]) btnNow[B_DOWN] = true;
	if (k[SDL_SCANCODE_LEFT]) btnNow[B_LEFT] = true;
	if (k[SDL_SCANCODE_RIGHT]) btnNow[B_RIGHT] = true;
	if (k[SDL_SCANCODE_TAB]) btnNow[B_L2] = true;
	if (k[SDL_SCANCODE_SPACE]) btnNow[B_R2] = true;
	if (k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_F1] || k[SDL_SCANCODE_HOME] || k[SDL_SCANCODE_AC_HOME] ||
	    k[SDL_SCANCODE_MENU] || k[SDL_SCANCODE_APPLICATION]) {
		btnNow[B_HOME] = true;
	}

	for (int i = 0; i < B_COUNT; ++i) {
		if (scriptBtns & (1u << i)) btnNow[i] = true;
	}
}

/* ------------------------------------------------------------- drawing */

void ui_clear(void) {
	for (int i = 0; i < nViews; ++i) {
		SDL_SetRenderDrawColor(views[i].ren, 0, 0, 0, 255);
		SDL_RenderClear(views[i].ren);
	}
}

void ui_present(void) {
	if (pendingShot[0]) {
		ui_screenshot(pendingShot);
		pendingShot[0] = 0;
	}
	for (int i = 0; i < nViews; ++i) SDL_RenderPresent(views[i].ren);
}

SDL_Renderer* ui_ren(int screen) {
	return views[screens[screen].view].ren;
}

static void fit_rect(const SDL_Rect* area, int w, int h, const char* mode, SDL_Rect* out) {
	if (!strcmp(mode, "stretch")) {
		*out = *area;
		return;
	}
	double s = SDL_min((double) area->w / w, (double) area->h / h);
	if (!strcmp(mode, "integer") && s >= 1.0) s = (int) s;
	out->w = (int) (w * s);
	out->h = (int) (h * s);
	out->x = area->x + (area->w - out->w) / 2;
	out->y = area->y + (area->h - out->h) / 2;
}

void ui_image(int screen, SDL_Texture* const* texPerView, int w, int h, const char* mode, const SDL_Rect* areaLocal) {
	Screen* s = &screens[screen];
	SDL_Rect area = areaLocal ? *areaLocal : (SDL_Rect) { 0, 0, s->rect.w, s->rect.h };
	area.x += s->rect.x;
	area.y += s->rect.y;
	SDL_Rect dst;
	fit_rect(&area, w, h, mode, &dst);
	SDL_RenderCopy(views[s->view].ren, texPerView[s->view], NULL, &dst);
}


static PipeEntry* pipe_lookup(PipeEntry* cache, int w, int h, int k, int strength, bool* hit) {
	PipeEntry* victim = &cache[0];
	for (int i = 0; i < PIPE_CACHE; ++i) {
		PipeEntry* e = &cache[i];
		if (e->tex && e->w == w && e->h == h && e->k == k && e->strength == strength) {
			e->used = ++pipeClock;
			*hit = true;
			return e;
		}
		if (!e->tex || e->used < victim->used) victim = e;
	}
	if (victim->tex) SDL_DestroyTexture(victim->tex);
	memset(victim, 0, sizeof(*victim));
	victim->w = w, victim->h = h, victim->k = k, victim->strength = strength;
	victim->used = ++pipeClock;
	*hit = false;
	return victim;
}

static SDL_Texture* pipe_target(int v, int w, int h) {
	bool hit;
	PipeEntry* e = pipe_lookup(pipeTargets[v], w, h, 0, 0, &hit);
	if (!hit) {
		e->tex = SDL_CreateTexture(views[v].ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
		if (e->tex) SDL_SetTextureBlendMode(e->tex, SDL_BLENDMODE_NONE);
	}
	return e->tex;
}

/* A multiply mask: each source pixel becomes a k x k cell whose right column
 * and bottom row are darkened, like the gaps between LCD pixels. Built once. */
static SDL_Texture* pipe_grid(int v, int w, int h, int k, int strength) {
	strength = strength < 0 ? 0 : strength > 100 ? 100 : strength;
	bool hit;
	PipeEntry* e = pipe_lookup(pipeGrids[v], w, h, k, strength, &hit);
	if (hit) return e->tex;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w * k, h * k, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) return NULL;
	Uint8 line = (Uint8) (255 * (100 - strength) / 100);
	Uint8 soft = (Uint8) (255 * (100 - strength / 3) / 100); /* top/left edge: a lighter line for depth */
	for (int y = 0; y < h * k; ++y) {
		Uint32* row = (Uint32*) ((Uint8*) s->pixels + y * s->pitch);
		int cy = y % k;
		for (int x = 0; x < w * k; ++x) {
			int cx = x % k;
			Uint8 c = 255;
			if (cx == k - 1 || cy == k - 1) c = line;
			else if (k >= 5 && (cx == 0 || cy == 0)) c = soft;
			row[x] = 0xFF000000u | (c << 16) | (c << 8) | c;
		}
	}
	e->tex = SDL_CreateTextureFromSurface(views[v].ren, s);
	SDL_FreeSurface(s);
	if (e->tex) {
		SDL_SetTextureBlendMode(e->tex, SDL_BLENDMODE_MOD);
		SDL_SetTextureScaleMode(e->tex, SDL_ScaleModeNearest);
	}
	return e->tex;
}

void ui_image_pixels(int screen, SDL_Texture* const* texPerView, int w, int h, const char* mode, bool lcd) {
	Screen* s = &screens[screen];
	int v = s->view;
	SDL_Renderer* ren = views[v].ren;
	SDL_Texture* src = texPerView[v];
	SDL_Rect dst;
	fit_rect(&s->rect, w, h, mode, &dst);
	int k = SDL_max(1, SDL_min(dst.w / w, dst.h / h));
	bool exact = dst.w == w * k && dst.h == h * k;
	if (!lcd && (exact || cfg->smooth)) {
		SDL_SetTextureScaleMode(src, cfg->smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
		SDL_RenderCopy(ren, src, NULL, &dst);
		return;
	}
	if (lcd && exact && k >= 3) {
		/* Pixel perfect: draw straight to the screen, grid multiplied on top.
		 * No render-target switch, so tiled GPUs don't have to flush and
		 * reload the whole framebuffer. */
		SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
		SDL_RenderCopy(ren, src, NULL, &dst);
		SDL_Texture* grid = pipe_grid(v, w, h, k, cfg->lcdStrength);
		if (grid) SDL_RenderCopy(ren, grid, NULL, &dst);
		return;
	}
	if (lcd && k < 3) k = 3; /* a grid needs a few pixels per cell */
	SDL_Texture* target = SDL_RenderTargetSupported(ren) ? pipe_target(v, w * k, h * k) : NULL;
	if (!target) {
		/* no render targets: draw directly, grid on top */
		SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
		SDL_RenderCopy(ren, src, NULL, &dst);
		if (lcd) {
			SDL_Texture* grid = pipe_grid(v, w, h, k, cfg->lcdStrength);
			if (grid) SDL_RenderCopy(ren, grid, NULL, &dst);
		}
		return;
	}
	SDL_SetRenderTarget(ren, target);
	SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
	SDL_RenderCopy(ren, src, NULL, NULL);
	if (lcd) {
		SDL_Texture* grid = pipe_grid(v, w, h, k, cfg->lcdStrength);
		if (grid) SDL_RenderCopy(ren, grid, NULL, NULL);
	}
	SDL_SetRenderTarget(ren, NULL);
	SDL_SetTextureScaleMode(target, exact ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
	SDL_RenderCopy(ren, target, NULL, &dst);
}

SDL_Rect ui_image_rect(int screen, int w, int h, const char* mode) {
	SDL_Rect area = { 0, 0, screens[screen].rect.w, screens[screen].rect.h }, dst;
	fit_rect(&area, w, h, mode, &dst);
	return dst;
}

void ui_fill(int screen, SDL_Rect r, SDL_Color c) {
	Screen* s = &screens[screen];
	r.x += s->rect.x;
	r.y += s->rect.y;
	SDL_Renderer* ren = views[s->view].ren;
	SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
	SDL_RenderFillRect(ren, &r);
}

void ui_dim(int screen, Uint8 alpha) {
	ui_fill(screen, (SDL_Rect) { 0, 0, screens[screen].rect.w, screens[screen].rect.h }, (SDL_Color) { 0, 0, 0, alpha });
}

int ui_px(int screen) {
	return SDL_max(2, screens[screen].rect.h / 192);
}

int ui_text_width(const char* s, int px) {
	int n = (int) strlen(s);
	return n ? n * 6 * px - px : 0;
}

void ui_text(int screen, int x, int y, int px, const char* str, SDL_Color c) {
	Screen* s = &screens[screen];
	x += s->rect.x;
	y += s->rect.y;
	SDL_Renderer* ren = views[s->view].ren;
	SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
	SDL_Rect rects[256];
	int n = 0;
	for (; *str; ++str, x += 6 * px) {
		const uint8_t* g = font_glyph(*str);
		for (int row = 0; row < 7; ++row) {
			for (int col = 0; col < 5; ++col) {
				if ((g[row] >> (4 - col)) & 1) {
					rects[n++] = (SDL_Rect) { x + col * px, y + row * px, px, px };
					if (n == 256) {
						SDL_RenderFillRects(ren, rects, n);
						n = 0;
					}
				}
			}
		}
	}
	if (n) SDL_RenderFillRects(ren, rects, n);
}

void ui_text_center(int screen, int cx, int y, int px, const char* s, SDL_Color c) {
	ui_text(screen, cx - ui_text_width(s, px) / 2, y, px, s, c);
}

SDL_Surface* image_load(const char* path); /* image.c */

SDL_Surface* image_load_mem(const unsigned char* data, size_t size); /* image.c */

static bool textures_from(SDL_Surface* s, SDL_Texture* out[2], int* w, int* h, bool smooth) {
	out[0] = out[1] = NULL;
	if (!s) return false;
	*w = s->w;
	*h = s->h;
	for (int v = 0; v < nViews; ++v) {
		out[v] = SDL_CreateTextureFromSurface(views[v].ren, s);
		if (out[v]) SDL_SetTextureBlendMode(out[v], SDL_BLENDMODE_BLEND);
		if (out[v] && smooth) SDL_SetTextureScaleMode(out[v], SDL_ScaleModeLinear);
	}
	SDL_FreeSurface(s);
	return true;
}

bool ui_load_image(const char* path, SDL_Texture* out[2], int* w, int* h, bool smooth) {
	return textures_from(image_load(path), out, w, h, smooth);
}

bool ui_load_brand(int id, SDL_Texture* out[2], int* w, int* h) {
	return textures_from(image_load_mem(brandImages[id].png, brandImages[id].size), out, w, h, true);
}

void ui_free_image(SDL_Texture* tex[2]) {
	for (int v = 0; v < 2; ++v) {
		if (tex[v]) SDL_DestroyTexture(tex[v]);
		tex[v] = NULL;
	}
}

static bool file_there(const char* p) {
	FILE* f = fopen(p, "rb");
	if (f) fclose(f);
	return f != NULL;
}

bool ui_load_logo(const Config* c, const char* rom, const char* code, SDL_Texture* tex[2], int* w, int* h,
                  bool* pixelArt) {
	char path[1200];
	/* a file the user supplied wins */
	const char* exts[] = { "png", "bmp" };
	for (int i = 0; i < 2; ++i) {
		snprintf(path, sizeof(path), "%s/logos/%.4s.%s", c->assetDir, code, exts[i]);
		if (file_there(path)) {
			*pixelArt = false;
			return ui_load_image(path, tex, w, h, true);
		}
	}
	/* otherwise the one extracted from the game's own title screen */
	snprintf(path, sizeof(path), "%s/logos", c->stateDir);
	mkdirs(path);
	snprintf(path, sizeof(path), "%s/logos/%.4s.png", c->stateDir, code);
	*pixelArt = true;
	if (ui_load_image(path, tex, w, h, false)) return true;
	if (rom) logo_request(rom, code, path);
	return false;
}

void ui_screenshot(const char* path) {
	View* v = &views[0];
	int w, h;
	SDL_GetRendererOutputSize(v->ren, &w, &h);
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (SDL_RenderReadPixels(v->ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
		SDL_SaveBMP(s, path);
		fprintf(stderr, "screenshot %dx%d -> %s\n", w, h, path);
	} else {
		fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
	}
	SDL_FreeSurface(s);
}

void ui_osd(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(osdText, sizeof(osdText), fmt, ap);
	va_end(ap);
	osdUntil = ticks + 120;
}

const char* ui_osd_text(void) {
	return ticks < osdUntil ? osdText : NULL;
}

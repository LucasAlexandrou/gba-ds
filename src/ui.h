/* Windows, physical screens, input and drawing shared by the library and the game. */
#ifndef GBADUAL_UI_H
#define GBADUAL_UI_H

#include "config.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
	SDL_Window* win;
	SDL_Renderer* ren;
} View;

/* A physical screen: a rectangle inside one view's output. */
typedef struct {
	int view;
	SDL_Rect rect;
} Screen;

extern View views[2];
extern int nViews;
extern Screen screens[2];
extern int nScreens;

/* Logical buttons (after face-button mapping). */
enum {
	B_A, B_B, B_X, B_Y, B_START, B_SELECT, B_HOME, B_L, B_R, B_L2, B_R2,
	B_UP, B_DOWN, B_LEFT, B_RIGHT, B_COUNT
};
extern bool btnNow[B_COUNT], btnPrev[B_COUNT];
static inline bool btn_pressed(int b) {
	return btnNow[b] && !btnPrev[b];
}
static inline bool btn_released(int b) {
	return !btnNow[b] && btnPrev[b];
}

/* Pointer (touch or mouse) event in screen-local pixels. */
typedef struct {
	int kind; /* 0 none, 1 down, 2 up, 3 motion */
	int screen;
	int x, y;
} Pointer;

bool ui_init(Config* cfg, bool wantDual);
void ui_shutdown(void);
void ui_layout(void);
/* Call once per loop iteration: sway fixups, script ticks. */
void ui_tick(void);
uint64_t ui_ticks(void);

int ui_touch_screen(void); /* index into screens[] */
int ui_other_screen(void); /* the non-touch screen, or the touch screen if there's only one */

/* Pump SDL events. Returns false when the app should quit. Pointer events are
 * queued for ui_next_pointer(); window changes re-run the layout. */
bool ui_events(void);
bool ui_next_pointer(Pointer* p);
void ui_read_buttons(void);

/* Input script for automated testing (see config_usage). */
void ui_load_script(const char* path);

void ui_clear(void);
void ui_present(void);

SDL_Renderer* ui_ren(int screen);
/* Draws a texture (one per view, indexed by view) letterboxed into a screen area. */
void ui_image(int screen, SDL_Texture* const* texPerView, int w, int h, const char* mode, const SDL_Rect* areaLocal);
/* Pixel-art drawing for the game and map: whole-number prescale (sharp at any
 * size), optional LCD grid, then filtered to the final size on the GPU. */
void ui_image_pixels(int screen, SDL_Texture* const* texPerView, int w, int h, const char* mode, bool lcd);
/* Where ui_image_pixels puts a w x h image on a screen (screen-local coords). */
SDL_Rect ui_image_rect(int screen, int w, int h, const char* mode);
void ui_fill(int screen, SDL_Rect local, SDL_Color c);
void ui_dim(int screen, Uint8 alpha);
void ui_text(int screen, int x, int y, int px, const char* s, SDL_Color c);
int ui_text_width(const char* s, int px);
void ui_text_center(int screen, int cx, int y, int px, const char* s, SDL_Color c);
/* Pixel size for text so that it is legible on this screen. */
int ui_px(int screen);

/* Loads a BMP into one texture per view. Returns false if missing. */
bool ui_load_image(const char* path, SDL_Texture* out[2], int* w, int* h, bool smooth);
void ui_free_image(SDL_Texture* tex[2]);
/* A game's title logo: <asset_dir>/logos/<CODE>.png|.bmp if the user put one
 * there, else the one extracted from the ROM (<state_dir>/logos/<CODE>.png),
 * which is queued for extraction in the background if it doesn't exist yet
 * (watch logo_generation() and call again). pixelArt: draw with "integer". */
bool ui_load_logo(const Config* cfg, const char* rom, const char* code, SDL_Texture* tex[2], int* w, int* h,
                  bool* pixelArt);

void ui_screenshot(const char* path);
void ui_osd(const char* fmt, ...);
const char* ui_osd_text(void); /* NULL when expired */

extern volatile int uiQuit; /* set by signals */

#endif

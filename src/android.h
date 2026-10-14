/* Android glue (android.c, Android builds only): the second screen, which
 * Android gives to a Presentation window in Java instead of to SDL, the log,
 * and the start-up defaults. */
#ifndef GBADS_ANDROID_H
#define GBADS_ANDROID_H
#ifdef __ANDROID__

#include "config.h"

#include <SDL.h>
#include <stdbool.h>

/* windowID on the finger events forwarded from the second screen */
#define GBADS_SECOND_WINDOW_ID 0x6BD50002u

/* stderr (where all of GBA DS's logging goes) to logcat (tag GBADS) and to
 * <GBADS_HOME>/gba-ds.log. Call first thing in main. */
void android_init_log(void);
/* The folder Java gave us for config, states, logos (GBADS_HOME), or NULL. */
const char* android_home(void);
/* Android defaults: ROM folders, screenshots, cheats, no sway. */
void android_config_defaults(Config* cfg);
/* No game given: the first game in the ROM folders that has a map profile
 * (else the first game at all). False if there's none. */
bool android_pick_rom(const Config* cfg, char* out, size_t size);

/* Size of the second display, if there is one. */
bool android_second_size(int* w, int* h);
/* Copies a finished frame (ABGR8888, i.e. R,G,B,A bytes) to the second
 * display's surface, if it has one right now. */
void android_second_present(SDL_Surface* frame);

/* GPU path: draw the second screen with the game screen's OpenGL ES
 * context. _gpu_init on the game thread with the context current (w x h: the
 * game screen's size); then each redraw between _begin (false: no window
 * yet, or it failed) and _end. */
bool android_second_gpu_init(int w, int h);
bool android_second_begin(void);
void android_second_end(void);

#endif
#endif

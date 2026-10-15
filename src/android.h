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
/* Opens Android's folder picker (the activity goes to the background while
 * it's up). False if it couldn't. The answer comes later: */
bool android_pick_folder(void);
/* 1: a folder was picked (its path in out), -1: cancelled, 0: no answer yet. */
int android_take_picked_folder(char* out, size_t size);

/* RetroAchievements: an HTTP request through Java (Http.java), from any
 * thread. post NULL: GET. outPath: download to that file (body unused).
 * Returns the HTTP status, or -1 if there was no answer; *body is
 * malloc'd and 0-terminated. */
int android_http(const char* url, const char* contentType, const char* post, const char* userAgent, const char* outPath,
                 char** body, size_t* len);
/* The sign-in dialog (username prefilled): false if it couldn't open. Its
 * answer comes later: 1 entered (user, password), -1 cancelled, 0 none yet. */
bool android_ra_dialog(const char* user);
int android_take_ra_credentials(char* user, size_t userSize, char* pass, size_t passSize);

/* Performance hint (Android 13+): on the game thread, once; then each
 * frame's work time (excluding the wait for the next frame). */
void android_perf_init(int64_t targetNs);
void android_perf_report(int64_t workNs);

/* Size of the second display, if there is one. */
bool android_second_size(int* w, int* h);
/* Copies a finished frame (ABGR8888, i.e. R,G,B,A bytes) to the second
 * display's surface, if it has one right now. */
void android_second_present(SDL_Surface* frame);

/* GPU path: draw the second screen with the game activity's OpenGL ES
 * context. _gpu_init on the game thread with the context current (w x h: the
 * activity's screen size); then each redraw between _begin (false: no window
 * yet, or it failed) and _end, after which that surface stays current. */
bool android_second_gpu_init(int w, int h);
bool android_second_begin(void);
void android_second_end(void);
/* Back to SDL's own window surface (before drawing the game activity's
 * screen, and before Android pauses the app). */
void android_second_restore(void);
void android_second_log_timing(int frames);

#endif
#endif

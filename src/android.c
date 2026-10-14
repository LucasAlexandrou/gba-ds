/* Android glue: see android.h. The Java side is in android/app/src/main/java
 * (GbaDsActivity, SecondScreen). */
#ifdef __ANDROID__

#include "android.h"
#include "emu.h"
#include "profiles.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <dirent.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define TAG "GBADS"

/* ------------------------------------------------------------- log */

static FILE* logFile;

static void* log_thread(void* arg) {
	FILE* in = fdopen((int) (intptr_t) arg, "r");
	char line[1024];
	while (in && fgets(line, sizeof(line), in)) {
		size_t n = strlen(line);
		if (logFile) {
			fputs(line, logFile);
			fflush(logFile);
		}
		if (n && line[n - 1] == '\n') line[n - 1] = 0;
		__android_log_write(ANDROID_LOG_INFO, TAG, line);
	}
	return NULL;
}

void android_init_log(void) {
	static bool done;
	if (done) return;
	done = true;
	const char* home = android_home();
	if (home) {
		char path[1100];
		snprintf(path, sizeof(path), "%s/gba-ds.log", home);
		logFile = fopen(path, "w");
	}
	int fds[2];
	if (pipe(fds) != 0) return;
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);
	close(fds[1]);
	pthread_t t;
	if (pthread_create(&t, NULL, log_thread, (void*) (intptr_t) fds[0]) == 0) pthread_detach(t);
}

const char* android_home(void) {
	const char* h = getenv("GBADS_HOME");
	return h && *h ? h : NULL;
}

/* ------------------------------------------------------------- defaults */

void android_config_defaults(Config* c) {
	snprintf(c->romDir, sizeof(c->romDir), "/storage/emulated/0/ROMs/gba:/storage/emulated/0/ROMs/GBA");
	snprintf(c->screenshotDir, sizeof(c->screenshotDir), "/storage/emulated/0/Pictures/GBA DS");
	snprintf(c->cheatDir, sizeof(c->cheatDir), "/storage/emulated/0/RetroArch/cheats");
	c->esConfigDir[0] = 0;
	c->sway = false;
}

static int cmp_names(const void* a, const void* b) {
	return strcasecmp(*(char* const*) a, *(char* const*) b);
}

bool android_pick_rom(const Config* cfg, char* out, size_t size) {
	char dirs[sizeof(cfg->romDir)];
	snprintf(dirs, sizeof(dirs), "%s", cfg->romDir);
	char first[1024] = "";
	for (char* d = strtok(dirs, ":"); d; d = strtok(NULL, ":")) {
		DIR* dir = opendir(d);
		if (!dir) {
			fprintf(stderr, "rom folder %s: not readable\n", d);
			continue;
		}
		char** names = NULL;
		int n = 0;
		struct dirent* de;
		while ((de = readdir(dir))) {
			if (de->d_name[0] == '.' || !emu_is_rom_name(de->d_name)) continue;
			names = realloc(names, sizeof(*names) * (n + 1));
			names[n++] = strdup(de->d_name);
		}
		closedir(dir);
		qsort(names, n, sizeof(*names), cmp_names);
		fprintf(stderr, "rom folder %s: %d game(s)\n", d, n);
		bool found = false;
		for (int i = 0; i < n && !found; ++i) {
			char path[1024];
			snprintf(path, sizeof(path), "%s/%s", d, names[i]);
			uint8_t hdr[0xB0];
			if (!emu_rom_header(path, hdr, sizeof(hdr))) continue;
			char code[5];
			memcpy(code, hdr + 0xAC, 4);
			code[4] = 0;
			if (!first[0]) snprintf(first, sizeof(first), "%s", path);
			if (profile_find(code)) {
				snprintf(out, size, "%s", path);
				found = true;
			}
		}
		for (int i = 0; i < n; ++i) free(names[i]);
		free(names);
		if (found) return true;
	}
	if (first[0]) {
		snprintf(out, size, "%s", first);
		return true;
	}
	return false;
}

/* ------------------------------------------------------------- second screen */

/* Frames go to the second display from a thread of their own: handing a
 * buffer to a window (ANativeWindow_lock) waits for that display's refresh,
 * and doing it on the game loop on top of the game screen's own wait capped
 * the game at 30 fps. The loop copies the finished frame into "back" and
 * moves on; the thread takes the newest frame and posts it. */
static pthread_mutex_t winLock = PTHREAD_MUTEX_INITIALIZER;
static ANativeWindow* secondWin;
static int dispW, dispH;
static bool geometrySet;

static pthread_mutex_t bufLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bufCond = PTHREAD_COND_INITIALIZER;
static uint8_t *back, *front;
static int bufW, bufH;
static bool fresh;

bool android_second_size(int* w, int* h) {
	pthread_mutex_lock(&winLock);
	*w = dispW;
	*h = dispH;
	pthread_mutex_unlock(&winLock);
	return *w > 0 && *h > 0;
}

static void post_frame(const uint8_t* px, int fw, int fh) {
	pthread_mutex_lock(&winLock);
	if (secondWin) {
		if (!geometrySet) {
			/* the compositor scales our buffer to the surface if they differ */
			ANativeWindow_setBuffersGeometry(secondWin, fw, fh, WINDOW_FORMAT_RGBX_8888);
			geometrySet = true;
		}
		ANativeWindow_Buffer b;
		if (ANativeWindow_lock(secondWin, &b, NULL) == 0) {
			int w = SDL_min(b.width, fw), h = SDL_min(b.height, fh);
			for (int y = 0; y < h; ++y) {
				memcpy((uint8_t*) b.bits + (size_t) y * b.stride * 4, px + (size_t) y * fw * 4, (size_t) w * 4);
			}
			ANativeWindow_unlockAndPost(secondWin);
		}
	}
	pthread_mutex_unlock(&winLock);
}

static void* present_thread(void* arg) {
	for (;;) {
		pthread_mutex_lock(&bufLock);
		while (!fresh) pthread_cond_wait(&bufCond, &bufLock);
		uint8_t* t = front;
		front = back;
		back = t;
		fresh = false;
		int w = bufW, h = bufH;
		pthread_mutex_unlock(&bufLock);
		post_frame(front, w, h);
	}
	return NULL;
}

void android_second_present(SDL_Surface* s) {
	static bool started;
	pthread_mutex_lock(&bufLock);
	if (!back || bufW != s->w || bufH != s->h) {
		free(back);
		free(front);
		back = malloc((size_t) s->w * s->h * 4);
		front = malloc((size_t) s->w * s->h * 4);
		bufW = s->w;
		bufH = s->h;
	}
	if (back && front) {
		for (int y = 0; y < s->h; ++y) {
			memcpy(back + (size_t) y * s->w * 4, (const uint8_t*) s->pixels + (size_t) y * s->pitch, (size_t) s->w * 4);
		}
		fresh = true;
		pthread_cond_signal(&bufCond);
	}
	pthread_mutex_unlock(&bufLock);
	if (!started) {
		started = true;
		pthread_t t;
		if (pthread_create(&t, NULL, present_thread, NULL) == 0) pthread_detach(t);
	}
}

JNIEXPORT void JNICALL Java_io_github_kyannon_gbads_SecondScreen_nativeSetDisplay(JNIEnv* env, jclass cls, jint w, jint h) {
	pthread_mutex_lock(&winLock);
	dispW = w;
	dispH = h;
	pthread_mutex_unlock(&winLock);
}

/* Called on the UI thread; holding the lock means the native side is done
 * with the old surface before Java lets it go. */
JNIEXPORT void JNICALL Java_io_github_kyannon_gbads_SecondScreen_nativeSetSurface(JNIEnv* env, jclass cls, jobject surface) {
	pthread_mutex_lock(&winLock);
	if (secondWin) ANativeWindow_release(secondWin);
	secondWin = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
	geometrySet = false;
	pthread_mutex_unlock(&winLock);
	__android_log_print(ANDROID_LOG_INFO, TAG, "second screen surface %s", surface ? "ready" : "gone");
}

/* kind: 1 down, 2 up, 3 motion; x, y: 0..1 across the second display.
 * Goes through SDL's event queue (thread-safe) to ui.c, which treats the
 * window id as the second view. */
JNIEXPORT void JNICALL Java_io_github_kyannon_gbads_SecondScreen_nativeTouch(JNIEnv* env, jclass cls, jint kind, jfloat x,
                                                                            jfloat y) {
	if (!SDL_WasInit(SDL_INIT_EVENTS)) return;
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = kind == 1 ? SDL_FINGERDOWN : kind == 2 ? SDL_FINGERUP : SDL_FINGERMOTION;
	ev.tfinger.touchId = 2;
	ev.tfinger.fingerId = 0;
	ev.tfinger.x = x;
	ev.tfinger.y = y;
	ev.tfinger.pressure = 1.0f;
	ev.tfinger.windowID = GBADS_SECOND_WINDOW_ID;
	SDL_PushEvent(&ev);
}

#endif

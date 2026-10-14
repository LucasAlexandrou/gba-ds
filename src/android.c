/* Android glue: see android.h. The Java side is in android/app/src/main/java
 * (GbaDsActivity, SecondScreen). */
#ifdef __ANDROID__

#include "android.h"
#include "emu.h"
#include "profiles.h"

#include <EGL/egl.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
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
	/* Android keeps HOME for itself */
	snprintf(c->menuCombo, sizeof(c->menuCombo), "select+start");
}

/* ------------------------------------------------------------- ROM folder picker */

static pthread_mutex_t pickLock = PTHREAD_MUTEX_INITIALIZER;
static char picked[1024];
static int pickState; /* 0 nothing, 1 a folder picked, -1 cancelled */

bool android_pick_folder(void) {
	JNIEnv* env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	jobject activity = (jobject) SDL_AndroidGetActivity();
	if (!env || !activity) return false;
	/* by the activity's class: FindClass on this thread wouldn't see the app's classes */
	jclass cls = (*env)->GetObjectClass(env, activity);
	jmethodID m = (*env)->GetMethodID(env, cls, "pickRomFolder", "()V");
	bool ok = m != NULL;
	if (ok) {
		pthread_mutex_lock(&pickLock);
		pickState = 0;
		pthread_mutex_unlock(&pickLock);
		(*env)->CallVoidMethod(env, activity, m);
	}
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionClear(env);
		ok = false;
	}
	(*env)->DeleteLocalRef(env, cls);
	(*env)->DeleteLocalRef(env, activity);
	return ok;
}

int android_take_picked_folder(char* out, size_t size) {
	pthread_mutex_lock(&pickLock);
	int st = pickState;
	if (st == 1) snprintf(out, size, "%s", picked);
	pickState = 0;
	pthread_mutex_unlock(&pickLock);
	return st;
}

JNIEXPORT void JNICALL Java_io_github_kyannon_gbads_GbaDsActivity_nativeFolderPicked(JNIEnv* env, jclass cls, jstring path) {
	const char* p = path ? (*env)->GetStringUTFChars(env, path, NULL) : NULL;
	pthread_mutex_lock(&pickLock);
	if (p && *p) {
		snprintf(picked, sizeof(picked), "%s", p);
		pickState = 1;
	} else {
		pickState = -1;
	}
	pthread_mutex_unlock(&pickLock);
	if (p) (*env)->ReleaseStringUTFChars(env, path, p);
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

/* --- GPU path: SDL's OpenGL ES context draws into an EGL window surface on
 * the second display's window, between android_second_begin / _end on the
 * game thread. The software path above is the fallback. */
static EGLConfig gpuConfig;
static bool gpuOn;
static int gpuW, gpuH;
static EGLDisplay surf2Dpy = EGL_NO_DISPLAY;
static EGLSurface surf2 = EGL_NO_SURFACE;
static bool surf2Interval;
static EGLDisplay savedDpy;
static EGLSurface savedDraw, savedRead;
static EGLContext savedCtx;

bool android_second_gpu_init(int w, int h) {
	EGLDisplay d = eglGetCurrentDisplay();
	EGLContext c = eglGetCurrentContext();
	if (d == EGL_NO_DISPLAY || c == EGL_NO_CONTEXT) return false;
	EGLint id = 0, n = 0;
	if (!eglQueryContext(d, c, EGL_CONFIG_ID, &id)) return false;
	EGLint attrs[] = { EGL_CONFIG_ID, id, EGL_NONE };
	if (!eglChooseConfig(d, attrs, &gpuConfig, 1, &n) || n != 1) return false;
	gpuW = w;
	gpuH = h;
	gpuOn = true;
	return true;
}

bool android_second_begin(void) {
	pthread_mutex_lock(&winLock);
	EGLDisplay d = eglGetCurrentDisplay();
	EGLContext c = eglGetCurrentContext();
	if (!gpuOn || !secondWin || d == EGL_NO_DISPLAY || c == EGL_NO_CONTEXT) goto fail;
	if (surf2 == EGL_NO_SURFACE) {
		EGLint fmt = 0;
		eglGetConfigAttrib(d, gpuConfig, EGL_NATIVE_VISUAL_ID, &fmt);
		/* the same size as the game screen, so SDL's viewport fits both; the
		 * compositor scales it to the display if they differ */
		ANativeWindow_setBuffersGeometry(secondWin, gpuW, gpuH, fmt);
		surf2 = eglCreateWindowSurface(d, gpuConfig, secondWin, NULL);
		if (surf2 == EGL_NO_SURFACE) {
			static int logged;
			if (logged++ < 3) fprintf(stderr, "second screen: eglCreateWindowSurface failed (0x%x)\n", eglGetError());
			goto fail;
		}
		surf2Dpy = d;
		surf2Interval = false;
		fprintf(stderr, "second screen: drawing with the GPU (%dx%d)\n", gpuW, gpuH);
	}
	savedDpy = d;
	savedCtx = c;
	savedDraw = eglGetCurrentSurface(EGL_DRAW);
	savedRead = eglGetCurrentSurface(EGL_READ);
	if (!eglMakeCurrent(d, surf2, surf2, c)) {
		static int logged;
		if (logged++ < 3) fprintf(stderr, "second screen: eglMakeCurrent failed (0x%x)\n", eglGetError());
		goto fail;
	}
	if (!surf2Interval) {
		eglSwapInterval(d, 0); /* never wait for the second display's refresh */
		surf2Interval = true;
	}
	return true; /* winLock stays held until android_second_end */
fail:
	pthread_mutex_unlock(&winLock);
	return false;
}

void android_second_end(void) {
	eglSwapBuffers(savedDpy, surf2);
	eglMakeCurrent(savedDpy, savedDraw, savedRead, savedCtx);
	pthread_mutex_unlock(&winLock);
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
	/* never current here: the game thread only makes it current while holding winLock */
	if (surf2 != EGL_NO_SURFACE) eglDestroySurface(surf2Dpy, surf2);
	surf2 = EGL_NO_SURFACE;
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

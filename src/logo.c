#include "logo.h"
#include "emu.h"
#include "profiles.h"

#include <mgba/flags.h>
#include <mgba/core/core.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8 0
#include "stb_image_write.h"
#pragma GCC diagnostic pop

enum { NLAYERS = 5 }; /* bg0-3, obj */
static volatile bool cancelAll;

static void layers(Emu* e, int only) {
	for (int i = 0; i < NLAYERS; ++i) e->core->enableVideoLayer(e->core, i, only == -2 || i == only);
}

bool logo_extract(const char* romPath, const char* gameCode, const char* outPng) {
	const MapProfile* p = profile_find(gameCode);
	if (!p || !p->logo.wait) return false;
	Emu e;
	if (!emu_open(&e, romPath, NULL, false)) return false;

	for (int f = 0; f < p->logo.wait && !cancelAll; ++f) emu_frame(&e, 0);
	if (p->logo.pressStart) {
		for (int f = 0; f < 4; ++f) emu_frame(&e, K_START);
		for (int f = 0; f < p->logo.wait2 && !cancelAll; ++f) emu_frame(&e, 0);
	}
	if (cancelAll) {
		emu_close(&e);
		return false;
	}

	size_t sz = emu_state_size(&e);
	void* st = malloc(sz);
	uint32_t* back = malloc(GBA_W * GBA_H * 4);
	uint32_t* fg = malloc(GBA_W * GBA_H * 4);
	uint8_t* opaque = calloc(GBA_W * GBA_H, 1);
	emu_state_save(&e, st);
	layers(&e, -1); /* backdrop only */
	emu_frame(&e, 0);
	memcpy(back, e.fb, GBA_W * GBA_H * 4);
	emu_state_load(&e, st);
	layers(&e, p->logo.layer);
	emu_frame(&e, 0);
	memcpy(fg, e.fb, GBA_W * GBA_H * 4);
	layers(&e, -2);
	emu_close(&e);
	free(st);

	/* opaque mask and bounding box above maxY */
	int x0 = GBA_W, y0 = GBA_H, x1 = -1, y1 = -1, count = 0;
	int maxY = p->logo.maxY ? p->logo.maxY : GBA_H;
	for (int y = 0; y < maxY; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = fg[y * GBA_W + x];
			bool clear = c == back[y * GBA_W + x];
			if (p->logo.blackIsClear && (c & 0xFF) < 24 && ((c >> 8) & 0xFF) < 24 && ((c >> 16) & 0xFF) < 24) clear = true;
			if (clear) continue;
			opaque[y * GBA_W + x] = 1;
			++count;
			if (x < x0) x0 = x;
			if (x > x1) x1 = x;
			if (y < y0) y0 = y;
			if (y > y1) y1 = y;
		}
	}
	bool ok = false;
	if (count >= 400) {
		int w = x1 - x0 + 1, h = y1 - y0 + 1;
		uint8_t* rgba = malloc((size_t) w * h * 4);
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				int i = (y0 + y) * GBA_W + x0 + x;
				uint32_t c = fg[i];
				uint8_t* o = &rgba[(y * w + x) * 4];
				o[0] = c & 0xFF;
				o[1] = (c >> 8) & 0xFF;
				o[2] = (c >> 16) & 0xFF;
				o[3] = opaque[i] ? 255 : 0;
			}
		}
		char tmp[1100];
		snprintf(tmp, sizeof(tmp), "%s.tmp", outPng);
		ok = stbi_write_png(tmp, w, h, 4, rgba, w * 4) && rename(tmp, outPng) == 0;
		free(rgba);
		fprintf(stderr, "logo: %s %dx%d -> %s\n", gameCode, w, h, ok ? outPng : "write failed");
	} else {
		fprintf(stderr, "logo: %s not found on the title screen (%d px)\n", gameCode, count);
	}
	free(back);
	free(fg);
	free(opaque);
	return ok;
}

/* ------------------------------------------------------------ queue */

typedef struct Job {
	char rom[1024], code[5], out[1024];
	struct Job* next;
} Job;

static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qcond = PTHREAD_COND_INITIALIZER;
static Job* queue;
static bool running, stopping;
static pthread_t thread;
static volatile uint32_t generation;

static bool exists(const char* p) {
	struct stat st;
	return stat(p, &st) == 0;
}

static void* worker(void* arg) {
	(void) arg;
	pthread_mutex_lock(&qlock);
	while (!stopping) {
		Job* j = queue;
		if (!j) {
			pthread_cond_wait(&qcond, &qlock);
			continue;
		}
		queue = j->next;
		pthread_mutex_unlock(&qlock);
		if (!exists(j->out)) logo_extract(j->rom, j->code, j->out);
		free(j);
		__atomic_add_fetch(&generation, 1, __ATOMIC_SEQ_CST);
		pthread_mutex_lock(&qlock);
	}
	pthread_mutex_unlock(&qlock);
	return NULL;
}

void logo_request(const char* romPath, const char* gameCode, const char* outPng) {
	if (exists(outPng)) return;
	const MapProfile* p = profile_find(gameCode);
	if (!p || !p->logo.wait) return;
	pthread_mutex_lock(&qlock);
	for (Job* j = queue; j; j = j->next) {
		if (!strcmp(j->out, outPng)) {
			pthread_mutex_unlock(&qlock);
			return;
		}
	}
	Job* j = calloc(1, sizeof(*j));
	snprintf(j->rom, sizeof(j->rom), "%s", romPath);
	snprintf(j->code, sizeof(j->code), "%.4s", gameCode);
	snprintf(j->out, sizeof(j->out), "%s", outPng);
	Job** tail = &queue;
	while (*tail) tail = &(*tail)->next;
	*tail = j;
	if (!running) {
		running = true;
		pthread_create(&thread, NULL, worker, NULL);
	}
	pthread_cond_signal(&qcond);
	pthread_mutex_unlock(&qlock);
}

uint32_t logo_generation(void) {
	return __atomic_load_n(&generation, __ATOMIC_SEQ_CST);
}

void logo_shutdown(void) {
	cancelAll = true;
	pthread_mutex_lock(&qlock);
	stopping = true;
	while (queue) {
		Job* j = queue;
		queue = j->next;
		free(j);
	}
	pthread_cond_signal(&qcond);
	pthread_mutex_unlock(&qlock);
	if (running) pthread_join(thread, NULL);
	running = false;
}

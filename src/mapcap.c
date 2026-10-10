#include "mapcap.h"
#include "profiles.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct MapCapture {
	const MapProfile* prof;
	Emu shadow;
	size_t stateSize;
	size_t cropPixels;

	/* scratch buffers used by the capture routine (worker thread only) */
	uint32_t* work[MAP_MAX_FRAMES];
	uint32_t* prev;
	int workFrames;

	/* async machinery */
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool running;
	bool pending; /* a snapshot is waiting for the worker */
	bool busy;
	void* snapshot;

	uint32_t* published[MAP_MAX_FRAMES];
	int pubFrames;
	uint32_t serial;
	const char* lastError;
};

static int probe_count(const MapProfile* p) {
	int n = 0;
	while (n < 8 && (p->probes[n].x || p->probes[n].y || p->probes[n].rgb)) {
		++n;
	}
	return n;
}

static bool color_near(uint32_t c, uint32_t rgb, int tol) {
	int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	int er = (rgb >> 16) & 0xFF, eg = (rgb >> 8) & 0xFF, eb = rgb & 0xFF;
	return abs(r - er) <= tol && abs(g - eg) <= tol && abs(b - eb) <= tol;
}

static bool screen_matches(const MapProfile* p, const uint32_t* fb) {
	int n = probe_count(p);
	int need = p->minProbes ? p->minProbes : n;
	int hit = 0;
	for (int i = 0; i < n; ++i) {
		const Probe* pr = &p->probes[i];
		if (color_near(fb[pr->y * GBA_W + pr->x], pr->rgb, p->tolerance)) {
			++hit;
		}
	}
	if (hit < need) {
		return false;
	}
	if (p->minForeground > 0) {
		int fg = 0;
		for (int y = p->crop_y; y < p->crop_y + p->crop_h && fg < p->minForeground; ++y) {
			for (int x = p->crop_x; x < p->crop_x + p->crop_w; ++x) {
				if (!color_near(fb[y * GBA_W + x], p->bgColor, p->tolerance) && ++fg >= p->minForeground) {
					break;
				}
			}
		}
		if (fg < p->minForeground) {
			return false;
		}
	}
	return true;
}

bool memprobes_match(const MemProbe* list, int n, Emu* e) {
	for (int i = 0; i < n && list[i].addr; ++i) {
		const MemProbe* m = &list[i];
		uint32_t v;
		switch (m->size) {
		case 2: v = emu_read16(e, m->addr); break;
		case 4: v = emu_read32(e, m->addr); break;
		default: v = emu_read8(e, m->addr); break;
		}
		uint32_t mask = m->mask ? m->mask : 0xFFFFFFFF;
		if ((v & mask) != (m->value & mask)) {
			return false;
		}
	}
	return true;
}

static void crop(const MapProfile* p, const uint32_t* fb, uint32_t* out) {
	for (int y = 0; y < p->crop_h; ++y) {
		memcpy(&out[y * p->crop_w], &fb[(p->crop_y + y) * GBA_W + p->crop_x], p->crop_w * sizeof(uint32_t));
	}
}

static int count_diff(const uint32_t* a, const uint32_t* b, size_t n, int limit) {
	int d = 0;
	for (size_t i = 0; i < n; ++i) {
		if (a[i] != b[i] && ++d > limit) {
			break;
		}
	}
	return d;
}

static bool map_up(const MapProfile* p, Emu* s) {
	return memprobes_match(p->mem, 4, s) && screen_matches(p, s->fb);
}

/* Runs the shadow core from `state`; returns NULL on success or a reason.
 * Fills mc->work[0..workFrames). */
static const char* do_capture(MapCapture* mc, const void* state) {
	const MapProfile* p = mc->prof;
	Emu* s = &mc->shadow;
	size_t n = mc->cropPixels;
	mc->workFrames = 0;
	if (!emu_state_load(s, state)) {
		return "state load failed";
	}
	/* If the screen already passes as "the map" before we press anything,
	 * the probes can't tell it apart from whatever is showing (or the player
	 * has the map open themselves). Either way, don't trust a capture. */
	emu_frame(s, 0);
	if (map_up(p, s)) {
		return "screen already looks like the map";
	}
	for (int i = 0; i < 8 && p->open[i].frames; ++i) {
		for (int f = 0; f < p->open[i].frames; ++f) {
			emu_frame(s, p->open[i].keys);
		}
	}
	int stable = 0;
	bool havePrev = false;
	bool everUp = false;
	int f;
	for (f = 0; f < p->settleMax; ++f) {
		emu_frame(s, 0);
		if (!map_up(p, s)) {
			stable = 0;
			havePrev = false;
			continue;
		}
		everUp = true;
		crop(p, s->fb, mc->work[0]);
		if (havePrev && count_diff(mc->prev, mc->work[0], n, p->maxDiff) <= p->maxDiff) {
			if (++stable >= p->stableFrames) {
				break;
			}
		} else {
			stable = 0;
		}
		memcpy(mc->prev, mc->work[0], n * sizeof(uint32_t));
		havePrev = true;
	}
	if (f >= p->settleMax) {
		return everUp ? "map screen never settled" : "map screen did not appear";
	}
	mc->workFrames = 1;
	int want = p->animFrames > MAP_MAX_FRAMES ? MAP_MAX_FRAMES : p->animFrames;
	bool animated = false;
	for (int i = 1; i < want; ++i) {
		for (int k = 0; k < p->animStep; ++k) {
			emu_frame(s, 0);
		}
		if (!map_up(p, s)) {
			break;
		}
		crop(p, s->fb, mc->work[i]);
		if (memcmp(mc->work[i], mc->work[0], n * sizeof(uint32_t))) {
			animated = true;
		}
		mc->workFrames = i + 1;
	}
	if (!animated) {
		mc->workFrames = 1;
	}
	return NULL;
}

static void* worker(void* arg) {
	MapCapture* mc = arg;
	size_t n = mc->cropPixels;
	pthread_mutex_lock(&mc->lock);
	while (mc->running) {
		if (!mc->pending) {
			pthread_cond_wait(&mc->cond, &mc->lock);
			continue;
		}
		mc->pending = false;
		mc->busy = true;
		pthread_mutex_unlock(&mc->lock);

		const char* err = do_capture(mc, mc->snapshot);

		pthread_mutex_lock(&mc->lock);
		mc->lastError = err;
		if (!err) {
			for (int i = 0; i < mc->workFrames; ++i) {
				memcpy(mc->published[i], mc->work[i], n * sizeof(uint32_t));
			}
			mc->pubFrames = mc->workFrames;
			++mc->serial;
		}
		mc->busy = false;
	}
	pthread_mutex_unlock(&mc->lock);
	return NULL;
}

MapCapture* mapcap_create(const char* romPath, const char* gameCode) {
	const MapProfile* p = profile_find(gameCode);
	if (!p) {
		return NULL;
	}
	MapCapture* mc = calloc(1, sizeof(*mc));
	mc->prof = p;
	if (!emu_open(&mc->shadow, romPath, NULL, false)) {
		free(mc);
		return NULL;
	}
	mc->stateSize = emu_state_size(&mc->shadow);
	mc->snapshot = malloc(mc->stateSize);
	mc->cropPixels = (size_t) p->crop_w * p->crop_h;
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		mc->work[i] = calloc(mc->cropPixels, sizeof(uint32_t));
		mc->published[i] = calloc(mc->cropPixels, sizeof(uint32_t));
	}
	mc->prev = calloc(mc->cropPixels, sizeof(uint32_t));
	pthread_mutex_init(&mc->lock, NULL);
	pthread_cond_init(&mc->cond, NULL);
	return mc;
}

static void start_thread(MapCapture* mc) {
	if (!mc->running) {
		mc->running = true;
		pthread_create(&mc->thread, NULL, worker, mc);
	}
}

void mapcap_destroy(MapCapture* mc) {
	if (!mc) {
		return;
	}
	if (mc->running) {
		pthread_mutex_lock(&mc->lock);
		mc->running = false;
		pthread_cond_signal(&mc->cond);
		pthread_mutex_unlock(&mc->lock);
		pthread_join(mc->thread, NULL);
	}
	emu_close(&mc->shadow);
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		free(mc->work[i]);
		free(mc->published[i]);
	}
	free(mc->prev);
	free(mc->snapshot);
	pthread_mutex_destroy(&mc->lock);
	pthread_cond_destroy(&mc->cond);
	free(mc);
}

const char* mapcap_title(MapCapture* mc) {
	return mc->prof->title;
}

void mapcap_size(MapCapture* mc, int* w, int* h) {
	*w = mc->prof->crop_w;
	*h = mc->prof->crop_h;
}

MapResult mapcap_capture_sync(MapCapture* mc, Emu* live) {
	MapResult r;
	memset(&r, 0, sizeof(r));
	if (!emu_state_save(live, mc->snapshot)) {
		r.why = "state save failed";
		return r;
	}
	r.why = do_capture(mc, mc->snapshot);
	r.ok = !r.why;
	r.w = mc->prof->crop_w;
	r.h = mc->prof->crop_h;
	r.nFrames = mc->workFrames;
	r.frameStep = mc->prof->animStep;
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		r.frames[i] = mc->work[i];
	}
	r.pixels = r.frames[0];
	return r;
}

bool mapcap_request(MapCapture* mc, Emu* live) {
	start_thread(mc);
	pthread_mutex_lock(&mc->lock);
	if (mc->busy || mc->pending) {
		pthread_mutex_unlock(&mc->lock);
		return false;
	}
	/* The worker only reads the snapshot after `pending` is set. */
	bool ok = emu_state_save(live, mc->snapshot);
	if (ok) {
		mc->pending = true;
		pthread_cond_signal(&mc->cond);
	}
	pthread_mutex_unlock(&mc->lock);
	return ok;
}

bool mapcap_busy(MapCapture* mc) {
	pthread_mutex_lock(&mc->lock);
	bool b = mc->busy || mc->pending;
	pthread_mutex_unlock(&mc->lock);
	return b;
}

bool mapcap_poll(MapCapture* mc, uint32_t lastSerial, MapResult* out) {
	size_t n = mc->cropPixels;
	pthread_mutex_lock(&mc->lock);
	bool fresh = mc->serial != lastSerial && mc->serial != 0;
	if (fresh) {
		out->ok = true;
		out->why = NULL;
		out->w = mc->prof->crop_w;
		out->h = mc->prof->crop_h;
		out->nFrames = mc->pubFrames;
		out->frameStep = mc->prof->animStep;
		for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
			if (!out->frames[i]) {
				out->frames[i] = malloc(n * sizeof(uint32_t));
			}
			if (i < mc->pubFrames) {
				memcpy(out->frames[i], mc->published[i], n * sizeof(uint32_t));
			}
		}
		out->pixels = out->frames[0];
		out->serial = mc->serial;
	}
	pthread_mutex_unlock(&mc->lock);
	return fresh;
}

bool mapcap_live_ok(MapCapture* mc, Emu* live) {
	return memprobes_match(mc->prof->live, 4, live);
}

uint32_t mapcap_room_key(MapCapture* mc, Emu* live) {
	uint32_t key = 2166136261u;
	bool any = false;
	for (int i = 0; i < 4 && mc->prof->room[i].addr; ++i) {
		const MemProbe* m = &mc->prof->room[i];
		uint32_t v = m->size == 4 ? emu_read32(live, m->addr) : m->size == 2 ? emu_read16(live, m->addr) : emu_read8(live, m->addr);
		if (m->mask) {
			v &= m->mask;
		}
		key = (key ^ v) * 16777619u;
		any = true;
	}
	return any ? key : 0;
}

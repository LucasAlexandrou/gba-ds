/* mGBA's gba.h uses GBA_H as its include guard, which emu.h defines as the
 * screen height: include it first, then let emu.h take the name over. */
#include <mgba/flags.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/renderers/video-software.h>
#include <mgba/internal/gba/cheats.h>
#undef GBA_H
#include "emu.h"

#include <mgba/flags.h>

#include <mgba/core/core.h>
#include <mgba/core/cheats.h>
#include <mgba/core/serialize.h>
#include <mgba/core/config.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <mgba/core/log.h>
#include <stdarg.h>
#include <pthread.h>
#include <ctype.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

static void quiet_log(struct mLogger* l, int cat, enum mLogLevel level, const char* fmt, va_list args) {
	(void) l;
	(void) cat;
	if (level & (mLOG_FATAL | mLOG_ERROR)) {
		vfprintf(stderr, fmt, args);
		fputc('\n', stderr);
	}
}
static struct mLogger quietLogger = { .log = quiet_log };

bool emu_open(Emu* e, const char* romPath, const char* savePath, bool audio) {
	memset(e, 0, sizeof(*e));
	mLogSetDefaultLogger(&quietLogger);
	e->core = mCoreFind(romPath);
	if (!e->core) {
		fprintf(stderr, "mCoreFind failed for %s\n", romPath);
		return false;
	}
	if (!e->core->init(e->core)) {
		fprintf(stderr, "core init failed\n");
		return false;
	}
	mCoreInitConfig(e->core, NULL);
	struct mCoreOptions opts = { .useBios = false, .volume = 0x100, .audioBuffers = 1024 };
	mCoreConfigLoadDefaults(&e->core->config, &opts);
	mCoreLoadConfig(e->core);

	e->fb = calloc(GBA_W * GBA_H, sizeof(uint32_t));
	e->core->setVideoBuffer(e->core, (mColor*) e->fb, GBA_W);
	e->audio = audio;
	if (audio) {
		e->core->setAudioBufferSize(e->core, 2048);
	}

	if (!mCoreLoadFile(e->core, romPath)) {
		fprintf(stderr, "could not load ROM %s\n", romPath);
		return false;
	}
	if (savePath) {
		struct VFile* vf = VFileOpen(savePath, O_CREAT | O_RDWR);
		if (vf) {
			e->core->loadSave(e->core, vf);
		} else {
			fprintf(stderr, "warning: cannot open save file %s\n", savePath);
		}
	}
	e->core->reset(e->core);

	struct mGameInfo info;
	memset(&info, 0, sizeof(info));
	e->core->getGameInfo(e->core, &info);
	memcpy(e->gameCode, info.code, 4);
	e->gameCode[4] = 0;
	return true;
}

static void hide_release(Emu* e);

void emu_close(Emu* e) {
	if (e->core) {
		hide_release(e);
		mCoreConfigDeinit(&e->core->config);
		e->core->deinit(e->core);
		e->core = NULL;
	}
	free(e->fb);
	e->fb = NULL;
}

void emu_frame(Emu* e, uint32_t keys) {
	e->core->setKeys(e->core, keys);
	e->core->runFrame(e->core);
	if (!e->audio) {
		struct mAudioBuffer* ab = e->core->getAudioBuffer(e->core);
		if (ab) {
			mAudioBufferClear(ab);
		}
	}
}

void emu_reset(Emu* e) {
	e->core->reset(e->core);
}

size_t emu_state_size(Emu* e) {
	return e->core->stateSize(e->core);
}

bool emu_state_save(Emu* e, void* buf) {
	return e->core->saveState(e->core, buf);
}

bool emu_state_load(Emu* e, const void* buf) {
	return e->core->loadState(e->core, buf);
}

bool emu_state_save_file(Emu* e, const char* path) {
	struct VFile* vf = VFileOpen(path, O_CREAT | O_TRUNC | O_RDWR);
	if (!vf) {
		return false;
	}
	bool ok = mCoreSaveStateNamed(e->core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC | SAVESTATE_METADATA);
	vf->close(vf);
	return ok;
}

bool emu_state_load_file(Emu* e, const char* path) {
	struct VFile* vf = VFileOpen(path, O_RDONLY);
	if (!vf) {
		return false;
	}
	bool ok = mCoreLoadStateNamed(e->core, vf, SAVESTATE_RTC);
	vf->close(vf);
	return ok;
}

uint8_t emu_read8(Emu* e, uint32_t a) {
	return e->core->rawRead8(e->core, a, -1);
}
uint16_t emu_read16(Emu* e, uint32_t a) {
	return e->core->rawRead16(e->core, a, -1);
}
uint32_t emu_read32(Emu* e, uint32_t a) {
	return e->core->rawRead32(e->core, a, -1);
}

size_t emu_audio_read(Emu* e, int16_t* out, size_t maxFrames) {
	struct mAudioBuffer* ab = e->core->getAudioBuffer(e->core);
	if (!ab) {
		return 0;
	}
	return mAudioBufferRead(ab, out, maxFrames);
}

unsigned emu_audio_rate(Emu* e) {
	return e->core->audioSampleRate(e->core);
}

/* ------------------------------------------------------------ sprite hiding */

/* Rules per renderer: the game core hides the Metroid HUD mini-map, the map
 * capture core (another thread) hides prompts drawn over the map. */
typedef struct {
	struct GBAVideoRenderer* renderer;
	void (*orig)(struct GBAVideoRenderer* renderer, int y);
	SpriteRule rules[8];
	int n;
} HideSlot;
static HideSlot hideSlots[8];
static pthread_mutex_t hideLock = PTHREAD_MUTEX_INITIALIZER;

static HideSlot* slot_for(struct GBAVideoRenderer* r) {
	for (int i = 0; i < 8; ++i) {
		if (hideSlots[i].renderer == r) return &hideSlots[i];
	}
	return NULL;
}

static bool hidden(const HideSlot* s, const struct GBAObj* obj) {
	int x = (int) (GBAObjAttributesBGetX(obj->b) << 23) >> 23;
	int y = GBAObjAttributesAGetY(obj->a);
	if (y >= 160) y -= 256;
	int tile = GBAObjAttributesCGetTile(obj->c);
	int prio = GBAObjAttributesCGetPriority(obj->c);
	for (int i = 0; i < s->n; ++i) {
		const SpriteRule* r = &s->rules[i];
		if (x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1 && tile >= r->tileMin && tile <= r->tileMax &&
		    prio == r->prio) {
			return true;
		}
	}
	return false;
}

/* Runs before the software renderer's own scanline code: when the sprite table
 * has changed, build the renderer's sprite list the way it would, minus the
 * hidden sprites, and mark it clean so the renderer uses ours. */
static void filtered_draw_scanline(struct GBAVideoRenderer* renderer, int y) {
	struct GBAVideoSoftwareRenderer* sw = (struct GBAVideoSoftwareRenderer*) renderer;
	HideSlot* s = slot_for(renderer);
	if (!s) return;
	if (s->n && sw->oamDirty) {
		int n = GBAVideoRendererCleanOAM(sw->d.oam->obj, sw->sprites, sw->objOffsetY);
		int kept = 0;
		for (int i = 0; i < n; ++i) {
			if (!hidden(s, &sw->sprites[i].obj)) sw->sprites[kept++] = sw->sprites[i];
		}
		sw->oamMax = kept;
		sw->oamDirty = false;
	}
	s->orig(renderer, y);
}

void emu_hide_sprites(Emu* e, const SpriteRule* rules, int n) {
	struct GBA* gba = e->core->board;
	struct GBAVideoRenderer* r = gba->video.renderer;
	if (!r) return;
	pthread_mutex_lock(&hideLock);
	HideSlot* s = slot_for(r);
	if (!s) s = slot_for(NULL);
	if (s) {
		s->n = n > 8 ? 8 : n;
		if (s->n) memcpy(s->rules, rules, sizeof(*rules) * s->n);
		if (s->renderer != r) {
			s->orig = r->drawScanline;
			s->renderer = r;
			r->drawScanline = filtered_draw_scanline;
		}
	}
	pthread_mutex_unlock(&hideLock);
	((struct GBAVideoSoftwareRenderer*) r)->oamDirty = true;
}

static void hide_release(Emu* e) {
	struct GBA* gba = e->core->board;
	pthread_mutex_lock(&hideLock);
	HideSlot* s = slot_for(gba->video.renderer);
	if (s) memset(s, 0, sizeof(*s));
	pthread_mutex_unlock(&hideLock);
}

/* ---------------------------------------------------------------- cheats */

void emu_cheats_clear(Emu* e) {
	struct mCheatDevice* device = e->core->cheatDevice(e->core);
	if (!device) return;
	/* mCheatDeviceClear only frees the sets: a Code Breaker hook (the
	 * "Enable Code") would stay patched into the ROM, and the next hook
	 * would save that patch as the original instruction and loop forever.
	 * Switch each set off (restores ROM patches) and remove it (restores
	 * the hooked instruction) first. */
	while (mCheatSetsSize(&device->cheats)) {
		struct mCheatSet* set = *mCheatSetsGetPointer(&device->cheats, mCheatSetsSize(&device->cheats) - 1);
		set->enabled = false;
		mCheatRefresh(device, set);
		mCheatRemoveSet(device, set);
		mCheatSetDeinit(set);
	}
}

static bool is_hex(const char* s, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		if (!isxdigit((unsigned char) s[i])) return false;
	}
	return true;
}

int emu_cheats_add(Emu* e, const char* name, const char* const* codes, int n, int type) {
	struct mCheatDevice* device = e->core->cheatDevice(e->core);
	if (!device) return 0;
	int mtype = type == EMU_CHEAT_CODEBREAKER ? GBA_CHEAT_CODEBREAKER :
	            type == EMU_CHEAT_GAMESHARK ? GBA_CHEAT_GAMESHARK :
	            type == EMU_CHEAT_ACTION_REPLAY ? GBA_CHEAT_PRO_ACTION_REPLAY : GBA_CHEAT_AUTODETECT;
	struct mCheatSet* set = device->createSet(device, name);
	int added = 0;
	for (int c = 0; c < n; ++c) {
		/* split into hex words on '+' and spaces, then pair them up:
		 * 8 + 4 digits is a Code Breaker line, 8 + 8 a GameShark / AR one */
		char words[64][20];
		int nw = 0;
		const char* p = codes[c];
		while (*p && nw < 64) {
			while (*p == '+' || isspace((unsigned char) *p)) ++p;
			const char* q = p;
			while (*q && *q != '+' && !isspace((unsigned char) *q)) ++q;
			size_t len = (size_t) (q - p);
			if (len && len < sizeof(words[0])) {
				memcpy(words[nw], p, len);
				words[nw++][len] = 0;
			}
			p = q;
		}
		for (int w = 0; w + 1 < nw; w += 2) {
			size_t l1 = strlen(words[w]), l2 = strlen(words[w + 1]);
			if (l1 != 8 || (l2 != 4 && l2 != 8) || !is_hex(words[w], l1) || !is_hex(words[w + 1], l2)) {
				fprintf(stderr, "cheat %s: skipping \"%s %s\"\n", name ? name : "", words[w], words[w + 1]);
				continue;
			}
			char line[20];
			snprintf(line, sizeof(line), "%s %s", words[w], words[w + 1]);
			int t = l2 == 4 ? GBA_CHEAT_CODEBREAKER : mtype == GBA_CHEAT_CODEBREAKER ? GBA_CHEAT_AUTODETECT : mtype;
			if (mCheatAddLine(set, line, t)) ++added;
			else fprintf(stderr, "cheat %s: mGBA rejected \"%s\"\n", name ? name : "", line);
		}
	}
	set->enabled = true;
	mCheatAddSet(device, set);
	return added;
}

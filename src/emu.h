/* Thin wrapper around a libmgba core. */
#ifndef GBADS_EMU_H
#define GBADS_EMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GBA_W 240
#define GBA_H 160

/* GBA key bits as used by mCore::setKeys */
enum {
	K_A = 1 << 0,
	K_B = 1 << 1,
	K_SELECT = 1 << 2,
	K_START = 1 << 3,
	K_RIGHT = 1 << 4,
	K_LEFT = 1 << 5,
	K_UP = 1 << 6,
	K_DOWN = 1 << 7,
	K_R = 1 << 8,
	K_L = 1 << 9,
};

struct mCore;

/* A sprite to leave out when drawing (e.g. a HUD mini-map): matches when its
 * top-left corner is inside x0..x1 / y0..y1 (y may be negative), its tile
 * number is within tileMin..tileMax and its priority equals prio. */
typedef struct SpriteRule {
	int x0, y0, x1, y1;
	int tileMin, tileMax;
	int prio;
} SpriteRule;

typedef struct Emu {
	struct mCore* core;
	uint32_t* fb; /* GBA_W * GBA_H, mGBA native 32-bit color (XBGR8888, i.e. bytes R,G,B,X) */
	char gameCode[5];
	bool audio;
} Emu;

/* savePath may be NULL (no battery save attached). */
bool emu_open(Emu* e, const char* romPath, const char* savePath, bool audio);
void emu_close(Emu* e);
void emu_frame(Emu* e, uint32_t keys);
void emu_reset(Emu* e);

size_t emu_state_size(Emu* e);
bool emu_state_save(Emu* e, void* buf);
bool emu_state_load(Emu* e, const void* buf);

/* Persist full savestates (including battery data) to disk. */
bool emu_state_save_file(Emu* e, const char* path);
bool emu_state_load_file(Emu* e, const char* path);

uint8_t emu_read8(Emu* e, uint32_t addr);
uint16_t emu_read16(Emu* e, uint32_t addr);
uint32_t emu_read32(Emu* e, uint32_t addr);

/* Pull up to maxFrames stereo frames of audio; returns frames read. */
size_t emu_audio_read(Emu* e, int16_t* out, size_t maxFrames);
unsigned emu_audio_rate(Emu* e);

/* tools/png.c (host tools only) */
bool write_png(const char* path, const uint32_t* px, int w, int h, int stride);

/* Hide sprites matching any of the n rules (n = 0 turns it off). Display only:
 * the game's memory and state are untouched. One emulator at a time. */
void emu_hide_sprites(Emu* e, const SpriteRule* rules, int n);
/* Which layers are drawn: bits 0-3 BG0-3, bit 4 sprites (0x1F = all). Takes
 * effect when the display registers are next written (e.g. a state load). */
void emu_layers(Emu* e, unsigned mask);

/* Cheats. emu_cheats_clear removes them all; emu_cheats_add adds one set
 * (applied every frame) from codes in RetroArch's .cht form: hex words
 * joined with '+', paired up into "XXXXXXXX YYYY" (Code Breaker) or
 * "XXXXXXXX YYYYYYYY" (GameShark / Action Replay) lines. type: 0 = detect,
 * else EMU_CHEAT_*. Returns the number of lines mGBA accepted. */
enum { EMU_CHEAT_DETECT = 0, EMU_CHEAT_CODEBREAKER, EMU_CHEAT_GAMESHARK, EMU_CHEAT_ACTION_REPLAY };
void emu_cheats_clear(Emu* e);
int emu_cheats_add(Emu* e, const char* name, const char* const* codes, int n, int type);

#endif

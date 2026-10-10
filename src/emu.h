/* Thin wrapper around a libmgba core. */
#ifndef GBADUAL_EMU_H
#define GBADUAL_EMU_H

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

#endif

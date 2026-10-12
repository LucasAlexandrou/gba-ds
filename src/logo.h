/* Title-screen logos pulled out of the game itself (no image files needed).
 *
 * The ROM is booted in a silent emulator core, run to its title screen, and
 * the background layer holding the logo is rendered on its own; pixels that
 * match a backdrop-only render are transparent. The result is cropped and
 * cached as a PNG, so this only happens once per game. */
#ifndef GBADS_LOGO_H
#define GBADS_LOGO_H

#include <stdbool.h>
#include <stdint.h>

/* Synchronous extraction. Returns false if the game has no logo profile or
 * the logo couldn't be found. */
bool logo_extract(const char* romPath, const char* gameCode, const char* outPng);

/* Map-screen border pieces from one of the game's menu screens, saved as a
 * (2t+p) x (2t+p) atlas: corners in the corners, one edge slice between them
 * (see MapProfile.frame). */
bool frame_extract(const char* romPath, const char* gameCode, const char* outPng);

/* Background queue: extracts outPng if it doesn't exist yet. */
void logo_request(const char* romPath, const char* gameCode, const char* outPng);
void frame_request(const char* romPath, const char* gameCode, const char* outPng);
/* Changes every time a queued logo finishes; compare to know when to reload. */
uint32_t logo_generation(void);
/* Background work done / total (queued + running + done) since the queue was
 * last idle; done == total when there's nothing left to do. */
void logo_progress(int* done, int* total);
void logo_shutdown(void);

#endif

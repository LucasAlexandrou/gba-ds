/* Title-screen logos pulled out of the game itself (no image files needed).
 *
 * The ROM is booted in a silent emulator core, run to its title screen, and
 * the background layer holding the logo is rendered on its own; pixels that
 * match a backdrop-only render are transparent. The result is cropped and
 * cached as a PNG, so this only happens once per game. */
#ifndef GBADUAL_LOGO_H
#define GBADUAL_LOGO_H

#include <stdbool.h>
#include <stdint.h>

/* Synchronous extraction. Returns false if the game has no logo profile or
 * the logo couldn't be found. */
bool logo_extract(const char* romPath, const char* gameCode, const char* outPng);

/* Background queue: extracts outPng if it doesn't exist yet. */
void logo_request(const char* romPath, const char* gameCode, const char* outPng);
/* Changes every time a queued logo finishes; compare to know when to reload. */
uint32_t logo_generation(void);
void logo_shutdown(void);

#endif

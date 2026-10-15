/* RetroAchievements, casual (softcore) mode, through rcheevos' rc_client.
 *
 * The account is ROCKNIX's (EmulationStation's RetroAchievements settings,
 * global.retroachievements.* in system.cfg) unless gba-ds.cfg names one.
 * Server requests run the system's curl in a background thread; their
 * results are handed to rc_client on the main thread. Unlocks and the
 * game's progress show as popups on the second screen.
 *
 * Built where GBADS_RA is defined (desktop and ROCKNIX); elsewhere these are
 * no-ops. */
#ifndef GBADS_RA_H
#define GBADS_RA_H

#include "config.h"
#include "emu.h"

#include <stdbool.h>

#ifdef GBADS_RA
/* At start-up: reads the account and starts signing in. */
void ra_init(const Config* cfg);
void ra_shutdown(void);
/* A game started / ended (the core running it stays valid in between). */
void ra_game_start(Emu* e, const char* romPath);
void ra_game_end(void);
/* After each emulated frame. */
void ra_frame(void);
/* Each loop iteration without emulation (paused, menus). */
void ra_idle(void);
/* The game was restarted. */
void ra_reset(void);
/* Save states: the achievement progress goes in <state>.ra beside them. */
void ra_state_saved(const char* statePath);
void ra_state_loaded(const char* statePath);
/* Popups, at the top of the given screen. */
void ra_draw(int screen);
#else
static inline void ra_init(const Config* cfg) { (void) cfg; }
static inline void ra_shutdown(void) {}
static inline void ra_game_start(Emu* e, const char* romPath) { (void) e, (void) romPath; }
static inline void ra_game_end(void) {}
static inline void ra_frame(void) {}
static inline void ra_idle(void) {}
static inline void ra_reset(void) {}
static inline void ra_state_saved(const char* statePath) { (void) statePath; }
static inline void ra_state_loaded(const char* statePath) { (void) statePath; }
static inline void ra_draw(int screen) { (void) screen; }
#endif

#endif

/* Offering GBA DS as an emulator for GBA games in EmulationStation (ROCKNIX).
 *
 * ROCKNIX's EmulationStation merges es_systems_*.cfg files from its config
 * folder into its system list. Turning this on writes es_systems_gbads.cfg
 * there: the GBA system's emulator list (copied from es_systems.cfg) plus
 * "gbads", and its launch command pointed at gbads-launch.sh, also written
 * there. That script starts GBA DS when it's the chosen emulator and hands
 * every other launch to /usr/bin/runemu.sh unchanged. */
#ifndef GBADS_ESINTEG_H
#define GBADS_ESINTEG_H

#include "config.h"

#include <stdbool.h>
#include <stddef.h>

bool es_integration_on(const Config* cfg);
/* Turns it on or off; msg gets a short message for the user (upper case). */
bool es_integration_set(const Config* cfg, bool on, char* msg, size_t msgSize);
/* If it's on, rewrites the files (ROCKNIX updates can change the GBA
 * system's emulator list, and GBA DS may have moved). */
void es_integration_refresh(const Config* cfg);

#endif

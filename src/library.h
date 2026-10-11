#ifndef GBADUAL_LIBRARY_H
#define GBADUAL_LIBRARY_H

#include "config.h"

#include <stdbool.h>
#include <stddef.h>

/* Shows the game list (rescanned from cfg->romDir every time it opens).
 * Returns true with the chosen ROM in outPath, or false to quit the app. */
bool library_run(Config* cfg, char* outPath, size_t outSize);

#endif

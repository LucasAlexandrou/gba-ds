/* GBA DS: a GBA emulator for dual-screen handhelds that keeps the game's own
 * map on the second screen.
 *
 *   library.c  game list (rescans the ROM folder every time it opens)
 *   game.c     play session, pause menu, auto save/resume
 *   mapcap.c   map capture with a shadow emulator core; profiles.c per game
 *   ui.c       windows, screens, touch/buttons, drawing
 */
#include "android.h"
#include "config.h"
#include "esinteg.h"
#include "game.h"
#include "library.h"
#include "logo.h"
#include "paths.h"
#include "ui.h"

#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void on_signal(int sig) {
	(void) sig;
	uiQuit = 1;
}

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--version")) {
			printf("GBA DS %s\n", GBADS_VERSION);
			return 0;
		}
	}
#ifdef __ANDROID__
	android_init_log();
#endif
	fprintf(stderr, "GBA DS %s\n", GBADS_VERSION);
	Config cfg;
	config_defaults(&cfg);
#ifdef __ANDROID__
	android_config_defaults(&cfg);
#endif

	char exeDir[1024] = ".";
	char exe[1024];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = 0;
		snprintf(exeDir, sizeof(exeDir), "%s", dirname(exe));
	}
#ifdef __ANDROID__
	/* the executable is Android's app_process: use the app's own folder */
	if (android_home()) snprintf(exeDir, sizeof(exeDir), "%s", android_home());
#endif

	/* defaults < gba-ds.cfg < states/prefs.cfg (choices made in the menu) < command line */
	char path[1100];
	if (getenv("GBADS_CONFIG")) {
		config_load_file(&cfg, getenv("GBADS_CONFIG"));
	} else {
		snprintf(path, sizeof(path), "%s/gba-ds.cfg", exeDir);
		config_load_file(&cfg, path);
		if (getenv("HOME")) {
			snprintf(path, sizeof(path), "%s/.config/gba-ds/gba-ds.cfg", getenv("HOME"));
			config_load_file(&cfg, path);
		}
	}
	if (!config_parse_args(&cfg, argc, argv)) {
		config_usage(argv[0]);
		return 1;
	}
	if (!cfg.assetDir[0]) snprintf(cfg.assetDir, sizeof(cfg.assetDir), "%s", exeDir);
	if (!cfg.stateDir[0]) snprintf(cfg.stateDir, sizeof(cfg.stateDir), "%s/states", exeDir);
	mkdirs(cfg.stateDir);
	snprintf(path, sizeof(path), "%s/prefs.cfg", cfg.stateDir);
	config_load_file(&cfg, path);
	config_parse_args(&cfg, argc, argv);

#ifndef __ANDROID__
	/* keep EmulationStation's entry for GBA DS current, if it's switched on */
	es_integration_refresh(&cfg);
#else
	/* first milestone: no game given -> the first game with a map in the ROM folder */
	if (!cfg.rom[0] && android_pick_rom(&cfg, cfg.rom, sizeof(cfg.rom))) fprintf(stderr, "playing %s\n", cfg.rom);
#endif

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);

	if (!ui_init(&cfg, true)) return 1;
	if (cfg.inputScript[0]) ui_load_script(cfg.inputScript);

	if (cfg.rom[0]) {
		game_run(&cfg, cfg.rom, false);
	} else {
		char rom[1024];
		while (!uiQuit && library_run(&cfg, rom, sizeof(rom))) {
			if (game_run(&cfg, rom, true) == GAME_QUIT_APP) break;
		}
	}
	logo_shutdown();
	ui_shutdown();
	return 0;
}

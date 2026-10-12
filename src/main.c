/* GBA DS: a GBA emulator for dual-screen handhelds that keeps the game's own
 * map on the second screen.
 *
 *   library.c  game list (rescans the ROM folder every time it opens)
 *   game.c     play session, pause menu, auto save/resume
 *   mapcap.c   map capture with a shadow emulator core; profiles.c per game
 *   ui.c       windows, screens, touch/buttons, drawing
 */
#include "config.h"
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
	Config cfg;
	config_defaults(&cfg);

	char exeDir[1024] = ".";
	char exe[1024];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = 0;
		snprintf(exeDir, sizeof(exeDir), "%s", dirname(exe));
	}

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

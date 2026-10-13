#include "paths.h"

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

bool file_exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

void mkdirs(const char* path) {
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char* p = tmp + 1; *p; ++p) {
		if (*p == '/') {
			*p = 0;
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

bool copy_file(const char* from, const char* to) {
	FILE* in = fopen(from, "rb");
	if (!in) return false;
	FILE* out = fopen(to, "wb");
	if (!out) {
		fclose(in);
		return false;
	}
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
	fclose(in);
	fclose(out);
	return true;
}

void game_paths(const Config* cfg, const char* rom, GamePaths* o) {
	char a[1024], b[1024];
	snprintf(a, sizeof(a), "%s", rom);
	snprintf(b, sizeof(b), "%s", rom);
	const char* romDir = dirname(a);
	snprintf(o->base, sizeof(o->base), "%s", basename(b));
	char* dot = strrchr(o->base, '.');
	if (dot) *dot = 0;

	const char* saveDir = cfg->saveDir[0] ? cfg->saveDir : romDir;
	mkdirs(saveDir);
	mkdirs(cfg->stateDir);
	snprintf(o->sav, sizeof(o->sav), "%s/%s.sav", saveDir, o->base);
	snprintf(o->state, sizeof(o->state), "%s/%s.state", cfg->stateDir, o->base);
	snprintf(o->autoState, sizeof(o->autoState), "%s/%s.auto.state", cfg->stateDir, o->base);
	snprintf(o->thumb, sizeof(o->thumb), "%s/%s.game.bmp", cfg->stateDir, o->base);
	snprintf(o->mapThumb, sizeof(o->mapThumb), "%s/%s.map.bmp", cfg->stateDir, o->base);
	snprintf(o->cheats, sizeof(o->cheats), "%s/%s.cheats", cfg->stateDir, o->base);

	/* First run: carry over a RetroArch battery save (.srm) */
	if (!file_exists(o->sav)) {
		char srm[1100];
		snprintf(srm, sizeof(srm), "%s/%s.srm", saveDir, o->base);
		if (copy_file(srm, o->sav)) fprintf(stderr, "imported %s -> %s\n", srm, o->sav);
	}
	/* Older builds (when the app was called gbadual) kept the manual state next to the ROM */
	if (!file_exists(o->state)) {
		char old[1100];
		snprintf(old, sizeof(old), "%s/%s.gbadual.ss0", saveDir, o->base);
		if (file_exists(old) && rename(old, o->state) != 0) copy_file(old, o->state);
	}
}

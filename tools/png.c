#include "../src/emu.h"

#include <png.h>
#include <stdio.h>
#include <stdlib.h>

bool write_png(const char* path, const uint32_t* px, int w, int h, int stride) {
	FILE* f = fopen(path, "wb");
	if (!f) {
		return false;
	}
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png_create_info_struct(png);
	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(f);
		return false;
	}
	png_init_io(png, f);
	png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);
	uint8_t* row = malloc(w * 3);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			uint32_t c = px[y * stride + x];
			row[x * 3 + 0] = c & 0xFF;
			row[x * 3 + 1] = (c >> 8) & 0xFF;
			row[x * 3 + 2] = (c >> 16) & 0xFF;
		}
		png_write_row(png, row);
	}
	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	free(row);
	fclose(f);
	return true;
}

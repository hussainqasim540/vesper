#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *vimg_parse(const uint8_t *data, size_t size, int *w, int *h)
{
	if (size < 8 || memcmp(data, "VIMG", 4) != 0)
		return NULL;
	int ww = data[4] | data[5] << 8, hh = data[6] | data[7] << 8;
	size_t need = (size_t)ww * (size_t)hh * 3;
	if (ww == 0 || hh == 0 || size - 8 < need)
		return NULL;
	uint8_t *px = malloc(need);
	if (!px)
		return NULL;
	memcpy(px, data + 8, need);
	*w = ww;
	*h = hh;
	return px;
}

uint8_t *vimg_load(const char *path, int *w, int *h)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	uint8_t *buf = NULL, *px = NULL;
	if (fseek(f, 0, SEEK_END) == 0) {
		long size = ftell(f);
		if (size > 0 && size < 64L << 20 && fseek(f, 0, SEEK_SET) == 0) {
			buf = malloc((size_t)size);
			if (buf && fread(buf, 1, (size_t)size, f) == (size_t)size)
				px = vimg_parse(buf, (size_t)size, w, h);
		}
	}
	free(buf);
	fclose(f);
	return px;
}

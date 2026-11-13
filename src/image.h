/* .vimg pictures: "VIMG", width and height as little-endian uint16, then
 * RGB888 rows, top first (tools/make_background.py writes them). */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Returns malloc'd RGB888 pixels, or NULL. */
uint8_t *vimg_load(const char *path, int *w, int *h);
/* Same, from memory. */
uint8_t *vimg_parse(const uint8_t *data, size_t size, int *w, int *h);

/* Drawing with GLES2, in panel pixels with the origin top-left. */
#pragma once
#include <stdint.h>

#include "font.h"
#include "layout.h"

enum tex_format { TEX_RGB565, TEX_XRGB8888, TEX_RGB888, TEX_ALPHA };
enum tex_filter {
	FILTER_NEAREST,
	FILTER_LINEAR,
	FILTER_SHARP, /* nearest to the integer scale, then one bilinear step */
};

struct tex {
	unsigned id;
	int w, h; /* allocated size */
	enum tex_format fmt;
};

/* 0xRRGGBBAA */
typedef uint32_t rgba;

int gfx_init(int screen_w, int screen_h);
void gfx_shutdown(void);
void gfx_clear(rgba color);

int tex_create(struct tex *t, int w, int h, enum tex_format fmt);
/* Pixels packed tight, w x h, at the top-left of the texture. A larger
 * frame reallocates the texture. */
void tex_upload(struct tex *t, const void *pixels, int w, int h);
void tex_destroy(struct tex *t);

/* Draws src (texels) of t into dst (panel pixels). */
void gfx_draw(const struct tex *t, struct rect src, struct rect dst,
              enum tex_filter filter, float alpha);
void gfx_fill(struct rect r, rgba color);
/* Text with its baseline at y. Returns the advance. */
float gfx_text(enum font_face face, float x, float y, rgba color, const char *utf8);

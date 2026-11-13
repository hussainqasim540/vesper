/* Text from the Inter glyph atlas in font_data.h. This part only measures
 * and looks glyphs up; gfx.c draws them. */
#pragma once
#include <stddef.h>
#include <stdint.h>

enum font_face { FONT_SMALL, FONT_BODY, FONT_TITLE };

struct glyph {
	int x, y, w, h; /* in the atlas */
	int ox, oy;     /* from the pen on the baseline to the top-left */
	float adv;
};

/* Next code point of a UTF-8 string; advances *s. Bad bytes give U+FFFD. */
uint32_t utf8_next(const char **s);
/* The glyph of cp, or of '?' when the atlas lacks it. */
const struct glyph *font_glyph(enum font_face face, uint32_t cp);
int font_ascent(enum font_face face);
int font_line_height(enum font_face face);
float font_text_width(enum font_face face, const char *utf8);
/* Copies into out the longest prefix of utf8 that fits max_w, with "…"
 * when it had to cut. */
void font_ellipsize(enum font_face face, const char *utf8, float max_w, char *out, size_t n);

const uint8_t *font_atlas_pixels(int *w, int *h);

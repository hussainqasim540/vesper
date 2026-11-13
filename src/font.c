#include "font.h"

#include <string.h>

#include "font_data.h"

static struct glyph glyphs[FONT_FACES][FONT_GLYPHS_PER_FACE];
static int ready;

static void init(void)
{
	for (int f = 0; f < FONT_FACES; f++)
		for (int i = 0; i < FONT_GLYPHS_PER_FACE; i++) {
			const struct font_glyph_data *d = &font_glyphs[f][i];
			glyphs[f][i] = (struct glyph){d->x, d->y, d->w, d->h, d->ox, d->oy, d->adv};
		}
	ready = 1;
}

uint32_t utf8_next(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t cp;
	int extra;
	if (p[0] < 0x80) {
		cp = p[0];
		extra = 0;
	} else if ((p[0] & 0xE0) == 0xC0) {
		cp = p[0] & 0x1F;
		extra = 1;
	} else if ((p[0] & 0xF0) == 0xE0) {
		cp = p[0] & 0x0F;
		extra = 2;
	} else if ((p[0] & 0xF8) == 0xF0) {
		cp = p[0] & 0x07;
		extra = 3;
	} else {
		*s += 1;
		return 0xFFFD;
	}
	for (int i = 1; i <= extra; i++) {
		if ((p[i] & 0xC0) != 0x80) { /* truncated sequence */
			*s += i;
			return 0xFFFD;
		}
		cp = cp << 6 | (p[i] & 0x3F);
	}
	*s += 1 + extra;
	return cp;
}

static const struct glyph *lookup(enum font_face face, uint32_t cp)
{
	const struct font_glyph_data *d = font_glyphs[face];
	/* The atlas is small and ASCII comes first, in order. */
	if (cp >= 32 && cp < 127)
		return &glyphs[face][cp - 32];
	for (int i = 127 - 32; i < FONT_GLYPHS_PER_FACE; i++)
		if (d[i].cp == cp)
			return &glyphs[face][i];
	return NULL;
}

const struct glyph *font_glyph(enum font_face face, uint32_t cp)
{
	if (!ready)
		init();
	if ((unsigned)face >= FONT_FACES)
		face = FONT_BODY;
	const struct glyph *g = lookup(face, cp);
	return g ? g : lookup(face, '?');
}

int font_ascent(enum font_face face)
{
	return font_faces[face].ascent;
}

int font_line_height(enum font_face face)
{
	return font_faces[face].ascent + font_faces[face].descent;
}

float font_text_width(enum font_face face, const char *utf8)
{
	float w = 0;
	while (*utf8)
		w += font_glyph(face, utf8_next(&utf8))->adv;
	return w;
}

void font_ellipsize(enum font_face face, const char *utf8, float max_w, char *out, size_t n)
{
	if (n == 0)
		return;
	if (font_text_width(face, utf8) <= max_w) {
		size_t len = strlen(utf8);
		if (len >= n)
			len = n - 1;
		memcpy(out, utf8, len);
		out[len] = '\0';
		return;
	}
	static const char ell[] = "\xE2\x80\xA6"; /* … */
	float budget = max_w - font_glyph(face, 0x2026)->adv, w = 0;
	const char *p = utf8, *cut = utf8;
	while (*p) {
		const char *q = p;
		w += font_glyph(face, utf8_next(&q))->adv;
		if (w > budget)
			break;
		p = q;
		cut = p;
	}
	size_t len = (size_t)(cut - utf8);
	while (len > 0 && utf8[len - 1] == ' ')
		len--;
	if (len + sizeof ell > n)
		len = n > sizeof ell ? n - sizeof ell : 0;
	memcpy(out, utf8, len);
	memcpy(out + len, ell, sizeof ell);
}

const uint8_t *font_atlas_pixels(int *w, int *h)
{
	*w = FONT_ATLAS_W;
	*h = FONT_ATLAS_H;
	return font_atlas;
}

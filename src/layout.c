#include "layout.h"

struct rect fit_rect(int src_w, int src_h, int dst_w, int dst_h, enum scale_mode mode)
{
	struct rect r = {0, 0, dst_w, dst_h};
	if (src_w <= 0 || src_h <= 0)
		return r;
	if (mode == SCALE_INTEGER) {
		int sx = dst_w / src_w, sy = dst_h / src_h;
		int s = sx < sy ? sx : sy;
		if (s >= 1) {
			r.w = src_w * s;
			r.h = src_h * s;
		} else {
			mode = SCALE_FIT; /* bigger than the screen: shrink it */
		}
	}
	if (mode == SCALE_FIT) {
		/* Compare dst_w/src_w with dst_h/src_h without floats. */
		if ((long)dst_w * src_h <= (long)dst_h * src_w) {
			r.w = dst_w;
			r.h = (int)((long)src_h * dst_w / src_w);
		} else {
			r.h = dst_h;
			r.w = (int)((long)src_w * dst_h / src_h);
		}
	}
	r.x = (dst_w - r.w) / 2;
	r.y = (dst_h - r.h) / 2;
	return r;
}

static struct ds_screen shown(int x, int y, int w, int h)
{
	return (struct ds_screen){{x, y, w, h}, 1, 1.0f};
}

struct ds_placement ds_place(enum ds_layout layout, int swapped)
{
	struct ds_screen first = {{0}, 0, 1.0f}, second = {{0}, 0, 1.0f};
	switch (layout) {
	case DS_SINGLE:
		first = shown(0, 0, 640, 480);
		break;
	case DS_SINGLE_PIP:
		first = shown(0, 0, 640, 480);
		second = shown(640 - 192 - 8, 480 - 144 - 8, 192, 144);
		second.alpha = 0.85f;
		break;
	case DS_STACKED:
		first = shown(160, 0, 320, 240);
		second = shown(160, 240, 320, 240);
		break;
	case DS_SIDE:
		first = shown(0, 120, 320, 240);
		second = shown(320, 120, 320, 240);
		break;
	case DS_LARGE_SMALL:
		first = shown(0, 60, 480, 360);
		second = shown(480, 60, 160, 120);
		break;
	case DS_SHARP_SINGLE:
		first = shown(64, 48, 512, 384);
		break;
	case DS_SHARP_STACKED:
	case DS_LAYOUT_COUNT:
	default:
		first = shown(192, 48, 256, 192);
		second = shown(192, 240, 256, 192);
		break;
	}
	struct ds_placement p;
	p.top = swapped ? second : first;
	p.bottom = swapped ? first : second;
	return p;
}

const char *ds_layout_name(enum ds_layout layout)
{
	static const char *names[DS_LAYOUT_COUNT] = {
		"Singolo", "Singolo + riquadro", "Impilati", "Affiancati",
		"Grande + piccolo", "Nitido singolo", "Nitido impilati",
	};
	return (unsigned)layout < DS_LAYOUT_COUNT ? names[layout] : "?";
}

int ds_touch_point(const struct ds_placement *p, int px, int py, int *tx, int *ty)
{
	const struct ds_screen *b = &p->bottom;
	if (!b->visible || b->r.w <= 0 || b->r.h <= 0)
		return 0;
	if (px < b->r.x || py < b->r.y || px >= b->r.x + b->r.w || py >= b->r.y + b->r.h)
		return 0;
	*tx = (px - b->r.x) * DS_W / b->r.w;
	*ty = (py - b->r.y) * DS_H / b->r.h;
	return 1;
}

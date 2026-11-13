/* Where pictures go on the 640x480 panel. Pure arithmetic. */
#pragma once

#define SCREEN_W 640
#define SCREEN_H 480
#define DS_W 256
#define DS_H 192

struct rect {
	int x, y, w, h;
};

enum scale_mode {
	SCALE_INTEGER, /* the largest whole multiple that fits */
	SCALE_FIT,     /* as large as fits, keeping the aspect */
};

/* A src_w x src_h picture centred in dst_w x dst_h. */
struct rect fit_rect(int src_w, int src_h, int dst_w, int dst_h, enum scale_mode mode);

/* The DS layouts of docs/0001. */
enum ds_layout {
	DS_SINGLE,        /* one screen at 2.5x, 640x480 */
	DS_SINGLE_PIP,    /* the same, the other one at 0.75x in a corner */
	DS_STACKED,       /* both at 1.25x, 320x480 */
	DS_SIDE,          /* both at 1.25x, 640x240 */
	DS_LARGE_SMALL,   /* 480x360 and 160x120 */
	DS_SHARP_SINGLE,  /* one at 2x, 512x384 */
	DS_SHARP_STACKED, /* both at 1x, 256x384 */
	DS_LAYOUT_COUNT
};

struct ds_screen {
	struct rect r;
	int visible;
	float alpha;
};

struct ds_placement {
	struct ds_screen top, bottom;
};

/* swapped puts the bottom screen where the top one goes, and in the single
 * layouts shows the bottom screen. */
struct ds_placement ds_place(enum ds_layout layout, int swapped);
const char *ds_layout_name(enum ds_layout layout);

/* A point on the panel to DS touch coordinates (0-255, 0-191). Returns 0
 * when the bottom screen is hidden or the point is outside it. */
int ds_touch_point(const struct ds_placement *p, int px, int py, int *tx, int *ty);

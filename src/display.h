/* The panel through KMS/DRM, with GBM and EGL for GLES2. */
#pragma once
#include <stdint.h>

struct display {
	int fd;
	int w, h;
	int refresh_mhz; /* refresh rate in mHz, e.g. 60000 */
	void *priv;
};

int display_open(struct display *d);
/* No panel: a w x h pbuffer through Mesa's surfaceless platform, for
 * screenshots in a container (with llvmpipe). */
int display_open_offscreen(struct display *d, int w, int h);
/* The picture as RGB888 rows, top first (w * h * 3 bytes). */
int display_read_rgb(struct display *d, unsigned char *out);
/* Shows what GLES drew and waits for the page flip (vblank). */
int display_present(struct display *d);
void display_close(struct display *d);

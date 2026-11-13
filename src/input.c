#include "input.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

#define TEST_BIT(bits, n) ((bits)[(n) / (8 * sizeof(long))] >> ((n) % (8 * sizeof(long))) & 1)

/* Key codes measured on the R36S under Lume (r36s-lightos README). */
static int map_key(int code)
{
	switch (code) {
	case BTN_DPAD_UP: return B_UP;
	case BTN_DPAD_DOWN: return B_DOWN;
	case BTN_DPAD_LEFT: return B_LEFT;
	case BTN_DPAD_RIGHT: return B_RIGHT;
	case BTN_EAST: return B_A;
	case BTN_SOUTH: return B_B;
	case BTN_NORTH: return B_X;
	case BTN_WEST: return B_Y;
	case BTN_TL: return B_L1;
	case BTN_TR: return B_R1;
	case BTN_TL2: return B_L2;
	case BTN_TR2: return B_R2;
	case BTN_SELECT: return B_SELECT;
	case BTN_START: return B_START;
	case BTN_MODE: return B_FN;
	case BTN_THUMBL: return B_L3;
	case BTN_THUMBR: return B_R3;
	case KEY_VOLUMEUP: return B_VOLUP;
	case KEY_VOLUMEDOWN: return B_VOLDOWN;
	case KEY_POWER: return B_POWER;
	default: return -1;
	}
}

static const int axes[4] = {ABS_X, ABS_Y, ABS_RX, ABS_RY};

int input_open(struct input *in)
{
	memset(in, 0, sizeof *in);
	DIR *d = opendir("/dev/input");
	if (!d) {
		LOG("/dev/input: %s", strerror(errno));
		return -1;
	}
	struct dirent *e;
	while ((e = readdir(d)) && in->nfds < 16) {
		if (strncmp(e->d_name, "event", 5) != 0)
			continue;
		char path[64];
		snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		unsigned long evbits[EV_MAX / (8 * sizeof(long)) + 1] = {0};
		if (ioctl(fd, EVIOCGBIT(0, sizeof evbits), evbits) < 0 || !TEST_BIT(evbits, EV_KEY)) {
			close(fd); /* not a button device; closing it now costs nothing yet */
			continue;
		}
		char name[64] = "?";
		ioctl(fd, EVIOCGNAME(sizeof name), name);
		int i = in->nfds++;
		in->fds[i] = fd;
		for (int a = 0; a < 4; a++) {
			struct input_absinfo ai;
			if (TEST_BIT(evbits, EV_ABS) && ioctl(fd, EVIOCGABS(axes[a]), &ai) == 0 && ai.maximum > ai.minimum) {
				in->abs[i][a].min = ai.minimum;
				in->abs[i][a].max = ai.maximum;
			}
		}
		LOG("input %s: %s", path, name);
	}
	closedir(d);
	return in->nfds ? 0 : -1;
}

static float norm_axis(int v, int min, int max)
{
	if (max <= min)
		return 0;
	float f = ((float)(v - min) / (float)(max - min)) * 2.f - 1.f;
	if (f > -0.2f && f < 0.2f)
		return 0; /* dead zone */
	return f < -1 ? -1 : f > 1 ? 1 : f;
}

/* The left stick as a d-pad, with hysteresis so it doesn't chatter. */
static int stick_dirs(float x, float y, int prev)
{
	int dirs = 0;
	float on = 0.5f, off = 0.35f;
	if (y < -((prev & BIT(B_UP)) ? off : on)) dirs |= BIT(B_UP);
	if (y > ((prev & BIT(B_DOWN)) ? off : on)) dirs |= BIT(B_DOWN);
	if (x < -((prev & BIT(B_LEFT)) ? off : on)) dirs |= BIT(B_LEFT);
	if (x > ((prev & BIT(B_RIGHT)) ? off : on)) dirs |= BIT(B_RIGHT);
	return dirs;
}

void input_poll(struct input *in)
{
	uint32_t before = in->held;
	struct input_event ev[32];
	for (int i = 0; i < in->nfds; i++) {
		for (;;) {
			ssize_t n = read(in->fds[i], ev, sizeof ev);
			if (n <= 0)
				break;
			for (size_t k = 0; k < (size_t)n / sizeof ev[0]; k++) {
				if (ev[k].type == EV_KEY) {
					int b = map_key(ev[k].code);
					if (b < 0)
						continue;
					if (ev[k].value)
						in->keys |= BIT(b);
					else
						in->keys &= ~BIT(b);
				} else if (ev[k].type == EV_ABS) {
					for (int a = 0; a < 4; a++) {
						if (ev[k].code != axes[a])
							continue;
						float f = norm_axis(ev[k].value, in->abs[i][a].min, in->abs[i][a].max);
						if (a == 0) in->lx = f;
						if (a == 1) in->ly = f;
						if (a == 2) in->rx = f;
						if (a == 3) in->ry = f;
					}
				}
			}
		}
	}
	in->stick_dirs = stick_dirs(in->lx, in->ly, in->stick_dirs);
	in->held = in->keys | (uint32_t)in->stick_dirs;
	in->pressed = in->held & ~before;
	in->released = before & ~in->held;
}

void input_close(struct input *in)
{
	for (int i = 0; i < in->nfds; i++)
		close(in->fds[i]);
	in->nfds = 0;
}

int input_repeat(struct input *in, struct repeat *r, int button, uint64_t now_ms)
{
	if (in->pressed & BIT(button)) {
		r->button = button;
		r->next = now_ms + 300;
		return 1;
	}
	if (r->button == button && (in->held & BIT(button)) && now_ms >= r->next) {
		r->next = now_ms + 70;
		return 1;
	}
	return 0;
}

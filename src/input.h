/* The R36S buttons from evdev. Every event device stays open for the whole
 * run: closing one costs about 40 ms (synchronize_rcu) and drops frames. */
#pragma once
#include <stdint.h>

enum button {
	B_UP, B_DOWN, B_LEFT, B_RIGHT,
	B_A, B_B, B_X, B_Y,
	B_L1, B_R1, B_L2, B_R2,
	B_SELECT, B_START, B_FN, B_L3, B_R3,
	B_VOLUP, B_VOLDOWN, B_POWER,
	B_COUNT
};

#define BIT(b) (1u << (b))

struct input {
	int fds[16];
	int nfds;
	uint32_t held;     /* down now (the left stick counts as the d-pad) */
	uint32_t pressed;  /* went down since the last poll */
	uint32_t released; /* went up since the last poll */
	float lx, ly, rx, ry; /* sticks, -1..1, with a dead zone */
	uint32_t keys;     /* buttons alone, without the stick */
	int stick_dirs;    /* the left stick as d-pad bits */
	struct {
		int min, max;
	} abs[16][4];      /* per device: ABS_X, ABS_Y, ABS_RX, ABS_RY */
};

int input_open(struct input *in);
void input_poll(struct input *in);
void input_close(struct input *in);

/* Repeats a held direction for menus: 1 on the press, then after delay
 * every interval (milliseconds), given the time now. */
struct repeat {
	int button;
	uint64_t next;
};
int input_repeat(struct input *in, struct repeat *r, int button, uint64_t now_ms);

/* Audio plumbing that needs no device: a lock-free ring between the game
 * thread and the audio thread, a linear resampler, and dynamic rate
 * control. Frames are interleaved stereo int16. */
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* One producer, one consumer. cap is in frames. */
struct ring {
	int16_t *buf;
	size_t cap;
	atomic_size_t head; /* frames written, ever */
	atomic_size_t tail; /* frames read, ever */
};

int ring_init(struct ring *r, size_t cap);
void ring_free(struct ring *r);
size_t ring_fill(struct ring *r);
/* Write up to n frames; returns how many fit. */
size_t ring_write(struct ring *r, const int16_t *frames, size_t n);
/* Read up to n frames; returns how many there were. */
size_t ring_read(struct ring *r, int16_t *out, size_t n);
/* Drop everything (the consumer must not be reading). */
void ring_clear(struct ring *r);

struct resampler {
	double pos;      /* position inside the current input block */
	int16_t last[2]; /* the last frame of the previous block */
};

/* Resample n input frames by step (input frames per output frame, so
 * in_rate / out_rate). Writes at most out_cap frames; returns how many. */
size_t resample_linear(struct resampler *rs, const int16_t *in, size_t n,
                       double step, int16_t *out, size_t out_cap);

/* Dynamic rate control: a factor in [1 - max_dev, 1 + max_dev] for the
 * resampling step that pulls fill towards target. Above target, it
 * returns more than 1: fewer output frames, the ring drains. */
double drc_factor(size_t fill, size_t target, double max_dev);

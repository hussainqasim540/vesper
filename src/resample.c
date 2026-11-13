#include "resample.h"

#include <stdlib.h>
#include <string.h>

int ring_init(struct ring *r, size_t cap)
{
	r->buf = calloc(cap * 2, sizeof *r->buf);
	r->cap = cap;
	atomic_init(&r->head, 0);
	atomic_init(&r->tail, 0);
	return r->buf ? 0 : -1;
}

void ring_free(struct ring *r)
{
	free(r->buf);
	r->buf = NULL;
}

size_t ring_fill(struct ring *r)
{
	return atomic_load_explicit(&r->head, memory_order_acquire) -
	       atomic_load_explicit(&r->tail, memory_order_acquire);
}

size_t ring_write(struct ring *r, const int16_t *frames, size_t n)
{
	size_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
	size_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
	size_t space = r->cap - (head - tail);
	if (n > space)
		n = space;
	for (size_t done = 0; done < n;) {
		size_t at = (head + done) % r->cap;
		size_t chunk = r->cap - at;
		if (chunk > n - done)
			chunk = n - done;
		memcpy(r->buf + at * 2, frames + done * 2, chunk * 2 * sizeof *frames);
		done += chunk;
	}
	atomic_store_explicit(&r->head, head + n, memory_order_release);
	return n;
}

size_t ring_read(struct ring *r, int16_t *out, size_t n)
{
	size_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
	size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
	size_t avail = head - tail;
	if (n > avail)
		n = avail;
	for (size_t done = 0; done < n;) {
		size_t at = (tail + done) % r->cap;
		size_t chunk = r->cap - at;
		if (chunk > n - done)
			chunk = n - done;
		memcpy(out + done * 2, r->buf + at * 2, chunk * 2 * sizeof *out);
		done += chunk;
	}
	atomic_store_explicit(&r->tail, tail + n, memory_order_release);
	return n;
}

void ring_clear(struct ring *r)
{
	atomic_store_explicit(&r->tail, atomic_load(&r->head), memory_order_release);
}

/* Output at position pos lies between input frames floor(pos) - 1 and
 * floor(pos), where frame -1 is the previous block's last: one frame of
 * delay, and no gap between blocks. */
size_t resample_linear(struct resampler *rs, const int16_t *in, size_t n,
                       double step, int16_t *out, size_t out_cap)
{
	if (n == 0 || step <= 0)
		return 0;
	size_t o = 0;
	double pos = rs->pos;
	while (pos < (double)n && o < out_cap) {
		size_t i = (size_t)pos;
		double frac = pos - (double)i;
		const int16_t *a = i == 0 ? rs->last : in + (i - 1) * 2;
		const int16_t *b = in + i * 2;
		for (int c = 0; c < 2; c++)
			out[o * 2 + c] = (int16_t)((double)a[c] + ((double)b[c] - (double)a[c]) * frac);
		o++;
		pos += step;
	}
	/* Out of room: drop the rest of the block rather than carry it. */
	rs->pos = pos >= (double)n ? pos - (double)n : 0;
	rs->last[0] = in[(n - 1) * 2];
	rs->last[1] = in[(n - 1) * 2 + 1];
	return o;
}

double drc_factor(size_t fill, size_t target, double max_dev)
{
	if (target == 0)
		return 1.0;
	double d = ((double)fill - (double)target) / (double)target;
	if (d > 1)
		d = 1;
	if (d < -1)
		d = -1;
	return 1.0 + d * max_dev;
}

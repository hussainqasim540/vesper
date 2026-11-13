/* Sound out through ALSA (plug:default at 48 kHz), from a thread of its
 * own. The game thread pushes samples at the core's rate; they are
 * resampled here, with dynamic rate control keeping the ring half full. */
#pragma once
#include <stddef.h>
#include <stdint.h>

int audio_open(void);
void audio_close(void);
/* The rate of what will be pushed; also empties the ring. */
void audio_set_source_rate(double hz);
void audio_push(const int16_t *frames, size_t n);
/* 0-100, applied in software. */
void audio_set_volume(int percent);
int audio_volume(void);

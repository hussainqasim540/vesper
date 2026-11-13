#include "audio.h"

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "resample.h"

#define OUT_RATE 48000
#define RING_FRAMES 4096
#define TARGET_FRAMES 1600 /* about 33 ms */
#define PERIOD_FRAMES 256

static snd_pcm_t *pcm;
static pthread_t thread;
static atomic_int running, flush, volume = 80;
static struct ring ring;
static struct resampler rs;
static double source_rate = OUT_RATE;

static void *audio_thread(void *arg)
{
	(void)arg;
	int16_t buf[PERIOD_FRAMES * 2];
	while (atomic_load(&running)) {
		if (atomic_exchange(&flush, 0))
			ring_clear(&ring); /* only the reader moves the tail */
		size_t got = ring_read(&ring, buf, PERIOD_FRAMES);
		if (got < PERIOD_FRAMES) /* underrun, or no game: silence */
			memset(buf + got * 2, 0, (PERIOD_FRAMES - got) * 2 * sizeof *buf);
		int vol = atomic_load(&volume);
		if (vol < 100) {
			/* A curve: the ear hears loudness roughly as a power. */
			int gain = vol * vol * 256 / 10000;
			for (size_t i = 0; i < PERIOD_FRAMES * 2; i++)
				buf[i] = (int16_t)(buf[i] * gain / 256);
		}
		snd_pcm_sframes_t n = snd_pcm_writei(pcm, buf, PERIOD_FRAMES);
		if (n < 0) {
			n = snd_pcm_recover(pcm, (int)n, 1);
			if (n < 0) {
				LOG("alsa: %s", snd_strerror((int)n));
				usleep(5000);
			}
		}
	}
	return NULL;
}

int audio_open(void)
{
	int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		LOG("alsa open: %s", snd_strerror(err));
		return -1;
	}
	/* About 20 ms in ALSA, on top of the ring. */
	err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
	                         OUT_RATE, 1, 20000);
	if (err < 0) {
		LOG("alsa params: %s", snd_strerror(err));
		snd_pcm_close(pcm);
		pcm = NULL;
		return -1;
	}
	if (ring_init(&ring, RING_FRAMES))
		return -1;
	atomic_store(&running, 1);
	if (pthread_create(&thread, NULL, audio_thread, NULL)) {
		atomic_store(&running, 0);
		return -1;
	}
	return 0;
}

void audio_close(void)
{
	if (!pcm)
		return;
	atomic_store(&running, 0);
	pthread_join(thread, NULL);
	snd_pcm_drop(pcm);
	snd_pcm_close(pcm);
	pcm = NULL;
	ring_free(&ring);
}

void audio_set_source_rate(double hz)
{
	source_rate = hz > 0 ? hz : OUT_RATE;
	rs = (struct resampler){0};
	atomic_store(&flush, 1);
}

void audio_push(const int16_t *frames, size_t n)
{
	if (!pcm || n == 0)
		return;
	double step = source_rate / OUT_RATE * drc_factor(ring_fill(&ring), TARGET_FRAMES, 0.005);
	int16_t out[2048 * 2];
	while (n > 0) {
		size_t chunk = n > 1024 ? 1024 : n;
		size_t o = resample_linear(&rs, frames, chunk, step, out, 2048);
		ring_write(&ring, out, o); /* a full ring drops the rest */
		frames += chunk * 2;
		n -= chunk;
	}
}

void audio_set_volume(int percent)
{
	atomic_store(&volume, percent < 0 ? 0 : percent > 100 ? 100 : percent);
}

int audio_volume(void)
{
	return atomic_load(&volume);
}

/* vesper --headless CORE.so ROM [FRAMES]
 *
 * Runs a core with no display, input or sound, as fast as it goes, and
 * prints what it produced and how long each frame took. It checks the
 * libretro host anywhere (a container, a chroot) and measures a core's
 * CPU cost on the console without the 60 Hz pacing. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "core.h"
#include "headless.h"

static size_t audio_frames;

static void count_audio(void *ud, const int16_t *frames, size_t n)
{
	(void)ud, (void)frames;
	audio_frames += n;
}

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int headless_main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s --headless CORE.so ROM [FRAMES] [DUMP]\n", argv[0]);
		return 2;
	}
	int frames = argc > 4 ? atoi(argv[4]) : 600;
	static struct core c;
	if (core_open(&c, argv[2], "/tmp", "/tmp"))
		return 1;
	c.audio = count_audio;
	if (core_load_game(&c, argv[3])) {
		core_close(&c);
		return 1;
	}
	double t0 = now_s(), worst = 0;
	for (int i = 0; i < frames; i++) {
		/* Press START now and then, so a game leaves its title screen. */
		c.joypad = (i / 30) % 4 == 1 ? 1u << RETRO_DEVICE_ID_JOYPAD_START : 0;
		double t = now_s();
		core_run(&c);
		double dt = now_s() - t;
		if (dt > worst)
			worst = dt;
	}
	double total = now_s() - t0;

	uint32_t sum = 0;
	const uint8_t *px = c.frame.pixels;
	size_t bytes = (size_t)c.frame.w * (size_t)c.frame.h * (c.frame.fmt == FRAME_XRGB8888 ? 4 : 2);
	for (size_t i = 0; px && i < bytes; i++)
		sum = sum * 31 + px[i];
	if (argc > 5 && px) { /* the last picture, raw, as the core drew it */
		FILE *f = fopen(argv[5], "wb");
		if (f) {
			fwrite(px, 1, bytes, f);
			fclose(f);
		}
	}
	printf("frames %d, picture %dx%d %s, %d new pictures, checksum %08x\n", frames, c.frame.w,
	       c.frame.h, c.frame.fmt == FRAME_XRGB8888 ? "XRGB8888" : "RGB565", c.frame.serial, sum);
	printf("audio %zu frames (%.0f per video frame, core says %.0f Hz at %.3f fps)\n", audio_frames,
	       (double)audio_frames / frames, c.av.timing.sample_rate, c.av.timing.fps);
	printf("time %.2f ms per frame on average, %.2f ms worst (budget 16.7)\n", total * 1000 / frames,
	       worst * 1000);
	core_close(&c);
	return 0;
}

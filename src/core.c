#include "core.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

static struct core *cur;

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
	static const char *names[] = {"debug", "info", "warn", "error"};
	if (level == RETRO_LOG_DEBUG)
		return;
	char msg[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	size_t len = strlen(msg);
	while (len && (msg[len - 1] == '\n' || msg[len - 1] == '\r'))
		msg[--len] = '\0';
	LOG("%s [%s]: %s", cur ? cur->name : "core",
	    (unsigned)level < 4 ? names[level] : "?", msg);
}

static bool environment(unsigned cmd, void *data)
{
	struct core *c = cur;
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*(bool *)data = true;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		enum retro_pixel_format f = *(const enum retro_pixel_format *)data;
		if (f != RETRO_PIXEL_FORMAT_0RGB1555 && f != RETRO_PIXEL_FORMAT_RGB565 &&
		    f != RETRO_PIXEL_FORMAT_XRGB8888)
			return false;
		c->pixfmt = f;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*(const char **)data = c->system_dir;
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = c->save_dir;
		return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *)data)->log = core_log;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE:
		/* No options yet: every core runs with its defaults. */
		((struct retro_variable *)data)->value = NULL;
		return false;
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = false;
		return true;
	case RETRO_ENVIRONMENT_SET_VARIABLES:
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
	case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
		return true;
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned *)data = 0; /* so cores fall back to SET_VARIABLES */
		return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
		*(int *)data = 1 | 2;
		return true;
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
		c->av.geometry = *(const struct retro_game_geometry *)data;
		return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		c->av = *(const struct retro_system_av_info *)data;
		return true;
	default:
		return false;
	}
}

/* Repacks and converts into c->frame. 0RGB1555 becomes RGB565, so the GPU
 * sees two formats only. */
static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
	struct core *c = cur;
	if (!data || w == 0 || h == 0)
		return; /* a dupe: keep the last frame */
	int bpp = c->pixfmt == RETRO_PIXEL_FORMAT_XRGB8888 ? 4 : 2;
	size_t need = (size_t)w * h * (size_t)bpp;
	if (need > c->frame_cap) {
		void *p = realloc(c->frame.pixels, need);
		if (!p)
			return;
		c->frame.pixels = p;
		c->frame_cap = need;
	}
	const uint8_t *src = data;
	uint8_t *dst = c->frame.pixels;
	for (unsigned y = 0; y < h; y++, src += pitch, dst += (size_t)w * bpp) {
		if (c->pixfmt == RETRO_PIXEL_FORMAT_0RGB1555) {
			const uint16_t *s = (const uint16_t *)src;
			uint16_t *d = (uint16_t *)dst;
			for (unsigned x = 0; x < w; x++) {
				uint16_t p = s[x];
				/* RRRRR GGGGG BBBBB -> RRRRR GGGGGG BBBBB, low green bit from the top one */
				d[x] = (uint16_t)((p & 0x7C00) << 1 | (p & 0x03E0) << 1 | (p & 0x0200) >> 4 | (p & 0x001F));
			}
		} else {
			memcpy(dst, src, (size_t)w * bpp);
		}
	}
	c->frame.w = (int)w;
	c->frame.h = (int)h;
	c->frame.fmt = bpp == 4 ? FRAME_XRGB8888 : FRAME_RGB565;
	c->frame.serial++;
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
	if (cur->audio)
		cur->audio(cur->audio_ud, data, frames);
	return frames;
}

static void audio_sample(int16_t l, int16_t r)
{
	int16_t f[2] = {l, r};
	audio_batch(f, 1);
}

static void input_poll(void)
{
	/* The frontend polls evdev once per frame before retro_run. */
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
	(void)index;
	if (port != 0 || device != RETRO_DEVICE_JOYPAD)
		return 0;
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
		return (int16_t)cur->joypad;
	return id < 16 ? (cur->joypad >> id) & 1 : 0;
}

#define SYM(name)                                                    \
	do {                                                         \
		*(void **)&c->name = dlsym(c->dl, #name);            \
		if (!c->name) {                                      \
			LOG("%s: no symbol %s", so_path, #name);     \
			goto fail;                                   \
		}                                                    \
	} while (0)

int core_open(struct core *c, const char *so_path, const char *system_dir, const char *save_dir)
{
	memset(c, 0, sizeof *c);
	c->pixfmt = RETRO_PIXEL_FORMAT_0RGB1555; /* libretro's default */
	snprintf(c->system_dir, sizeof c->system_dir, "%s", system_dir);
	snprintf(c->save_dir, sizeof c->save_dir, "%s", save_dir);
	const char *base = strrchr(so_path, '/');
	snprintf(c->name, sizeof c->name, "%s", base ? base + 1 : so_path);
	char *dot = strstr(c->name, "_libretro");
	if (dot)
		*dot = '\0';

	c->dl = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
	if (!c->dl) {
		LOG("dlopen %s: %s", so_path, dlerror());
		return -1;
	}
	SYM(retro_init);
	SYM(retro_deinit);
	SYM(retro_api_version);
	SYM(retro_get_system_info);
	SYM(retro_get_system_av_info);
	SYM(retro_set_environment);
	SYM(retro_set_video_refresh);
	SYM(retro_set_audio_sample);
	SYM(retro_set_audio_sample_batch);
	SYM(retro_set_input_poll);
	SYM(retro_set_input_state);
	SYM(retro_set_controller_port_device);
	SYM(retro_reset);
	SYM(retro_run);
	SYM(retro_load_game);
	SYM(retro_unload_game);
	SYM(retro_get_memory_data);
	SYM(retro_get_memory_size);

	if (c->retro_api_version() != RETRO_API_VERSION) {
		LOG("%s: libretro API %u, expected %d", so_path, c->retro_api_version(), RETRO_API_VERSION);
		goto fail;
	}
	cur = c;
	c->retro_set_environment(environment);
	c->retro_set_video_refresh(video_refresh);
	c->retro_set_audio_sample(audio_sample);
	c->retro_set_audio_sample_batch(audio_batch);
	c->retro_set_input_poll(input_poll);
	c->retro_set_input_state(input_state);
	c->retro_get_system_info(&c->info);
	c->retro_init();
	LOG("core %s %s", c->info.library_name, c->info.library_version);
	return 0;
fail:
	dlclose(c->dl);
	c->dl = NULL;
	return -1;
}

static void *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	void *buf = NULL;
	if (fseek(f, 0, SEEK_END) == 0) {
		long n = ftell(f);
		if (n >= 0 && n < 512L << 20 && fseek(f, 0, SEEK_SET) == 0) {
			buf = malloc(n ? (size_t)n : 1);
			if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
				free(buf);
				buf = NULL;
			}
			*size = (size_t)n;
		}
	}
	fclose(f);
	return buf;
}

int core_load_game(struct core *c, const char *rom_path)
{
	struct retro_game_info gi = {.path = rom_path};
	if (!c->info.need_fullpath) {
		size_t size = 0;
		c->rom = read_file(rom_path, &size);
		if (!c->rom) {
			LOG("cannot read %s: %s", rom_path, strerror(errno));
			return -1;
		}
		gi.data = c->rom;
		gi.size = size;
	}
	cur = c;
	if (!c->retro_load_game(&gi)) {
		LOG("%s refused %s", c->name, rom_path);
		free(c->rom);
		c->rom = NULL;
		return -1;
	}
	c->game_loaded = 1;
	c->retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
	c->retro_get_system_av_info(&c->av);
	LOG("%s: %ux%u, %.3f fps, %.0f Hz", rom_path, c->av.geometry.base_width,
	    c->av.geometry.base_height, c->av.timing.fps, c->av.timing.sample_rate);
	return 0;
}

void core_run(struct core *c)
{
	cur = c;
	c->retro_run();
}

void core_close(struct core *c)
{
	if (!c->dl)
		return;
	cur = c;
	if (c->game_loaded)
		c->retro_unload_game();
	c->retro_deinit();
	dlclose(c->dl);
	free(c->rom);
	free(c->frame.pixels);
	cur = NULL;
	memset(c, 0, sizeof *c);
}

int core_sram_load(struct core *c, const char *path)
{
	size_t size = c->retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	void *mem = c->retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	if (!size || !mem)
		return 0;
	size_t got = 0;
	void *buf = read_file(path, &got);
	if (!buf)
		return 0;
	memcpy(mem, buf, got < size ? got : size);
	free(buf);
	LOG("save loaded: %s (%zu bytes)", path, got);
	return 1;
}

int core_sram_save(struct core *c, const char *path)
{
	size_t size = c->retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	const void *mem = c->retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	if (!size || !mem)
		return 0;
	size_t old_size = 0;
	void *old = read_file(path, &old_size);
	int same = old && old_size == size && memcmp(old, mem, size) == 0;
	free(old);
	if (same)
		return 0;

	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		LOG("cannot write %s: %s", tmp, strerror(errno));
		return -1;
	}
	const uint8_t *p = mem;
	size_t left = size;
	while (left) {
		ssize_t n = write(fd, p, left);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			LOG("write %s: %s", tmp, strerror(errno));
			close(fd);
			unlink(tmp);
			return -1;
		}
		p += n;
		left -= (size_t)n;
	}
	if (fsync(fd) != 0 || close(fd) != 0 || rename(tmp, path) != 0) {
		LOG("save %s: %s", path, strerror(errno));
		unlink(tmp);
		return -1;
	}
	/* Make the rename itself durable. */
	char dir[1100];
	snprintf(dir, sizeof dir, "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash) {
		*slash = '\0';
		int dfd = open(dir, O_RDONLY);
		if (dfd >= 0) {
			fsync(dfd);
			close(dfd);
		}
	}
	return 1;
}

/* A libretro core loaded with dlopen. libretro allows one instance per
 * process, so there is one current core at a time. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "libretro.h"

/* The picture the core drew last, repacked tight. */
enum frame_format {
	FRAME_RGB565,   /* uint16 per pixel */
	FRAME_XRGB8888, /* uint32 per pixel, bytes B G R X in memory */
};

struct core_frame {
	void *pixels;
	int w, h;
	enum frame_format fmt;
	int serial; /* goes up with each new frame */
};

typedef void (*core_audio_fn)(void *ud, const int16_t *frames, size_t n);

struct core {
	void *dl;
	char name[64];
	struct retro_system_info info;
	struct retro_system_av_info av;
	enum retro_pixel_format pixfmt;
	struct core_frame frame;
	size_t frame_cap;
	void *rom;        /* the ROM in memory, for cores that want it so */
	uint16_t joypad;  /* RETRO_DEVICE_ID_JOYPAD_* bits */
	char system_dir[512], save_dir[512];
	core_audio_fn audio;
	void *audio_ud;
	int game_loaded;

	void (*retro_init)(void);
	void (*retro_deinit)(void);
	unsigned (*retro_api_version)(void);
	void (*retro_get_system_info)(struct retro_system_info *);
	void (*retro_get_system_av_info)(struct retro_system_av_info *);
	void (*retro_set_environment)(retro_environment_t);
	void (*retro_set_video_refresh)(retro_video_refresh_t);
	void (*retro_set_audio_sample)(retro_audio_sample_t);
	void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*retro_set_input_poll)(retro_input_poll_t);
	void (*retro_set_input_state)(retro_input_state_t);
	void (*retro_set_controller_port_device)(unsigned, unsigned);
	void (*retro_reset)(void);
	void (*retro_run)(void);
	bool (*retro_load_game)(const struct retro_game_info *);
	void (*retro_unload_game)(void);
	void *(*retro_get_memory_data)(unsigned);
	size_t (*retro_get_memory_size)(unsigned);
};

/* Loads the .so and calls retro_init. */
int core_open(struct core *c, const char *so_path, const char *system_dir, const char *save_dir);
int core_load_game(struct core *c, const char *rom_path);
void core_run(struct core *c);
/* Unloads the game (if any), calls retro_deinit and closes the .so. */
void core_close(struct core *c);

/* The battery save: the core's SAVE_RAM. */
int core_sram_load(struct core *c, const char *path);
/* Writes only when it differs from the file; atomically (tmp, fsync,
 * rename). Returns 1 written, 0 unchanged or none, -1 error. */
int core_sram_save(struct core *c, const char *path);

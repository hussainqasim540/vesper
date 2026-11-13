/* Vesper: the shelf, and GBA/GBC games through libretro cores.
 *
 * The folder Vesper runs from (VESPER_ROOT, or the parent of the folder
 * holding the binary) has:
 *   bin/vesper                   this program
 *   cores/<name>_libretro.so     gpsp, mgba, gambatte...
 *   games/                       ROMs, also in subfolders
 *   saves/                       battery saves (.sav)
 *   system/                      BIOS files, for the cores that want them
 *   assets/background.vimg       the background (tools/make_background.py)
 */
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "core.h"
#include "display.h"
#include "gfx.h"
#include "headless.h"
#include "image.h"
#include "input.h"
#include "layout.h"
#include "log.h"
#include "shelf.h"

/* QuisLab's palette. */
#define LILAC 0x9D88ECFFu
#define NIGHT 0x0E0C16FFu
#define WARM 0xF2EEE8FFu
#define WITH_ALPHA(c, a) (((c) & 0xFFFFFF00u) | (uint32_t)(a))

#define SAVE_EVERY_MS 10000

enum mode { MODE_SHELF, MODE_GAME, MODE_POWER };

struct app {
	char root[PATH_MAX];
	struct display disp;
	struct input in;
	struct shelf shelf;
	int sel, first;
	struct repeat rep;
	struct tex bg;
	struct core core;
	struct tex frame_tex;
	int frame_serial;
	const struct game *game;
	char save_path[PATH_MAX + 64];
	uint64_t next_save;
	enum mode mode, before_power;
	int power_sel;
	char toast[128];
	uint64_t toast_until;
	int audio_ok;
};

static volatile sig_atomic_t stop_requested;

static void on_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void toast(struct app *a, const char *msg)
{
	snprintf(a->toast, sizeof a->toast, "%s", msg);
	a->toast_until = now_ms() + 1800;
	LOG("%s", msg);
}

static void find_root(struct app *a)
{
	const char *env = getenv("VESPER_ROOT");
	if (env && *env) {
		snprintf(a->root, sizeof a->root, "%s", env);
		return;
	}
	char exe[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (n > 0) {
		exe[n] = '\0';
		char *s = strrchr(exe, '/'); /* .../bin/vesper -> .../bin */
		if (s)
			*s = '\0';
		s = strrchr(exe, '/'); /* .../bin -> ... */
		if (s && s != exe)
			*s = '\0';
		snprintf(a->root, sizeof a->root, "%s", exe);
		return;
	}
	snprintf(a->root, sizeof a->root, ".");
}

static void path_in(const struct app *a, char *out, size_t n, const char *sub)
{
	snprintf(out, n, "%s/%s", a->root, sub);
}

static int file_exists(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* The first core of the list that is on the card. */
static int core_path(const struct app *a, enum system sys, char *out, size_t n)
{
	static const char *gba[] = {"gpsp", "mgba", NULL};
	static const char *gb[] = {"gambatte", "mgba", NULL};
	const char **names = sys == SYS_GBA ? gba : (sys == SYS_GB || sys == SYS_GBC) ? gb : NULL;
	for (; names && *names; names++) {
		snprintf(out, n, "%s/cores/%s_libretro.so", a->root, *names);
		if (file_exists(out))
			return 0;
	}
	return -1;
}

/* ---- games ---- */

static void audio_from_core(void *ud, const int16_t *frames, size_t n)
{
	(void)ud;
	audio_push(frames, n);
}

static void save_now(struct app *a)
{
	if (!a->core.game_loaded)
		return;
	if (core_sram_save(&a->core, a->save_path) > 0)
		LOG("saved %s", a->save_path);
}

static void start_game(struct app *a, const struct game *g)
{
	if (g->sys == SYS_NDS) {
		toast(a, "DS: arriva con DSperate, nella prossima fase");
		return;
	}
	char so[PATH_MAX + 64], sys_dir[PATH_MAX + 16], save_dir[PATH_MAX + 16];
	if (core_path(a, g->sys, so, sizeof so)) {
		toast(a, "Manca il core per questo sistema (cores/)");
		return;
	}
	path_in(a, sys_dir, sizeof sys_dir, "system");
	path_in(a, save_dir, sizeof save_dir, "saves");
	mkdir(save_dir, 0755);
	if (core_open(&a->core, so, sys_dir, save_dir)) {
		toast(a, "Il core non si apre");
		return;
	}
	a->core.audio = audio_from_core;
	if (core_load_game(&a->core, g->path)) {
		core_close(&a->core);
		toast(a, "Il gioco non parte");
		return;
	}
	char name[256];
	const char *base = strrchr(g->path, '/');
	snprintf(name, sizeof name, "%s", base ? base + 1 : g->path);
	char *dot = strrchr(name, '.');
	if (dot)
		*dot = '\0';
	snprintf(a->save_path, sizeof a->save_path, "%s/%s.sav", save_dir, name);
	core_sram_load(&a->core, a->save_path);
	audio_set_source_rate(a->core.av.timing.sample_rate);
	a->game = g;
	a->frame_serial = -1;
	a->next_save = now_ms() + SAVE_EVERY_MS;
	a->mode = MODE_GAME;
}

static void stop_game(struct app *a)
{
	if (!a->core.dl)
		return;
	save_now(a);
	core_close(&a->core);
	audio_set_source_rate(48000);
	a->game = NULL;
	a->mode = MODE_SHELF;
	sync();
}

static uint16_t joypad_bits(uint32_t held)
{
	static const struct {
		int b;
		unsigned id;
	} map[] = {
		{B_B, RETRO_DEVICE_ID_JOYPAD_B}, {B_Y, RETRO_DEVICE_ID_JOYPAD_Y},
		{B_SELECT, RETRO_DEVICE_ID_JOYPAD_SELECT}, {B_START, RETRO_DEVICE_ID_JOYPAD_START},
		{B_UP, RETRO_DEVICE_ID_JOYPAD_UP}, {B_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
		{B_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT}, {B_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT},
		{B_A, RETRO_DEVICE_ID_JOYPAD_A}, {B_X, RETRO_DEVICE_ID_JOYPAD_X},
		{B_L1, RETRO_DEVICE_ID_JOYPAD_L}, {B_R1, RETRO_DEVICE_ID_JOYPAD_R},
		{B_L2, RETRO_DEVICE_ID_JOYPAD_L2}, {B_R2, RETRO_DEVICE_ID_JOYPAD_R2},
		{B_L3, RETRO_DEVICE_ID_JOYPAD_L3}, {B_R3, RETRO_DEVICE_ID_JOYPAD_R3},
	};
	uint16_t bits = 0;
	for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
		if (held & BIT(map[i].b))
			bits |= (uint16_t)(1u << map[i].id);
	return bits;
}

static void game_frame(struct app *a)
{
	if (a->in.pressed & BIT(B_FN)) {
		stop_game(a);
		return;
	}
	a->core.joypad = joypad_bits(a->in.held);
	core_run(&a->core);

	struct core_frame *f = &a->core.frame;
	gfx_clear(NIGHT);
	if (f->pixels && f->w > 0) {
		enum tex_format fmt = f->fmt == FRAME_XRGB8888 ? TEX_XRGB8888 : TEX_RGB565;
		if (!a->frame_tex.id || a->frame_tex.fmt != fmt) {
			tex_destroy(&a->frame_tex);
			tex_create(&a->frame_tex, 256, 256, fmt);
		}
		if (f->serial != a->frame_serial) {
			tex_upload(&a->frame_tex, f->pixels, f->w, f->h);
			a->frame_serial = f->serial;
		}
		/* The GB's 160x144 fits 3x exactly; the GBA fills the width
		 * (2.67x) through the sharp filter. */
		int gba = a->game && a->game->sys == SYS_GBA;
		struct rect dst = fit_rect(f->w, f->h, a->disp.w, a->disp.h, gba ? SCALE_FIT : SCALE_INTEGER);
		gfx_draw(&a->frame_tex, (struct rect){0, 0, f->w, f->h}, dst, FILTER_SHARP, 1.f);
	}

	uint64_t t = now_ms();
	if (t >= a->next_save) {
		save_now(a);
		a->next_save = t + SAVE_EVERY_MS;
	}
}

/* ---- the shelf ---- */

#define PANEL_X 32
#define PANEL_Y 24
#define PANEL_W 576
#define PANEL_H 432
#define LIST_Y 104
#define ROW_H 34
#define ROWS 9

static void draw_background(struct app *a)
{
	if (a->bg.id)
		gfx_draw(&a->bg, (struct rect){0, 0, SCREEN_W, SCREEN_H},
		         (struct rect){0, 0, a->disp.w, a->disp.h}, FILTER_LINEAR, 1.f);
	else
		gfx_clear(NIGHT);
}

static void draw_shelf(struct app *a)
{
	draw_background(a);
	gfx_fill((struct rect){PANEL_X, PANEL_Y, PANEL_W, PANEL_H}, WITH_ALPHA(NIGHT, 0xC8));

	float base = PANEL_Y + 20 + font_ascent(FONT_TITLE);
	gfx_text(FONT_TITLE, PANEL_X + 24, base, LILAC, "Vesper");
	char count[32];
	snprintf(count, sizeof count, a->shelf.count == 1 ? "%d gioco" : "%d giochi", a->shelf.count);
	float cw = font_text_width(FONT_SMALL, count);
	gfx_text(FONT_SMALL, PANEL_X + PANEL_W - 24 - cw, base, WITH_ALPHA(WARM, 0xA0), count);

	if (a->shelf.count == 0) {
		gfx_text(FONT_BODY, PANEL_X + 24, LIST_Y + 40, WARM, "Nessun gioco sulla scheda.");
		gfx_text(FONT_SMALL, PANEL_X + 24, LIST_Y + 72, WITH_ALPHA(WARM, 0xB0),
		         "Copia le ROM (.gba, .gbc, .gb, .nds) in vesper/games.");
	}

	if (a->sel < a->first)
		a->first = a->sel;
	if (a->sel >= a->first + ROWS)
		a->first = a->sel - ROWS + 1;
	for (int r = 0; r < ROWS && a->first + r < a->shelf.count; r++) {
		int i = a->first + r;
		const struct game *g = &a->shelf.games[i];
		int y = LIST_Y + r * ROW_H;
		int selected = i == a->sel;
		if (selected)
			gfx_fill((struct rect){PANEL_X + 12, y, PANEL_W - 24, ROW_H}, LILAC);
		float text_y = (float)y + (ROW_H + font_ascent(FONT_BODY)) / 2.f - 2;
		const char *tag = system_label(g->sys);
		float tw = font_text_width(FONT_SMALL, tag);
		char name[160];
		font_ellipsize(FONT_BODY, g->name, PANEL_W - 24 - 24 - tw - 24, name, sizeof name);
		rgba fg = selected ? NIGHT : WARM;
		rgba tagc = selected ? WITH_ALPHA(NIGHT, 0xB0) : (g->sys == SYS_NDS ? WITH_ALPHA(LILAC, 0x90) : LILAC);
		gfx_text(FONT_BODY, PANEL_X + 24, text_y, fg, name);
		gfx_text(FONT_SMALL, PANEL_X + PANEL_W - 24 - tw, text_y, tagc, tag);
	}

	gfx_text(FONT_SMALL, PANEL_X + 24, PANEL_Y + PANEL_H - 18, WITH_ALPHA(WARM, 0xB0),
	         "A  Gioca     START  Spegni     FN  esce dal gioco");
}

static void shelf_frame(struct app *a)
{
	uint64_t t = now_ms();
	int n = a->shelf.count;
	if (n > 0) {
		if (input_repeat(&a->in, &a->rep, B_DOWN, t))
			a->sel = (a->sel + 1) % n;
		if (input_repeat(&a->in, &a->rep, B_UP, t))
			a->sel = (a->sel + n - 1) % n;
		if (input_repeat(&a->in, &a->rep, B_RIGHT, t) || input_repeat(&a->in, &a->rep, B_R1, t))
			a->sel = a->sel + ROWS < n ? a->sel + ROWS : n - 1;
		if (input_repeat(&a->in, &a->rep, B_LEFT, t) || input_repeat(&a->in, &a->rep, B_L1, t))
			a->sel = a->sel - ROWS > 0 ? a->sel - ROWS : 0;
		if (a->in.pressed & BIT(B_A))
			start_game(a, &a->shelf.games[a->sel]);
	}
	if (a->in.pressed & (BIT(B_START) | BIT(B_POWER))) {
		a->before_power = MODE_SHELF;
		a->power_sel = 0;
		a->mode = MODE_POWER;
	}
	if (a->mode == MODE_SHELF)
		draw_shelf(a);
}

/* ---- power ---- */

/* Lume's poweroff and reboot are shims that note the request and SIGKILL
 * their parent: so exec them straight from this process, after saving. */
static void run_power(struct app *a, const char *cmd)
{
	stop_game(a);
	sync();
	LOG("%s", cmd);
	pid_t pid = fork();
	if (pid == 0) {
		execlp(cmd, cmd, (char *)NULL);
		_exit(127);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
	toast(a, "Non si spegne: il comando non ha funzionato");
}

static void power_frame(struct app *a)
{
	static const char *items[] = {"Spegni", "Riavvia", "Annulla"};
	uint64_t t = now_ms();
	if (input_repeat(&a->in, &a->rep, B_DOWN, t))
		a->power_sel = (a->power_sel + 1) % 3;
	if (input_repeat(&a->in, &a->rep, B_UP, t))
		a->power_sel = (a->power_sel + 2) % 3;
	if (a->in.pressed & (BIT(B_B) | BIT(B_START)))
		a->mode = a->before_power;
	if (a->in.pressed & BIT(B_A)) {
		if (a->power_sel == 0)
			run_power(a, "poweroff");
		else if (a->power_sel == 1)
			run_power(a, "reboot");
		a->mode = MODE_SHELF;
	}
	if (a->mode != MODE_POWER)
		return;

	draw_shelf(a);
	gfx_fill((struct rect){0, 0, a->disp.w, a->disp.h}, WITH_ALPHA(NIGHT, 0x90));
	struct rect box = {SCREEN_W / 2 - 130, SCREEN_H / 2 - 90, 260, 180};
	gfx_fill(box, WITH_ALPHA(NIGHT, 0xF0));
	for (int i = 0; i < 3; i++) {
		int y = box.y + 24 + i * 44;
		if (i == a->power_sel)
			gfx_fill((struct rect){box.x + 16, y, box.w - 32, 40}, LILAC);
		float w = font_text_width(FONT_BODY, items[i]);
		gfx_text(FONT_BODY, box.x + (box.w - w) / 2, (float)y + 20 + font_ascent(FONT_BODY) / 2.f - 2,
		         i == a->power_sel ? NIGHT : WARM, items[i]);
	}
}

static void draw_toast(struct app *a)
{
	if (now_ms() >= a->toast_until)
		return;
	float w = font_text_width(FONT_SMALL, a->toast);
	struct rect r = {(int)(SCREEN_W - w) / 2 - 16, SCREEN_H - 64, (int)w + 32, 34};
	gfx_fill(r, WITH_ALPHA(NIGHT, 0xE8));
	gfx_text(FONT_SMALL, (float)r.x + 16, (float)r.y + 17 + font_ascent(FONT_SMALL) / 2.f - 1, WARM, a->toast);
}

static void handle_volume(struct app *a)
{
	int v = audio_volume();
	if (a->in.pressed & BIT(B_VOLUP))
		v += 10;
	else if (a->in.pressed & BIT(B_VOLDOWN))
		v -= 10;
	else
		return;
	audio_set_volume(v);
	char msg[32];
	snprintf(msg, sizeof msg, "Volume %d%%", audio_volume());
	toast(a, msg);
}

/* Display (or a pbuffer), GLES, input, sound, background, shelf. */
static int app_init(struct app *a, int offscreen)
{
	find_root(a);
	LOG("root %s", a->root);
	if (offscreen ? display_open_offscreen(&a->disp, SCREEN_W, SCREEN_H) : display_open(&a->disp)) {
		LOG("no display");
		return -1;
	}
	if (gfx_init(a->disp.w, a->disp.h)) {
		LOG("GLES setup failed");
		display_close(&a->disp);
		return -1;
	}
	if (!offscreen) {
		if (input_open(&a->in))
			LOG("no input devices");
		a->audio_ok = audio_open() == 0;
	}

	char p[PATH_MAX + 64];
	path_in(a, p, sizeof p, "assets/background.vimg");
	int bw, bh;
	uint8_t *bg = vimg_load(p, &bw, &bh);
	if (bg) {
		tex_create(&a->bg, bw, bh, TEX_RGB888);
		tex_upload(&a->bg, bg, bw, bh);
		free(bg);
	} else {
		LOG("no background at %s", p);
	}

	path_in(a, p, sizeof p, "games");
	if (shelf_scan(&a->shelf, p) < 0)
		LOG("cannot read %s", p);
	LOG("%d games", a->shelf.count);
	return 0;
}

static void app_shutdown(struct app *a)
{
	stop_game(a);
	audio_close();
	tex_destroy(&a->frame_tex);
	tex_destroy(&a->bg);
	gfx_shutdown();
	input_close(&a->in);
	display_close(&a->disp);
	shelf_free(&a->shelf);
}

/* vesper --screenshot OUT.ppm [shelf|power|game [FRAMES]]
 * Draws one screen into a pbuffer and saves it, with no panel. "game"
 * runs the first game on the shelf for FRAMES frames first. */
static int screenshot_main(int argc, char **argv)
{
	static struct app app;
	struct app *a = &app;
	if (argc < 3) {
		fprintf(stderr, "usage: %s --screenshot OUT.ppm [shelf|power|game [FRAMES]]\n", argv[0]);
		return 2;
	}
	const char *what = argc > 3 ? argv[3] : "shelf";
	if (app_init(a, 1))
		return 1;
	if (strcmp(what, "game") == 0 && a->shelf.count > 0) {
		start_game(a, &a->shelf.games[0]);
		int frames = argc > 4 ? atoi(argv[4]) : 300;
		for (int i = 0; i < frames && a->mode == MODE_GAME; i++) {
			/* START now and then, past the title screen. */
			a->in.held = (i / 30) % 4 == 1 ? BIT(B_START) : 0;
			game_frame(a);
		}
	} else if (strcmp(what, "power") == 0) {
		a->mode = MODE_POWER;
		power_frame(a);
	} else {
		shelf_frame(a);
	}
	draw_toast(a);
	display_present(&a->disp);

	unsigned char *rgb = malloc((size_t)a->disp.w * a->disp.h * 3);
	int rc = 1;
	FILE *f = fopen(argv[2], "wb");
	if (rgb && f && display_read_rgb(&a->disp, rgb) == 0) {
		fprintf(f, "P6\n%d %d\n255\n", a->disp.w, a->disp.h);
		fwrite(rgb, 1, (size_t)a->disp.w * a->disp.h * 3, f);
		rc = 0;
	}
	if (f)
		fclose(f);
	free(rgb);
	app_shutdown(a);
	return rc;
}

int main(int argc, char **argv)
{
	setvbuf(stderr, NULL, _IOLBF, 0);
	if (argc > 1 && strcmp(argv[1], "--headless") == 0)
		return headless_main(argc, argv);
	if (argc > 1 && strcmp(argv[1], "--screenshot") == 0)
		return screenshot_main(argc, argv);
	static struct app app;
	struct app *a = &app;
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGPIPE, SIG_IGN);
	if (app_init(a, 0))
		return 1;

	uint64_t stat_start = now_ms(), last = stat_start;
	int frames = 0, late = 0;
	while (!stop_requested) {
		input_poll(&a->in);
		handle_volume(a);
		if (a->mode == MODE_GAME && (a->in.pressed & BIT(B_POWER))) {
			stop_game(a);
			a->before_power = MODE_SHELF;
			a->power_sel = 0;
			a->mode = MODE_POWER;
		}
		switch (a->mode) {
		case MODE_GAME:
			game_frame(a);
			break;
		case MODE_POWER:
			power_frame(a);
			break;
		case MODE_SHELF:
		default:
			shelf_frame(a);
			break;
		}
		draw_toast(a);
		if (display_present(&a->disp))
			break;

		/* Pacing statistics, to compare with slot's 60 fps. */
		uint64_t t = now_ms();
		frames++;
		if (t - last > 25)
			late++;
		last = t;
		if (t - stat_start >= 10000) {
			LOG("pace: %d frames in %.2f s, %d late", frames, (double)(t - stat_start) / 1000.0, late);
			frames = late = 0;
			stat_start = t;
		}
	}

	app_shutdown(a);
	return 0;
}

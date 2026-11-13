/* Host tests for the parts of Vesper that need no device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "font.h"
#include "image.h"
#include "layout.h"
#include "resample.h"
#include "shelf.h"

static int failures, checks;

#define CHECK(cond)                                                           \
	do {                                                                  \
		checks++;                                                     \
		if (!(cond)) {                                                \
			failures++;                                           \
			fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
		}                                                             \
	} while (0)

#define CHECK_STR(a, b)                                                                     \
	do {                                                                                \
		checks++;                                                                   \
		if (strcmp((a), (b)) != 0) {                                                \
			failures++;                                                         \
			fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
		}                                                                           \
	} while (0)

static int rect_eq(struct rect r, int x, int y, int w, int h)
{
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void test_names(void)
{
	char n[128];
	game_display_name("/x/Pokemon - Emerald Version (USA, Europe).gba", n, sizeof n);
	CHECK_STR(n, "Pokemon - Emerald Version");
	game_display_name("Tetris_DX [!].gbc", n, sizeof n);
	CHECK_STR(n, "Tetris DX");
	game_display_name("(Beta).gb", n, sizeof n);
	CHECK_STR(n, "(Beta)");
	game_display_name("noext", n, sizeof n);
	CHECK_STR(n, "noext");
	game_display_name("A Very Long Name.gba", n, 7);
	CHECK_STR(n, "A Very");

	CHECK(system_from_path("a.GBA") == SYS_GBA);
	CHECK(system_from_path("a.gbc") == SYS_GBC);
	CHECK(system_from_path("a.gb") == SYS_GB);
	CHECK(system_from_path("a.nds") == SYS_NDS);
	CHECK(system_from_path("a.zip") == -1);
	CHECK(system_from_path("gba") == -1);
}

static void touch(const char *path)
{
	FILE *f = fopen(path, "w");
	if (f)
		fclose(f);
}

static void test_shelf(void)
{
	char dir[] = "/tmp/vesper-test-XXXXXX";
	if (!mkdtemp(dir)) {
		CHECK(!"mkdtemp");
		return;
	}
	char p[256];
	snprintf(p, sizeof p, "%s/gba", dir);
	mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/gba/b (USA).gba", dir), touch(p);
	snprintf(p, sizeof p, "%s/gba/._b (USA).gba", dir), touch(p); /* macOS litter */
	snprintf(p, sizeof p, "%s/A game.gbc", dir), touch(p);
	snprintf(p, sizeof p, "%s/notes.txt", dir), touch(p);
	snprintf(p, sizeof p, "%s/c.nds", dir), touch(p);

	struct shelf s = {0};
	CHECK(shelf_scan(&s, dir) == 3);
	if (s.count == 3) {
		CHECK_STR(s.games[0].name, "A game");
		CHECK(s.games[0].sys == SYS_GBC);
		CHECK_STR(s.games[1].name, "b");
		CHECK(s.games[1].sys == SYS_GBA);
		CHECK_STR(s.games[2].name, "c");
	}
	/* Another folder from a list file, with a comment, CRLF, a bad path. */
	char other[] = "/tmp/vesper-test2-XXXXXX";
	if (mkdtemp(other)) {
		snprintf(p, sizeof p, "%s/GBA", other), mkdir(p, 0755);
		snprintf(p, sizeof p, "%s/GBA/Zeta.gba", other), touch(p);
		char list[300];
		snprintf(list, sizeof list, "%s/dirs.txt", dir);
		FILE *f = fopen(list, "w");
		if (f) {
			fprintf(f, "# slot's games\r\n  %s\r\n/nonexistent/x\n\n", other);
			fclose(f);
		}
		CHECK(shelf_add_dirs_from(&s, list) == 1);
		CHECK(s.count == 4 && strcmp(s.games[3].name, "Zeta") == 0);
		CHECK(shelf_add_dirs_from(&s, "/nonexistent/list") == 0);
		char cmd2[300];
		snprintf(cmd2, sizeof cmd2, "rm -rf '%s'", other);
		CHECK(system(cmd2) == 0);
	}
	shelf_free(&s);
	CHECK(shelf_scan(&s, "/nonexistent/vesper") == -1);

	char cmd[300];
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
	CHECK(system(cmd) == 0);
}

static void test_ring_and_resampler(void)
{
	struct ring r;
	CHECK(ring_init(&r, 8) == 0);
	int16_t in[20], out[20];
	for (int i = 0; i < 20; i++)
		in[i] = (int16_t)i;
	CHECK(ring_write(&r, in, 6) == 6);
	CHECK(ring_fill(&r) == 6);
	CHECK(ring_read(&r, out, 4) == 4);
	CHECK(out[0] == 0 && out[7] == 7);
	CHECK(ring_write(&r, in, 10) == 6); /* wraps, then full */
	CHECK(ring_fill(&r) == 8);
	CHECK(ring_read(&r, out, 10) == 8);
	CHECK(out[0] == 8 && out[3] == 11 && out[4] == 0 && out[15] == 11);
	ring_write(&r, in, 3);
	ring_clear(&r);
	CHECK(ring_fill(&r) == 0);
	ring_free(&r);

	/* Step 1: the input, one frame late. */
	struct resampler rs = {0};
	int16_t a[8] = {100, -100, 200, -200, 300, -300, 400, -400}, o[64];
	CHECK(resample_linear(&rs, a, 4, 1.0, o, 32) == 4);
	CHECK(o[0] == 0 && o[2] == 100 && o[4] == 200 && o[6] == 300 && o[7] == -300);
	/* Step 0.5 doubles the frames and interpolates. */
	rs = (struct resampler){0};
	CHECK(resample_linear(&rs, a, 4, 0.5, o, 32) == 8);
	CHECK(o[2] == 50 && o[4] == 100 && o[6] == 150);
	/* Blocks join without a gap: 1000 frames in 7 blocks, step 0.75. */
	rs = (struct resampler){0};
	int16_t big[2000] = {0};
	size_t total = 0;
	for (int b = 0; b < 1000; b += 143)
		total += resample_linear(&rs, big, b + 143 <= 1000 ? 143 : 1000 - b, 0.75, o, 32);
	(void)total; /* o is small: this checks it never overflows */
	rs = (struct resampler){0};
	int16_t *huge = calloc(4000, sizeof *huge);
	total = 0;
	for (int b = 0; b < 1000; b += 100)
		total += resample_linear(&rs, big, 100, 0.75, huge, 2000);
	CHECK(total >= 1332 && total <= 1335);
	free(huge);

	CHECK(drc_factor(100, 100, 0.005) == 1.0);
	CHECK(drc_factor(200, 100, 0.005) > 1.0);
	CHECK(drc_factor(0, 100, 0.005) < 1.0);
	CHECK(drc_factor(1000, 100, 0.005) <= 1.005 + 1e-9);
}

static void test_layout(void)
{
	CHECK(rect_eq(fit_rect(160, 144, 640, 480, SCALE_INTEGER), 80, 24, 480, 432));
	CHECK(rect_eq(fit_rect(240, 160, 640, 480, SCALE_INTEGER), 80, 80, 480, 320));
	CHECK(rect_eq(fit_rect(240, 160, 640, 480, SCALE_FIT), 0, 27, 640, 426));
	CHECK(rect_eq(fit_rect(256, 192, 640, 480, SCALE_FIT), 0, 0, 640, 480));
	CHECK(rect_eq(fit_rect(1280, 960, 640, 480, SCALE_INTEGER), 0, 0, 640, 480));

	for (int l = 0; l < DS_LAYOUT_COUNT; l++)
		for (int s = 0; s < 2; s++) {
			struct ds_placement p = ds_place((enum ds_layout)l, s);
			const struct ds_screen *sc[2] = {&p.top, &p.bottom};
			for (int k = 0; k < 2; k++) {
				if (!sc[k]->visible)
					continue;
				CHECK(sc[k]->r.x >= 0 && sc[k]->r.y >= 0);
				CHECK(sc[k]->r.x + sc[k]->r.w <= SCREEN_W && sc[k]->r.y + sc[k]->r.h <= SCREEN_H);
				/* every layout keeps the DS's 4:3 */
				CHECK(sc[k]->r.w * 3 == sc[k]->r.h * 4);
			}
			CHECK(p.top.visible || p.bottom.visible);
		}

	struct ds_placement p = ds_place(DS_SINGLE, 0);
	CHECK(rect_eq(p.top.r, 0, 0, 640, 480) && !p.bottom.visible);
	p = ds_place(DS_SINGLE, 1);
	CHECK(rect_eq(p.bottom.r, 0, 0, 640, 480) && !p.top.visible);
	p = ds_place(DS_STACKED, 0);
	CHECK(rect_eq(p.top.r, 160, 0, 320, 240) && rect_eq(p.bottom.r, 160, 240, 320, 240));

	int tx, ty;
	CHECK(ds_touch_point(&p, 160, 240, &tx, &ty) && tx == 0 && ty == 0);
	CHECK(ds_touch_point(&p, 479, 479, &tx, &ty) && tx == 255 && ty == 191);
	CHECK(!ds_touch_point(&p, 200, 100, &tx, &ty)); /* on the top screen */
	p = ds_place(DS_SINGLE, 1);
	CHECK(ds_touch_point(&p, 320, 240, &tx, &ty) && tx == 128 && ty == 96);
	p = ds_place(DS_SINGLE, 0);
	CHECK(!ds_touch_point(&p, 320, 240, &tx, &ty));
}

static void test_image(void)
{
	uint8_t buf[8 + 2 * 1 * 3] = {'V', 'I', 'M', 'G', 2, 0, 1, 0, 1, 2, 3, 4, 5, 6};
	int w, h;
	uint8_t *px = vimg_parse(buf, sizeof buf, &w, &h);
	CHECK(px && w == 2 && h == 1 && px[5] == 6);
	free(px);
	CHECK(!vimg_parse(buf, sizeof buf - 1, &w, &h));
	buf[0] = 'X';
	CHECK(!vimg_parse(buf, sizeof buf, &w, &h));
}

static void test_font(void)
{
	const char *s = "aè€\xff";
	CHECK(utf8_next(&s) == 'a');
	CHECK(utf8_next(&s) == 0xE8);
	CHECK(utf8_next(&s) == 0x20AC);
	CHECK(utf8_next(&s) == 0xFFFD);
	CHECK(*s == '\0');
	const char *cut = "\xC3";
	CHECK(utf8_next(&cut) == 0xFFFD && *cut == '\0');

	CHECK(font_glyph(FONT_BODY, 0xE8)->adv > 0); /* è is in the atlas */
	CHECK(font_glyph(FONT_BODY, 0x20AC) == font_glyph(FONT_BODY, '?'));
	CHECK(font_text_width(FONT_BODY, "Vesper") > 40);
	CHECK(font_text_width(FONT_TITLE, "Vesper") > font_text_width(FONT_BODY, "Vesper"));

	char out[64];
	font_ellipsize(FONT_BODY, "short", 500, out, sizeof out);
	CHECK_STR(out, "short");
	font_ellipsize(FONT_BODY, "a rather long game name indeed", 80, out, sizeof out);
	CHECK(font_text_width(FONT_BODY, out) <= 80);
	CHECK(strstr(out, "\xE2\x80\xA6") != NULL);
	font_ellipsize(FONT_BODY, "a rather long game name indeed", 80, out, 6);
	CHECK(strlen(out) < 6);
}

int main(void)
{
	test_names();
	test_shelf();
	test_ring_and_resampler();
	test_layout();
	test_image();
	test_font();
	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}

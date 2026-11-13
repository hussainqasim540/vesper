#include "shelf.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const struct {
	const char *ext;
	enum system sys;
} exts[] = {
	{".gb", SYS_GB}, {".gbc", SYS_GBC}, {".gba", SYS_GBA}, {".nds", SYS_NDS},
};

int system_from_path(const char *path)
{
	const char *dot = strrchr(path, '.');
	if (!dot)
		return -1;
	for (size_t i = 0; i < sizeof exts / sizeof exts[0]; i++)
		if (strcasecmp(dot, exts[i].ext) == 0)
			return exts[i].sys;
	return -1;
}

const char *system_label(enum system sys)
{
	static const char *labels[SYS_COUNT] = {"GB", "GBC", "GBA", "DS"};
	return (unsigned)sys < SYS_COUNT ? labels[sys] : "?";
}

void game_display_name(const char *filename, char *out, size_t n)
{
	if (n == 0)
		return;
	const char *base = strrchr(filename, '/');
	base = base ? base + 1 : filename;
	const char *end = strrchr(base, '.');
	if (!end || end == base)
		end = base + strlen(base);

	size_t o = 0;
	int depth = 0;
	for (const char *p = base; p < end && o + 1 < n; p++) {
		if (*p == '(' || *p == '[') {
			depth++;
			continue;
		}
		if ((*p == ')' || *p == ']') && depth > 0) {
			depth--;
			continue;
		}
		if (depth > 0)
			continue;
		char c = *p == '_' ? ' ' : *p;
		if (c == ' ' && (o == 0 || out[o - 1] == ' '))
			continue; /* no leading or double spaces */
		out[o++] = c;
	}
	while (o > 0 && out[o - 1] == ' ')
		o--;
	out[o] = '\0';
	if (o == 0) { /* a name that was all tags: keep the file name */
		size_t len = (size_t)(end - base);
		if (len >= n)
			len = n - 1;
		memcpy(out, base, len);
		out[len] = '\0';
	}
}

static int add(struct shelf *s, const char *path, int sys)
{
	if (s->count == s->cap) {
		int cap = s->cap ? s->cap * 2 : 64;
		struct game *g = realloc(s->games, (size_t)cap * sizeof *g);
		if (!g)
			return -1;
		s->games = g;
		s->cap = cap;
	}
	struct game *g = &s->games[s->count];
	if (snprintf(g->path, sizeof g->path, "%s", path) >= (int)sizeof g->path)
		return 0; /* path too long: skip it */
	game_display_name(path, g->name, sizeof g->name);
	g->sys = (enum system)sys;
	s->count++;
	return 0;
}

static void scan_dir(struct shelf *s, const char *dir, int depth)
{
	DIR *d = opendir(dir);
	if (!d)
		return;
	struct dirent *e;
	char path[1024];
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue; /* hidden files, and macOS's ._ files on FAT */
		if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path)
			continue;
		struct stat st;
		if (stat(path, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth > 0)
				scan_dir(s, path, depth - 1);
		} else if (S_ISREG(st.st_mode)) {
			int sys = system_from_path(e->d_name);
			if (sys >= 0)
				add(s, path, sys);
		}
	}
	closedir(d);
}

static int cmp_games(const void *a, const void *b)
{
	const struct game *x = a, *y = b;
	int c = strcasecmp(x->name, y->name);
	if (c)
		return c;
	if (x->sys != y->sys)
		return (int)x->sys - (int)y->sys;
	return strcmp(x->path, y->path);
}

int shelf_add_dir(struct shelf *s, const char *dir)
{
	DIR *d = opendir(dir);
	if (!d)
		return -1;
	closedir(d);
	int before = s->count;
	scan_dir(s, dir, 2); /* dir/GBA/game.gba, dir/gba/hacks/game.gba */
	if (s->count > 1)
		qsort(s->games, (size_t)s->count, sizeof *s->games, cmp_games);
	return s->count - before;
}

int shelf_scan(struct shelf *s, const char *dir)
{
	s->count = 0;
	return shelf_add_dir(s, dir) < 0 ? -1 : s->count;
}

int shelf_add_dirs_from(struct shelf *s, const char *list_path)
{
	FILE *f = fopen(list_path, "r");
	if (!f)
		return 0;
	char line[1024];
	int added = 0;
	while (fgets(line, sizeof line, f)) {
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		size_t len = strlen(p);
		while (len && (p[len - 1] == '\n' || p[len - 1] == '\r' || p[len - 1] == ' '))
			p[--len] = '\0';
		if (len == 0 || *p == '#')
			continue;
		int n = shelf_add_dir(s, p);
		if (n > 0)
			added += n;
	}
	fclose(f);
	return added;
}

void shelf_free(struct shelf *s)
{
	free(s->games);
	s->games = NULL;
	s->count = s->cap = 0;
}

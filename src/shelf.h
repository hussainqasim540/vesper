/* The shelf: the games found on the card. */
#pragma once
#include <stddef.h>

enum system { SYS_GB, SYS_GBC, SYS_GBA, SYS_NDS, SYS_COUNT };

struct game {
	char path[512];
	char name[128]; /* shown on the shelf */
	enum system sys;
};

struct shelf {
	struct game *games;
	int count, cap;
};

/* Fills the shelf from dir and its subfolders (two levels), by extension,
 * sorted by name. Returns the number of games, or -1 if dir can't be read. */
int shelf_scan(struct shelf *s, const char *dir);
/* Adds the games of another folder (and its subfolders), then sorts the
 * whole shelf again. Returns how many it added, or -1. */
int shelf_add_dir(struct shelf *s, const char *dir);
/* Adds every folder listed in a text file, one path a line ('#' starts a
 * comment). Returns how many games it added. */
int shelf_add_dirs_from(struct shelf *s, const char *list_path);
void shelf_free(struct shelf *s);

/* The system of a file name, from its extension; -1 when it is no game. */
int system_from_path(const char *path);
const char *system_label(enum system sys);

/* "Pokemon - Emerald Version (USA, Europe).gba" -> "Pokemon - Emerald Version".
 * Drops the extension and the bracketed tags, turns '_' into spaces. */
void game_display_name(const char *filename, char *out, size_t n);

#ifndef FASTNOTE_RECENTS_H
#define FASTNOTE_RECENTS_H

#include <stdbool.h>

// Recently opened files and folders, persisted to a small text file
// ($XDG_CONFIG_HOME/fastnote/recent, one "F\\t<path>" / "D\\t<path>" per
// line). All functions are best-effort and SDL-free: I/O failures simply
// leave the in-memory lists untouched. Pushes never touch disk; call
// recents_save explicitly.
#define RECENT_MAX 10
#define RECENT_PATH_LEN 1024

typedef struct {
    char files[RECENT_MAX][RECENT_PATH_LEN];
    int nfiles;
    char dirs[RECENT_MAX][RECENT_PATH_LEN];
    int ndirs;
} Recents;

void recents_init(Recents *r); // load from disk; missing file -> empty
void recents_save(const Recents *r);
// Most-recent-first with dedupe; overlong paths ignored.
void recents_push_file(Recents *r, const char *path);
void recents_push_dir(Recents *r, const char *path);
// Drop entries that no longer exist.
void recents_prune(Recents *r);

#endif

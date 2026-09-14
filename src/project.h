#ifndef FASTNOTE_PROJECT_H
#define FASTNOTE_PROJECT_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

#include "font.h"
#include "theme.h"
#include "ui.h" // UiMetrics

// Minimal project tree: a flat visible listing built depth-first from the
// project root (directories first, sorted, dotfiles and symlinks skipped).
// Directories start expanded; collapsing a directory hides its children.
// No watching: rescanned on open-folder, window focus and successful save.
typedef struct {
    char *rel;        // path relative to root, e.g. "src/main.c" (owned)
    const char *name; // pointer into rel, after the last '/'
    bool is_dir;
    bool expanded; // dirs only; files always false
    int depth;
} ProjEntry;

typedef struct {
    char root[1024];
    bool has;
    bool visible;
    ProjEntry *ents; // visible rows only
    size_t n;
    size_t cap;
    char **collapsed; // rel paths of collapsed dirs (persist across rescans)
    size_t ncollapsed;
    size_t scroll; // first visible row
    bool truncated; // entry cap hit during scan
} Project;

void project_init(Project *p);
void project_quit(Project *p);
// Set root (must be a readable directory) and scan. Keeps old state on failure.
bool project_open(Project *p, const char *root, char *err, size_t errcap);
bool project_rescan(Project *p); // errors swallowed (tree simply stays stale)
void project_set_visible(Project *p, bool visible);

int project_row_h(Font *f, const UiMetrics *m);
// Rows fitting the sidebar area [top, bottom) below the header row.
int project_visible_rows(Font *f, const UiMetrics *m, int top, int bottom);
// Draw sidebar in [0, sidebar_w) x [top, bottom). cur_path is the absolute
// path of the open file (or NULL) for the current-file highlight.
void project_draw(SDL_Renderer *ren, Font *f, const Theme *th,
                  const UiMetrics *m, Project *p, int top, int bottom,
                  const char *cur_path);
// Click in framebuffer coordinates.
typedef enum { PCK_NONE, PCK_FILE, PCK_TOGGLED } ProjClick;
ProjClick project_click(Project *p, Font *f, const UiMetrics *m, float x,
                        float y, int top, int bottom, const char **rel_out);
// Scroll by rows; visible_rows = rows fitting the sidebar area.
void project_scroll(Project *p, long delta, int visible_rows);
void project_clamp(Project *p, int visible_rows);

#endif

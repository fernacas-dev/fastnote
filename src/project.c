#define _POSIX_C_SOURCE 200809L // lstat, needed with strict ISO C

#include "project.h"
#include "text_buffer.h" // utf8_decode (for ui_draw_text via ui.h)

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROJ_MAX_ENTRIES 20000
#define PROJ_MAX_DEPTH 64
#define PROJ_MAX_COLLAPSED 1024

void project_init(Project *p) {
    memset(p, 0, sizeof(*p));
}

static void entries_free(Project *p) {
    for (size_t i = 0; i < p->n; i++)
        free(p->ents[i].rel);
    free(p->ents);
    p->ents = NULL;
    p->n = p->cap = 0;
}

static void collapsed_free(Project *p) {
    for (size_t i = 0; i < p->ncollapsed; i++)
        free(p->collapsed[i]);
    free(p->collapsed);
    p->collapsed = NULL;
    p->ncollapsed = 0;
}

void project_quit(Project *p) {
    entries_free(p);
    collapsed_free(p);
    memset(p, 0, sizeof(*p));
}

void project_set_visible(Project *p, bool visible) {
    p->visible = visible;
}

static bool is_collapsed(const Project *p, const char *rel) {
    for (size_t i = 0; i < p->ncollapsed; i++) {
        if (strcmp(p->collapsed[i], rel) == 0)
            return true;
    }
    return false;
}

// collapsed=true hides children; false (or missing entry) shows them.
static void set_collapsed(Project *p, const char *rel, bool collapsed) {
    for (size_t i = 0; i < p->ncollapsed; i++) {
        if (strcmp(p->collapsed[i], rel) == 0) {
            if (!collapsed) {
                free(p->collapsed[i]);
                memmove(p->collapsed + i, p->collapsed + i + 1,
                        (p->ncollapsed - i - 1) * sizeof(char *));
                p->ncollapsed--;
            }
            return;
        }
    }
    if (!collapsed || p->ncollapsed >= PROJ_MAX_COLLAPSED)
        return;
    char *cpy = malloc(strlen(rel) + 1);
    if (!cpy)
        return;
    strcpy(cpy, rel);
    char **nc = realloc(p->collapsed, (p->ncollapsed + 1) * sizeof(char *));
    if (!nc) {
        free(cpy);
        return;
    }
    p->collapsed = nc;
    p->collapsed[p->ncollapsed++] = cpy;
}

static bool entries_add(Project *p, const char *rel, bool is_dir,
                        bool expanded, int depth) {
    if (p->n >= PROJ_MAX_ENTRIES) {
        p->truncated = true;
        return true; // not fatal: show what we have
    }
    if (p->n == p->cap) {
        size_t ncap = p->cap ? p->cap * 2 : 256;
        ProjEntry *ne = realloc(p->ents, ncap * sizeof(ProjEntry));
        if (!ne)
            return false;
        p->ents = ne;
        p->cap = ncap;
    }
    char *cpy = malloc(strlen(rel) + 1);
    if (!cpy)
        return false;
    strcpy(cpy, rel);
    const char *slash = strrchr(cpy, '/');
    p->ents[p->n].rel = cpy;
    p->ents[p->n].name = slash ? slash + 1 : cpy;
    p->ents[p->n].is_dir = is_dir;
    p->ents[p->n].expanded = expanded;
    p->ents[p->n].depth = depth;
    p->n++;
    return true;
}

typedef struct {
    char *name;
    bool is_dir;
} Child;

static int child_cmp(const void *a, const void *b) {
    const Child *x = a, *y = b;
    if (x->is_dir != y->is_dir)
        return x->is_dir ? -1 : 1; // directories first
    return strcmp(x->name, y->name);
}

// Recursive depth-first scan building the VISIBLE list: collapsed
// directories are listed but not descended into. rel is "" at the root
// (always < 2048 chars, enforced when built below); full fits in 4096.
static bool scan_dir(Project *p, const char *rel, int depth) {
    if (depth > PROJ_MAX_DEPTH)
        return true;
    char full[4096];
    if (rel[0])
        snprintf(full, sizeof(full), "%s/%s", p->root, rel);
    else
        snprintf(full, sizeof(full), "%s", p->root);
    DIR *d = opendir(full);
    if (!d)
        return true; // unreadable subdir: skip, keep the rest
    Child *kids = NULL;
    size_t nk = 0, ck = 0;
    bool ok = true;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.')
            continue; // skip ".", ".." and hidden files
        if (nk == ck) {
            size_t nc = ck ? ck * 2 : 64;
            Child *nkids = realloc(kids, nc * sizeof(Child));
            if (!nkids) {
                ok = false;
                break;
            }
            kids = nkids;
            ck = nc;
        }
        char *nm = malloc(strlen(de->d_name) + 1);
        if (!nm) {
            ok = false;
            break;
        }
        strcpy(nm, de->d_name);
        // lstat: symlinks are skipped entirely (avoids cycles).
        // (Manual join: provably bounded, no -Wformat-truncation noise.)
        bool is_dir = false;
        {
            char cfull[4096];
            size_t fl = strlen(full), dl = strlen(de->d_name);
            if (fl + 1 + dl < sizeof(cfull)) {
                memcpy(cfull, full, fl);
                cfull[fl] = '/';
                memcpy(cfull + fl + 1, de->d_name, dl + 1);
                struct stat st;
                is_dir = lstat(cfull, &st) == 0 && S_ISDIR(st.st_mode);
            }
        }
        kids[nk].name = nm;
        kids[nk].is_dir = is_dir;
        nk++;
    }
    closedir(d);
    if (ok && nk > 0) {
        qsort(kids, nk, sizeof(Child), child_cmp);
        for (size_t i = 0; i < nk && ok; i++) {
            if (strlen(rel) + 1 + strlen(kids[i].name) >= 2048)
                continue; // absurdly deep path: skip, keep the rest
            char crel[2048];
            if (rel[0])
                snprintf(crel, sizeof(crel), "%s/%s", rel, kids[i].name);
            else
                snprintf(crel, sizeof(crel), "%s", kids[i].name);
            bool collapsed = kids[i].is_dir && is_collapsed(p, crel);
            if (!entries_add(p, crel, kids[i].is_dir, !collapsed, depth))
                ok = false;
            else if (kids[i].is_dir && !collapsed)
                ok = scan_dir(p, crel, depth + 1);
            if (p->truncated)
                break;
        }
    }
    for (size_t i = 0; i < nk; i++)
        free(kids[i].name);
    free(kids);
    return ok;
}

// Rebuild the visible list from disk, honoring the collapsed set.
static bool build_visible(Project *p) {
    entries_free(p);
    p->truncated = false;
    return scan_dir(p, "", 0);
}

static bool is_readable_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
        return false;
    DIR *d = opendir(path);
    if (!d)
        return false;
    closedir(d);
    return true;
}

bool project_open(Project *p, const char *root, char *err, size_t errcap) {
    if (!root || !root[0] || !is_readable_dir(root)) {
        if (err)
            snprintf(err, errcap, "Not a readable directory");
        return false;
    }
    char clean[1024];
    snprintf(clean, sizeof(clean), "%s", root);
    // Drop trailing slashes (keep "/" intact).
    size_t L = strlen(clean);
    while (L > 1 && clean[L - 1] == '/')
        clean[--L] = '\0';

    // Scan into a scratch project so failure keeps the old state.
    Project tmp;
    memset(&tmp, 0, sizeof(tmp));
    snprintf(tmp.root, sizeof(tmp.root), "%s", clean);
    if (!build_visible(&tmp)) {
        entries_free(&tmp);
        if (err)
            snprintf(err, errcap, "Out of memory scanning folder");
        return false;
    }
    entries_free(p);
    collapsed_free(p);
    *p = tmp; // fresh folder: everything expanded
    p->has = true;
    p->visible = true;
    p->scroll = 0;
    return true;
}

bool project_rescan(Project *p) {
    if (!p->has)
        return false;
    if (!build_visible(p))
        return false; // keep stale tree on OOM
    return true;
}

int project_row_h(Font *f, const UiMetrics *m) {
    return f->line_h + (int)(4.0f * m->scale + 0.5f);
}

int project_visible_rows(Font *f, const UiMetrics *m, int top, int bottom) {
    int rh = project_row_h(f, m);
    int rows = (bottom - top - rh) / rh;
    return rows < 0 ? 0 : rows;
}

void project_clamp(Project *p, int visible_rows) {
    size_t max = 0;
    if ((long)p->n > (long)visible_rows)
        max = p->n - (size_t)visible_rows;
    if (p->scroll > max)
        p->scroll = max;
}

void project_scroll(Project *p, long delta, int visible_rows) {
    long s = (long)p->scroll + delta;
    if (s < 0)
        s = 0;
    p->scroll = (size_t)s;
    project_clamp(p, visible_rows);
}

static const char *root_basename(const Project *p) {
    const char *s = strrchr(p->root, '/');
    const char *b = s ? s + 1 : p->root;
    return b[0] ? b : p->root;
}

// Small triangle before directory names: right (collapsed) or down
// (expanded). Drawn with lines so it never depends on font coverage.
static void draw_triangle(SDL_Renderer *ren, float x, float mid_y, float size,
                          bool expanded) {
    if (expanded) {
        float x0 = x, x1 = x + size, ym = mid_y + size / 2.0f;
        SDL_RenderLine(ren, x0, mid_y, x1, mid_y);
        SDL_RenderLine(ren, x0, mid_y, x0 + size / 2.0f, ym);
        SDL_RenderLine(ren, x1, mid_y, x0 + size / 2.0f, ym);
    } else {
        float ym0 = mid_y - size / 2.0f, ym1 = mid_y + size / 2.0f;
        SDL_RenderLine(ren, x, ym0, x, ym1);
        SDL_RenderLine(ren, x, ym0, x + size, mid_y);
        SDL_RenderLine(ren, x, ym1, x + size, mid_y);
    }
}

void project_draw(SDL_Renderer *ren, Font *f, const Theme *th,
                  const UiMetrics *m, Project *p, int top, int bottom,
                  const char *cur_path) {
    if (!p->has || !p->visible)
        return;
    float w = (float)m->sidebar_w;
    int rh = project_row_h(f, m);

    // Panel background + right border.
    SDL_FRect bg = {0, (float)top, w, (float)(bottom - top)};
    SDL_SetRenderDrawColor(ren, th->menu.r, th->menu.g, th->menu.b, 0xFF);
    SDL_RenderFillRect(ren, &bg);
    SDL_SetRenderDrawColor(ren, th->border.r, th->border.g, th->border.b,
                           0xFF);
    SDL_RenderLine(ren, w - 1.0f, (float)top, w - 1.0f, (float)bottom);

    float indent_unit = 14.0f * m->scale;
    float tri = 8.0f * m->scale;
    float gap = 6.0f * m->scale;
    float text_x = (float)m->pad_x;

    // Header: folder name + entry count.
    char head[160];
    snprintf(head, sizeof(head), "%s (%zu)", root_basename(p), p->n);
    float hbase = (float)top + (float)rh / 2.0f + (float)f->asc -
                  (float)f->line_h / 2.0f;
    ui_draw_text(ren, f, head, strlen(head), text_x, hbase, th->foreground);
    float hy = (float)top + (float)rh;
    SDL_SetRenderDrawColor(ren, th->border.r, th->border.g, th->border.b,
                           0xFF);
    SDL_RenderLine(ren, 0, hy, w, hy);

    int rows = project_visible_rows(f, m, top, bottom);
    project_clamp(p, rows);
    for (int i = 0; i < rows; i++) {
        size_t idx = p->scroll + (size_t)i;
        if (idx >= p->n)
            break;
        ProjEntry *e = &p->ents[idx];
        float ry = (float)(top + rh) + (float)i * (float)rh;
        // Current-file highlight.
        if (!e->is_dir && cur_path) {
            char full[2048];
            snprintf(full, sizeof(full), "%s/%s", p->root, e->rel);
            if (strcmp(full, cur_path) == 0) {
                SDL_FRect hl = {0, ry, w, (float)rh};
                SDL_SetRenderDrawColor(ren, th->selection.r, th->selection.g,
                                       th->selection.b, 0xFF);
                SDL_RenderFillRect(ren, &hl);
            }
        }
        float mid_y = ry + (float)rh / 2.0f;
        float lx = text_x + (float)e->depth * indent_unit;
        if (e->is_dir) {
            SDL_SetRenderDrawColor(ren, th->foreground.r, th->foreground.g,
                                   th->foreground.b, 0xFF);
            draw_triangle(ren, lx, mid_y, tri, e->expanded);
            lx += tri + gap;
        } else {
            lx += tri + gap; // align file names with directory names
        }
        char label[1050];
        if (e->is_dir)
            snprintf(label, sizeof(label), "%s/", e->name);
        else
            snprintf(label, sizeof(label), "%s", e->name);
        float lb = mid_y + (float)f->asc - (float)f->line_h / 2.0f;
        ui_draw_text(ren, f, label, strlen(label), lx, lb, th->foreground);
    }
    if (p->n == 0) {
        const char *empty = "(empty)";
        float lb = (float)(top + rh) + (float)rh / 2.0f + (float)f->asc -
                   (float)f->line_h / 2.0f;
        ui_draw_text(ren, f, empty, strlen(empty), text_x, lb, th->foreground);
    }
}

ProjClick project_click(Project *p, Font *f, const UiMetrics *m, float x,
                        float y, int top, int bottom, const char **rel_out) {
    if (rel_out)
        *rel_out = NULL;
    if (!p->has || !p->visible)
        return PCK_NONE;
    if (x < 0 || x >= (float)m->sidebar_w)
        return PCK_NONE;
    int rh = project_row_h(f, m);
    if (y < (float)(top + rh) || y >= (float)bottom)
        return PCK_NONE; // header or outside
    int rows = project_visible_rows(f, m, top, bottom);
    project_clamp(p, rows);
    size_t idx = p->scroll + (size_t)((y - (float)(top + rh)) / (float)rh);
    if (idx >= p->n)
        return PCK_NONE;
    if (p->ents[idx].is_dir) {
        // Toggle collapse and rebuild the visible list in place. Copy rel
        // first: rebuild frees the entry it points into.
        char toggled[2048];
        snprintf(toggled, sizeof(toggled), "%s", p->ents[idx].rel);
        bool now_collapsed = !is_collapsed(p, toggled);
        set_collapsed(p, toggled, now_collapsed);
        if (build_visible(p)) {
            // Keep the toggled directory itself on screen.
            for (size_t i = 0; i < p->n; i++) {
                if (strcmp(p->ents[i].rel, toggled) == 0) {
                    p->scroll = i;
                    break;
                }
            }
        }
        project_clamp(p, rows);
        return PCK_TOGGLED;
    }
    if (rel_out)
        *rel_out = p->ents[idx].rel;
    return PCK_FILE;
}

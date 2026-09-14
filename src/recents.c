#define _POSIX_C_SOURCE 200809L // access(), needed with strict ISO C

#include "recents.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void config_path(char *out, size_t cap) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(out, cap, "%s/fastnote/recent", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (!home || !home[0])
        home = "/tmp";
    snprintf(out, cap, "%s/.config/fastnote/recent", home);
}

void recents_init(Recents *r) {
    memset(r, 0, sizeof(*r));
    char path[4096];
    config_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t L = strlen(line);
        while (L > 0 && (line[L - 1] == '\n' || line[L - 1] == '\r'))
            line[--L] = '\0';
        if (L < 3 || line[1] != '\t')
            continue;
        bool is_file = line[0] == 'F';
        bool is_dir = line[0] == 'D';
        if (!is_file && !is_dir)
            continue;
        const char *p = line + 2;
        if (!p[0] || strlen(p) >= RECENT_PATH_LEN)
            continue;
        if (access(p, F_OK) != 0)
            continue; // prune missing entries on load
        if (is_file) {
            if (r->nfiles < RECENT_MAX)
                snprintf(r->files[r->nfiles++], RECENT_PATH_LEN, "%s", p);
        } else {
            if (r->ndirs < RECENT_MAX)
                snprintf(r->dirs[r->ndirs++], RECENT_PATH_LEN, "%s", p);
        }
    }
    fclose(f);
}

static bool mkdirs(const char *file) {
    // Create parent directories one level at a time (best effort).
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", file);
    for (char *s = tmp + 1; *s; s++) {
        if (*s == '/') {
            *s = '\0';
            mkdir(tmp, 0755); // ignore EEXIST and friends
            *s = '/';
        }
    }
    return true;
}

void recents_save(const Recents *r) {
    char path[4096];
    config_path(path, sizeof(path));
    mkdirs(path);
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (int i = 0; i < r->nfiles; i++)
        fprintf(f, "F\t%s\n", r->files[i]);
    for (int i = 0; i < r->ndirs; i++)
        fprintf(f, "D\t%s\n", r->dirs[i]);
    fclose(f);
}

static void push_slot(char slots[RECENT_MAX][RECENT_PATH_LEN], int *n,
                      const char *path) {
    if (!path || !path[0] || strlen(path) >= RECENT_PATH_LEN)
        return;
    int at = -1;
    for (int i = 0; i < *n; i++) {
        if (strcmp(slots[i], path) == 0) {
            at = i;
            break;
        }
    }
    if (at == 0)
        return; // already most recent: list unchanged
    if (at > 0) {
        // Duplicate: move to front, count unchanged.
        memmove(slots[1], slots[0], (size_t)at * RECENT_PATH_LEN);
        snprintf(slots[0], RECENT_PATH_LEN, "%s", path);
        return;
    }
    // New entry (drop the oldest when full).
    if (*n < RECENT_MAX)
        memmove(slots[1], slots[0], (size_t)(*n) * RECENT_PATH_LEN);
    else
        memmove(slots[1], slots[0], (size_t)(RECENT_MAX - 1) * RECENT_PATH_LEN);
    snprintf(slots[0], RECENT_PATH_LEN, "%s", path);
    if (*n < RECENT_MAX)
        (*n)++;
}

void recents_push_file(Recents *r, const char *path) {
    push_slot(r->files, &r->nfiles, path);
}

void recents_push_dir(Recents *r, const char *path) {
    push_slot(r->dirs, &r->ndirs, path);
}

void recents_prune(Recents *r) {
    for (int i = 0; i < r->nfiles;) {
        if (access(r->files[i], F_OK) != 0) {
            memmove(r->files[i], r->files[i + 1],
                    (size_t)(r->nfiles - i - 1) * RECENT_PATH_LEN);
            r->nfiles--;
        } else {
            i++;
        }
    }
    for (int i = 0; i < r->ndirs;) {
        if (access(r->dirs[i], F_OK) != 0) {
            memmove(r->dirs[i], r->dirs[i + 1],
                    (size_t)(r->ndirs - i - 1) * RECENT_PATH_LEN);
            r->ndirs--;
        } else {
            i++;
        }
    }
}

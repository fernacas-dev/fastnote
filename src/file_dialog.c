#include "file_dialog.h"

#include <stdlib.h>
#include <string.h>

static uint32_t dlg_event = 0;
static bool event_registered = false;

uint32_t filedialog_event_type(void) {
    if (!event_registered) {
        dlg_event = SDL_RegisterEvents(1);
        event_registered = true;
    }
    return dlg_event;
}

// Runs on whatever thread SDL delivers the dialog callback on; only uses
// thread-safe SDL_PushEvent plus local allocations.
static void SDLCALL dialog_callback(void *userdata, const char *const *filelist,
                                    int filter) {
    (void)filter;
    uint32_t type = filedialog_event_type();
    if (type == 0)
        return;
    FileDialogResult *res = malloc(sizeof(*res));
    if (!res)
        return;
    res->is_save = userdata != NULL;
    res->path = NULL;
    if (filelist && filelist[0]) {
        res->path = malloc(strlen(filelist[0]) + 1);
        if (res->path)
            strcpy(res->path, filelist[0]);
    }
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.user.data1 = res;
    if (!SDL_PushEvent(&ev)) {
        free(res->path);
        free(res);
    }
}

static const SDL_DialogFileFilter text_filters[] = {
    {"Text files", "txt;md;log;ini;cfg;c;h;py;js;json;xml;yaml;yml"},
    {"All files", "*"},
};

void filedialog_open(SDL_Window *win) {
    SDL_ShowOpenFileDialog(dialog_callback, NULL /* open */, win,
                           text_filters, 2, NULL, false);
}

void filedialog_save(SDL_Window *win) {
    SDL_ShowSaveFileDialog(dialog_callback, (void *)1 /* save */, win,
                           text_filters, 2, NULL);
}

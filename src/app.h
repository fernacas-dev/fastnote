#ifndef FASTNOTE_APP_H
#define FASTNOTE_APP_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "editor.h"
#include "file_dialog.h"
#include "font.h"
#include "project.h"
#include "recents.h"
#include "theme.h"
#include "ui.h"

// Pending follow-up for the unsaved-changes confirm dialog.
typedef enum {
    AFTER_NONE,
    AFTER_NEW,
    AFTER_OPEN,
    AFTER_EXIT,
    AFTER_CLOSE,
} AfterAction;

typedef struct App {
    SDL_Window *win;
    SDL_Renderer *ren;
    Font font;
    // Open documents: one Editor per tab. tabs[cur] is the active one.
    Editor *tabs;
    size_t ntabs;
    size_t tabs_cap;
    size_t cur;
    bool exit_mode; // quitting: confirming unsaved tabs one by one
    Recents recents;
    Theme theme;
    Project project;
    MenuState menu;
    ModalState modal;
    // HiDPI: layout is done in physical (framebuffer) pixels. base_font_px
    // is the user-facing size (14 default); the rasterized size is
    // base * display_scale. Mouse events arrive in window (logical)
    // coordinates and are converted with mouse_sx/mouse_sy.
    float display_scale;
    int base_font_px;
    UiMetrics m;
    float mouse_sx;
    float mouse_sy;
    bool running;
    bool dirty;      // a re-render is needed
    bool blink_on;   // cursor currently visible phase
    uint64_t last_blink;
    bool dragging;   // mouse drag selection in progress
    bool dialog_open; // native file dialog outstanding
    AfterAction after;
    char pending_path[1024]; // file to open after confirm-save
    uint32_t dlg_event;
    int win_w, win_h; // framebuffer size in physical pixels
    // Reusable scratch for per-line highlight kinds (grows to 64 KB max).
    uint8_t *hl_scratch;
    size_t hl_scratch_cap;
} App;

bool app_init(App *app, const char *open_path, const char *font_path, char *err,
              size_t errcap);
void app_quit(App *app);
int app_run(App *app);
// One event-loop iteration (event wait + blink + conditional render).
// timeout_ms < 0 waits blink-aware (for the real loop); 0 only polls.
// Exposed so tests can drive the application with synthetic events.
void app_step(App *app, int timeout_ms);
void app_mark_dirty(App *app);

#endif

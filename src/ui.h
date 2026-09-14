#ifndef FASTNOTE_UI_H
#define FASTNOTE_UI_H

#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdbool.h>

#include "editor.h"
#include "font.h"
#include "theme.h"

typedef enum {
    ACT_NONE,
    ACT_NEW,
    ACT_OPEN,
    ACT_SAVE,
    ACT_SAVE_AS,
    ACT_EXIT,
    ACT_UNDO,
    ACT_REDO,
    ACT_CUT,
    ACT_COPY,
    ACT_PASTE,
    ACT_SELECT_ALL,
    ACT_FONT_INC,
    ACT_FONT_DEC,
    ACT_FONT_RESET,
    ACT_OPEN_FOLDER,
    ACT_TOGGLE_SIDEBAR,
    ACT_TOGGLE_HL,
    ACT_CLOSE_TAB,
    ACT_TAB_NEXT,
    ACT_TAB_PREV,
    ACT_RECENT_FILE,
    ACT_RECENT_FOLDER,
    ACT_ABOUT,
} MenuAction;

typedef struct {
    const char *label;
    const char *shortcut; // displayed only
    MenuAction action;
} MenuItem;

typedef struct {
    bool open;
    int menu;       // 0=File, 1=Edit, 2=View, -1=none
    int hover_item; // row hovered in the open dropdown, -1=none
} MenuState;

// Scale-aware chrome geometry. All layout is done in physical (framebuffer)
// pixels; metrics scale the 1x base sizes from theme.h by the display scale
// so the UI keeps its proportions on HiDPI displays.
typedef struct {
    float scale;
    int menu_h;
    int tab_h;
    int status_h;
    int pad_x;
    int cursor_w;
    int sidebar_w;
    int scrollbar_w;
} UiMetrics;

UiMetrics ui_metrics_for(float scale);

// True when the document needs a scrollbar (more lines than visible).
static inline bool ui_has_scrollbar(size_t nlines, size_t visible) {
    return nlines > visible;
}
// Track + thumb rectangles (framebuffer pixels) for the editor area.
// Returns false when no scrollbar is needed; geometry matches hit-testing.
bool ui_scrollbar_geom(const UiMetrics *m, size_t nlines, size_t visible,
                       size_t scroll, int area_top, int area_h, int win_w,
                       SDL_FRect *track, SDL_FRect *thumb);

// --- Menu model ---
int ui_menu_count(void);
const char *ui_menu_title(int m);
int ui_menu_items(int m, const MenuItem **out);

// --- Menu interaction (mouse, framebuffer coordinates) ---
// Click routing. Returns the chosen action (ACT_NONE when only the
// open/close state changed). Considers bar + open dropdown.
MenuAction ui_menu_click(MenuState *st, Font *f, const UiMetrics *m, float x,
                         float y, int win_w);
// Hover tracking while a menu is open (switches menus, highlights rows).
void ui_menu_motion(MenuState *st, Font *f, const UiMetrics *m, float x,
                    float y, int win_w);
static inline bool ui_menu_is_open(const MenuState *st) { return st->open; }
static inline void ui_menu_close(MenuState *st) {
    st->open = false;
    st->menu = -1;
    st->hover_item = -1;
}
// True when the point hits the menu bar or the open dropdown.
bool ui_point_in_chrome(MenuState *st, Font *f, const UiMetrics *m, float x,
                        float y, int win_w);

// --- Tab strip (one tab per open document) ---
typedef enum { TABACT_NONE, TABACT_SWITCH, TABACT_CLOSE } TabClick;
// Draw the strip in [x0, win_w) x [y0, y0 + m->tab_h). Tabs share the
// width evenly (shrunk when crowded); labels clip to their tab.
void ui_draw_tabs(SDL_Renderer *ren, Font *f, const Theme *th,
                  const UiMetrics *m, const Editor *tabs, int ntabs, int cur,
                  float x0, float y0, float win_w);
// Hit-test a click (framebuffer coordinates). Returns the action and, for
// SWITCH/CLOSE, the tab index in *idx.
TabClick ui_tab_click(const UiMetrics *m, const Editor *tabs, int ntabs,
                      float x0, float y0, float win_w, float x, float y,
                      int *idx);

// --- Modal dialogs (in-app Save/Discard/Cancel, errors, recent picker) ---
typedef enum {
    MODAL_NONE,
    MODAL_CONFIRM,
    MODAL_ERROR,
    MODAL_PICKER
} ModalKind;
typedef enum { MB_NONE, MB_SAVE, MB_DISCARD, MB_CANCEL, MB_OK } ModalButton;

// Max rows offered by the recent-files/folders picker.
#define PICK_MAX 10

typedef struct {
    ModalKind kind;
    char title[128];
    char msg[512];
    SDL_FRect buttons[3];
    ModalButton btn_id[3];
    const char *btn_label[3];
    int nbtn;
    // Picker state (kind == MODAL_PICKER).
    char pick_items[PICK_MAX][1024];
    SDL_FRect pick_rows[PICK_MAX];
    int npick;
    int pick_sel;
    bool pick_folders;
} ModalState;

void ui_modal_confirm(ModalState *m, const char *what);
void ui_modal_error(ModalState *m, const char *msg);
// About box (an OK dialog with the application info).
void ui_modal_about(ModalState *m);
// Recent picker: title + up to PICK_MAX absolute paths. Selection starts
// at row 0; folders=true opens folders, false opens files.
void ui_modal_picker(ModalState *m, const char *title,
                     char paths[][1024], int n, bool folders);
// Row index under the point, or -1 (uses rects from the last draw).
int ui_modal_pick_click(ModalState *m, float x, float y);
// Move the keyboard selection, clamped.
void ui_modal_pick_move(ModalState *m, int delta);
static inline void ui_modal_close(ModalState *m) { m->kind = MODAL_NONE; }
static inline bool ui_modal_is_open(const ModalState *m) {
    return m->kind != MODAL_NONE;
}
// Mouse click (uses button rects stored by the last ui_draw_modal call).
ModalButton ui_modal_click(ModalState *m, float x, float y);
// Keyboard: Enter -> SAVE/OK, Escape -> CANCEL/OK.
ModalButton ui_modal_key(ModalState *m, SDL_Keycode key);

// --- Drawing ---
float ui_draw_text(SDL_Renderer *ren, Font *f, const char *s, size_t n,
                   float x, float baseline_y, uint8_t color);
float ui_text_width(Font *f, const char *s, size_t n);
// Gutter width for total_lines (digits of the last line number + padding).
int ui_gutter_w(Font *f, const UiMetrics *m, size_t total_lines);
// Menu bar + dropdown + status bar. font_px is the user-facing (unscaled)
// size shown in the menu bar; lang is the status-bar language label.
void ui_draw_chrome(SDL_Renderer *ren, Font *f, const Theme *th,
                    const UiMetrics *m, int win_w, int win_h, MenuState *st,
                    int font_px, size_t line, size_t col, bool modified,
                    const char *lang);
void ui_draw_modal(SDL_Renderer *ren, Font *f, const Theme *th,
                   const UiMetrics *m, ModalState *modal, int win_w,
                   int win_h);

#endif

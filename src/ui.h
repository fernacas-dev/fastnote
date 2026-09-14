#ifndef FASTNOTE_UI_H
#define FASTNOTE_UI_H

#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdbool.h>

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
    int status_h;
    int pad_x;
    int cursor_w;
} UiMetrics;

UiMetrics ui_metrics_for(float scale);

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

// --- Modal dialogs (in-app Save/Discard/Cancel + error popups) ---
typedef enum { MODAL_NONE, MODAL_CONFIRM, MODAL_ERROR } ModalKind;
typedef enum { MB_NONE, MB_SAVE, MB_DISCARD, MB_CANCEL, MB_OK } ModalButton;

typedef struct {
    ModalKind kind;
    char title[128];
    char msg[512];
    SDL_FRect buttons[3];
    ModalButton btn_id[3];
    const char *btn_label[3];
    int nbtn;
} ModalState;

void ui_modal_confirm(ModalState *m, const char *what);
void ui_modal_error(ModalState *m, const char *msg);
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
                   float x, float baseline_y);
float ui_text_width(Font *f, const char *s, size_t n);
// Menu bar + dropdown + status bar. font_px is the user-facing (unscaled)
// size shown in the menu bar.
void ui_draw_chrome(SDL_Renderer *ren, Font *f, const Theme *th,
                    const UiMetrics *m, int win_w, int win_h, MenuState *st,
                    int font_px, size_t line, size_t col, bool modified);
void ui_draw_modal(SDL_Renderer *ren, Font *f, const Theme *th,
                   const UiMetrics *m, ModalState *modal, int win_w,
                   int win_h);

#endif

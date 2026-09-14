#include "ui.h"
#include "text_buffer.h" // utf8_decode

#include <stdio.h>
#include <string.h>

// Scale a 1x base pixel constant by the display scale.
#define SC(m, v) ((float)(v) * (m)->scale)

UiMetrics ui_metrics_for(float scale) {
    if (!(scale > 0.0f))
        scale = 1.0f;
    UiMetrics m;
    m.scale = scale;
    m.menu_h = (int)(FN_MENU_H * scale + 0.5f);
    m.status_h = (int)(FN_STATUS_H * scale + 0.5f);
    m.pad_x = (int)(FN_PAD_X * scale + 0.5f);
    m.cursor_w = (int)(FN_CURSOR_W * scale + 0.5f);
    m.sidebar_w = (int)(220.0f * scale + 0.5f);
    if (m.menu_h < 1)
        m.menu_h = 1;
    if (m.status_h < 1)
        m.status_h = 1;
    if (m.pad_x < 1)
        m.pad_x = 1;
    if (m.cursor_w < 1)
        m.cursor_w = 1;
    if (m.sidebar_w < 1)
        m.sidebar_w = 1;
    return m;
}

static const MenuItem file_items[] = {
    {"New", "Ctrl+N", ACT_NEW},
    {"Open", "Ctrl+O", ACT_OPEN},
    {"Open Folder", "", ACT_OPEN_FOLDER},
    {"Save", "Ctrl+S", ACT_SAVE},
    {"Save As", "Ctrl+Shift+S", ACT_SAVE_AS},
    {"Exit", "Ctrl+Q", ACT_EXIT},
};
static const MenuItem edit_items[] = {
    {"Undo", "Ctrl+Z", ACT_UNDO},
    {"Redo", "Ctrl+Y", ACT_REDO},
    {"Cut", "Ctrl+X", ACT_CUT},
    {"Copy", "Ctrl+C", ACT_COPY},
    {"Paste", "Ctrl+V", ACT_PASTE},
    {"Select All", "Ctrl+A", ACT_SELECT_ALL},
};
static const MenuItem view_items[] = {
    {"Increase Font Size", "Ctrl++", ACT_FONT_INC},
    {"Decrease Font Size", "Ctrl+-", ACT_FONT_DEC},
    {"Reset Font Size", "Ctrl+0", ACT_FONT_RESET},
    {"Toggle Sidebar", "Ctrl+B", ACT_TOGGLE_SIDEBAR},
    {"Toggle Highlight", "", ACT_TOGGLE_HL},
};

int ui_menu_count(void) { return 3; }

const char *ui_menu_title(int m) {
    static const char *titles[] = {"File", "Edit", "View"};
    if (m < 0 || m > 2)
        return "";
    return titles[m];
}

int ui_menu_items(int m, const MenuItem **out) {
    switch (m) {
    case 0:
        *out = file_items;
        return (int)(sizeof(file_items) / sizeof(file_items[0]));
    case 1:
        *out = edit_items;
        return (int)(sizeof(edit_items) / sizeof(edit_items[0]));
    default:
        *out = view_items;
        return (int)(sizeof(view_items) / sizeof(view_items[0]));
    }
}

// --- Text helpers ---

float ui_draw_text(SDL_Renderer *ren, Font *f, const char *s, size_t n,
                   float x, float baseline_y, SDL_Color color) {
    float pen = x;
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        size_t k = utf8_decode(s + i, n - i, &cp);
        if (k == 0)
            break;
        const Glyph *g = font_get(f, cp);
        if (g) {
            font_draw_glyph(ren, f, g, pen, baseline_y, color);
            pen += (float)g->adv;
        }
        i += k;
    }
    return pen - x;
}

float ui_text_width(Font *f, const char *s, size_t n) {
    float w = 0;
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        size_t k = utf8_decode(s + i, n - i, &cp);
        if (k == 0)
            break;
        const Glyph *g = font_get(f, cp);
        if (g)
            w += (float)g->adv;
        i += k;
    }
    return w;
}

int ui_gutter_w(Font *f, const UiMetrics *m, size_t total_lines) {
    // Decimal digits of the last line number (total_lines >= 1 always).
    int digits = 1;
    for (size_t t = total_lines; t >= 10; t /= 10)
        digits++;
    const Glyph *zero = font_get(f, (uint32_t)'0');
    int adv = (zero && zero->adv > 0) ? zero->adv : f->px / 2;
    if (adv < 1)
        adv = 1;
    return digits * adv + (int)(2.0f * (float)m->pad_x + 0.5f);
}

// --- Menu geometry (all in framebuffer pixels) ---

static float title_x(Font *f, const UiMetrics *m, int menu) {
    float x = (float)m->pad_x;
    for (int i = 0; i < menu; i++)
        x += ui_text_width(f, ui_menu_title(i), strlen(ui_menu_title(i))) +
             SC(m, 24);
    return x;
}

static float title_w(Font *f, const UiMetrics *m, int menu) {
    return ui_text_width(f, ui_menu_title(menu), strlen(ui_menu_title(menu))) +
           SC(m, 24);
}

static float item_h(Font *f, const UiMetrics *m) {
    return (float)f->line_h + SC(m, 6);
}

// Dropdown rectangle for the currently open menu.
static bool dropdown_rect(MenuState *st, Font *f, const UiMetrics *m, int win_w,
                          SDL_FRect *out) {
    if (!st->open || st->menu < 0)
        return false;
    const MenuItem *items = NULL;
    int n = ui_menu_items(st->menu, &items);
    float w = 0;
    for (int i = 0; i < n; i++) {
        float iw = ui_text_width(f, items[i].label, strlen(items[i].label)) +
                   ui_text_width(f, items[i].shortcut,
                                 strlen(items[i].shortcut)) +
                   SC(m, 48);
        if (iw > w)
            w = iw;
    }
    float min_w = SC(m, 200);
    if (w < min_w)
        w = min_w;
    float x = title_x(f, m, st->menu);
    if (x + w > (float)win_w - SC(m, 4))
        x = (float)win_w - w - SC(m, 4);
    if (x < 0)
        x = 0;
    out->x = x;
    out->y = (float)m->menu_h;
    out->w = w;
    out->h = item_h(f, m) * (float)n + SC(m, 8);
    return true;
}

// Which top-level title is under x (-1 = none)?
static int title_at(Font *f, const UiMetrics *m, float x) {
    for (int i = 0; i < ui_menu_count(); i++) {
        float tx = title_x(f, m, i), tw = title_w(f, m, i);
        if (x >= tx && x < tx + tw)
            return i;
    }
    return -1;
}

static int item_at(MenuState *st, Font *f, const UiMetrics *m, float x, float y,
                   int win_w) {
    SDL_FRect r;
    if (!dropdown_rect(st, f, m, win_w, &r))
        return -1;
    if (x < r.x || x >= r.x + r.w || y < r.y + SC(m, 4))
        return -1;
    int row = (int)((y - r.y - SC(m, 4)) / item_h(f, m));
    const MenuItem *items = NULL;
    int n = ui_menu_items(st->menu, &items);
    if (row < 0 || row >= n)
        return -1;
    return row;
}

bool ui_point_in_chrome(MenuState *st, Font *f, const UiMetrics *m, float x,
                        float y, int win_w) {
    if (y < (float)m->menu_h)
        return true;
    SDL_FRect r;
    if (dropdown_rect(st, f, m, win_w, &r))
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    return false;
}

MenuAction ui_menu_click(MenuState *st, Font *f, const UiMetrics *m, float x,
                         float y, int win_w) {
    if (y < (float)m->menu_h) {
        int menu = title_at(f, m, x);
        if (menu < 0) {
            ui_menu_close(st);
            return ACT_NONE;
        }
        if (st->open && st->menu == menu) {
            ui_menu_close(st);
        } else {
            st->open = true;
            st->menu = menu;
            st->hover_item = -1;
        }
        return ACT_NONE;
    }
    if (st->open) {
        int row = item_at(st, f, m, x, y, win_w);
        const MenuItem *items = NULL;
        (void)ui_menu_items(st->menu, &items);
        if (row >= 0) {
            MenuAction a = items[row].action;
            ui_menu_close(st);
            return a;
        }
        ui_menu_close(st);
    }
    return ACT_NONE;
}

void ui_menu_motion(MenuState *st, Font *f, const UiMetrics *m, float x,
                    float y, int win_w) {
    if (!st->open)
        return;
    if (y < (float)m->menu_h) {
        int menu = title_at(f, m, x);
        if (menu >= 0) {
            st->menu = menu;
            st->hover_item = -1;
        }
        return;
    }
    st->hover_item = item_at(st, f, m, x, y, win_w);
}

// --- Modal ---

void ui_modal_confirm(ModalState *m, const char *what) {
    m->kind = MODAL_CONFIRM;
    snprintf(m->title, sizeof(m->title), "Unsaved changes");
    snprintf(m->msg, sizeof(m->msg),
             "%s has unsaved changes.\nSave before continuing?", what);
    m->nbtn = 3;
    m->btn_id[0] = MB_SAVE;
    m->btn_label[0] = "Save";
    m->btn_id[1] = MB_DISCARD;
    m->btn_label[1] = "Discard";
    m->btn_id[2] = MB_CANCEL;
    m->btn_label[2] = "Cancel";
}

void ui_modal_error(ModalState *m, const char *msg) {
    m->kind = MODAL_ERROR;
    snprintf(m->title, sizeof(m->title), "Error");
    snprintf(m->msg, sizeof(m->msg), "%s", msg);
    m->nbtn = 1;
    m->btn_id[0] = MB_OK;
    m->btn_label[0] = "OK";
}

ModalButton ui_modal_click(ModalState *m, float x, float y) {
    for (int i = 0; i < m->nbtn; i++) {
        SDL_FRect r = m->buttons[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return m->btn_id[i];
    }
    return MB_NONE;
}

ModalButton ui_modal_key(ModalState *m, SDL_Keycode key) {
    if (m->kind == MODAL_NONE)
        return MB_NONE;
    if (key == SDLK_ESCAPE)
        return (m->kind == MODAL_CONFIRM) ? MB_CANCEL : MB_OK;
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
        return (m->kind == MODAL_CONFIRM) ? MB_SAVE : MB_OK;
    return MB_NONE;
}

// --- Drawing ---

static void fill_rect(SDL_Renderer *ren, SDL_Color c, const SDL_FRect *r) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, r);
}

void ui_draw_chrome(SDL_Renderer *ren, Font *f, const Theme *th,
                    const UiMetrics *m, int win_w, int win_h, MenuState *st,
                    int font_px, size_t line, size_t col, bool modified,
                    const char *lang) {
    float baseline =
        (float)f->asc + ((float)m->menu_h - (float)f->line_h) / 2.0f;

    // Menu bar.
    SDL_FRect bar = {0, 0, (float)win_w, (float)m->menu_h};
    fill_rect(ren, th->menu, &bar);
    for (int i = 0; i < ui_menu_count(); i++) {
        float tx = title_x(f, m, i), tw = title_w(f, m, i);
        if (st->open && st->menu == i) {
            SDL_FRect hl = {tx, 0, tw, (float)m->menu_h};
            fill_rect(ren, th->selection, &hl);
        }
        const char *t = ui_menu_title(i);
        ui_draw_text(ren, f, t, strlen(t), tx + (float)m->pad_x, baseline, th->foreground);
    }
    // Font-size indicator, right aligned (user-facing, unscaled size).
    char sizebuf[32];
    snprintf(sizebuf, sizeof(sizebuf), "%d px", font_px);
    float sw = ui_text_width(f, sizebuf, strlen(sizebuf));
    ui_draw_text(ren, f, sizebuf, strlen(sizebuf),
                 (float)win_w - sw - (float)m->pad_x, baseline, th->foreground);

    // Dropdown.
    SDL_FRect dd;
    if (dropdown_rect(st, f, m, win_w, &dd)) {
        fill_rect(ren, th->menu, &dd);
        SDL_SetRenderDrawColor(ren, th->border.r, th->border.g, th->border.b,
                               th->border.a);
        SDL_RenderRect(ren, &dd);
        const MenuItem *items = NULL;
        int n = ui_menu_items(st->menu, &items);
        float ih = item_h(f, m);
        float ib =
            dd.y + SC(m, 4) + (float)f->asc + ((ih - (float)f->line_h) / 2.0f);
        for (int i = 0; i < n; i++) {
            if (i == st->hover_item) {
                SDL_FRect hl = {dd.x + SC(m, 2), dd.y + SC(m, 4) + ih * (float)i,
                                dd.w - SC(m, 4), ih};
                fill_rect(ren, th->selection, &hl);
            }
            ui_draw_text(ren, f, items[i].label, strlen(items[i].label),
                         dd.x + SC(m, 14), ib + ih * (float)i, th->foreground);
            float scw = ui_text_width(f, items[i].shortcut,
                                      strlen(items[i].shortcut));
            ui_draw_text(ren, f, items[i].shortcut, strlen(items[i].shortcut),
                         dd.x + dd.w - scw - SC(m, 14), ib + ih * (float)i, th->foreground);
        }
    }

    // Status bar.
    SDL_FRect sb = {0, (float)(win_h - m->status_h), (float)win_w,
                    (float)m->status_h};
    fill_rect(ren, th->status_bar, &sb);
    SDL_SetRenderDrawColor(ren, th->border.r, th->border.g, th->border.b,
                           th->border.a);
    SDL_RenderLine(ren, 0, (float)(win_h - m->status_h), (float)win_w,
                   (float)(win_h - m->status_h));
    float sbase = (float)(win_h - m->status_h) + (float)f->asc +
                  ((float)m->status_h - (float)f->line_h) / 2.0f;
    char lc[64];
    snprintf(lc, sizeof(lc), "Ln %zu, Col %zu", line, col);
    ui_draw_text(ren, f, lc, strlen(lc), (float)m->pad_x, sbase, th->foreground);
    const char *enc = "UTF-8";
    float ew = ui_text_width(f, enc, strlen(enc));
    float ex = (float)win_w - ew - (float)m->pad_x;
    float rx = ex; // right-to-left cursor for the optional items
    if (modified) {
        const char *mod = "Modified  ";
        float mw = ui_text_width(f, mod, strlen(mod));
        ui_draw_text(ren, f, mod, strlen(mod), rx - mw, sbase, th->foreground);
        rx -= mw;
    }
    if (lang && lang[0]) {
        char lg[64];
        snprintf(lg, sizeof(lg), "%s  ", lang);
        float lw = ui_text_width(f, lg, strlen(lg));
        ui_draw_text(ren, f, lg, strlen(lg), rx - lw, sbase, th->foreground);
    }
    ui_draw_text(ren, f, enc, strlen(enc), ex, sbase, th->foreground);
}

// Count newlines to lay out a multi-line message box.
static int msg_lines(const char *msg) {
    int n = 1;
    for (const char *p = msg; *p; p++)
        if (*p == '\n')
            n++;
    return n;
}

void ui_draw_modal(SDL_Renderer *ren, Font *f, const Theme *th,
                   const UiMetrics *m, ModalState *modal, int win_w,
                   int win_h) {
    if (modal->kind == MODAL_NONE)
        return;
    // Dim the window.
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
    SDL_FRect full = {0, 0, (float)win_w, (float)win_h};
    SDL_RenderFillRect(ren, &full);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    int rows = msg_lines(modal->msg);
    float bw = SC(m, 340);
    float bh = SC(m, 60) + (float)rows * ((float)f->line_h + SC(m, 4)) +
               SC(m, 56);
    if (bw > (float)win_w - SC(m, 40))
        bw = (float)win_w - SC(m, 40);
    SDL_FRect box = {(float)(((double)win_w - (double)bw) / 2.0),
                     (float)(((double)win_h - (double)bh) / 2.0), bw, bh};
    fill_rect(ren, th->menu, &box);
    SDL_SetRenderDrawColor(ren, th->border.r, th->border.g, th->border.b,
                           th->border.a);
    SDL_RenderRect(ren, &box);

    float tx = box.x + SC(m, 16);
    float baseline = box.y + SC(m, 14) + (float)f->asc;
    ui_draw_text(ren, f, modal->title, strlen(modal->title), tx, baseline, th->foreground);
    baseline += SC(m, 10);
    // Message lines.
    const char *p = modal->msg;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        baseline += (float)f->line_h + SC(m, 4);
        ui_draw_text(ren, f, p, n, tx, baseline, th->foreground);
        if (!nl)
            break;
        p = nl + 1;
    }
    // Buttons, right aligned.
    float btn_w = SC(m, 84), btn_h = (float)f->line_h + SC(m, 12);
    float bx = box.x + box.w - SC(m, 16) - btn_w;
    float by = box.y + box.h - btn_h - SC(m, 14);
    for (int i = modal->nbtn - 1; i >= 0; i--) {
        SDL_FRect r = {bx, by, btn_w, btn_h};
        modal->buttons[i] = r;
        fill_rect(ren, th->selection, &r);
        SDL_SetRenderDrawColor(ren, th->border.r, th->border.g, th->border.b,
                               th->border.a);
        SDL_RenderRect(ren, &r);
        float lw =
            ui_text_width(f, modal->btn_label[i], strlen(modal->btn_label[i]));
        ui_draw_text(ren, f, modal->btn_label[i], strlen(modal->btn_label[i]),
                     bx + (btn_w - lw) / 2.0f, by + SC(m, 6) + (float)f->asc, th->foreground);
        bx -= btn_w + SC(m, 10);
    }
}

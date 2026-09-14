#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render.h"

#define FN_WIN_W 1152
#define FN_WIN_H 648

void app_mark_dirty(App *app) {
    app->dirty = true;
    // Any activity restarts the blink phase so the cursor is visible.
    app->blink_on = true;
    app->last_blink = SDL_GetTicks();
}

// Effective rasterized font size for the current display scale.
static int eff_font_px(const App *app) {
    int px = (int)((float)app->base_font_px * app->display_scale + 0.5f);
    return px < FN_FONT_MIN_PX ? FN_FONT_MIN_PX : px;
}

// Refresh framebuffer size + logical->physical mouse factors.
static void update_window_sizes(App *app) {
    int lw = 0, lh = 0, fw = 0, fh = 0;
    SDL_GetWindowSize(app->win, &lw, &lh);
    SDL_GetWindowSizeInPixels(app->win, &fw, &fh);
    if (fw > 0 && fh > 0) {
        app->win_w = fw;
        app->win_h = fh;
    }
    app->mouse_sx = (lw > 0 && fw > 0) ? (float)fw / (float)lw : 1.0f;
    app->mouse_sy = (lh > 0 && fh > 0) ? (float)fh / (float)lh : 1.0f;
}

static size_t visible_lines(const App *app) {
    int h = app->win_h - app->m.menu_h - app->m.status_h;
    if (h <= 0 || app->font.line_h <= 0)
        return 1;
    size_t v = (size_t)(h / app->font.line_h);
    return v == 0 ? 1 : v;
}

static float editor_area_x(const App *app) { return (float)app->m.pad_x; }

static float editor_area_w(const App *app) {
    float w = (float)app->win_w - editor_area_x(app) -
              (float)app->m.pad_x / 2.0f;
    return w < 0 ? 0 : w;
}

static void update_title(App *app) {
    char title[1152];
    const char *name = "Untitled";
    if (app->ed.has_path) {
        const char *slash = strrchr(app->ed.path, '/');
        name = slash ? slash + 1 : app->ed.path;
        if (!name[0])
            name = "Untitled";
    }
    if (app->ed.modified)
        snprintf(title, sizeof(title), "FastNote - %s *", name);
    else
        snprintf(title, sizeof(title), "FastNote - %s", name);
    SDL_SetWindowTitle(app->win, title);
}

// Keep the cursor on screen (vertical + horizontal) and tell the OS where
// text input happens (IME candidate window placement; SDL wants logical
// coordinates here, so convert back from framebuffer pixels).
static void reveal_cursor(App *app) {
    size_t vis = visible_lines(app);
    editor_ensure_visible(&app->ed, vis);

    int tab_w = font_tab_width(&app->font, FN_TAB_WIDTH_COLS);
    size_t line = tb_line_of(&app->ed.buf, app->ed.cursor);
    size_t ls = tb_line_start(&app->ed.buf, line);
    float w = editor_area_w(app);
    int lim = app->ed.scroll_x + (w > 0 ? (int)w : 0) + 4096;
    if (lim < 0)
        lim = 0;
    float cx = (float)font_text_width_max(&app->font, app->ed.buf.data + ls,
                                          app->ed.cursor - ls, tab_w, lim);
    if (cx - (float)app->ed.scroll_x < 0)
        app->ed.scroll_x = (int)cx - 20 < 0 ? 0 : (int)cx - 20;
    else if (cx - (float)app->ed.scroll_x > w - 20.0f && w > 40.0f)
        app->ed.scroll_x = (int)(cx - (w - 20.0f));
    if (app->ed.scroll_x < 0)
        app->ed.scroll_x = 0;

    float isx = app->mouse_sx > 0 ? app->mouse_sx : 1.0f;
    float isy = app->mouse_sy > 0 ? app->mouse_sy : 1.0f;
    SDL_Rect area = {(int)((float)app->m.pad_x / isx),
                     (int)((float)app->m.menu_h / isy),
                     (int)(w / isx),
                     (int)((float)(app->win_h - app->m.menu_h -
                                   app->m.status_h) /
                           isy)};
    if (area.w < 1)
        area.w = 1;
    if (area.h < 1)
        area.h = 1;
    SDL_SetTextInputArea(app->win, &area, 0);
    app_mark_dirty(app);
}

// Map a mouse point (framebuffer pixels) in the editor area to a buffer offset.
static size_t offset_at_point(App *app, float x, float y) {
    Editor *e = &app->ed;
    int tab_w = font_tab_width(&app->font, FN_TAB_WIDTH_COLS);
    long row = (long)((y - (float)app->m.menu_h) / (float)app->font.line_h);
    if (row < 0)
        row = 0;
    size_t line = e->scroll_line + (size_t)row;
    if (line >= e->buf.nlines)
        line = e->buf.nlines - 1;
    size_t ls = tb_line_start(&e->buf, line);
    size_t llen = tb_line_len(&e->buf, line);
    float target = x - editor_area_x(app) + (float)e->scroll_x;
    if (target <= 0)
        return ls;
    float pen = 0;
    size_t i = 0;
    while (i < llen) {
        uint32_t cp;
        size_t k = utf8_decode(e->buf.data + ls + i, llen - i, &cp);
        if (k == 0)
            break;
        float adv;
        if (cp == '\t') {
            int cur = (int)pen;
            adv = (float)(((cur / tab_w) + 1) * tab_w - cur);
        } else {
            const Glyph *g = font_get(&app->font, cp);
            adv = g ? (float)g->adv : 0;
        }
        if (target <= pen + adv / 2.0f)
            break;
        pen += adv;
        i += k;
    }
    return ls + i;
}

// Re-read the display scale (monitor move, DPI change, or override) and
// apply it to metrics + font size.
static void refresh_scale(App *app) {
    float scale = 0.0f;
    const char *env = SDL_getenv("FASTNOTE_SCALE");
    if (env && env[0])
        scale = (float)atof(env);
    if (!(scale > 0.0f))
        scale = SDL_GetWindowDisplayScale(app->win);
    if (!(scale > 0.0f))
        scale = 1.0f;
    if (scale > 4.0f)
        scale = 4.0f;
    if (scale == app->display_scale && app->font.px == eff_font_px(app))
        return;
    app->display_scale = scale;
    app->m = ui_metrics_for(scale);
    font_set_size(&app->font, eff_font_px(app));
    update_window_sizes(app);
    reveal_cursor(app); // clamps scroll to the new metrics, marks dirty
}

// --- File actions ---

static void run_after(App *app) {
    AfterAction a = app->after;
    app->after = AFTER_NONE;
    if (a == AFTER_NEW) {
        editor_new(&app->ed);
    } else if (a == AFTER_OPEN) {
        char err[256];
        if (!editor_load(&app->ed, app->pending_path, err, sizeof(err))) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not open file:\n%s", err);
            ui_modal_error(&app->modal, msg);
        }
    } else if (a == AFTER_EXIT) {
        app->running = false;
    }
    update_title(app);
    reveal_cursor(app);
}

static void do_save(App *app) {
    if (app->ed.has_path) {
        char err[256];
        if (!editor_save(&app->ed, err, sizeof(err))) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not save file:\n%s", err);
            ui_modal_error(&app->modal, msg);
        } else if (app->after != AFTER_NONE) {
            run_after(app);
        }
        update_title(app);
        app_mark_dirty(app);
    } else {
        // No filename yet: ask, then continue the pending action (if any).
        if (!app->dialog_open) {
            filedialog_save(app->win);
            app->dialog_open = true;
        }
        ui_modal_close(&app->modal);
    }
}

static void request_new(App *app) {
    if (app->ed.modified) {
        app->after = AFTER_NEW;
        ui_modal_confirm(&app->modal, "This document");
    } else {
        editor_new(&app->ed);
        update_title(app);
        reveal_cursor(app);
    }
}

static void request_open_dialog(App *app) {
    if (app->dialog_open)
        return;
    filedialog_open(app->win);
    app->dialog_open = true;
}

static void request_exit(App *app) {
    if (app->ed.modified) {
        app->after = AFTER_EXIT;
        ui_modal_confirm(&app->modal, "This document");
    } else {
        app->running = false;
    }
}

static void apply_font_size(App *app) {
    int px = eff_font_px(app);
    if (px != app->font.px && font_set_size(&app->font, px))
        reveal_cursor(app);
    else
        app_mark_dirty(app); // keep the size indicator fresh
}

static void do_action(App *app, MenuAction a) {
    switch (a) {
    case ACT_NONE:
        break;
    case ACT_NEW:
        request_new(app);
        break;
    case ACT_OPEN:
        request_open_dialog(app);
        break;
    case ACT_SAVE:
        do_save(app);
        break;
    case ACT_SAVE_AS:
        if (!app->dialog_open) {
            filedialog_save(app->win);
            app->dialog_open = true;
        }
        break;
    case ACT_EXIT:
        request_exit(app);
        break;
    case ACT_UNDO:
        if (editor_undo(&app->ed)) {
            update_title(app);
            reveal_cursor(app);
        }
        break;
    case ACT_REDO:
        if (editor_redo(&app->ed)) {
            update_title(app);
            reveal_cursor(app);
        }
        break;
    case ACT_COPY: {
        char *s = editor_selection_text(&app->ed);
        if (s) {
            SDL_SetClipboardText(s);
            free(s);
        }
        break;
    }
    case ACT_CUT: {
        char *s = editor_selection_text(&app->ed);
        if (s) {
            SDL_SetClipboardText(s);
            free(s);
            editor_delete_selection(&app->ed);
            update_title(app);
            reveal_cursor(app);
        }
        break;
    }
    case ACT_PASTE:
        if (SDL_HasClipboardText()) {
            char *t = SDL_GetClipboardText();
            if (t) {
                editor_insert(&app->ed, t, strlen(t));
                SDL_free(t);
                update_title(app);
                reveal_cursor(app);
            }
        }
        break;
    case ACT_SELECT_ALL:
        editor_select_all(&app->ed);
        reveal_cursor(app);
        break;
    case ACT_FONT_INC:
    case ACT_FONT_DEC:
    case ACT_FONT_RESET: {
        if (a == ACT_FONT_INC)
            app->base_font_px++;
        else if (a == ACT_FONT_DEC)
            app->base_font_px--;
        else
            app->base_font_px = FN_FONT_DEFAULT_PX;
        if (app->base_font_px < FN_FONT_MIN_PX)
            app->base_font_px = FN_FONT_MIN_PX;
        if (app->base_font_px > FN_FONT_MAX_PX)
            app->base_font_px = FN_FONT_MAX_PX;
        apply_font_size(app);
        break;
    }
    }
}

// --- Modal handling ---

static void handle_modal_button(App *app, ModalButton b) {
    if (b == MB_NONE || !ui_modal_is_open(&app->modal))
        return;
    if (app->modal.kind == MODAL_ERROR) {
        ui_modal_close(&app->modal);
        app_mark_dirty(app);
        return;
    }
    if (b == MB_SAVE) {
        do_save(app); // closes modal itself when a dialog is needed
        if (app->ed.has_path)
            ui_modal_close(&app->modal);
    } else if (b == MB_DISCARD) {
        ui_modal_close(&app->modal);
        run_after(app);
    } else { // MB_CANCEL
        app->after = AFTER_NONE;
        ui_modal_close(&app->modal);
        app_mark_dirty(app);
    }
}

// --- Native dialog results ---

static void handle_dialog_result(App *app, FileDialogResult *res) {
    app->dialog_open = false;
    if (!res)
        return;
    if (res->is_save) {
        if (res->path) {
            char err[256];
            if (!editor_save_as(&app->ed, res->path, err, sizeof(err))) {
                char msg[512];
                snprintf(msg, sizeof(msg), "Could not save file:\n%s", err);
                ui_modal_error(&app->modal, msg);
                app->after = AFTER_NONE;
            } else if (app->after != AFTER_NONE) {
                // A confirm-save requested this dialog; continue.
                AfterAction keep = app->after;
                if (keep == AFTER_NEW || keep == AFTER_EXIT ||
                    keep == AFTER_OPEN)
                    run_after(app);
                else
                    app->after = AFTER_NONE;
            }
            update_title(app);
            reveal_cursor(app);
        } else {
            // Cancelled: drop the pending follow-up.
            app->after = AFTER_NONE;
        }
    } else if (res->path) {
        if (app->ed.modified) {
            snprintf(app->pending_path, sizeof(app->pending_path), "%s",
                     res->path);
            app->after = AFTER_OPEN;
            ui_modal_confirm(&app->modal, "This document");
        } else {
            char err[256];
            if (!editor_load(&app->ed, res->path, err, sizeof(err))) {
                char msg[512];
                snprintf(msg, sizeof(msg), "Could not open file:\n%s", err);
                ui_modal_error(&app->modal, msg);
            }
            update_title(app);
            reveal_cursor(app);
        }
    }
    free(res->path);
    free(res);
    app_mark_dirty(app);
}

// --- Keyboard ---

static void on_key_down(App *app, const SDL_KeyboardEvent *k) {
    SDL_Keycode key = k->key;
    SDL_Keymod mod = k->mod;
    bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    bool shift = (mod & SDL_KMOD_SHIFT) != 0;

    if (ui_modal_is_open(&app->modal)) {
        handle_modal_button(app, ui_modal_key(&app->modal, key));
        return;
    }

    bool menu_was_open = ui_menu_is_open(&app->menu);
    if (key == SDLK_ESCAPE) {
        if (menu_was_open)
            ui_menu_close(&app->menu);
        else
            editor_clear_selection(&app->ed);
        app_mark_dirty(app);
        return;
    }
    if (menu_was_open) {
        // Menus are mouse-driven; any other key dismisses and goes through.
        ui_menu_close(&app->menu);
    }

    // Ctrl shortcuts.
    if (ctrl && !((mod & SDL_KMOD_ALT) != 0)) {
        MenuAction a = ACT_NONE;
        switch (key) {
        case SDLK_N: a = ACT_NEW; break;
        case SDLK_O: a = ACT_OPEN; break;
        case SDLK_S: a = shift ? ACT_SAVE_AS : ACT_SAVE; break;
        case SDLK_Q: a = ACT_EXIT; break;
        case SDLK_Z: a = shift ? ACT_REDO : ACT_UNDO; break;
        case SDLK_Y: a = ACT_REDO; break;
        case SDLK_A: a = ACT_SELECT_ALL; break;
        case SDLK_C: a = ACT_COPY; break;
        case SDLK_X: a = ACT_CUT; break;
        case SDLK_V: a = ACT_PASTE; break;
        case SDLK_PLUS:
        case SDLK_EQUALS:
        case SDLK_KP_PLUS: a = ACT_FONT_INC; break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: a = ACT_FONT_DEC; break;
        case SDLK_0:
        case SDLK_KP_0: a = ACT_FONT_RESET; break;
        default: break;
        }
        if (a != ACT_NONE) {
            do_action(app, a);
            app_mark_dirty(app);
            return;
        }
        // Anything else with Ctrl held (e.g. Ctrl+arrows) falls through to
        // the navigation handling below; printable Ctrl combos never reach
        // text input (see SDL_EVENT_TEXT_INPUT handling).
    }

    Editor *e = &app->ed;
    switch (key) {
    case SDLK_LEFT:
        if (ctrl)
            editor_move_word_left(e, shift);
        else
            editor_move_left(e, shift);
        reveal_cursor(app);
        break;
    case SDLK_RIGHT:
        if (ctrl)
            editor_move_word_right(e, shift);
        else
            editor_move_right(e, shift);
        reveal_cursor(app);
        break;
    case SDLK_UP:
        editor_move_up(e, shift);
        reveal_cursor(app);
        break;
    case SDLK_DOWN:
        editor_move_down(e, shift);
        reveal_cursor(app);
        break;
    case SDLK_HOME:
        if (ctrl)
            editor_move_doc_start(e, shift);
        else
            editor_move_home(e, shift);
        reveal_cursor(app);
        break;
    case SDLK_END:
        if (ctrl)
            editor_move_doc_end(e, shift);
        else
            editor_move_end(e, shift);
        reveal_cursor(app);
        break;
    case SDLK_PAGEUP:
        editor_move_page(e, -(int)visible_lines(app), shift);
        reveal_cursor(app);
        break;
    case SDLK_PAGEDOWN:
        editor_move_page(e, (int)visible_lines(app), shift);
        reveal_cursor(app);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        editor_newline(e);
        update_title(app);
        reveal_cursor(app);
        break;
    case SDLK_BACKSPACE:
        editor_backspace(e);
        update_title(app);
        reveal_cursor(app);
        break;
    case SDLK_DELETE:
        editor_delete_fwd(e);
        update_title(app);
        reveal_cursor(app);
        break;
    case SDLK_TAB:
        if (!ctrl) {
            editor_tab(e);
            update_title(app);
            reveal_cursor(app);
        }
        break;
    default:
        break;
    }
}

// --- Mouse (event coordinates are logical; convert to framebuffer) ---

static float fb_x(const App *app, float x) { return x * app->mouse_sx; }
static float fb_y(const App *app, float y) { return y * app->mouse_sy; }

static void on_mouse_down(App *app, const SDL_MouseButtonEvent *b) {
    if (b->button != SDL_BUTTON_LEFT)
        return;
    float x = fb_x(app, b->x), y = fb_y(app, b->y);
    if (ui_modal_is_open(&app->modal)) {
        handle_modal_button(app, ui_modal_click(&app->modal, x, y));
        return;
    }
    if (ui_point_in_chrome(&app->menu, &app->font, &app->m, x, y,
                           app->win_w)) {
        MenuAction a = ui_menu_click(&app->menu, &app->font, &app->m, x, y,
                                     app->win_w);
        do_action(app, a);
        app_mark_dirty(app);
        return;
    }
    ui_menu_close(&app->menu);
    size_t at = offset_at_point(app, x, y);
    editor_set_cursor(&app->ed, at, false);
    app->dragging = true;
    reveal_cursor(app);
}

static void on_mouse_motion(App *app, const SDL_MouseMotionEvent *mo) {
    if (ui_modal_is_open(&app->modal))
        return;
    float x = fb_x(app, mo->x), y = fb_y(app, mo->y);
    if (app->dragging) {
        size_t at = offset_at_point(app, x, y);
        editor_set_cursor(&app->ed, at, true);
        reveal_cursor(app);
        return;
    }
    if (ui_menu_is_open(&app->menu)) {
        ui_menu_motion(&app->menu, &app->font, &app->m, x, y, app->win_w);
        app_mark_dirty(app);
    }
}

static void on_wheel(App *app, const SDL_MouseWheelEvent *w) {
    if (ui_modal_is_open(&app->modal))
        return;
    if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
        app->ed.scroll_x -= (int)(w->y * 40.0f * app->display_scale);
        editor_clamp_scroll(&app->ed, visible_lines(app));
    } else {
        long sl = (long)app->ed.scroll_line - (long)(w->y * FN_WHEEL_LINES);
        if (sl < 0)
            sl = 0;
        app->ed.scroll_line = (size_t)sl;
        editor_clamp_scroll(&app->ed, visible_lines(app));
    }
    app_mark_dirty(app);
}

// --- Lifecycle ---

bool app_init(App *app, const char *open_path, const char *font_path, char *err,
              size_t errcap) {
    memset(app, 0, sizeof(*app));
    app->theme = theme_dark();
    app->menu.menu = -1;
    app->base_font_px = FN_FONT_DEFAULT_PX;
    app->display_scale = 1.0f;
    app->m = ui_metrics_for(1.0f);
    app->mouse_sx = app->mouse_sy = 1.0f;
    app->running = true;
    app->blink_on = true;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        snprintf(err, errcap, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    // HIGH_PIXEL_DENSITY: the drawable is physical pixels, so scaled text
    // stays crisp instead of being upscaled by the compositor. The requested
    // size stays logical (1152x648); the compositor scales its appearance.
    app->win = SDL_CreateWindow("FastNote", FN_WIN_W, FN_WIN_H,
                                SDL_WINDOW_RESIZABLE |
                                    SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app->win) {
        snprintf(err, errcap, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }
    SDL_SetWindowPosition(app->win, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
    app->ren = SDL_CreateRenderer(app->win, NULL);
    if (!app->ren) {
        snprintf(err, errcap, "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(app->win);
        SDL_Quit();
        return false;
    }
    // Display scale before font creation: glyphs rasterize at physical size.
    {
        float scale = 0.0f;
        const char *env = SDL_getenv("FASTNOTE_SCALE");
        if (env && env[0])
            scale = (float)atof(env);
        if (!(scale > 0.0f))
            scale = SDL_GetWindowDisplayScale(app->win);
        if (!(scale > 0.0f))
            scale = 1.0f;
        if (scale > 4.0f)
            scale = 4.0f;
        app->display_scale = scale;
        app->m = ui_metrics_for(scale);
    }
    update_window_sizes(app);
    app->win_w = app->win_w > 0 ? app->win_w : FN_WIN_W;
    app->win_h = app->win_h > 0 ? app->win_h : FN_WIN_H;
    if (!font_init(&app->font, app->ren, font_path, eff_font_px(app))) {
        snprintf(err, errcap, "No monospace font found (try FASTNOTE_FONT)");
        SDL_DestroyRenderer(app->ren);
        SDL_DestroyWindow(app->win);
        SDL_Quit();
        return false;
    }
    if (!editor_init(&app->ed)) {
        snprintf(err, errcap, "Out of memory");
        font_quit(&app->font);
        SDL_DestroyRenderer(app->ren);
        SDL_DestroyWindow(app->win);
        SDL_Quit();
        return false;
    }
    if (open_path) {
        char lerr[256];
        if (!editor_load(&app->ed, open_path, lerr, sizeof(lerr))) {
            // Non-fatal: start with an empty document, remember the name so
            // that Save writes where the user expected.
            snprintf(app->ed.path, sizeof(app->ed.path), "%s", open_path);
            app->ed.has_path = true;
            ui_modal_error(&app->modal, lerr);
        }
    }
    app->dlg_event = filedialog_event_type();
    SDL_StartTextInput(app->win);
    update_title(app);
    reveal_cursor(app);
    app->last_blink = SDL_GetTicks();
    return true;
}

void app_quit(App *app) {
    SDL_StopTextInput(app->win);
    editor_quit(&app->ed);
    font_quit(&app->font);
    if (app->ren)
        SDL_DestroyRenderer(app->ren);
    if (app->win)
        SDL_DestroyWindow(app->win);
    SDL_Quit();
}

void app_step(App *app, int timeout_ms) {
    if (timeout_ms < 0) {
        // Sleep until the next event or the next cursor-blink edge so idle
        // CPU stays near zero.
        uint64_t now = SDL_GetTicks();
        uint64_t elapsed = now - app->last_blink;
        timeout_ms = (int)(elapsed >= FN_BLINK_MS ? 0
                                                  : (FN_BLINK_MS - elapsed));
    }
    SDL_Event ev;
    if (SDL_WaitEventTimeout(&ev, timeout_ms)) {
        do {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                request_exit(app);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                update_window_sizes(app);
                editor_clamp_scroll(&app->ed, visible_lines(app));
                reveal_cursor(app);
                break;
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                refresh_scale(app);
                break;
            case SDL_EVENT_KEY_DOWN:
                on_key_down(app, &ev.key);
                break;
            case SDL_EVENT_TEXT_INPUT:
                if (!ui_modal_is_open(&app->modal)) {
                    SDL_Keymod mod = SDL_GetModState();
                    // Pure-Ctrl combinations are shortcuts (handled in
                    // KEY_DOWN); anything else (incl. AltGr) is text.
                    bool pure_ctrl =
                        (mod & SDL_KMOD_CTRL) && !(mod & SDL_KMOD_ALT);
                    if (!pure_ctrl) {
                        ui_menu_close(&app->menu);
                        editor_insert(&app->ed, ev.text.text,
                                      strlen(ev.text.text));
                        update_title(app);
                        reveal_cursor(app);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                on_mouse_down(app, &ev.button);
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button == SDL_BUTTON_LEFT)
                    app->dragging = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                on_mouse_motion(app, &ev.motion);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                on_wheel(app, &ev.wheel);
                break;
            default:
                if (app->dlg_event && ev.type == app->dlg_event) {
                    handle_dialog_result(app,
                                         (FileDialogResult *)ev.user.data1);
                }
                break;
            }
        } while (SDL_PollEvent(&ev));
    }
    uint64_t then = SDL_GetTicks();
    if (then - app->last_blink >= FN_BLINK_MS) {
        app->last_blink = then;
        app->blink_on = !app->blink_on;
        app->dirty = true;
    }
    if (app->dirty) {
        update_title(app);
        render_frame(app);
        app->dirty = false;
    }
}

int app_run(App *app) {
    update_title(app);
    while (app->running)
        app_step(app, -1);
    return 0;
}

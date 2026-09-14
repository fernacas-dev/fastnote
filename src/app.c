#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render.h"

_Static_assert(RECENT_MAX == PICK_MAX,
               "recents lists must fit the picker rows");

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
    int h = app->win_h - app->m.menu_h - app->m.tab_h - app->m.status_h;
    if (h <= 0 || app->font.line_h <= 0)
        return 1;
    size_t v = (size_t)(h / app->font.line_h);
    return v == 0 ? 1 : v;
}

static float editor_area_x(App *app) {
    float x = (float)app->m.pad_x;
    if (app->project.has && app->project.visible)
        x += (float)app->m.sidebar_w;
    // Line-number gutter (cache lookups only; digits rarely change).
    x += (float)ui_gutter_w(&app->font, &app->m, app->tabs[app->cur].buf.nlines);
    return x;
}

static float editor_area_w(App *app) {
    float w = (float)app->win_w - editor_area_x(app) -
              (float)app->m.pad_x / 2.0f;
    // Keep text clear of the scrollbar when one is shown.
    Editor *e = &app->tabs[app->cur];
    size_t visible = visible_lines(app);
    if (ui_has_scrollbar(e->buf.nlines, visible))
        w -= (float)app->m.scrollbar_w;
    return w < 0 ? 0 : w;
}

// Scrollbar geometry for the current tab (false when hidden).
static bool tab_scrollbar(App *app, SDL_FRect *track, SDL_FRect *thumb) {
    Editor *e = &app->tabs[app->cur];
    size_t visible = visible_lines(app);
    int top = app->m.menu_h + app->m.tab_h;
    int h = app->win_h - top - app->m.status_h;
    return ui_scrollbar_geom(&app->m, e->buf.nlines, visible, e->scroll_line,
                            top, h, app->win_w, track, thumb);
}

static void update_title(App *app) {
    char title[1152];
    const char *name = "Untitled";
    if (app->tabs[app->cur].has_path) {
        const char *slash = strrchr(app->tabs[app->cur].path, '/');
        name = slash ? slash + 1 : app->tabs[app->cur].path;
        if (!name[0])
            name = "Untitled";
    }
    if (app->tabs[app->cur].modified)
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
    editor_ensure_visible(&app->tabs[app->cur], vis);

    int tab_w = font_tab_width(&app->font, FN_TAB_WIDTH_COLS);
    size_t line = tb_line_of(&app->tabs[app->cur].buf, app->tabs[app->cur].cursor);
    size_t ls = tb_line_start(&app->tabs[app->cur].buf, line);
    float w = editor_area_w(app);
    int lim = app->tabs[app->cur].scroll_x + (w > 0 ? (int)w : 0) + 4096;
    if (lim < 0)
        lim = 0;
    float cx = (float)font_text_width_max(&app->font, app->tabs[app->cur].buf.data + ls,
                                          app->tabs[app->cur].cursor - ls, tab_w, lim);
    if (cx - (float)app->tabs[app->cur].scroll_x < 0)
        app->tabs[app->cur].scroll_x = (int)cx - 20 < 0 ? 0 : (int)cx - 20;
    else if (cx - (float)app->tabs[app->cur].scroll_x > w - 20.0f && w > 40.0f)
        app->tabs[app->cur].scroll_x = (int)(cx - (w - 20.0f));
    if (app->tabs[app->cur].scroll_x < 0)
        app->tabs[app->cur].scroll_x = 0;

    float isx = app->mouse_sx > 0 ? app->mouse_sx : 1.0f;
    float isy = app->mouse_sy > 0 ? app->mouse_sy : 1.0f;
    SDL_Rect area = {(int)((float)app->m.pad_x / isx),
                     (int)(((float)app->m.menu_h + (float)app->m.tab_h) /
                           isy),
                     (int)(w / isx),
                     (int)((float)(app->win_h - app->m.menu_h -
                                   app->m.tab_h - app->m.status_h) /
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
    Editor *e = &app->tabs[app->cur];
    int tab_w = font_tab_width(&app->font, FN_TAB_WIDTH_COLS);
    long row =
        (long)(((float)y - (float)app->m.menu_h - (float)app->m.tab_h) /
               (float)app->font.line_h);
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
            const Glyph *g = font_get(&app->font, cp, GCOL_FG);
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

// Open path with the unsaved-changes confirm when needed. Shared by the
// open-file dialog and sidebar clicks.
static void request_open_path(App *app, const char *path) {
    if (app->tabs[app->cur].modified) {
        snprintf(app->pending_path, sizeof(app->pending_path), "%s", path);
        app->after = AFTER_OPEN;
        ui_modal_confirm(&app->modal, "This document");
    } else {
        char err[256];
        if (!editor_load(&app->tabs[app->cur], path, err, sizeof(err))) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not open file:\n%s", err);
            ui_modal_error(&app->modal, msg);
        } else {
            recents_push_file(&app->recents, path);
            recents_save(&app->recents);
        }
        update_title(app);
        reveal_cursor(app);
    }
}

// --- Tabs (one Editor per open document) ---

static bool tabs_grow(App *app) {
    if (app->ntabs < app->tabs_cap)
        return true;
    size_t ncap = app->tabs_cap ? app->tabs_cap * 2 : 4;
    Editor *nt = realloc(app->tabs, ncap * sizeof(Editor));
    if (!nt)
        return false;
    app->tabs = nt;
    app->tabs_cap = ncap;
    return true;
}

// The current tab can take a fresh document without opening a new tab.
static bool cur_reusable(const App *app) {
    if (app->ntabs == 0)
        return false;
    const Editor *e = &app->tabs[app->cur];
    return !e->has_path && !e->modified && e->buf.len == 0;
}

static void switch_tab(App *app, size_t i) {
    if (i >= app->ntabs)
        return;
    app->cur = i;
    update_title(app);
    reveal_cursor(app);
}

// Drop the current tab. The last tab resets in place (never zero tabs).
static void close_cur_tab(App *app) {
    if (app->ntabs == 0)
        return;
    if (app->ntabs == 1) {
        editor_new(&app->tabs[app->cur]);
    } else {
        editor_quit(&app->tabs[app->cur]);
        memmove(&app->tabs[app->cur], &app->tabs[app->cur + 1],
                (app->ntabs - app->cur - 1) * sizeof(Editor));
        app->ntabs--;
        if (app->cur >= app->ntabs)
            app->cur = app->ntabs - 1;
    }
    update_title(app);
    reveal_cursor(app);
}

// Tab index that should receive an opened file: reuse an empty untitled
// tab, else append a fresh one (falling back to current on OOM).
static size_t target_tab_for_open(App *app) {
    if (cur_reusable(app))
        return app->cur;
    if (tabs_grow(app) && editor_init(&app->tabs[app->ntabs])) {
        app->cur = app->ntabs;
        app->ntabs++;
        return app->cur;
    }
    return app->cur;
}

// Exit flow: confirm unsaved tabs one by one, then quit.
static void continue_exit(App *app) {
    for (size_t i = 0; i < app->ntabs; i++) {
        if (app->tabs[i].modified) {
            app->cur = i;
            app->after = AFTER_EXIT;
            ui_modal_confirm(&app->modal, "This document");
            update_title(app);
            reveal_cursor(app);
            return;
        }
    }
    app->exit_mode = false;
    app->running = false;
}

// Close button / Ctrl+W on tab i: focus it, confirm only if modified.
static void request_close_tab(App *app, size_t i) {
    if (i >= app->ntabs)
        return;
    app->cur = i;
    if (app->tabs[app->cur].modified) {
        app->after = AFTER_CLOSE;
        ui_modal_confirm(&app->modal, "This document");
        update_title(app);
        reveal_cursor(app);
    } else {
        close_cur_tab(app);
    }
}

// --- Recent files/folders ---

// Open the picker's selected entry (file -> fresh tab, folder -> project).
static void accept_pick(App *app) {
    ModalState *m = &app->modal;
    if (m->kind != MODAL_PICKER || m->pick_sel < 0 ||
        m->pick_sel >= m->npick)
        return;
    char path[1024];
    snprintf(path, sizeof(path), "%s", m->pick_items[m->pick_sel]);
    bool folders = m->pick_folders;
    ui_modal_close(m);
    if (folders) {
        char err[256];
        if (!project_open(&app->project, path, err, sizeof(err))) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not open folder:\n%s", err);
            ui_modal_error(&app->modal, msg);
        }
    } else {
        app->cur = target_tab_for_open(app);
        request_open_path(app, path);
    }
    app_mark_dirty(app);
}

static void open_recents_picker(App *app, bool folders) {
    recents_prune(&app->recents);
    if (folders) {
        if (app->recents.ndirs <= 0) {
            ui_modal_error(&app->modal, "No recent folders yet");
            app_mark_dirty(app);
            return;
        }
        ui_modal_picker(&app->modal, "Open Recent Folder",
                        app->recents.dirs, app->recents.ndirs, true);
    } else {
        if (app->recents.nfiles <= 0) {
            ui_modal_error(&app->modal, "No recent files yet");
            app_mark_dirty(app);
            return;
        }
        ui_modal_picker(&app->modal, "Open Recent File",
                        app->recents.files, app->recents.nfiles, false);
    }
    app_mark_dirty(app);
}

// --- File actions ---

static void run_after(App *app) {
    AfterAction a = app->after;
    app->after = AFTER_NONE;
    if (a == AFTER_NEW) {
        editor_new(&app->tabs[app->cur]);
    } else if (a == AFTER_OPEN) {
        char err[256];
        if (!editor_load(&app->tabs[app->cur], app->pending_path, err, sizeof(err))) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not open file:\n%s", err);
            ui_modal_error(&app->modal, msg);
        } else {
            recents_push_file(&app->recents, app->pending_path);
            recents_save(&app->recents);
        }
    } else if (a == AFTER_EXIT) {
        if (app->exit_mode)
            continue_exit(app); // next unsaved tab, or quit
        else
            app->running = false;
    } else if (a == AFTER_CLOSE) {
        close_cur_tab(app);
    }
    update_title(app);
    reveal_cursor(app);
}

static void do_save(App *app) {
    if (app->tabs[app->cur].has_path) {
        char err[256];
        if (!editor_save(&app->tabs[app->cur], err, sizeof(err))) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not save file:\n%s", err);
            ui_modal_error(&app->modal, msg);
        } else if (app->after != AFTER_NONE) {
            run_after(app);
        }
        if (app->project.has)
            project_rescan(&app->project); // a save may add files
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
    if (app->tabs[app->cur].modified) {
        app->after = AFTER_NEW;
        ui_modal_confirm(&app->modal, "This document");
    } else if (cur_reusable(app)) {
        editor_new(&app->tabs[app->cur]);
        update_title(app);
        reveal_cursor(app);
    } else if (tabs_grow(app) && editor_init(&app->tabs[app->ntabs])) {
        app->cur = app->ntabs;
        app->ntabs++;
        update_title(app);
        reveal_cursor(app);
    } else {
        ui_modal_error(&app->modal, "Out of memory");
        app_mark_dirty(app);
    }
}

static void request_open_dialog(App *app) {
    if (app->dialog_open)
        return;
    filedialog_open(app->win);
    app->dialog_open = true;
}

static void request_exit(App *app) {
    app->exit_mode = true;
    continue_exit(app);
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
    case ACT_OPEN_FOLDER:
        if (!app->dialog_open) {
            filedialog_open_folder(app->win);
            app->dialog_open = true;
        }
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
    case ACT_RECENT_FILE:
        open_recents_picker(app, false);
        break;
    case ACT_RECENT_FOLDER:
        open_recents_picker(app, true);
        break;
    case ACT_ABOUT:
        ui_modal_about(&app->modal);
        app_mark_dirty(app);
        break;
    case ACT_CLOSE_TAB:
        request_close_tab(app, app->cur);
        break;
    case ACT_TAB_NEXT:
        if (app->ntabs > 0)
            switch_tab(app, (app->cur + 1) % app->ntabs);
        break;
    case ACT_TAB_PREV:
        if (app->ntabs > 0)
            switch_tab(app, (app->cur + app->ntabs - 1) % app->ntabs);
        break;
    case ACT_UNDO:
        if (editor_undo(&app->tabs[app->cur])) {
            update_title(app);
            reveal_cursor(app);
        }
        break;
    case ACT_REDO:
        if (editor_redo(&app->tabs[app->cur])) {
            update_title(app);
            reveal_cursor(app);
        }
        break;
    case ACT_COPY: {
        char *s = editor_selection_text(&app->tabs[app->cur]);
        if (s) {
            SDL_SetClipboardText(s);
            free(s);
        }
        break;
    }
    case ACT_CUT: {
        char *s = editor_selection_text(&app->tabs[app->cur]);
        if (s) {
            SDL_SetClipboardText(s);
            free(s);
            editor_delete_selection(&app->tabs[app->cur]);
            update_title(app);
            reveal_cursor(app);
        }
        break;
    }
    case ACT_PASTE:
        if (SDL_HasClipboardText()) {
            char *t = SDL_GetClipboardText();
            if (t) {
                editor_insert(&app->tabs[app->cur], t, strlen(t));
                SDL_free(t);
                update_title(app);
                reveal_cursor(app);
            }
        }
        break;
    case ACT_SELECT_ALL:
        editor_select_all(&app->tabs[app->cur]);
        reveal_cursor(app);
        break;
    case ACT_TOGGLE_SIDEBAR:
        if (app->project.has) {
            project_set_visible(&app->project, !app->project.visible);
            reveal_cursor(app); // re-clamp to the new editor area
        }
        break;
    case ACT_TOGGLE_HL:
        app->tabs[app->cur].hl_on = !app->tabs[app->cur].hl_on;
        app_mark_dirty(app);
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
        if (app->tabs[app->cur].has_path)
            ui_modal_close(&app->modal);
    } else if (b == MB_DISCARD) {
        ui_modal_close(&app->modal);
        run_after(app);
    } else { // MB_CANCEL
        app->after = AFTER_NONE;
        app->exit_mode = false;
        ui_modal_close(&app->modal);
        app_mark_dirty(app);
    }
}

// --- Native dialog results ---

static void handle_dialog_result(App *app, FileDialogResult *res) {
    app->dialog_open = false;
    if (!res)
        return;
    if (res->is_folder) {
        if (res->path) {
            char err[256];
            if (!project_open(&app->project, res->path, err, sizeof(err))) {
                char msg[512];
                snprintf(msg, sizeof(msg), "Could not open folder:\n%s", err);
                ui_modal_error(&app->modal, msg);
            } else {
                recents_push_dir(&app->recents, res->path);
                recents_save(&app->recents);
            }
        }
    } else if (res->is_save) {
        if (res->path) {
            char err[256];
            if (!editor_save_as(&app->tabs[app->cur], res->path, err, sizeof(err))) {
                char msg[512];
                snprintf(msg, sizeof(msg), "Could not save file:\n%s", err);
                ui_modal_error(&app->modal, msg);
                app->after = AFTER_NONE;
            } else {
                recents_push_file(&app->recents, res->path);
                recents_save(&app->recents);
            }
            if (app->after != AFTER_NONE) {
                // A confirm-save requested this dialog; continue.
                AfterAction keep = app->after;
                if (keep == AFTER_NEW || keep == AFTER_EXIT ||
                    keep == AFTER_OPEN || keep == AFTER_CLOSE)
                    run_after(app);
                else
                    app->after = AFTER_NONE;
            }
            if (app->project.has)
                project_rescan(&app->project); // a save may add files
            update_title(app);
            reveal_cursor(app);
        } else {
            // Cancelled: drop the pending follow-up.
            app->after = AFTER_NONE;
        }
    } else if (res->path) {
        // Opened files land in a fresh tab (reusing an empty one).
        app->cur = target_tab_for_open(app);
        request_open_path(app, res->path);
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
        if (app->modal.kind == MODAL_PICKER) {
            if (key == SDLK_UP) {
                ui_modal_pick_move(&app->modal, -1);
                app_mark_dirty(app);
            } else if (key == SDLK_DOWN) {
                ui_modal_pick_move(&app->modal, 1);
                app_mark_dirty(app);
            } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                accept_pick(app);
            } else if (key == SDLK_ESCAPE) {
                ui_modal_close(&app->modal);
                app_mark_dirty(app);
            }
            return;
        }
        handle_modal_button(app, ui_modal_key(&app->modal, key));
        return;
    }

    bool menu_was_open = ui_menu_is_open(&app->menu);
    if (key == SDLK_ESCAPE) {
        if (menu_was_open)
            ui_menu_close(&app->menu);
        else
            editor_clear_selection(&app->tabs[app->cur]);
        app_mark_dirty(app);
        return;
    }
    if (menu_was_open) {
        // Menus are mouse-driven; any other key dismisses and goes through.
        ui_menu_close(&app->menu);
    }
    if (key == SDLK_F1 && !ctrl) {
        do_action(app, ACT_ABOUT);
        app_mark_dirty(app);
        return;
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
        case SDLK_B: a = ACT_TOGGLE_SIDEBAR; break;
        case SDLK_R: a = ACT_RECENT_FILE; break;
        case SDLK_W: a = ACT_CLOSE_TAB; break;
        case SDLK_TAB: a = shift ? ACT_TAB_PREV : ACT_TAB_NEXT; break;
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

    Editor *e = &app->tabs[app->cur];
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
            editor_tab(&app->tabs[app->cur]);
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
        if (app->modal.kind == MODAL_PICKER) {
            int row = ui_modal_pick_click(&app->modal, x, y);
            if (row >= 0) {
                app->modal.pick_sel = row;
                accept_pick(app);
            } else {
                handle_modal_button(app, ui_modal_click(&app->modal, x, y));
            }
            return;
        }
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
    // Sidebar file clicks (framebuffer coordinates).
    if (app->project.has && app->project.visible &&
        x < (float)app->m.sidebar_w) {
        const char *rel = NULL;
        ProjClick pc = project_click(&app->project, &app->font, &app->m, x,
                                     y, app->m.menu_h,
                                     app->win_h - app->m.status_h, &rel);
        if (pc == PCK_FILE && rel) {
            char full[2048];
            snprintf(full, sizeof(full), "%s/%s", app->project.root, rel);
            // Sidebar opens into a fresh tab (reusing an empty one).
            app->cur = target_tab_for_open(app);
            request_open_path(app, full);
        }
        app_mark_dirty(app);
        return;
    }
    // Tab strip clicks (below the menu bar, right of the sidebar).
    {
        float strip_y = (float)app->m.menu_h;
        float sx0 = (app->project.has && app->project.visible)
                        ? (float)app->m.sidebar_w
                        : 0.0f;
        if (y >= strip_y && y < strip_y + (float)app->m.tab_h && x >= sx0) {
            int idx = -1;
            TabClick tc = ui_tab_click(
                &app->m, app->tabs, (int)app->ntabs, sx0, strip_y,
                (float)app->win_w, x, y, &idx);
            if (tc == TABACT_SWITCH)
                switch_tab(app, (size_t)idx);
            else if (tc == TABACT_CLOSE)
                request_close_tab(app, (size_t)idx);
            else
                app_mark_dirty(app);
            return;
        }
    }
    // Scrollbar: thumb drag starts, track click pages.
    {
        SDL_FRect track, thumb;
        if (tab_scrollbar(app, &track, &thumb) && x >= track.x &&
            x < track.x + track.w && y >= track.y &&
            y < track.y + track.h) {
            Editor *e = &app->tabs[app->cur];
            if (x >= thumb.x && x < thumb.x + thumb.w && y >= thumb.y &&
                y < thumb.y + thumb.h) {
                app->sb_drag = true;
                app->sb_grab = y - thumb.y;
            } else {
                size_t vis = visible_lines(app);
                long sl = (long)e->scroll_line;
                sl += (y < thumb.y) ? -(long)vis : (long)vis;
                if (sl < 0)
                    sl = 0;
                e->scroll_line = (size_t)sl;
                editor_clamp_scroll(e, vis);
                app_mark_dirty(app);
            }
            return;
        }
    }
    // Clicks on the line-number gutter are ignored.
    if (x < editor_area_x(app))
        return;
    size_t at = offset_at_point(app, x, y);
    editor_set_cursor(&app->tabs[app->cur], at, false);
    app->dragging = true;
    reveal_cursor(app);
}

static void on_mouse_motion(App *app, const SDL_MouseMotionEvent *mo) {
    if (ui_modal_is_open(&app->modal))
        return;
    float x = fb_x(app, mo->x), y = fb_y(app, mo->y);
    if (app->sb_drag) {
        // Drag the thumb: map pointer back to first-visible-line.
        SDL_FRect track, thumb;
        if (tab_scrollbar(app, &track, &thumb) &&
            track.h > thumb.h + 1.0f) {
            Editor *e = &app->tabs[app->cur];
            size_t visible = visible_lines(app);
            size_t max = e->buf.nlines > visible ? e->buf.nlines - visible
                                                 : 0;
            float frac = (y - app->sb_grab - track.y) /
                         (track.h - thumb.h);
            if (frac < 0.0f)
                frac = 0.0f;
            if (frac > 1.0f)
                frac = 1.0f;
            e->scroll_line = (size_t)(frac * (float)max + 0.5f);
            editor_clamp_scroll(e, visible);
        }
        app_mark_dirty(app);
        return;
    }
    if (app->dragging) {
        size_t at = offset_at_point(app, x, y);
        editor_set_cursor(&app->tabs[app->cur], at, true);
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
    // Wheel over the sidebar scrolls the project, not the document.
    if (app->project.has && app->project.visible &&
        fb_x(app, w->mouse_x) < (float)app->m.sidebar_w) {
        int rows = project_visible_rows(&app->font, &app->m, app->m.menu_h,
                                        app->win_h - app->m.status_h);
        project_scroll(&app->project, -(long)(w->y * FN_WHEEL_LINES), rows);
        app_mark_dirty(app);
        return;
    }
    if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
        app->tabs[app->cur].scroll_x -= (int)(w->y * 40.0f * app->display_scale);
        editor_clamp_scroll(&app->tabs[app->cur], visible_lines(app));
    } else {
        long sl = (long)app->tabs[app->cur].scroll_line - (long)(w->y * FN_WHEEL_LINES);
        if (sl < 0)
            sl = 0;
        app->tabs[app->cur].scroll_line = (size_t)sl;
        editor_clamp_scroll(&app->tabs[app->cur], visible_lines(app));
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
    app->tabs = NULL;
    app->ntabs = 0;
    app->tabs_cap = 0;
    app->cur = 0;
    recents_init(&app->recents);
    if (!tabs_grow(app) || !editor_init(&app->tabs[0])) {
        snprintf(err, errcap, "Out of memory");
        free(app->tabs);
        app->tabs = NULL;
        font_quit(&app->font);
        SDL_DestroyRenderer(app->ren);
        SDL_DestroyWindow(app->win);
        SDL_Quit();
        return false;
    }
    app->ntabs = 1;
    project_init(&app->project);
    {
        // Glyphs bake these colors: drawing then needs zero state changes.
        const SDL_Color palette[8] = {
            app->theme.foreground,  // GCOL_FG
            app->theme.hl_keyword,  // GCOL_KEYWORD
            app->theme.hl_string,   // GCOL_STRING
            app->theme.hl_comment,  // GCOL_COMMENT
            app->theme.hl_number,   // GCOL_NUMBER
            app->theme.hl_preproc,  // GCOL_PREPROC
            app->theme.hl_tag,      // GCOL_TAG
            app->theme.line_number, // GCOL_GREY
        };
        font_set_palette(&app->font, palette);
    }
    if (open_path) {
        char lerr[256];
        if (!editor_load(&app->tabs[app->cur], open_path, lerr, sizeof(lerr))) {
            // Non-fatal: start with an empty document, remember the name so
            // that Save writes where the user expected.
            snprintf(app->tabs[app->cur].path, sizeof(app->tabs[app->cur].path), "%s", open_path);
            app->tabs[app->cur].has_path = true;
            ui_modal_error(&app->modal, lerr);
        } else {
            recents_push_file(&app->recents, open_path);
            recents_save(&app->recents);
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
    free(app->hl_scratch);
    app->hl_scratch = NULL;
    app->hl_scratch_cap = 0;
    project_quit(&app->project);
    for (size_t i = 0; i < app->ntabs; i++)
        editor_quit(&app->tabs[i]);
    free(app->tabs);
    app->tabs = NULL;
    app->ntabs = app->tabs_cap = app->cur = 0;
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
                editor_clamp_scroll(&app->tabs[app->cur], visible_lines(app));
                reveal_cursor(app);
                break;
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                refresh_scale(app);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                // Pick up external file changes while we were away.
                if (app->project.has)
                    project_rescan(&app->project);
                app_mark_dirty(app);
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
                        editor_insert(&app->tabs[app->cur], ev.text.text,
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
                    if (ev.button.button == SDL_BUTTON_LEFT) {
                        app->dragging = false;
                        app->sb_drag = false;
                    }
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
        // A false return means highlight states are still catching up:
        // keep rendering until resolved.
        app->dirty = !render_frame(app);
    }
}

int app_run(App *app) {
    update_title(app);
    while (app->running)
        app_step(app, -1);
    return 0;
}

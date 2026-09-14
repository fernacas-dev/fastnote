#include "render.h"
#include "app.h"
#include "filetype.h"
#include "highlight.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Highlight recompute budget per frame (lines) and per-line kinds cap
// (bytes). Beyond the cap a line draws unhighlighted; the draw loop also
// stops past the viewport, so pathological single-line files stay fast.
#define HL_BUDGET_LINES 3000
#define HL_KINDS_CAP 65536

static SDL_Color hl_kind_color(const App *app, uint8_t kind) {
    switch (kind) {
    case HL_KEYWORD:
        return app->theme.hl_keyword;
    case HL_COMMENT:
        return app->theme.hl_comment;
    case HL_STRING:
        return app->theme.hl_string;
    case HL_NUMBER:
        return app->theme.hl_number;
    case HL_PREPROC:
        return app->theme.hl_preproc;
    case HL_TAG:
        return app->theme.hl_tag;
    default:
        return app->theme.foreground;
    }
}

// Draw one visible line: selection background, then glyphs.
// All coordinates are framebuffer (physical) pixels.
static void draw_line(App *app, size_t line, float top_y, float area_x,
                      float area_w, int tab_w) {
    Editor *e = &app->tabs[app->cur];
    Font *f = &app->font;
    SDL_Renderer *ren = app->ren;
    float baseline = top_y + (float)f->asc;

    size_t ls = tb_line_start(&e->buf, line);
    size_t llen = tb_line_len(&e->buf, line);

    // Tokenize this line when its block state is known. Kinds cover the
    // first HL_KINDS_CAP bytes; anything past that draws unhighlighted.
    HlLang lang = app->tabs[app->cur].hl_on ? app->tabs[app->cur].hl_lang : HLANG_NONE;
    const uint8_t *kinds = NULL;
    size_t scanned = 0;
    if (lang != HLANG_NONE && e->buf.lstate && line <= e->hl_clean) {
        scanned = llen < HL_KINDS_CAP ? llen : HL_KINDS_CAP;
        if (scanned > app->hl_scratch_cap) {
            uint8_t *ns = realloc(app->hl_scratch, scanned);
            if (ns) {
                app->hl_scratch = ns;
                app->hl_scratch_cap = scanned;
            } else {
                scanned = 0; // OOM: draw this line unhighlighted
            }
        }
        if (scanned > 0) {
            hl_scan_line(lang, e->buf.data + ls, scanned,
                         e->buf.lstate[line], app->hl_scratch, scanned);
            kinds = app->hl_scratch;
        }
    }

    // Selection background for this line. The overlap test must prove the
    // selection actually touches this line: with b < ls the subtraction
    // b - ls would wrap (size_t) and read out of bounds.
    if (editor_has_selection(e)) {
        size_t a, b;
        editor_selection_range(e, &a, &b);
        size_t le = ls + llen; // excludes '\n'
        if (a <= le && b > ls) {
            size_t s0 = a > ls ? a - ls : 0;
            size_t s1 = b < le ? b - ls : llen;
            // Bounded measurement: work stays proportional to the viewport.
            int lim = e->scroll_x + (area_w > 0 ? (int)area_w : 0) + 64;
            if (lim < 0)
                lim = 0;
            float x1 = area_x +
                       (float)font_text_width_max(f, e->buf.data + ls, s0,
                                                  tab_w, lim) -
                       (float)e->scroll_x;
            float x2;
            if (b > le)
                x2 = area_x + area_w; // selection covers the newline: to edge
            else
                x2 = area_x +
                     (float)font_text_width_max(f, e->buf.data + ls, s1,
                                                tab_w, lim) -
                     (float)e->scroll_x;
            if (x2 < area_x)
                x2 = area_x;
            if (x1 < area_x)
                x1 = area_x;
            if (x2 > x1) {
                SDL_FRect r = {x1, top_y, x2 - x1, (float)f->line_h};
                SDL_SetRenderDrawColor(ren, app->theme.selection.r,
                                       app->theme.selection.g,
                                       app->theme.selection.b, 0xFF);
                SDL_RenderFillRect(ren, &r);
            }
        }
    }

    // Glyphs.
    float pen = area_x - (float)e->scroll_x;
    float end_x = area_x + area_w;
    size_t i = 0;
    while (i < llen) {
        const char *p = e->buf.data + ls + i;
        uint32_t cp;
        size_t k = utf8_decode(p, llen - i, &cp);
        if (k == 0)
            break;
        if (cp == '\t') {
            int cur = (int)(pen - area_x + (float)e->scroll_x);
            pen += (float)(((cur / tab_w) + 1) * tab_w - cur);
        } else {
            const Glyph *g = font_get(f, cp);
            if (g) {
                if (g->tex && pen + (float)(g->bx + g->w) > area_x &&
                    pen < end_x) {
                    SDL_Color gc = (kinds && i < scanned)
                                       ? hl_kind_color(app, kinds[i])
                                       : app->theme.foreground;
                    font_draw_glyph(ren, f, g, pen, baseline, gc);
                }
                pen += (float)g->adv;
                if (pen - area_x > area_w + 64.0f && i > 0) {
                    // Past the right edge with margin: nothing else on this
                    // line needs the pen, so stop the line walk here.
                    break;
                }
            }
        }
        i += k;
    }
}

bool render_frame(App *app) {
    Editor *e = &app->tabs[app->cur];
    Font *f = &app->font;
    SDL_Renderer *ren = app->ren;
    const UiMetrics *m = &app->m;

    int tab_w = font_tab_width(f, FN_TAB_WIDTH_COLS);

    int side = (app->project.has && app->project.visible) ? m->sidebar_w : 0;
    int gutter = ui_gutter_w(f, m, e->buf.nlines);
    float area_y = (float)(m->menu_h + m->tab_h);
    float area_h = (float)(app->win_h - m->menu_h - m->tab_h - m->status_h);
    float area_x = (float)m->pad_x + (float)(side + gutter);
    float area_w = (float)app->win_w - area_x - (float)m->pad_x / 2.0f;
    if (area_w < 0)
        area_w = 0;

    size_t visible = area_h > 0 ? (size_t)(area_h / (float)f->line_h) : 1;
    if (visible == 0)
        visible = 1;
    editor_clamp_scroll(e, visible);

    // Bring highlight block states up to the last visible line, bounded by
    // the per-frame budget. Unfinished work keeps the frame "pending".
    bool hl_done = true;
    if (e->hl_on && e->hl_lang != HLANG_NONE && e->buf.nlines > 0) {
        size_t last = e->scroll_line + visible - 1;
        if (last >= e->buf.nlines)
            last = e->buf.nlines - 1;
        hl_done = editor_hl_update(e, e->hl_lang, last, HL_BUDGET_LINES);
    }

    // Background.
    SDL_SetRenderDrawColor(ren, app->theme.background.r,
                           app->theme.background.g, app->theme.background.b,
                           0xFF);
    SDL_RenderClear(ren);

    // Project sidebar (its own scroll/rows; editor area starts after it).
    if (side > 0) {
        project_draw(ren, f, &app->theme, m, &app->project, m->menu_h,
                     app->win_h - m->status_h,
                     e->has_path ? e->path : NULL);
    }

    // Tab strip under the menu bar, right of the sidebar.
    if (app->ntabs > 0) {
        ui_draw_tabs(ren, f, &app->theme, m, app->tabs, (int)app->ntabs,
                     (int)app->cur, (float)side, (float)m->menu_h,
                     (float)app->win_w);
    }

    // Visible lines only.
    size_t cursor_line = tb_line_of(&e->buf, e->cursor);
    for (size_t i = 0; i < visible; i++) {
        size_t line = e->scroll_line + i;
        if (line >= e->buf.nlines)
            break;
        float top_y = area_y + (float)i * (float)f->line_h;
        // Gutter: right-aligned number; current line in foreground.
        char num[32];
        snprintf(num, sizeof(num), "%zu", line + 1);
        float nw = ui_text_width(f, num, strlen(num));
        SDL_Color nc =
            line == cursor_line ? app->theme.foreground : app->theme.line_number;
        ui_draw_text(ren, f, num, strlen(num),
                     area_x - nw - (float)m->pad_x, top_y + (float)f->asc,
                     nc);
        draw_line(app, line, top_y, area_x, area_w, tab_w);
    }

    // Cursor (blinking).
    if (app->blink_on && !ui_modal_is_open(&app->modal)) {
        size_t cline = tb_line_of(&e->buf, e->cursor);
        if (cline >= e->scroll_line && cline < e->scroll_line + visible) {
            size_t ls = tb_line_start(&e->buf, cline);
            int lim = e->scroll_x + (area_w > 0 ? (int)area_w : 0) + 64;
            if (lim < 0)
                lim = 0;
            float cx = area_x +
                       (float)font_text_width_max(f, e->buf.data + ls,
                                                  e->cursor - ls, tab_w,
                                                  lim) -
                       (float)e->scroll_x;
            if (cx >= area_x - 2.0f && cx <= area_x + area_w) {
                float cy = area_y + (float)(cline - e->scroll_line) *
                                        (float)f->line_h;
                SDL_FRect cur = {cx, cy, (float)m->cursor_w,
                                 (float)f->line_h};
                SDL_SetRenderDrawColor(ren, app->theme.cursor.r,
                                       app->theme.cursor.g,
                                       app->theme.cursor.b, 0xFF);
                SDL_RenderFillRect(ren, &cur);
            }
        }
    }

    size_t ln, col;
    editor_line_col(e, &ln, &col);
    const char *lang = filetype_of(e->has_path ? e->path : NULL);
    ui_draw_chrome(ren, f, &app->theme, m, app->win_w, app->win_h, &app->menu,
                   app->base_font_px, ln, col, e->modified, lang);
    ui_draw_modal(ren, f, &app->theme, m, &app->modal, app->win_w, app->win_h);

    SDL_RenderPresent(ren);
    return hl_done;
}

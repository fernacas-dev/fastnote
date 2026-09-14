#include "render.h"
#include "app.h"
#include "ui.h"

// Draw one visible line: selection background, then glyphs.
// All coordinates are framebuffer (physical) pixels.
static void draw_line(App *app, size_t line, float top_y, float area_x,
                      float area_w, int tab_w) {
    Editor *e = &app->ed;
    Font *f = &app->font;
    SDL_Renderer *ren = app->ren;
    float baseline = top_y + (float)f->asc;

    size_t ls = tb_line_start(&e->buf, line);
    size_t llen = tb_line_len(&e->buf, line);

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
                    SDL_FRect dst = {pen + (float)g->bx,
                                     baseline - (float)g->by, (float)g->w,
                                     (float)g->h};
                    SDL_RenderTexture(ren, g->tex, NULL, &dst);
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

void render_frame(App *app) {
    Editor *e = &app->ed;
    Font *f = &app->font;
    SDL_Renderer *ren = app->ren;
    const UiMetrics *m = &app->m;

    int tab_w = font_tab_width(f, FN_TAB_WIDTH_COLS);

    float area_y = (float)m->menu_h;
    float area_h = (float)(app->win_h - m->menu_h - m->status_h);
    float area_x = (float)m->pad_x;
    float area_w = (float)app->win_w - area_x - (float)m->pad_x / 2.0f;
    if (area_w < 0)
        area_w = 0;

    size_t visible = area_h > 0 ? (size_t)(area_h / (float)f->line_h) : 1;
    if (visible == 0)
        visible = 1;
    editor_clamp_scroll(e, visible);

    // Background.
    SDL_SetRenderDrawColor(ren, app->theme.background.r,
                           app->theme.background.g, app->theme.background.b,
                           0xFF);
    SDL_RenderClear(ren);

    // Visible lines only.
    for (size_t i = 0; i < visible; i++) {
        size_t line = e->scroll_line + i;
        if (line >= e->buf.nlines)
            break;
        draw_line(app, line, area_y + (float)i * (float)f->line_h, area_x,
                  area_w, tab_w);
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
    ui_draw_chrome(ren, f, &app->theme, m, app->win_w, app->win_h, &app->menu,
                   app->base_font_px, ln, col, e->modified);
    ui_draw_modal(ren, f, &app->theme, m, &app->modal, app->win_w, app->win_h);

    SDL_RenderPresent(ren);
}

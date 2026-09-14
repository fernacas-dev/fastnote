#ifndef FASTNOTE_FONT_H
#define FASTNOTE_FONT_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t codepoint;
    SDL_Texture *tex; // NULL for blank glyphs (e.g. space); advance still valid
    int w, h;         // bitmap size in pixels
    int bx, by;       // bearing: left and top (distance from baseline to top)
    int adv;          // horizontal advance in pixels
    bool used;
} Glyph;

// Open-addressing cache: rasterize once via FreeType, reuse the texture.
typedef struct {
    Glyph *slots;
    size_t cap;
    size_t count;
} GlyphCache;

typedef struct {
    void *ft;   // FT_Library (void* to keep this header FreeType-free)
    void *face; // FT_Face
    SDL_Renderer *renderer;
    char path[1024];
    int px;
    int line_h; // distance between baselines
    int asc;    // baseline offset from line top (pixels)
    GlyphCache cache;
} Font;

// path may be NULL to auto-detect a system monospace font.
bool font_init(Font *f, SDL_Renderer *ren, const char *path, int px);
void font_quit(Font *f);
// Change pixel size; clears the cache. Returns false on failure (keeps old size).
bool font_set_size(Font *f, int px);
// Get (and rasterize on miss) the glyph for cp. Returns NULL only on OOM.
const Glyph *font_get(Font *f, uint32_t cp);
// Draw one glyph with an exact color. Glyphs rasterize white; the color
// comes from the texture color mod, so any theme color stays exact.
void font_draw_glyph(SDL_Renderer *ren, Font *f, const Glyph *g, float pen_x,
                     float baseline_y, SDL_Color color);
// Sum of advances for n bytes of UTF-8 text; tab_w is the tab stop in pixels.
int font_text_width(Font *f, const char *s, size_t n, int tab_w);
// Bounded variant: stops once the width exceeds max_x (returns > max_x).
// Keeps per-frame work proportional to the viewport even for pathological
// single-line files; exactness past the cap is irrelevant (off-screen).
int font_text_width_max(Font *f, const char *s, size_t n, int tab_w,
                        int max_x);
int font_tab_width(Font *f, int cols);

#endif

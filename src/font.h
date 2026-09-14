#ifndef FASTNOTE_FONT_H
#define FASTNOTE_FONT_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t codepoint;
    uint8_t color;    // GlyphColor cache key (see below)
    int w, h;         // bitmap size in pixels (0x0 for blank glyphs)
    int bx, by;       // bearing: left and top (distance from baseline to top)
    int adv;          // horizontal advance in pixels
    int sx, sy;       // atlas position in pixels (atlas mode, iff gen matches)
    SDL_Texture *tex; // tight texture (software mode, NULL when blank)
    uint32_t gen;
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
    // Glyph atlas: one RGBA texture holding every rasterized glyph.
    // Consecutive draws share one texture with zero state changes, which
    // is the fast path on GPU auto-batching. Shelf-packed; when full, the
    // generation bumps and packing restarts (stale entries re-rasterize
    // on demand). Unused on the software renderer (see use_atlas).
    SDL_Texture *atlas;
    int pack_x, pack_y, pack_row_h;
    uint32_t atlas_gen;
    // Raster backend selected from the renderer name: tight per-glyph
    // textures on software renderers (cache-friendly strided-free blits),
    // shared atlas everywhere else (no texture switches, GPU batching).
    // Measured on software: 0.24 vs 1.4 us/glyph in favor of tight.
    bool use_atlas;
    // Raster colors, indexed by GlyphColor. Glyphs bake their color so
    // drawing needs no per-glyph color-mod state changes.
    SDL_Color palette[8];
} Font;

// Cache key color: codepoint × one of these. Covers editor text (kinds),
// gutter numbers and all chrome (foreground/grey).
typedef enum {
    GCOL_FG = 0,
    GCOL_KEYWORD,
    GCOL_STRING,
    GCOL_COMMENT,
    GCOL_NUMBER,
    GCOL_PREPROC,
    GCOL_TAG,
    GCOL_GREY,
} GlyphColor;

// path may be NULL to auto-detect a system monospace font.
bool font_init(Font *f, SDL_Renderer *ren, const char *path, int px);
void font_quit(Font *f);
// Change pixel size; clears the cache. Returns false on failure (keeps old size).
bool font_set_size(Font *f, int px);
// Set the raster palette (clears cache + atlas). Call once after the
// theme is known; glyphs bake these colors at raster time.
void font_set_palette(Font *f, const SDL_Color palette[8]);
// Get (and rasterize on miss) the glyph for (cp, color). NULL only on OOM.
const Glyph *font_get(Font *f, uint32_t cp, uint8_t color);
// Blit one glyph; no-op for blank/stale glyphs.
void font_draw_glyph(SDL_Renderer *ren, Font *f, const Glyph *g, float pen_x,
                     float baseline_y);
// Sum of advances for n bytes of UTF-8 text; tab_w is the tab stop in pixels.
int font_text_width(Font *f, const char *s, size_t n, int tab_w);
// Bounded variant: stops once the width exceeds max_x (returns > max_x).
// Keeps per-frame work proportional to the viewport even for pathological
// single-line files; exactness past the cap is irrelevant (off-screen).
int font_text_width_max(Font *f, const char *s, size_t n, int tab_w,
                        int max_x);
int font_tab_width(Font *f, int cols);

#endif

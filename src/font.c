#include "font.h"
#include "text_buffer.h" // utf8_decode

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define GLYPH_CACHE_CAP 2048
#define ATLAS_SIZE 1024
#define ATLAS_PAD 1

static SDL_Texture *rasterize_tight(Font *f, FT_Bitmap *bm, uint8_t color);

// Monospace candidates, first existing file wins.
static const char *const font_candidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/droid/DroidSansMono.ttf",
    "/usr/share/fonts/truetype/hack/Hack-Regular.ttf",
    NULL,
};

static const char *pick_font(const char *want) {
    if (want && want[0]) {
        FILE *f = fopen(want, "rb");
        if (f) {
            fclose(f);
            return want;
        }
        return NULL;
    }
    const char *env = getenv("FASTNOTE_FONT");
    if (env && env[0]) {
        FILE *f = fopen(env, "rb");
        if (f) {
            fclose(f);
            return env;
        }
    }
    for (size_t i = 0; font_candidates[i]; i++) {
        FILE *f = fopen(font_candidates[i], "rb");
        if (f) {
            fclose(f);
            return font_candidates[i];
        }
    }
    return NULL;
}

static void cache_free(Font *f) {
    if (f->cache.slots) {
        for (size_t i = 0; i < f->cache.cap; i++) {
            if (f->cache.slots[i].used && f->cache.slots[i].tex) {
                SDL_DestroyTexture(f->cache.slots[i].tex);
                f->cache.slots[i].tex = NULL;
            }
        }
    }
    free(f->cache.slots);
    f->cache.slots = NULL;
    f->cache.cap = f->cache.count = 0;
    if (f->atlas) {
        SDL_DestroyTexture(f->atlas);
        f->atlas = NULL;
    }
    f->pack_x = f->pack_y = f->pack_row_h = 0;
}

static bool cache_alloc(Font *f) {
    cache_free(f);
    f->cache.slots = calloc(GLYPH_CACHE_CAP, sizeof(Glyph));
    if (!f->cache.slots)
        return false;
    f->cache.cap = GLYPH_CACHE_CAP;
    f->cache.count = 0;
    return true;
}

// Drop everything when the table fills: simple, bounded, and effective
// since real documents use far fewer distinct codepoints than the cap.
static bool cache_evict_all(Font *f) {
    return cache_alloc(f);
}

bool font_init(Font *f, SDL_Renderer *ren, const char *path, int px) {
    memset(f, 0, sizeof(*f));
    f->renderer = ren;
    f->px = px;
    for (int i = 0; i < 8; i++)
        f->palette[i] = (SDL_Color){0xFF, 0xFF, 0xFF, 0xFF};
    // Backend choice (measured): the software renderer blits tight
    // textures ~6x faster than atlas subrects (cache-friendly rows);
    // GPU renderers prefer one shared texture (no binds, auto-batching).
    f->use_atlas = true;
    const char *rname = SDL_GetRendererName(ren);
    if (rname && strcmp(rname, "software") == 0)
        f->use_atlas = false;

    const char *found = pick_font(path);
    if (!found)
        return false;
    snprintf(f->path, sizeof(f->path), "%s", found);

    FT_Library ft = NULL;
    if (FT_Init_FreeType(&ft) != 0)
        return false;
    f->ft = ft;

    FT_Face face = NULL;
    if (FT_New_Face(ft, f->path, 0, &face) != 0) {
        FT_Done_FreeType(ft);
        f->ft = NULL;
        return false;
    }
    f->face = face;

    if (!cache_alloc(f)) {
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        f->face = f->ft = NULL;
        return false;
    }
    // font_set_size fills in line_h/asc; on failure clean up.
    if (!font_set_size(f, px)) {
        cache_free(f);
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        f->face = f->ft = NULL;
        return false;
    }
    return true;
}

void font_quit(Font *f) {
    cache_free(f);
    if (f->face)
        FT_Done_Face((FT_Face)f->face);
    if (f->ft)
        FT_Done_FreeType((FT_Library)f->ft);
    memset(f, 0, sizeof(*f));
}

bool font_set_size(Font *f, int px) {
    if (px <= 0)
        return false;
    FT_Face face = (FT_Face)f->face;
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px) != 0)
        return false;
    f->px = px;
    int h = (int)(face->size->metrics.height >> 6);
    if (h < px)
        h = px;
    if (h <= 0)
        return false;
    f->line_h = h + 2; // slight leading so lines don't touch
    f->asc = (int)(face->size->metrics.ascender >> 6) + 1;
    // New metrics invalidate every cached texture.
    return cache_alloc(f);
}

void font_set_palette(Font *f, const SDL_Color palette[8]) {
    for (int i = 0; i < 8; i++)
        f->palette[i] = palette[i];
    // Baked colors changed: drop rasterized glyphs (metrics reload lazily).
    cache_alloc(f);
}

// Rasterize a FreeType bitmap (8-bit gray) into the atlas at a shelf-packed
// position, pre-colored with palette[color]. Returns false when the glyph
// can never fit (drawn as blank instead).
static bool atlas_place(Font *f, FT_Bitmap *bm, uint8_t color, int *dx_out,
                        int *dy_out) {
    int w = (int)bm->width, h = (int)bm->rows;
    if (w <= 0 || h <= 0 || w + 2 * ATLAS_PAD > ATLAS_SIZE ||
        h + 2 * ATLAS_PAD > ATLAS_SIZE)
        return false;
    if (f->pack_x + w + ATLAS_PAD > ATLAS_SIZE) {
        f->pack_x = ATLAS_PAD;
        f->pack_y += f->pack_row_h;
        f->pack_row_h = 0;
    }
    if (f->pack_y + h + ATLAS_PAD > ATLAS_SIZE) {
        // Atlas full: drop everything, restart packing. Stale entries
        // re-rasterize on demand via the generation check in font_get.
        f->atlas_gen++;
        if (f->atlas) {
            SDL_DestroyTexture(f->atlas);
            f->atlas = NULL;
        }
        f->pack_x = f->pack_y = f->pack_row_h = ATLAS_PAD;
    }
    if (!f->atlas) {
        f->atlas = SDL_CreateTexture(f->renderer, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STATIC, ATLAS_SIZE,
                                     ATLAS_SIZE);
        if (!f->atlas)
            return false;
        SDL_SetTextureBlendMode(f->atlas, SDL_BLENDMODE_BLEND);
    }
    int dx = f->pack_x, dy = f->pack_y;
    // Expand coverage bytes to pre-colored RGBA for the upload.
    SDL_Color c = f->palette[color & 7];
    size_t np = (size_t)w * (size_t)h;
    uint8_t *rgba = malloc(np * 4);
    if (!rgba)
        return false;
    for (int y = 0; y < h; y++) {
        uint8_t *src = bm->buffer + (size_t)y * (size_t)bm->pitch;
        uint8_t *dst = rgba + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            dst[4 * x + 0] = c.r;
            dst[4 * x + 1] = c.g;
            dst[4 * x + 2] = c.b;
            dst[4 * x + 3] = src[x];
        }
    }
    SDL_Rect dst = {dx, dy, w, h};
    bool ok = SDL_UpdateTexture(f->atlas, &dst, rgba, w * 4);
    free(rgba);
    if (!ok)
        return false;
    f->pack_x += w + ATLAS_PAD;
    if (h + ATLAS_PAD > f->pack_row_h)
        f->pack_row_h = h + ATLAS_PAD;
    *dx_out = dx;
    *dy_out = dy;
    return true;
}

// Rasterize (cp, color) into slot g (fresh or stale generation).
static const Glyph *place_glyph(Font *f, Glyph *g, uint32_t cp,
                                uint8_t color) {
    FT_Face face = (FT_Face)f->face;
    if (FT_Load_Char(face, (FT_ULong)cp, FT_LOAD_RENDER) != 0) {
        // Unrenderable: substitute the replacement character once.
        if (cp == 0xFFFDu)
            return NULL;
        return font_get(f, 0xFFFDu, color);
    }
    FT_GlyphSlot s = face->glyph;
    g->codepoint = cp;
    g->color = color;
    g->w = (int)s->bitmap.width;
    g->h = (int)s->bitmap.rows;
    g->bx = s->bitmap_left;
    g->by = s->bitmap_top;
    g->adv = (int)(s->advance.x >> 6);
    g->sx = g->sy = 0;
    if (g->tex) {
        SDL_DestroyTexture(g->tex);
        g->tex = NULL;
    }
    g->gen = 0; // invalid until placed below
    g->used = true;
    if (g->w > 0 && g->h > 0) {
        if (f->use_atlas) {
            int dx, dy;
            if (atlas_place(f, &s->bitmap, color, &dx, &dy)) {
                g->sx = dx;
                g->sy = dy;
                g->gen = f->atlas_gen;
            }
            // else: oversized/failed glyph keeps its advance, draws blank.
        } else {
            g->tex = rasterize_tight(f, &s->bitmap, color);
            // NULL tex still usable (advance only).
        }
    }
    return g;
}

static size_t glyph_key(uint32_t cp, uint8_t color) {
    return (size_t)(cp * 2654435761u) ^ (size_t)((uint32_t)color * 0x9E3779B9u);
}

const Glyph *font_get(Font *f, uint32_t cp, uint8_t color) {
    color &= 7;
    // Linear probe. Entries are never deleted (only whole-cache clears),
    // so the first empty slot ends the chain for any key.
    GlyphCache *c = &f->cache;
    size_t mask = c->cap - 1; // cap is a power of two
    size_t i = glyph_key(cp, color) & mask;
    size_t free_i = c->cap; // sentinel: no free slot seen
    for (size_t n = 0; n < c->cap; n++) {
        Glyph *g = &c->slots[i];
        if (g->used) {
            if (g->codepoint == cp && g->color == color) {
                // Atlas may have been evicted underneath: re-rasterize
                // stale generations in place.
                if (g->w > 0 && g->h > 0 && g->gen != f->atlas_gen)
                    return place_glyph(f, g, cp, color);
                return g;
            }
        } else {
            free_i = i;
            break;
        }
        i = (i + 1) & mask;
    }
    if (free_i == c->cap) {
        // Table completely full (practically unreachable): drop all, retry.
        if (!cache_evict_all(f))
            return NULL;
        c = &f->cache;
        free_i = glyph_key(cp, color) & (c->cap - 1);
    }
    Glyph *g = &c->slots[free_i];
    const Glyph *placed = place_glyph(f, g, cp, color);
    if (placed)
        c->count++;
    return placed;
}

// Tight per-glyph texture for the software backend (cache-friendly rows).
// SDL_PIXELFORMAT_RGBA32 guarantees R,G,B,A byte order in memory.
static SDL_Texture *rasterize_tight(Font *f, FT_Bitmap *bm, uint8_t color) {
    int w = (int)bm->width, h = (int)bm->rows;
    SDL_Surface *sf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!sf)
        return NULL;
    SDL_Color c = f->palette[color & 7];
    uint8_t *px = (uint8_t *)sf->pixels;
    for (int y = 0; y < h; y++) {
        uint8_t *row = px + (size_t)y * (size_t)sf->pitch;
        uint8_t *src = bm->buffer + (size_t)y * (size_t)bm->pitch;
        for (int x = 0; x < w; x++) {
            row[4 * x + 0] = c.r;
            row[4 * x + 1] = c.g;
            row[4 * x + 2] = c.b;
            row[4 * x + 3] = src[x];
        }
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(f->renderer, sf);
    SDL_DestroySurface(sf);
    if (tex)
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

// A glyph draws only when its backing store is live.
static bool glyph_live(const Font *f, const Glyph *g) {
    if (!g || g->w <= 0 || g->h <= 0)
        return false;
    if (f->use_atlas)
        return g->gen == f->atlas_gen && f->atlas;
    return g->tex != NULL;
}

void font_draw_glyph(SDL_Renderer *ren, Font *f, const Glyph *g, float pen_x,
                     float baseline_y) {
    if (!glyph_live(f, g))
        return;
    SDL_FRect dst = {pen_x + (float)g->bx, baseline_y - (float)g->by,
                     (float)g->w, (float)g->h};
    if (f->use_atlas) {
        SDL_FRect src = {(float)g->sx, (float)g->sy, (float)g->w,
                         (float)g->h};
        SDL_RenderTexture(ren, f->atlas, &src, &dst);
    } else {
        SDL_RenderTexture(ren, g->tex, NULL, &dst);
    }
}

int font_tab_width(Font *f, int cols) {
    const Glyph *sp = font_get(f, (uint32_t)' ', GCOL_FG);
    int adv = (sp && sp->adv > 0) ? sp->adv : f->px / 2;
    if (adv <= 0)
        adv = 1;
    return adv * cols;
}

int font_text_width(Font *f, const char *s, size_t n, int tab_w) {
    return font_text_width_max(f, s, n, tab_w, 0x7FFFFFFF);
}

int font_text_width_max(Font *f, const char *s, size_t n, int tab_w,
                        int max_x) {
    int x = 0;
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        size_t k = utf8_decode(s + i, n - i, &cp);
        if (k == 0)
            break;
        if (cp == '\t') {
            x = ((x / tab_w) + 1) * tab_w;
        } else {
            const Glyph *g = font_get(f, cp, GCOL_FG);
            if (g)
                x += g->adv;
        }
        if (x > max_x)
            break;
        i += k;
    }
    return x;
}

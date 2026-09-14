#include "font.h"
#include "text_buffer.h" // utf8_decode

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define GLYPH_CACHE_CAP 2048

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
    if (!f->cache.slots)
        return;
    for (size_t i = 0; i < f->cache.cap; i++) {
        if (f->cache.slots[i].used && f->cache.slots[i].tex)
            SDL_DestroyTexture(f->cache.slots[i].tex);
    }
    free(f->cache.slots);
    f->cache.slots = NULL;
    f->cache.cap = f->cache.count = 0;
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

static size_t glyph_hash(uint32_t cp) {
    return (size_t)(cp * 2654435761u);
}

bool font_init(Font *f, SDL_Renderer *ren, const char *path, int px) {
    memset(f, 0, sizeof(*f));
    f->renderer = ren;
    f->px = px;

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

// Rasterize a FreeType bitmap (8-bit gray) into a white RGBA texture; the
// drawing color is applied later via SDL_SetTextureColorMod so one cached
// glyph serves any theme color. SDL_PIXELFORMAT_RGBA32 guarantees R,G,B,A
// byte order in memory.
static SDL_Texture *rasterize(Font *f, FT_Bitmap *bm) {
    int w = (int)bm->width, h = (int)bm->rows;
    SDL_Surface *sf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!sf)
        return NULL;
    uint8_t *px = (uint8_t *)sf->pixels;
    for (int y = 0; y < h; y++) {
        uint8_t *row = px + (size_t)y * (size_t)sf->pitch;
        uint8_t *src = bm->buffer + (size_t)y * (size_t)bm->pitch;
        for (int x = 0; x < w; x++) {
            row[4 * x + 0] = 0xFF;
            row[4 * x + 1] = 0xFF;
            row[4 * x + 2] = 0xFF;
            row[4 * x + 3] = src[x];
        }
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(f->renderer, sf);
    SDL_DestroySurface(sf);
    if (tex)
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

void font_draw_glyph(SDL_Renderer *ren, Font *f, const Glyph *g, float pen_x,
                     float baseline_y, SDL_Color color) {
    (void)f;
    if (!g || !g->tex)
        return;
    SDL_SetTextureColorMod(g->tex, color.r, color.g, color.b);
    SDL_FRect dst = {pen_x + (float)g->bx, baseline_y - (float)g->by,
                     (float)g->w, (float)g->h};
    SDL_RenderTexture(ren, g->tex, NULL, &dst);
}

const Glyph *font_get(Font *f, uint32_t cp) {
    // Linear probe. Entries are never deleted (only whole-cache clears),
    // so the first empty slot ends the chain for any key.
    GlyphCache *c = &f->cache;
    size_t mask = c->cap - 1; // cap is a power of two
    size_t i = glyph_hash(cp) & mask;
    size_t free_i = c->cap; // sentinel: no free slot seen
    for (size_t n = 0; n < c->cap; n++) {
        Glyph *g = &c->slots[i];
        if (g->used) {
            if (g->codepoint == cp)
                return g;
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
        free_i = glyph_hash(cp) & (c->cap - 1);
    }
    Glyph *g = &c->slots[free_i];
    FT_Face face = (FT_Face)f->face;
    if (FT_Load_Char(face, (FT_ULong)cp, FT_LOAD_RENDER) != 0) {
        // Unrenderable: substitute the replacement character once.
        if (cp == 0xFFFDu)
            return NULL;
        return font_get(f, 0xFFFDu);
    }
    FT_GlyphSlot s = face->glyph;
    g->codepoint = cp;
    g->w = (int)s->bitmap.width;
    g->h = (int)s->bitmap.rows;
    g->bx = s->bitmap_left;
    g->by = s->bitmap_top;
    g->adv = (int)(s->advance.x >> 6);
    g->tex = NULL;
    g->used = true;
    if (g->w > 0 && g->h > 0)
        g->tex = rasterize(f, &s->bitmap); // NULL tex still usable
    c->count++;
    return g;
}

int font_tab_width(Font *f, int cols) {
    const Glyph *sp = font_get(f, (uint32_t)' ');
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
            const Glyph *g = font_get(f, cp);
            if (g)
                x += g->adv;
        }
        if (x > max_x)
            break;
        i += k;
    }
    return x;
}

#include "text_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TB_INIT_CAP 4096
#define TB_INIT_LCAP 128
// Refuse to load absurd files instead of becoming catastrophically slow.
#define TB_MAX_FILE (512u * 1024u * 1024u)

bool tb_init(TextBuffer *tb) {
    tb->data = malloc(TB_INIT_CAP);
    tb->lines = malloc(TB_INIT_LCAP * sizeof(size_t));
    tb->lstate = calloc(TB_INIT_LCAP, 1);
    if (!tb->data || !tb->lines || !tb->lstate) {
        free(tb->data);
        free(tb->lines);
        free(tb->lstate);
        tb->data = NULL;
        tb->lines = NULL;
        tb->lstate = NULL;
        return false;
    }
    tb->len = 0;
    tb->cap = TB_INIT_CAP;
    tb->lines[0] = 0;
    tb->nlines = 1;
    tb->lcap = TB_INIT_LCAP;
    return true;
}

void tb_free(TextBuffer *tb) {
    free(tb->data);
    free(tb->lines);
    free(tb->lstate);
    tb->data = NULL;
    tb->lines = NULL;
    tb->lstate = NULL;
    tb->len = tb->cap = tb->nlines = tb->lcap = 0;
}

bool tb_clear(TextBuffer *tb) {
    tb->len = 0;
    tb->nlines = 1;
    if (tb->lcap == 0) {
        tb->lines = malloc(TB_INIT_LCAP * sizeof(size_t));
        tb->lstate = calloc(TB_INIT_LCAP, 1);
        if (!tb->lines || !tb->lstate) {
            free(tb->lines);
            free(tb->lstate);
            tb->lines = NULL;
            tb->lstate = NULL;
            return false;
        }
        tb->lcap = TB_INIT_LCAP;
    }
    tb->lines[0] = 0;
    tb->lstate[0] = 0; // fresh document starts outside any block construct
    return true;
}

static bool tb_reserve(TextBuffer *tb, size_t extra) {
    if (tb->len + extra <= tb->cap)
        return true;
    size_t ncap = tb->cap ? tb->cap : 64;
    while (ncap < tb->len + extra) {
        ncap *= 2;
        if (ncap > TB_MAX_FILE + 1)
            return false;
    }
    char *nd = realloc(tb->data, ncap);
    if (!nd)
        return false;
    tb->data = nd;
    tb->cap = ncap;
    return true;
}

static bool tb_reserve_lines(TextBuffer *tb, size_t extra) {
    if (tb->nlines + extra <= tb->lcap)
        return true;
    size_t ncap = tb->lcap ? tb->lcap : 64;
    while (ncap < tb->nlines + extra)
        ncap *= 2;
    size_t *nl = realloc(tb->lines, ncap * sizeof(size_t));
    if (!nl)
        return false;
    tb->lines = nl;
    // lstate grows in lockstep (new slots zeroed; stale values beyond the
    // editor's hl_clean are recomputed on demand).
    if (!tb->lstate) {
        tb->lstate = calloc(ncap, 1);
        if (!tb->lstate)
            return false;
    } else {
        uint8_t *ns = realloc(tb->lstate, ncap);
        if (!ns)
            return false;
        memset(ns + tb->lcap, 0, ncap - tb->lcap);
        tb->lstate = ns;
    }
    tb->lcap = ncap;
    return true;
}

size_t tb_line_of(const TextBuffer *tb, size_t pos) {
    if (pos > tb->len)
        pos = tb->len;
    // Upper bound: greatest i with lines[i] <= pos.
    size_t lo = 0, hi = tb->nlines;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (tb->lines[mid] <= pos)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

size_t tb_line_start(const TextBuffer *tb, size_t line) {
    if (line >= tb->nlines)
        return tb->len;
    return tb->lines[line];
}

size_t tb_line_len(const TextBuffer *tb, size_t line) {
    if (line >= tb->nlines)
        return 0;
    size_t start = tb->lines[line];
    size_t end = (line + 1 < tb->nlines) ? tb->lines[line + 1] : tb->len;
    if (end > start && tb->data[end - 1] == '\n')
        end--;
    return end - start;
}

bool tb_insert(TextBuffer *tb, size_t pos, const char *s, size_t n) {
    if (pos > tb->len || (n > 0 && s == NULL))
        return false;
    if (n == 0)
        return true;
    // Count new lines first so the index update needs one reservation.
    size_t nl = 0;
    for (size_t i = 0; i < n; i++)
        if (s[i] == '\n')
            nl++;
    if (!tb_reserve(tb, n) || !tb_reserve_lines(tb, nl))
        return false;
    memmove(tb->data + pos + n, tb->data + pos, tb->len - pos);
    memcpy(tb->data + pos, s, n);
    tb->len += n;

    size_t li = tb_line_of(tb, pos); // valid: index not yet updated below pos
    // Shift later line starts. Note tb_line_of was computed on the old
    // index which is still intact here (we only append so far).
    if (tb->nlines > li + 1) {
        memmove(tb->lines + li + 1 + nl, tb->lines + li + 1,
                (tb->nlines - li - 1) * sizeof(size_t));
        if (tb->lstate)
            memmove(tb->lstate + li + 1 + nl, tb->lstate + li + 1,
                    tb->nlines - li - 1);
    }
    for (size_t i = li + 1; i < tb->nlines; i++)
        tb->lines[i + nl] += n;
    // Record new line starts in order.
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n')
            tb->lines[li + 1 + k++] = pos + i + 1;
    }
    tb->nlines += nl;
    return true;
}

void tb_erase(TextBuffer *tb, size_t pos, size_t n) {
    if (pos >= tb->len || n == 0)
        return;
    if (n > tb->len - pos)
        n = tb->len - pos;
    size_t l1 = tb_line_of(tb, pos);
    size_t l2 = tb_line_of(tb, pos + n);
    memmove(tb->data + pos, tb->data + pos + n, tb->len - pos - n);
    tb->len -= n;
    if (l2 > l1) {
        // Drop line starts strictly inside (pos, pos+n].
        memmove(tb->lines + l1 + 1, tb->lines + l2 + 1,
                (tb->nlines - l2 - 1) * sizeof(size_t));
        if (tb->lstate)
            memmove(tb->lstate + l1 + 1, tb->lstate + l2 + 1,
                    tb->nlines - l2 - 1);
        tb->nlines -= (l2 - l1);
    }
    for (size_t i = l1 + 1; i < tb->nlines; i++)
        tb->lines[i] -= n;
}

// --- UTF-8 ---

size_t utf8_decode(const char *s, size_t maxlen, uint32_t *cp) {
    static const uint32_t repl = 0xFFFDu;
    if (maxlen == 0) {
        *cp = repl;
        return 0;
    }
    uint8_t c0 = (uint8_t)s[0];
    if (c0 < 0x80u) {
        *cp = c0;
        return 1;
    }
    size_t want = 0;
    uint32_t min = 0, acc = 0;
    if ((c0 & 0xE0u) == 0xC0u) {
        want = 2;
        min = 0x80u;
        acc = (uint32_t)(c0 & 0x1Fu);
    } else if ((c0 & 0xF0u) == 0xE0u) {
        want = 3;
        min = 0x800u;
        acc = (uint32_t)(c0 & 0x0Fu);
    } else if ((c0 & 0xF8u) == 0xF0u) {
        want = 4;
        min = 0x10000u;
        acc = (uint32_t)(c0 & 0x07u);
    } else {
        *cp = repl; // stray continuation or 0xFE/0xFF
        return 1;
    }
    if (want > maxlen) {
        *cp = repl; // truncated sequence: consume one byte, resync after
        return 1;
    }
    for (size_t i = 1; i < want; i++) {
        uint8_t c = (uint8_t)s[i];
        if ((c & 0xC0u) != 0x80u) {
            *cp = repl;
            return 1;
        }
        acc = (acc << 6) | (uint32_t)(c & 0x3Fu);
    }
    if (acc < min || acc > 0x10FFFFu || (acc >= 0xD800u && acc <= 0xDFFFu)) {
        *cp = repl; // overlong, out of range, or surrogate
        return 1;
    }
    *cp = acc;
    return want;
}

size_t utf8_prev_start(const char *s, size_t pos) {
    if (pos == 0)
        return 0;
    size_t p = pos - 1;
    // Back up over continuation bytes (max 3 for valid text; more if the
    // input was already sanitized this never triggers, but stay safe).
    size_t guard = 0;
    while (p > 0 && utf8_is_cont((uint8_t)s[p]) && guard < 4) {
        p--;
        guard++;
    }
    return p;
}

size_t utf8_count(const char *s, size_t n) {
    size_t c = 0, i = 0;
    while (i < n) {
        uint32_t cp;
        size_t w = utf8_decode(s + i, n - i, &cp);
        if (w == 0)
            break;
        i += w;
        c++;
    }
    return c;
}

// --- File I/O ---

// One input unit: how many bytes to consume, what to emit.
typedef struct {
    size_t consume; // input bytes (always >= 1)
    size_t emit;    // output bytes (1, k for valid sequences, 3 for U+FFFD)
    int newlines;   // lines added (0 or 1)
    bool copy;      // copy input bytes verbatim (false: emit '\n' or U+FFFD)
} Span;

static Span classify_span(const char *s, size_t n) {
    uint8_t c = (uint8_t)s[0];
    if (c == '\r') {
        bool crlf = n > 1 && s[1] == '\n';
        return (Span){crlf ? 2 : 1, 1, 1, false};
    }
    if (c == '\n' || c < 0x80u)
        return (Span){1, 1, c == '\n' ? 1 : 0, true};
    uint32_t cp;
    size_t k = utf8_decode(s, n, &cp);
    if (k == 0)
        k = 1; // cannot happen (n > 0), but never stall
    bool genuine_repl = (k == 3 && n >= 3 && (uint8_t)s[0] == 0xEF &&
                         (uint8_t)s[1] == 0xBF && (uint8_t)s[2] == 0xBD);
    if (cp != 0xFFFDu || genuine_repl)
        return (Span){k, k, 0, true};
    return (Span){k, 3, 0, false}; // emit U+FFFD
}

// Copy src into the buffer while normalizing line endings and repairing
// invalid UTF-8. Two passes (measure, then transform) so large files only
// ever hold ~2x transient (raw + output) instead of 4x. Returns false on OOM.
static bool tb_set_sanitized(TextBuffer *tb, const char *src, size_t n) {
    if (!tb_clear(tb))
        return false;
    // Pass 1: exact output size + newline count.
    size_t out = 0, nl = 0, i = 0;
    while (i < n) {
        Span sp = classify_span(src + i, n - i);
        out += sp.emit;
        nl += (size_t)sp.newlines;
        i += sp.consume;
    }
    if (!tb_reserve(tb, out + 1) || !tb_reserve_lines(tb, nl))
        return false;
    // Pass 2: transform.
    static const char repl[3] = {(char)0xEF, (char)0xBF, (char)0xBD};
    size_t w = 0;
    i = 0;
    char *d = tb->data;
    while (i < n) {
        Span sp = classify_span(src + i, n - i);
        if (sp.copy) {
            memcpy(d + w, src + i, sp.emit);
        } else if (sp.newlines) {
            d[w] = '\n';
        } else {
            memcpy(d + w, repl, 3);
        }
        w += sp.emit;
        i += sp.consume;
    }
    tb->len = w;
    // Rebuild line index (exact space was pre-reserved above).
    tb->nlines = 1;
    for (size_t j = 0; j < w; j++) {
        if (d[j] == '\n')
            tb->lines[tb->nlines++] = j + 1;
    }
    // Growth during load can leave ~2x slack; shrink to fit so a 100 MB
    // file doesn't pin ~200+ MB. Shrinking never fails fatally: on NULL
    // we simply keep the larger buffers.
    if (tb->cap > tb->len + 1024 && tb->len > 0) {
        char *nd = realloc(tb->data, tb->len);
        if (nd) {
            tb->data = nd;
            tb->cap = tb->len;
        }
    }
    if (tb->lcap > tb->nlines + 16) {
        size_t *shrunk = realloc(tb->lines, tb->nlines * sizeof(size_t));
        if (shrunk) {
            tb->lines = shrunk;
            tb->lcap = tb->nlines;
        }
    }
    return true;
}

bool tb_load(TextBuffer *tb, const char *path, char *err, size_t errcap) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err)
            snprintf(err, errcap, "Cannot open file");
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        if (err)
            snprintf(err, errcap, "Cannot seek file");
        fclose(f);
        return false;
    }
    long sz = ftell(f);
    if (sz < 0) {
        if (err)
            snprintf(err, errcap, "Cannot tell file size");
        fclose(f);
        return false;
    }
    if ((size_t)sz > TB_MAX_FILE) {
        if (err)
            snprintf(err, errcap, "File too large (>512 MB)");
        fclose(f);
        return false;
    }
    rewind(f);
    char *raw = NULL;
    if (sz > 0) {
        raw = malloc((size_t)sz);
        if (!raw) {
            if (err)
                snprintf(err, errcap, "Out of memory");
            fclose(f);
            return false;
        }
        size_t got = fread(raw, 1, (size_t)sz, f);
        if (got != (size_t)sz) {
            if (err)
                snprintf(err, errcap, "Error reading file");
            free(raw);
            fclose(f);
            return false;
        }
    }
    fclose(f);
    // Strip a UTF-8 BOM if present.
    size_t off = 0;
    if (sz >= 3 && (uint8_t)raw[0] == 0xEF && (uint8_t)raw[1] == 0xBB &&
        (uint8_t)raw[2] == 0xBF)
        off = 3;
    bool ok = tb_set_sanitized(tb, raw ? raw + off : NULL,
                               sz > 0 ? (size_t)sz - off : 0);
    free(raw);
    if (!ok && err)
        snprintf(err, errcap, "Out of memory");
    return ok;
}

bool tb_save(const TextBuffer *tb, const char *path, char *err, size_t errcap) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        if (err)
            snprintf(err, errcap, "Cannot open file for writing");
        return false;
    }
    // Buffered fwrite in chunks keeps huge saves reasonably fast.
    static const size_t CHUNK = 1 << 20;
    size_t off = 0;
    while (off < tb->len) {
        size_t n = tb->len - off < CHUNK ? tb->len - off : CHUNK;
        if (fwrite(tb->data + off, 1, n, f) != n) {
            if (err)
                snprintf(err, errcap, "Error writing file");
            fclose(f);
            return false;
        }
        off += n;
    }
    if (fclose(f) != 0) {
        if (err)
            snprintf(err, errcap, "Error closing file");
        return false;
    }
    return true;
}

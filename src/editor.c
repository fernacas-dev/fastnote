#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ED_UNDO_MAX_OPS 2000
#define ED_UNDO_MAX_BYTES (8u * 1024u * 1024u)
#define ED_COALESCE_MAX 512

bool editor_init(Editor *e) {
    memset(e, 0, sizeof(*e));
    return tb_init(&e->buf);
}

static void undo_free_stack(UndoStack *u) {
    for (size_t i = 0; i < u->len; i++)
        free(u->ops[i].text);
    free(u->ops);
    memset(u, 0, sizeof(*u));
}

void editor_quit(Editor *e) {
    tb_free(&e->buf);
    undo_free_stack(&e->undo);
}

void editor_new(Editor *e) {
    tb_clear(&e->buf);
    undo_free_stack(&e->undo);
    e->cursor = e->goal_col = e->anchor = 0;
    e->selecting = false;
    e->scroll_line = 0;
    e->scroll_x = 0;
    e->path[0] = '\0';
    e->has_path = false;
    e->modified = false;
}

bool editor_load(Editor *e, const char *path, char *err, size_t errcap) {
    if (!tb_load(&e->buf, path, err, errcap))
        return false;
    undo_free_stack(&e->undo);
    e->cursor = e->goal_col = e->anchor = 0;
    e->selecting = false;
    e->scroll_line = 0;
    e->scroll_x = 0;
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->has_path = true;
    e->modified = false;
    return true;
}

bool editor_save(Editor *e, char *err, size_t errcap) {
    if (!e->has_path) {
        if (err)
            snprintf(err, errcap, "No filename (use Save As)");
        return false;
    }
    if (!tb_save(&e->buf, e->path, err, errcap))
        return false;
    e->modified = false;
    return true;
}

bool editor_save_as(Editor *e, const char *path, char *err, size_t errcap) {
    if (!tb_save(&e->buf, path, err, errcap))
        return false;
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->has_path = true;
    e->modified = false;
    return true;
}

// --- Undo stack ---

static void undo_drop_oldest(Editor *e) {
    UndoStack *u = &e->undo;
    if (u->len == 0)
        return;
    u->bytes -= u->ops[0].len;
    free(u->ops[0].text);
    memmove(u->ops, u->ops + 1, (u->len - 1) * sizeof(UndoOp));
    u->len--;
    if (u->pos > 0)
        u->pos--;
}

// Make room for a new op: discard redo tail, enforce bounds.
static bool undo_prepare(Editor *e) {
    UndoStack *u = &e->undo;
    for (size_t i = u->pos; i < u->len; i++)
        free(u->ops[i].text);
    size_t kept = 0;
    for (size_t i = 0; i < u->pos; i++)
        kept += u->ops[i].len;
    u->bytes = kept;
    u->len = u->pos;
    while (u->len >= ED_UNDO_MAX_OPS || u->bytes >= ED_UNDO_MAX_BYTES)
        undo_drop_oldest(e);
    if (u->len == u->cap) {
        size_t ncap = u->cap ? u->cap * 2 : 64;
        UndoOp *nop = realloc(u->ops, ncap * sizeof(UndoOp));
        if (!nop)
            return false;
        u->ops = nop;
        u->cap = ncap;
    }
    return true;
}

static bool undo_push(Editor *e, size_t pos, const char *text, size_t len,
                      bool is_insert) {
    if (!undo_prepare(e))
        return false;
    UndoStack *u = &e->undo;
    char *cpy = NULL;
    if (len > 0) {
        cpy = malloc(len);
        if (!cpy)
            return false;
        memcpy(cpy, text, len);
    }
    UndoOp *op = &u->ops[u->len++];
    op->pos = pos;
    op->text = cpy;
    op->len = len;
    op->is_insert = is_insert;
    u->bytes += len;
    u->pos = u->len;
    return true;
}

// Merge typing/backspacing into the previous op when adjacent.
static bool undo_coalesce_insert(Editor *e, size_t pos, const char *s, size_t n) {
    UndoStack *u = &e->undo;
    if (u->pos == 0 || u->pos != u->len)
        return false;
    UndoOp *op = &u->ops[u->len - 1];
    if (!op->is_insert || op->pos + op->len != pos)
        return false;
    if (op->len + n > ED_COALESCE_MAX)
        return false;
    // Redo tail must be empty when pos == len; truncate defensively.
    char *ntext = realloc(op->text, op->len + n);
    if (!ntext)
        return false;
    memcpy(ntext + op->len, s, n);
    op->text = ntext;
    op->len += n;
    u->bytes += n;
    return true;
}

static bool undo_coalesce_erase(Editor *e, size_t pos, const char *s, size_t n,
                                bool prepend) {
    UndoStack *u = &e->undo;
    if (u->pos == 0 || u->pos != u->len)
        return false;
    UndoOp *op = &u->ops[u->len - 1];
    if (op->is_insert)
        return false;
    if (prepend) { // backspace: new bytes go before
        if (op->pos != pos + n || op->len + n > ED_COALESCE_MAX)
            return false;
        char *ntext = malloc(op->len + n);
        if (!ntext)
            return false;
        memcpy(ntext, s, n);
        memcpy(ntext + n, op->text, op->len);
        free(op->text);
        op->text = ntext;
        op->pos = pos;
        op->len += n;
    } else { // forward delete: append
        if (op->pos != pos || op->len + n > ED_COALESCE_MAX)
            return false;
        char *ntext = realloc(op->text, op->len + n);
        if (!ntext)
            return false;
        memcpy(ntext + op->len, s, n);
        op->text = ntext;
        op->len += n;
    }
    u->bytes += n;
    return true;
}

bool editor_can_undo(const Editor *e) { return e->undo.pos > 0; }
bool editor_can_redo(const Editor *e) { return e->undo.pos < e->undo.len; }

bool editor_undo(Editor *e) {
    UndoStack *u = &e->undo;
    if (u->pos == 0)
        return false;
    UndoOp *op = &u->ops[u->pos - 1];
    if (op->is_insert) {
        tb_erase(&e->buf, op->pos, op->len);
        e->cursor = op->pos;
    } else {
        if (!tb_insert(&e->buf, op->pos, op->text ? op->text : "", op->len))
            return false;
        e->cursor = op->pos + op->len;
    }
    u->pos--;
    e->selecting = false;
    e->anchor = e->cursor;
    e->goal_col = editor_col_of(e, tb_line_of(&e->buf, e->cursor), e->cursor);
    e->modified = true;
    return true;
}

bool editor_redo(Editor *e) {
    UndoStack *u = &e->undo;
    if (u->pos >= u->len)
        return false;
    UndoOp *op = &u->ops[u->pos];
    if (op->is_insert) {
        if (!tb_insert(&e->buf, op->pos, op->text ? op->text : "", op->len))
            return false;
        e->cursor = op->pos + op->len;
    } else {
        tb_erase(&e->buf, op->pos, op->len);
        e->cursor = op->pos;
    }
    u->pos++;
    e->selecting = false;
    e->anchor = e->cursor;
    e->goal_col = editor_col_of(e, tb_line_of(&e->buf, e->cursor), e->cursor);
    e->modified = true;
    return true;
}

// --- Selection ---

bool editor_has_selection(const Editor *e) {
    return e->selecting && e->anchor != e->cursor;
}

void editor_selection_range(const Editor *e, size_t *a, size_t *b) {
    if (e->anchor < e->cursor) {
        *a = e->anchor;
        *b = e->cursor;
    } else {
        *a = e->cursor;
        *b = e->anchor;
    }
}

char *editor_selection_text(const Editor *e) {
    size_t a, b;
    editor_selection_range(e, &a, &b);
    if (b <= a)
        return NULL;
    char *out = malloc(b - a + 1);
    if (!out)
        return NULL;
    memcpy(out, e->buf.data + a, b - a);
    out[b - a] = '\0';
    return out;
}

void editor_select_all(Editor *e) {
    e->anchor = 0;
    e->cursor = e->buf.len;
    e->selecting = true;
    e->goal_col = 0;
}

void editor_clear_selection(Editor *e) {
    e->selecting = false;
    e->anchor = e->cursor;
}

// --- Primitive edit application (buffer + undo + cursor) ---

static void after_edit(Editor *e) {
    e->modified = true;
    e->selecting = false;
    e->anchor = e->cursor;
    e->goal_col = editor_col_of(e, tb_line_of(&e->buf, e->cursor), e->cursor);
}

// Delete selection without recording undo (used when the caller records a
// combined op). Returns the deleted range.
static void erase_range_raw(Editor *e, size_t a, size_t b) {
    tb_erase(&e->buf, a, b - a);
    e->cursor = a;
}

void editor_delete_selection(Editor *e) {
    size_t a, b;
    editor_selection_range(e, &a, &b);
    if (b <= a) {
        editor_clear_selection(e);
        return;
    }
    // Copy first: tb_erase frees nothing but moves memory.
    char *tmp = malloc(b - a);
    bool ok = false;
    if (tmp) {
        memcpy(tmp, e->buf.data + a, b - a);
        erase_range_raw(e, a, b);
        ok = undo_push(e, a, tmp, b - a, false);
        free(tmp);
    }
    (void)ok;
    after_edit(e);
}

// Normalize pasted/typed text: CRLF/CR -> LF, strip NUL bytes.
static char *normalize_input(const char *s, size_t n, size_t *out_n) {
    char *d = malloc(n + 1);
    if (!d)
        return NULL;
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\0')
            continue;
        if (c == '\r') {
            d[w++] = '\n';
            if (i + 1 < n && s[i + 1] == '\n')
                i++;
        } else {
            d[w++] = c;
        }
    }
    d[w] = '\0';
    *out_n = w;
    return d;
}

bool editor_insert(Editor *e, const char *s, size_t n) {
    if (n == 0)
        return true;
    size_t nn = 0;
    char *norm = normalize_input(s, n, &nn);
    if (!norm)
        return false;
    bool ok = true;
    if (nn > 0) {
        if (editor_has_selection(e))
            editor_delete_selection(e);
        size_t at = e->cursor;
        if (tb_insert(&e->buf, at, norm, nn)) {
            if (!undo_coalesce_insert(e, at, norm, nn))
                undo_push(e, at, norm, nn, true);
            e->cursor = at + nn;
        } else {
            ok = false;
        }
    }
    free(norm);
    if (ok)
        after_edit(e);
    return ok;
}

bool editor_newline(Editor *e) { return editor_insert(e, "\n", 1); }

bool editor_tab(Editor *e) { return editor_insert(e, "\t", 1); }

void editor_backspace(Editor *e) {
    if (editor_has_selection(e)) {
        editor_delete_selection(e);
        return;
    }
    if (e->cursor == 0)
        return;
    size_t prev = utf8_prev_start(e->buf.data, e->cursor);
    size_t n = e->cursor - prev;
    char tmp[4];
    memcpy(tmp, e->buf.data + prev, n);
    tb_erase(&e->buf, prev, n);
    if (!undo_coalesce_erase(e, prev, tmp, n, true))
        undo_push(e, prev, tmp, n, false);
    e->cursor = prev;
    after_edit(e);
}

void editor_delete_fwd(Editor *e) {
    if (editor_has_selection(e)) {
        editor_delete_selection(e);
        return;
    }
    if (e->cursor >= e->buf.len)
        return;
    uint32_t cp;
    size_t k = utf8_decode(e->buf.data + e->cursor, e->buf.len - e->cursor, &cp);
    if (k == 0)
        return;
    char tmp[4];
    memcpy(tmp, e->buf.data + e->cursor, k);
    tb_erase(&e->buf, e->cursor, k);
    if (!undo_coalesce_erase(e, e->cursor, tmp, k, false))
        undo_push(e, e->cursor, tmp, k, false);
    after_edit(e);
}

// --- Motions ---

static void move_to(Editor *e, size_t pos, bool extend) {
    if (pos > e->buf.len)
        pos = e->buf.len;
    if (!extend) {
        e->selecting = false;
        e->anchor = pos;
    } else if (!e->selecting) {
        e->anchor = e->cursor;
        e->selecting = true;
    }
    e->cursor = pos;
    e->goal_col = editor_col_of(e, tb_line_of(&e->buf, pos), pos);
}

void editor_set_cursor(Editor *e, size_t pos, bool extend) {
    move_to(e, pos, extend);
}

void editor_move_left(Editor *e, bool extend) {
    if (e->cursor == 0) {
        move_to(e, 0, extend);
        return;
    }
    move_to(e, utf8_prev_start(e->buf.data, e->cursor), extend);
}

void editor_move_right(Editor *e, bool extend) {
    if (e->cursor >= e->buf.len) {
        move_to(e, e->buf.len, extend);
        return;
    }
    uint32_t cp;
    size_t k = utf8_decode(e->buf.data + e->cursor, e->buf.len - e->cursor, &cp);
    if (k == 0)
        k = 1;
    move_to(e, e->cursor + k, extend);
}

size_t editor_offset_of(const Editor *e, size_t line, size_t col_chars) {
    size_t start = tb_line_start(&e->buf, line);
    size_t llen = tb_line_len(&e->buf, line);
    size_t off = start, col = 0;
    while (off < start + llen && col < col_chars) {
        uint32_t cp;
        size_t k = utf8_decode(e->buf.data + off, start + llen - off, &cp);
        if (k == 0)
            break;
        off += k;
        col++;
    }
    return off;
}

size_t editor_col_of(const Editor *e, size_t line, size_t offset) {
    size_t start = tb_line_start(&e->buf, line);
    if (offset < start)
        return 0;
    return utf8_count(e->buf.data + start, offset - start);
}

void editor_move_up(Editor *e, bool extend) {
    size_t line = tb_line_of(&e->buf, e->cursor);
    if (line == 0) {
        move_to(e, 0, extend);
        return;
    }
    size_t goal = e->goal_col;
    size_t at = editor_offset_of(e, line - 1, goal);
    if (!extend) {
        e->selecting = false;
        e->anchor = at;
    } else if (!e->selecting) {
        e->anchor = e->cursor;
        e->selecting = true;
    }
    e->cursor = at;
    e->goal_col = goal; // preserve column while moving vertically
}

void editor_move_down(Editor *e, bool extend) {
    size_t line = tb_line_of(&e->buf, e->cursor);
    if (line + 1 >= e->buf.nlines) {
        move_to(e, e->buf.len, extend);
        return;
    }
    size_t goal = e->goal_col;
    size_t at = editor_offset_of(e, line + 1, goal);
    if (!extend) {
        e->selecting = false;
        e->anchor = at;
    } else if (!e->selecting) {
        e->anchor = e->cursor;
        e->selecting = true;
    }
    e->cursor = at;
    e->goal_col = goal;
}

void editor_move_home(Editor *e, bool extend) {
    size_t line = tb_line_of(&e->buf, e->cursor);
    move_to(e, tb_line_start(&e->buf, line), extend);
}

void editor_move_end(Editor *e, bool extend) {
    size_t line = tb_line_of(&e->buf, e->cursor);
    size_t start = tb_line_start(&e->buf, line);
    move_to(e, start + tb_line_len(&e->buf, line), extend);
}

static bool is_word_byte(uint8_t c) {
    if (c >= 0x80u)
        return true; // treat non-ASCII as word characters
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static bool is_space_byte(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n';
}

void editor_move_word_left(Editor *e, bool extend) {
    size_t p = e->cursor;
    const char *d = e->buf.data;
    while (p > 0 && is_space_byte((uint8_t)d[p - 1]))
        p = utf8_prev_start(d, p);
    if (p > 0) {
        bool word = is_word_byte((uint8_t)d[utf8_prev_start(d, p)]);
        while (p > 0) {
            size_t q = utf8_prev_start(d, p);
            if (is_word_byte((uint8_t)d[q]) != word)
                break;
            p = q;
        }
    }
    move_to(e, p, extend);
}

void editor_move_word_right(Editor *e, bool extend) {
    size_t p = e->cursor;
    const char *d = e->buf.data;
    size_t len = e->buf.len;
    // Skip the current word/class first (if inside one), then whitespace.
    if (p < len && !is_space_byte((uint8_t)d[p])) {
        bool word = is_word_byte((uint8_t)d[p]);
        while (p < len && !is_space_byte((uint8_t)d[p]) &&
               is_word_byte((uint8_t)d[p]) == word) {
            uint32_t cp;
            size_t k = utf8_decode(d + p, len - p, &cp);
            p += (k == 0) ? 1 : k;
        }
    }
    while (p < len && is_space_byte((uint8_t)d[p])) {
        uint32_t cp;
        size_t k = utf8_decode(d + p, len - p, &cp);
        p += (k == 0) ? 1 : k;
    }
    move_to(e, p, extend);
}

void editor_move_doc_start(Editor *e, bool extend) { move_to(e, 0, extend); }
void editor_move_doc_end(Editor *e, bool extend) { move_to(e, e->buf.len, extend); }

void editor_move_page(Editor *e, int lines, bool extend) {
    size_t line = tb_line_of(&e->buf, e->cursor);
    long target = (long)line + (long)lines;
    if (target < 0)
        target = 0;
    if ((size_t)target >= e->buf.nlines)
        target = (long)(e->buf.nlines - 1);
    size_t at = editor_offset_of(e, (size_t)target, e->goal_col);
    // Page keys also scroll: keep the cursor roughly in place on screen.
    long scroll = (long)e->scroll_line + (long)lines;
    if (scroll < 0)
        scroll = 0;
    e->scroll_line = (size_t)scroll;
    if (!extend) {
        e->selecting = false;
        e->anchor = at;
    } else if (!e->selecting) {
        e->anchor = e->cursor;
        e->selecting = true;
    }
    e->cursor = at;
}

void editor_ensure_visible(Editor *e, size_t visible_lines) {
    if (visible_lines == 0)
        visible_lines = 1;
    size_t line = tb_line_of(&e->buf, e->cursor);
    if (line < e->scroll_line) {
        e->scroll_line = line;
    } else if (line >= e->scroll_line + visible_lines) {
        e->scroll_line = line - visible_lines + 1;
    }
    editor_clamp_scroll(e, visible_lines);
}

void editor_clamp_scroll(Editor *e, size_t visible_lines) {
    size_t max = 0;
    if (e->buf.nlines > visible_lines)
        max = e->buf.nlines - visible_lines;
    if (e->scroll_line > max)
        e->scroll_line = max;
    if (e->scroll_x < 0)
        e->scroll_x = 0;
    if (e->scroll_x > ED_MAX_SCROLL_X)
        e->scroll_x = ED_MAX_SCROLL_X;
}

void editor_line_col(const Editor *e, size_t *line, size_t *col) {
    size_t l = tb_line_of(&e->buf, e->cursor);
    *line = l + 1;
    *col = editor_col_of(e, l, e->cursor) + 1;
}

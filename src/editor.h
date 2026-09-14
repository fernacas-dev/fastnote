#ifndef FASTNOTE_EDITOR_H
#define FASTNOTE_EDITOR_H

#include "text_buffer.h"
#include "highlight.h"

#include <stddef.h>
#include <stdbool.h>

// Horizontal scroll clamp: scrolling past this is meaningless, and it
// bounds per-frame line-walk work for pathological single-line files.
#define ED_MAX_SCROLL_X 100000

// One undoable change. Insert-op: text was inserted at pos (undo = erase).
// Delete-op: text was removed from pos (undo = re-insert).
typedef struct {
    size_t pos;
    char *text;
    size_t len;
    bool is_insert;
} UndoOp;

typedef struct {
    UndoOp *ops;
    size_t len; // total stored (applied + redo tail)
    size_t pos; // number currently applied (undo from pos-1, redo ops[pos])
    size_t cap;
    size_t bytes; // stored text bytes, bounded by ED_UNDO_MAX_BYTES
} UndoStack;

typedef struct {
    TextBuffer buf;
    size_t cursor;   // byte offset into buf
    size_t goal_col; // remembered character column for Up/Down
    size_t anchor;   // selection anchor byte offset
    bool selecting;  // anchor != cursor selection active
    size_t scroll_line;
    int scroll_x;
    char path[1024];
    bool has_path;
    bool modified;
    UndoStack undo;
    // Syntax highlighting: hl_lang selects the tokenizer (HLANG_NONE
    // disables), hl_on is the user toggle, and hl_clean tracks how far the
    // per-line block states in buf.lstate are valid (states[0..hl_clean]).
    // Edits invalidate from the edited line; the renderer recomputes
    // forward with a per-frame budget (see editor_hl_update).
    HlLang hl_lang;
    bool hl_on;
    size_t hl_clean;
} Editor;

bool editor_init(Editor *e);
void editor_quit(Editor *e);

// Document lifecycle.
void editor_new(Editor *e); // empty doc, forgets path/modified/undo
bool editor_load(Editor *e, const char *path, char *err, size_t errcap);
bool editor_save(Editor *e, char *err, size_t errcap);
bool editor_save_as(Editor *e, const char *path, char *err, size_t errcap);

// --- Editing (cursor-based, replace selection first) ---
bool editor_insert(Editor *e, const char *s, size_t n);
bool editor_newline(Editor *e);
bool editor_tab(Editor *e);
void editor_backspace(Editor *e);
void editor_delete_fwd(Editor *e);
void editor_delete_selection(Editor *e);

// --- Selection / clipboard data (SDL-free; app owns the OS clipboard) ---
bool editor_has_selection(const Editor *e);
void editor_selection_range(const Editor *e, size_t *a, size_t *b);
char *editor_selection_text(const Editor *e); // malloc'd, NULL if empty/fail
void editor_select_all(Editor *e);
void editor_clear_selection(Editor *e);

// --- Cursor motions (extend = keep/extend selection) ---
void editor_move_left(Editor *e, bool extend);
void editor_move_right(Editor *e, bool extend);
void editor_move_up(Editor *e, bool extend);
void editor_move_down(Editor *e, bool extend);
void editor_move_home(Editor *e, bool extend);
void editor_move_end(Editor *e, bool extend);
void editor_move_word_left(Editor *e, bool extend);
void editor_move_word_right(Editor *e, bool extend);
void editor_move_doc_start(Editor *e, bool extend);
void editor_move_doc_end(Editor *e, bool extend);
void editor_move_page(Editor *e, int lines, bool extend); // +/- visible lines
void editor_set_cursor(Editor *e, size_t pos, bool extend);

// Keep cursor visible. visible_lines = lines that fit the editor area.
void editor_ensure_visible(Editor *e, size_t visible_lines);
void editor_clamp_scroll(Editor *e, size_t visible_lines);

// 1-based line/column (column in characters) for the status bar.
void editor_line_col(const Editor *e, size_t *line, size_t *col);

// Undo/redo. undo returns false if nothing to undo.
bool editor_undo(Editor *e);
bool editor_redo(Editor *e);
bool editor_can_undo(const Editor *e);
bool editor_can_redo(const Editor *e);

// Extend valid highlight states forward through line need (inclusive),
// spending at most budget lines. Returns true when states[0..need] are
// valid. Stops early once recomputation reaches a previously valid region
// with an unchanged state (everything downstream still matches).
bool editor_hl_update(Editor *e, HlLang lang, size_t need, int budget);

// Line/column <-> offset helpers (columns in characters, 0-based here).
size_t editor_offset_of(const Editor *e, size_t line, size_t col_chars);
size_t editor_col_of(const Editor *e, size_t line, size_t offset);

#endif

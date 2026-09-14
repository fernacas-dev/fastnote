#ifndef FASTNOTE_TEXT_BUFFER_H
#define FASTNOTE_TEXT_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Contiguous gapless text buffer with exponential growth and an
// incrementally maintained line-start index.
//
// lines[] always satisfies:
//   lines[0] == 0, nlines >= 1,
//   lines[i] == byte offset of the first character of line i.
// An empty document has nlines == 1 and len == 0.
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    size_t *lines;
    size_t nlines;
    size_t lcap;
} TextBuffer;

bool tb_init(TextBuffer *tb);
void tb_free(TextBuffer *tb);
// Remove all content (keeps allocations). Returns false only on OOM while
// restoring the single-line invariant (practically never fails).
bool tb_clear(TextBuffer *tb);

// Insert n bytes at byte offset pos. Returns false on OOM or bad pos.
bool tb_insert(TextBuffer *tb, size_t pos, const char *s, size_t n);
// Erase n bytes starting at byte offset pos. Clamps n to the end.
void tb_erase(TextBuffer *tb, size_t pos, size_t n);

size_t tb_line_count(const TextBuffer *tb);
// Line index (0-based) containing byte offset pos.
size_t tb_line_of(const TextBuffer *tb, size_t pos);
size_t tb_line_start(const TextBuffer *tb, size_t line);
// Length of line in bytes, excluding the terminating '\n'.
size_t tb_line_len(const TextBuffer *tb, size_t line);

// Load/save whole files. load normalizes CRLF/CR to LF and replaces
// invalid UTF-8 with U+FFFD so the editor never sees malformed input.
// On failure returns false and writes a short message into err (if given).
bool tb_load(TextBuffer *tb, const char *path, char *err, size_t errcap);
bool tb_save(const TextBuffer *tb, const char *path, char *err, size_t errcap);

// --- UTF-8 helpers (all robust against malformed input) ---
// Decode one character at s (maxlen bytes available). Always advances:
// returns bytes consumed (1..4) and sets *cp (U+FFFD on invalid input).
size_t utf8_decode(const char *s, size_t maxlen, uint32_t *cp);
// Byte offset of the start of the character ending at or before pos.
size_t utf8_prev_start(const char *s, size_t pos);
// Number of Unicode characters in n bytes.
size_t utf8_count(const char *s, size_t n);
static inline bool utf8_is_cont(uint8_t c) { return (c & 0xC0u) == 0x80u; }

#endif

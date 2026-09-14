#ifndef FASTNOTE_HIGHLIGHT_H
#define FASTNOTE_HIGHLIGHT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Minimal syntax highlighting: hand-rolled per-line tokenizers, no
// dependencies. Only visible lines are tokenized per frame; multi-line
// constructs (/* */, Python triple quotes, <!-- -->) carry a small state
// across lines (see below).

typedef enum {
    HL_NORMAL = 0,
    HL_KEYWORD,
    HL_COMMENT,
    HL_STRING,
    HL_NUMBER,
    HL_PREPROC,
    HL_TAG,
} HlKind;

typedef enum {
    HLANG_NONE = 0,
    HLANG_C,    // C
    HLANG_CPP,  // C++
    HLANG_JAVA, // Java
    HLANG_JS,   // JavaScript / TypeScript
    HLANG_PY,   // Python
    HLANG_RUBY, // Ruby
    HLANG_SH,   // Shell
    HLANG_HTML, // HTML / XML
} HlLang;

// Map a filetype_of() display name ("C", "Python", ...) to a tokenizer.
// Unknown languages return HLANG_NONE (no highlighting).
HlLang hl_lang_for_name(const char *lang);

// Tokenize one line (n bytes, without the trailing '\n').
//
// state_in is the block state at line start; the return value is the state
// for the next line start. Bit 0 = inside /* */ (C-family) or <!-- -->
// (HTML); bits 1-2 = inside a Python triple-quoted string (1=''', 2=""").
// Interpretation is per-language; callers just thread the byte through.
//
// kinds may be NULL (state-only pass). Otherwise kinds[i] is set for every
// i < min(n, kcap); bytes beyond kcap are implicitly HL_NORMAL.
uint8_t hl_scan_line(HlLang lang, const char *s, size_t n, uint8_t state_in,
                     uint8_t *kinds, size_t kcap);

#endif

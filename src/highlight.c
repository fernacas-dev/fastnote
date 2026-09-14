#include "highlight.h"

#include <string.h>

HlLang hl_lang_for_name(const char *lang) {
    if (!lang)
        return HLANG_NONE;
    if (strcmp(lang, "C") == 0)
        return HLANG_C;
    if (strcmp(lang, "C++") == 0)
        return HLANG_CPP;
    if (strcmp(lang, "Java") == 0)
        return HLANG_JAVA;
    if (strcmp(lang, "JavaScript") == 0 || strcmp(lang, "TypeScript") == 0)
        return HLANG_JS;
    if (strcmp(lang, "Python") == 0)
        return HLANG_PY;
    if (strcmp(lang, "Ruby") == 0)
        return HLANG_RUBY;
    if (strcmp(lang, "Shell") == 0)
        return HLANG_SH;
    if (strcmp(lang, "HTML") == 0 || strcmp(lang, "XML") == 0)
        return HLANG_HTML;
    return HLANG_NONE;
}

// --- Keyword tables (alphabetical, shared prefixes kept minimal) ---

static const char *const kw_c[] = {
    "auto",     "break",   "case",     "char",   "const",    "continue",
    "default",  "do",      "double",   "else",   "enum",     "extern",
    "float",    "for",     "goto",     "if",     "inline",   "int",
    "long",     "register", "restrict", "return", "short",    "signed",
    "sizeof",   "static",  "struct",   "switch", "typedef",  "union",
    "unsigned", "void",    "volatile", "while",
};

static const char *const kw_cpp[] = {
    "alignas",   "alignof",  "and",      "and_eq",   "asm",
    "bitand",    "bitor",    "bool",     "break",    "case",
    "catch",     "char",     "char8_t",  "char16_t", "char32_t",
    "class",     "compl",    "concept",  "const",    "consteval",
    "constexpr", "constinit", "const_cast", "continue", "co_await",
    "co_return", "co_yield", "decltype", "default",  "delete",
    "do",        "double",   "dynamic_cast", "else", "enum",
    "explicit",  "export",   "extern",   "false",    "float",
    "for",       "friend",   "goto",     "if",       "inline",
    "int",       "long",     "mutable",  "namespace", "new",
    "noexcept",  "not",      "not_eq",   "nullptr",  "operator",
    "or",        "or_eq",    "private",  "protected", "public",
    "register",  "reinterpret_cast", "return", "short", "signed",
    "sizeof",    "static",   "static_assert", "static_cast", "struct",
    "switch",    "template", "this",     "thread_local", "throw",
    "true",      "try",      "typedef",  "typeid",   "typename",
    "union",     "unsigned", "using",    "virtual",  "void",
    "volatile",  "wchar_t",  "while",    "xor",      "xor_eq",
};

static const char *const kw_java[] = {
    "abstract", "assert",   "boolean",  "break",    "byte",
    "case",     "catch",    "char",     "class",    "const",
    "continue", "default",  "do",       "double",   "else",
    "enum",     "extends",  "false",    "final",    "finally",
    "float",    "for",      "goto",     "if",       "implements",
    "import",   "instanceof", "int",    "interface", "long",
    "native",   "new",      "null",     "package",  "private",
    "protected", "public",  "record",   "return",   "sealed",
    "short",    "static",   "strictfp", "super",    "switch",
    "synchronized", "this", "throw",    "throws",   "transient",
    "true",     "try",      "var",      "void",     "volatile",
    "while",
};

static const char *const kw_js[] = {
    "await",    "break",    "case",     "catch",    "class",
    "const",    "continue", "debugger", "default",  "delete",
    "do",       "else",     "enum",     "export",   "extends",
    "false",    "finally",  "for",      "function", "if",
    "import",   "in",       "instanceof", "let",    "new",
    "null",     "return",   "static",   "super",    "switch",
    "this",     "throw",    "true",     "try",      "typeof",
    "undefined", "var",     "void",     "while",    "with",
    "yield",
};

static const char *const kw_py[] = {
    "False",  "None",   "True",   "and",    "as",     "assert",
    "async",  "await",  "break",  "class",  "continue", "def",
    "del",    "elif",   "else",   "except", "finally", "for",
    "from",   "global", "if",     "import", "in",     "is",
    "lambda", "nonlocal", "not",  "or",     "pass",   "raise",
    "return", "try",    "while",  "with",   "yield",
};

static const char *const kw_ruby[] = {
    "alias",  "and",    "begin",  "BEGIN",  "break",  "case",
    "class",  "def",    "defined?", "do",   "else",   "elsif",
    "end",    "END",    "ensure", "false",  "for",    "if",
    "in",     "module", "next",   "nil",    "not",    "or",
    "redo",   "rescue", "retry",  "return", "self",   "super",
    "then",   "true",   "undef",  "unless", "until",  "when",
    "while",  "yield",
};

static const char *const kw_sh[] = {
    "case", "do",   "done", "elif", "else", "esac",
    "fi",   "for",  "function", "if", "in", "select",
    "then", "until", "while",
};

static bool is_keyword(const char *const *table, size_t n, const char *s,
                       size_t len) {
    for (size_t i = 0; i < n; i++) {
        if (strlen(table[i]) == len && memcmp(table[i], s, len) == 0)
            return true;
    }
    return false;
}

static void lang_keywords(HlLang lang, const char *const **table,
                          size_t *count) {
    switch (lang) {
    case HLANG_C:
        *table = kw_c;
        *count = sizeof(kw_c) / sizeof(kw_c[0]);
        break;
    case HLANG_CPP:
        *table = kw_cpp;
        *count = sizeof(kw_cpp) / sizeof(kw_cpp[0]);
        break;
    case HLANG_JAVA:
        *table = kw_java;
        *count = sizeof(kw_java) / sizeof(kw_java[0]);
        break;
    case HLANG_JS:
        *table = kw_js;
        *count = sizeof(kw_js) / sizeof(kw_js[0]);
        break;
    case HLANG_PY:
        *table = kw_py;
        *count = sizeof(kw_py) / sizeof(kw_py[0]);
        break;
    case HLANG_RUBY:
        *table = kw_ruby;
        *count = sizeof(kw_ruby) / sizeof(kw_ruby[0]);
        break;
    case HLANG_SH:
        *table = kw_sh;
        *count = sizeof(kw_sh) / sizeof(kw_sh[0]);
        break;
    default:
        *table = NULL;
        *count = 0;
        break;
    }
}

// --- Byte classes (non-ASCII bytes count as identifier parts so UTF-8 is
// never split or misread as punctuation) ---

static bool is_word_byte(uint8_t c) {
    if (c >= 0x80u)
        return true;
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '$';
}

static bool is_word_start(uint8_t c) {
    if (c >= 0x80u)
        return true;
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
           c == '$';
}

static bool is_digit_byte(uint8_t c) { return c >= '0' && c <= '9'; }

static void paint(uint8_t *kinds, size_t kcap, size_t from, size_t to,
                  uint8_t kind) {
    if (from >= kcap)
        return;
    if (to > kcap)
        to = kcap;
    for (size_t i = from; i < to; i++)
        kinds[i] = kind;
}

// Consume [0-9a-zA-Z_.'] style number bodies (hex, floats, suffixes).
static size_t consume_number(const char *s, size_t n, size_t i) {
    while (i < n) {
        uint8_t c = (uint8_t)s[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || c == '_' || c == '.' || c == '\'')
            i++;
        else
            break;
    }
    return i;
}

// --- C-family (C, C++, Java, JS): // and /* */, "...", '...', numbers ---

static uint8_t scan_c_like(HlLang lang, const char *s, size_t n, uint8_t st,
                           uint8_t *kinds, size_t kcap, bool js_template) {
    // Preprocessor: '#' as first non-blank char colors the whole line.
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    if (i < n && s[i] == '#' && !(st & 1u) && lang != HLANG_JS) {
        if (kinds)
            paint(kinds, kcap, 0, n, HL_PREPROC);
        return 0;
    }
    i = 0;
    const char *const *kw = NULL;
    size_t nkw = 0;
    lang_keywords(lang, &kw, &nkw);
    while (i < n) {
        uint8_t c = (uint8_t)s[i];
        if (st & 1u) {
            // Inside /* */: look for the closer.
            if (c == '*' && i + 1 < n && s[i + 1] == '/') {
                if (kinds)
                    paint(kinds, kcap, i, i + 2, HL_COMMENT);
                i += 2;
                st &= (uint8_t)~1u;
            } else {
                if (kinds && i < kcap)
                    kinds[i] = HL_COMMENT;
                i++;
            }
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            if (kinds)
                paint(kinds, kcap, i, n, HL_COMMENT);
            break;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            if (kinds)
                paint(kinds, kcap, i, i + 2, HL_COMMENT);
            i += 2;
            st |= 1u;
            continue;
        }
        if (c == '"' || c == '\'' ||
            (js_template && c == '`')) {
            uint8_t q = c;
            size_t j = i + 1;
            while (j < n) {
                if (s[j] == '\\' && j + 1 < n) {
                    j += 2;
                    continue;
                }
                if ((uint8_t)s[j] == q) {
                    j++;
                    break;
                }
                // Template literals may span lines; plain strings end here.
                j++;
            }
            if (kinds)
                paint(kinds, kcap, i, j, HL_STRING);
            i = j;
            continue;
        }
        if (is_digit_byte(c) ||
            (c == '.' && i + 1 < n && is_digit_byte((uint8_t)s[i + 1]))) {
            size_t j = consume_number(s, n, i);
            if (kinds)
                paint(kinds, kcap, i, j, HL_NUMBER);
            i = j;
            continue;
        }
        if (is_word_start(c)) {
            size_t j = i + 1;
            while (j < n && is_word_byte((uint8_t)s[j]))
                j++;
            if (kw && is_keyword(kw, nkw, s + i, j - i)) {
                if (kinds)
                    paint(kinds, kcap, i, j, HL_KEYWORD);
            }
            i = j;
            continue;
        }
        i++;
    }
    return st & 1u;
}

// --- Python / Ruby: # comments, quoted strings, triple-quote state ---

static uint8_t scan_py_like(HlLang lang, const char *s, size_t n, uint8_t st,
                            uint8_t *kinds, size_t kcap) {
    // Ongoing triple-quoted string from a previous line?
    uint8_t triple = 0; // '\'' or '"'
    if (st & 4u)
        triple = '\'';
    else if (st & 8u)
        triple = '"';
    size_t i = 0;
    const char *const *kw = NULL;
    size_t nkw = 0;
    lang_keywords(lang, &kw, &nkw);
    if (triple) {
        size_t j = i;
        bool closed = false;
        while (j < n) {
            if (s[j] == '\\' && j + 1 < n) {
                j += 2;
                continue;
            }
            if ((uint8_t)s[j] == triple && j + 2 < n && s[j + 1] == (char)triple &&
                s[j + 2] == (char)triple) {
                j += 3;
                closed = true;
                break;
            }
            j++;
        }
        if (kinds)
            paint(kinds, kcap, 0, j, HL_STRING);
        i = j;
        if (!closed)
            return st; // still inside
        st &= (uint8_t)~(4u | 8u);
    }
    while (i < n) {
        uint8_t c = (uint8_t)s[i];
        if (c == '#') {
            if (kinds)
                paint(kinds, kcap, i, n, HL_COMMENT);
            break;
        }
        // Triple-quoted string opener?
        if ((c == '\'' || c == '"') && i + 2 < n && s[i + 1] == (char)c &&
            s[i + 2] == (char)c) {
            size_t j = i + 3;
            bool closed = false;
            while (j < n) {
                if (s[j] == '\\' && j + 1 < n) {
                    j += 2;
                    continue;
                }
                if ((uint8_t)s[j] == c && j + 2 < n &&
                    s[j + 1] == (char)c && s[j + 2] == (char)c) {
                    j += 3;
                    closed = true;
                    break;
                }
                j++;
            }
            if (kinds)
                paint(kinds, kcap, i, j, HL_STRING);
            i = j;
            if (!closed)
                return (uint8_t)(st | (c == '\'' ? 4u : 8u));
            continue;
        }
        if (c == '\'' || c == '"') {
            size_t j = i + 1;
            while (j < n) {
                if (s[j] == '\\' && j + 1 < n) {
                    j += 2;
                    continue;
                }
                if ((uint8_t)s[j] == c) {
                    j++;
                    break;
                }
                j++;
            }
            if (kinds)
                paint(kinds, kcap, i, j, HL_STRING);
            i = j;
            continue;
        }
        if (is_digit_byte(c) ||
            (c == '.' && i + 1 < n && is_digit_byte((uint8_t)s[i + 1]))) {
            size_t j = consume_number(s, n, i);
            if (kinds)
                paint(kinds, kcap, i, j, HL_NUMBER);
            i = j;
            continue;
        }
        if (is_word_start(c) && c != '$') {
            size_t j = i + 1;
            while (j < n && is_word_byte((uint8_t)s[j]))
                j++;
            if (kw && is_keyword(kw, nkw, s + i, j - i)) {
                if (kinds)
                    paint(kinds, kcap, i, j, HL_KEYWORD);
            }
            i = j;
            continue;
        }
        i++;
    }
    return st & (4u | 8u);
}

// --- Shell: # comments, '...'/"..." strings, small keyword set ---

static uint8_t scan_sh(const char *s, size_t n, uint8_t st,
                       uint8_t *kinds, size_t kcap) {
    (void)st;
    size_t i = 0;
    const char *const *kw = NULL;
    size_t nkw = 0;
    lang_keywords(HLANG_SH, &kw, &nkw);
    while (i < n) {
        uint8_t c = (uint8_t)s[i];
        if (c == '#') {
            if (kinds)
                paint(kinds, kcap, i, n, HL_COMMENT);
            break;
        }
        if (c == '\'' || c == '"') {
            size_t j = i + 1;
            while (j < n) {
                // Backslash escapes inside "..." only; '...' is literal.
                if (c == '"' && s[j] == '\\' && j + 1 < n) {
                    j += 2;
                    continue;
                }
                if ((uint8_t)s[j] == c) {
                    j++;
                    break;
                }
                j++;
            }
            if (kinds)
                paint(kinds, kcap, i, j, HL_STRING);
            i = j;
            continue;
        }
        if (is_digit_byte(c)) {
            size_t j = consume_number(s, n, i);
            if (kinds)
                paint(kinds, kcap, i, j, HL_NUMBER);
            i = j;
            continue;
        }
        if (is_word_start(c)) {
            size_t j = i + 1;
            while (j < n && is_word_byte((uint8_t)s[j]))
                j++;
            if (is_keyword(kw, nkw, s + i, j - i)) {
                if (kinds)
                    paint(kinds, kcap, i, j, HL_KEYWORD);
            }
            i = j;
            continue;
        }
        i++;
    }
    return 0;
}

// --- HTML/XML: <!-- --> comments (stateful), <tag attr="..."> ---

static uint8_t scan_html(const char *s, size_t n, uint8_t st, uint8_t *kinds,
                         size_t kcap) {
    size_t i = 0;
    if (st & 1u) {
        size_t j = i;
        bool closed = false;
        while (j < n) {
            if (s[j] == '-' && j + 2 < n && s[j + 1] == '-' &&
                s[j + 2] == '>') {
                j += 3;
                closed = true;
                break;
            }
            j++;
        }
        if (kinds)
            paint(kinds, kcap, 0, j, HL_COMMENT);
        i = j;
        if (!closed)
            return 1u;
        st &= (uint8_t)~1u;
    }
    while (i < n) {
        if (s[i] == '<' && i + 3 < n && s[i + 1] == '!' &&
            s[i + 2] == '-' && s[i + 3] == '-') {
            size_t j = i + 4;
            bool closed = false;
            while (j < n) {
                if (s[j] == '-' && j + 2 < n && s[j + 1] == '-' &&
                    s[j + 2] == '>') {
                    j += 3;
                    closed = true;
                    break;
                }
                j++;
            }
            if (kinds)
                paint(kinds, kcap, i, j, HL_COMMENT);
            i = j;
            if (!closed)
                return 1u;
            continue;
        }
        if (s[i] == '<') {
            // Tag up to '>' (or EOL), skipping quoted spans so a '>'
            // inside an attribute value doesn't end the tag early.
            size_t j = i + 1;
            while (j < n && s[j] != '>') {
                if (s[j] == '"' || s[j] == '\'') {
                    uint8_t q = (uint8_t)s[j];
                    j++;
                    while (j < n && (uint8_t)s[j] != q)
                        j++;
                    if (j < n)
                        j++; // consume the closing quote
                } else {
                    j++;
                }
            }
            if (j < n)
                j++; // include '>'
            if (kinds)
                paint(kinds, kcap, i, j, HL_TAG);
            // Repaint quoted attribute values as strings.
            for (size_t k = i; k < j;) {
                if (s[k] == '"' || s[k] == '\'') {
                    uint8_t q = (uint8_t)s[k];
                    size_t e = k + 1;
                    while (e < j && (uint8_t)s[e] != q)
                        e++;
                    if (e < j)
                        e++; // closing quote
                    if (kinds)
                        paint(kinds, kcap, k, e, HL_STRING);
                    k = e;
                } else {
                    k++;
                }
            }
            i = j;
            continue;
        }
        i++; // ordinary text between tags
    }
    return 0;
}

uint8_t hl_scan_line(HlLang lang, const char *s, size_t n, uint8_t state_in,
                     uint8_t *kinds, size_t kcap) {
    if (kinds && kcap > 0) {
        size_t fill = n < kcap ? n : kcap;
        memset(kinds, HL_NORMAL, fill);
    }
    if (n == 0)
        return 0;
    switch (lang) {
    case HLANG_C:
    case HLANG_CPP:
    case HLANG_JAVA:
        return scan_c_like(lang, s, n, state_in, kinds, kcap, false);
    case HLANG_JS:
        return scan_c_like(lang, s, n, state_in, kinds, kcap, true);
    case HLANG_PY:
    case HLANG_RUBY:
        return scan_py_like(lang, s, n, state_in, kinds, kcap);
    case HLANG_SH:
        return scan_sh(s, n, state_in, kinds, kcap);
    case HLANG_HTML:
        return scan_html(s, n, state_in, kinds, kcap);
    default:
        return 0;
    }
}

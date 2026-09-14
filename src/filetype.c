#include "filetype.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    const char *ext;
    const char *lang;
} ExtMap;

// Keep alphabetical by extension for easy scanning.
static const ExtMap ext_table[] = {
    {"asm", "Assembly"},   {"bash", "Shell"},     {"c", "C"},
    {"cc", "C++"},         {"cfg", "Config"},     {"cjs", "JavaScript"},
    {"clj", "Clojure"},    {"conf", "Config"},    {"cpp", "C++"},
    {"cs", "C#"},          {"css", "CSS"},        {"csv", "CSV"},
    {"cxx", "C++"},        {"d", "D"},            {"dart", "Dart"},
    {"diff", "Diff"},      {"el", "Emacs Lisp"},  {"erl", "Erlang"},
    {"ex", "Elixir"},      {"exs", "Elixir"},     {"fish", "Shell"},
    {"go", "Go"},          {"groovy", "Groovy"},  {"h", "C"},
    {"hh", "C++"},         {"hpp", "C++"},        {"hs", "Haskell"},
    {"htm", "HTML"},       {"html", "HTML"},      {"hxx", "C++"},
    {"ini", "INI"},        {"java", "Java"},      {"jl", "Julia"},
    {"js", "JavaScript"},  {"json", "JSON"},      {"jsx", "JavaScript"},
    {"kt", "Kotlin"},      {"kts", "Kotlin"},     {"lisp", "Lisp"},
    {"log", "Log"},        {"lsp", "Lisp"},       {"lua", "Lua"},
    {"md", "Markdown"},    {"markdown", "Markdown"}, {"mjs", "JavaScript"},
    {"ml", "OCaml"},       {"mli", "OCaml"},      {"nim", "Nim"},
    {"patch", "Diff"},     {"php", "PHP"},        {"pl", "Perl"},
    {"pm", "Perl"},        {"ps1", "PowerShell"}, {"py", "Python"},
    {"pyw", "Python"},     {"r", "R"},            {"rb", "Ruby"},
    {"rs", "Rust"},        {"rst", "reStructuredText"}, {"s", "Assembly"},
    {"scala", "Scala"},    {"scss", "CSS"},       {"sh", "Shell"},
    {"sql", "SQL"},        {"swift", "Swift"},    {"tcl", "Tcl"},
    {"tex", "LaTeX"},      {"toml", "TOML"},      {"ts", "TypeScript"},
    {"tsx", "TypeScript"}, {"txt", "Plain Text"}, {"vim", "Vim Script"},
    {"xhtml", "HTML"},     {"xml", "XML"},        {"yaml", "YAML"},
    {"yml", "YAML"},       {"zig", "Zig"},        {"zsh", "Shell"},
};

static const ExtMap base_table[] = {
    {"dockerfile", "Docker"},
    {"gnumakefile", "Makefile"},
    {"makefile", "Makefile"},
};

const char *filetype_of(const char *path) {
    if (!path || !path[0])
        return "Plain Text";
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    if (!base[0])
        return "Plain Text";
    // Lowercase basename (ASCII only; caps at 127 chars + NUL).
    char low[128];
    size_t i = 0;
    for (; base[i] && i + 1 < sizeof(low); i++)
        low[i] = (char)tolower((unsigned char)base[i]);
    low[i] = '\0';
    for (size_t b = 0; b < sizeof(base_table) / sizeof(base_table[0]); b++) {
        if (strcmp(low, base_table[b].ext) == 0)
            return base_table[b].lang;
    }
    const char *dot = strrchr(low, '.');
    if (!dot || dot == low || !dot[1])
        return "Plain Text";
    const char *ext = dot + 1;
    for (size_t e = 0; e < sizeof(ext_table) / sizeof(ext_table[0]); e++) {
        if (strcmp(ext, ext_table[e].ext) == 0)
            return ext_table[e].lang;
    }
    return "Plain Text";
}

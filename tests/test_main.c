// Unit tests for text_buffer + editor (SDL-free, ASan/UBSan instrumented).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "filetype.h"
#include "highlight.h"
#include "text_buffer.h"

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                        \
        }                                                                      \
    } while (0)

// Verify the line index invariant after every mutation in tests.
static void check_lines(const TextBuffer *tb) {
    CHECK(tb->nlines >= 1);
    CHECK(tb->lines[0] == 0);
    for (size_t i = 0; i < tb->nlines; i++) {
        CHECK(tb->lines[i] <= tb->len);
        if (i > 0)
            CHECK(tb->lines[i] > tb->lines[i - 1]);
    }
    size_t nl = 0;
    for (size_t i = 0; i < tb->len; i++)
        if (tb->data[i] == '\n')
            nl++;
    CHECK(tb->nlines == nl + 1);
}

static void test_utf8(void) {
    uint32_t cp;
    CHECK(utf8_decode("A", 1, &cp) == 1 && cp == 'A');
    CHECK(utf8_decode("\xC3\xA9", 2, &cp) == 2 && cp == 0xE9);
    CHECK(utf8_decode("\xE2\x82\xAC", 3, &cp) == 3 && cp == 0x20AC);
    CHECK(utf8_decode("\xF0\x9F\x98\x80", 4, &cp) == 4 && cp == 0x1F600);
    // Invalid: stray continuation, overlong, surrogate, out of range.
    CHECK(utf8_decode("\x80", 1, &cp) == 1 && cp == 0xFFFD);
    CHECK(utf8_decode("\xC0\xAF", 2, &cp) == 1 && cp == 0xFFFD);
    CHECK(utf8_decode("\xED\xA0\x80", 3, &cp) == 1 && cp == 0xFFFD);
    CHECK(utf8_decode("\xF4\x90\x80\x80", 4, &cp) == 1 && cp == 0xFFFD);
    CHECK(utf8_decode("\xE2\x82", 2, &cp) == 1 && cp == 0xFFFD); // truncated
    CHECK(utf8_count("h\xC3\xA9llo", 6) == 5);
    const char *s = "a\xC3\xA9\xE2\x82\xAC" "z";
    CHECK(utf8_prev_start(s, 7) == 6);
    CHECK(utf8_prev_start(s, 6) == 3);
    CHECK(utf8_prev_start(s, 3) == 1);
    CHECK(utf8_prev_start(s, 0) == 0);
}

static void test_buffer_basic(void) {
    TextBuffer tb;
    CHECK(tb_init(&tb));
    CHECK(tb_insert(&tb, 0, "hello", 5));
    check_lines(&tb);
    CHECK(tb_insert(&tb, 5, "\nworld\n", 7));
    check_lines(&tb);
    CHECK(tb.nlines == 3);
    CHECK(tb_line_of(&tb, 0) == 0);
    CHECK(tb_line_of(&tb, 5) == 0);
    CHECK(tb_line_of(&tb, 6) == 1);
    CHECK(tb_line_len(&tb, 0) == 5);
    CHECK(tb_line_len(&tb, 1) == 5);
    CHECK(tb_line_len(&tb, 2) == 0);
    tb_erase(&tb, 5, 1); // join lines 0-1
    check_lines(&tb);
    CHECK(tb.nlines == 2);
    CHECK(tb_line_len(&tb, 0) == 10);
    tb_free(&tb);
}

static void test_buffer_growth(void) {
    TextBuffer tb;
    CHECK(tb_init(&tb));
    // Many small inserts: must not realloc per char visibly (capacity grows).
    for (int i = 0; i < 5000; i++) {
        char c = (char)('a' + (i % 26));
        if (i % 50 == 0)
            CHECK(tb_insert(&tb, tb.len, "\n", 1));
        CHECK(tb_insert(&tb, tb.len, &c, 1));
    }
    check_lines(&tb);
    CHECK(tb.len == 5100);
    CHECK(tb.nlines == 101);
    tb_free(&tb);
}

static void test_load_sanitize(void) {
    // Invalid bytes, CRLF, CR, BOM.
    const char raw[] = "\xEF\xBB\xBF" "a\r\nb\rc\x80\xC3\xA9\xED\xA0\x80";
    FILE *f = fopen("/tmp/fn_test.txt", "wb");
    CHECK(f != NULL);
    fwrite(raw, 1, sizeof(raw) - 1, f);
    fclose(f);
    TextBuffer tb;
    CHECK(tb_init(&tb));
    CHECK(tb_load(&tb, "/tmp/fn_test.txt", NULL, 0));
    check_lines(&tb);
    // "a\nb\nc" + REPLACEMENT + "é" + 3x REPLACEMENT (invalid bytes resync
    // one byte at a time, so the 3-byte surrogate becomes 3 replacements)
    CHECK(tb.nlines == 3);
    CHECK(tb_line_len(&tb, 0) == 1);
    CHECK(tb.len == 5 + 3 + 2 + 9);
    // No raw 0x80 / surrogate bytes remain.
    for (size_t i = 0; i < tb.len; i++)
        CHECK((uint8_t)tb.data[i] != 0x80);
    CHECK(tb_save(&tb, "/tmp/fn_test_out.txt", NULL, 0));
    tb_free(&tb);
    remove("/tmp/fn_test.txt");
    remove("/tmp/fn_test_out.txt");
}

static void test_editor_edits(void) {
    Editor e;
    CHECK(editor_init(&e));
    CHECK(editor_insert(&e, "hello", 5));
    CHECK(e.cursor == 5 && e.modified);
    CHECK(editor_newline(&e));
    CHECK(editor_insert(&e, "world", 5));
    CHECK(e.buf.nlines == 2);
    editor_move_up(&e, false);
    CHECK(e.cursor == 5); // same column clamped
    size_t ln, col;
    editor_line_col(&e, &ln, &col);
    CHECK(ln == 1 && col == 6);
    editor_move_doc_end(&e, false);
    editor_backspace(&e);
    CHECK(e.buf.data[e.buf.len - 1] == 'l');
    editor_move_doc_start(&e, false);
    editor_delete_fwd(&e);
    CHECK(e.buf.data[0] == 'e');
    // Undo everything back to empty.
    int n = 0;
    while (editor_undo(&e) && n++ < 100)
        ;
    CHECK(e.buf.len == 0);
    // Redo restores.
    n = 0;
    while (editor_redo(&e) && n++ < 100)
        ;
    CHECK(e.buf.len > 0);
    editor_quit(&e);
}

static void test_editor_selection(void) {
    Editor e;
    CHECK(editor_init(&e));
    CHECK(editor_insert(&e, "hello world", 11));
    editor_move_home(&e, false);
    editor_move_word_right(&e, true);
    CHECK(editor_has_selection(&e));
    char *s = editor_selection_text(&e);
    CHECK(s && strcmp(s, "hello ") == 0);
    free(s);
    editor_move_word_right(&e, false);
    CHECK(e.cursor == 11);
    editor_move_word_left(&e, true);
    s = editor_selection_text(&e);
    CHECK(s && strcmp(s, "world") == 0);
    free(s);
    editor_select_all(&e);
    s = editor_selection_text(&e);
    CHECK(s && strcmp(s, "hello world") == 0);
    free(s);
    editor_delete_selection(&e);
    CHECK(e.buf.len == 0);
    editor_quit(&e);
}

static void test_undo_coalesce(void) {
    Editor e;
    CHECK(editor_init(&e));
    for (int i = 0; i < 10; i++)
        CHECK(editor_insert(&e, "x", 1));
    CHECK(e.undo.len == 1); // typing coalesced into one op
    CHECK(editor_undo(&e));
    CHECK(e.buf.len == 0);
    CHECK(editor_redo(&e));
    CHECK(e.buf.len == 10);
    editor_quit(&e);
}

static void test_filetype(void) {
    CHECK(strcmp(filetype_of("/a/b/main.c"), "C") == 0);
    CHECK(strcmp(filetype_of("x.H"), "C") == 0);
    CHECK(strcmp(filetype_of("a.cpp"), "C++") == 0);
    CHECK(strcmp(filetype_of("A.Java"), "Java") == 0);
    CHECK(strcmp(filetype_of("index.HTML"), "HTML") == 0);
    CHECK(strcmp(filetype_of("app.py"), "Python") == 0);
    CHECK(strcmp(filetype_of("data.JSON"), "JSON") == 0);
    CHECK(strcmp(filetype_of("notes.md"), "Markdown") == 0);
    CHECK(strcmp(filetype_of("run.sh"), "Shell") == 0);
    CHECK(strcmp(filetype_of("Makefile"), "Makefile") == 0);
    CHECK(strcmp(filetype_of("/x/Dockerfile"), "Docker") == 0);
    CHECK(strcmp(filetype_of("archive.tar.gz"), "Plain Text") == 0);
    CHECK(strcmp(filetype_of("noext"), "Plain Text") == 0);
    CHECK(strcmp(filetype_of(".gitignore"), "Plain Text") == 0);
    CHECK(strcmp(filetype_of(NULL), "Plain Text") == 0);
    CHECK(strcmp(filetype_of(""), "Plain Text") == 0);
}

static void test_highlight(void) {
    uint8_t k[128];
    uint8_t st;
    // C keywords, comment, string, number, preproc.
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_C, "int x = 42;", 10, 0, k, sizeof(k));
    CHECK(st == 0);
    CHECK(k[0] == HL_KEYWORD && k[2] == HL_KEYWORD);
    CHECK(k[8] == HL_NUMBER && k[9] == HL_NUMBER);
    CHECK(k[4] == HL_NORMAL);
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_C, "// hi", 5, 0, k, sizeof(k));
    CHECK(st == 0 && k[0] == HL_COMMENT && k[4] == HL_COMMENT);
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_C, "char *s = \"a\\\"b\";", 17, 0, k,
                      sizeof(k));
    CHECK(st == 0);
    CHECK(k[4] == HL_NORMAL && k[10] == HL_STRING && k[14] == HL_STRING);
    CHECK(k[16] == HL_NORMAL);
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_C, "#include <x>", 11, 0, k, sizeof(k));
    CHECK(st == 0 && k[0] == HL_PREPROC && k[10] == HL_PREPROC);
    // Multi-line block comment threads state across lines.
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_C, "/* open", 7, 0, k, sizeof(k));
    CHECK(st == 1 && k[0] == HL_COMMENT);
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_C, "still", 5, st, k, sizeof(k));
    CHECK(st == 1 && k[0] == HL_COMMENT);
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_C, "end */ x", 8, st, k, sizeof(k));
    CHECK(st == 0 && k[0] == HL_COMMENT && k[7] == HL_NORMAL);
    // Python: comment, triple-quote state, keywords.
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_PY, "def f(): # c", 12, 0, k, sizeof(k));
    CHECK(st == 0 && k[0] == HL_KEYWORD && k[9] == HL_COMMENT);
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_PY, "x = '''start", 11, 0, k, sizeof(k));
    CHECK(st == 4 && k[4] == HL_STRING);
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_PY, "mid", 3, st, k, sizeof(k));
    CHECK(st == 4 && k[0] == HL_STRING);
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_PY, "end''' + 1", 10, st, k, sizeof(k));
    CHECK(st == 0 && k[0] == HL_STRING && k[9] == HL_NUMBER);
    // HTML tags, attributes, comments.
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_HTML, "<div class=\"a\">x", 16, 0, k,
                      sizeof(k));
    CHECK(st == 0 && k[0] == HL_TAG && k[1] == HL_TAG);
    CHECK(k[11] == HL_STRING && k[13] == HL_STRING);
    CHECK(k[14] == HL_TAG && k[15] == HL_NORMAL);
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_HTML, "<!-- a", 6, 0, k, sizeof(k));
    CHECK(st == 1);
    memset(k, 0, sizeof(k));
    st = hl_scan_line(HLANG_HTML, "b --> t", 7, st, k, sizeof(k));
    CHECK(st == 0 && k[0] == HL_COMMENT && k[6] == HL_NORMAL);
    // Language mapping.
    CHECK(hl_lang_for_name("C") == HLANG_C);
    CHECK(hl_lang_for_name("Java") == HLANG_JAVA);
    CHECK(hl_lang_for_name("TypeScript") == HLANG_JS);
    CHECK(hl_lang_for_name("Python") == HLANG_PY);
    CHECK(hl_lang_for_name("Shell") == HLANG_SH);
    CHECK(hl_lang_for_name("HTML") == HLANG_HTML);
    CHECK(hl_lang_for_name("Plain Text") == HLANG_NONE);
    CHECK(hl_lang_for_name(NULL) == HLANG_NONE);
    CHECK(hl_lang_for_name("CSS") == HLANG_NONE);
}

static void test_goal_col(void) {
    Editor e;
    CHECK(editor_init(&e));
    CHECK(editor_insert(&e, "12345\n12\n1234567", 16));
    editor_move_up(&e, false); // from end of line 2, goal col 7
    editor_move_up(&e, false);
    size_t ln, col;
    editor_line_col(&e, &ln, &col);
    CHECK(ln == 1 && col == 6); // column clamped, goal still 7
    editor_move_down(&e, false);
    editor_line_col(&e, &ln, &col);
    CHECK(ln == 2 && col == 3); // clamped, goal still 7
    editor_move_down(&e, false);
    editor_line_col(&e, &ln, &col);
    CHECK(ln == 3 && col == 8); // goal column restored on the long line
    editor_quit(&e);
}

int main(void) {
    test_utf8();
    test_buffer_basic();
    test_buffer_growth();
    test_load_sanitize();
    test_editor_edits();
    test_editor_selection();
    test_undo_coalesce();
    test_goal_col();
    test_filetype();
    test_highlight();
    if (failures == 0)
        printf("all tests passed\n");
    return failures != 0;
}

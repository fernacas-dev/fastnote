#define _POSIX_C_SOURCE 200809L // mkdir/rmdir with strict ISO C

// Integration test: drives the real App event loop with synthetic SDL
// events under the dummy video driver (ASan/UBSan instrumented).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "project.h"

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                        \
        }                                                                      \
    } while (0);

static void push_key(SDL_Keycode key, SDL_Keymod mod) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = key;
    ev.key.mod = mod;
    ev.key.down = true;
    CHECK(SDL_PushEvent(&ev));
}

static void push_text(const char *s) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_TEXT_INPUT;
    ev.text.text = s;
    CHECK(SDL_PushEvent(&ev));
}

static void push_click(float x, float y) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.x = x;
    ev.button.y = y;
    ev.button.clicks = 1;
    CHECK(SDL_PushEvent(&ev));
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.x = x;
    ev.button.y = y;
    CHECK(SDL_PushEvent(&ev));
}

static void push_motion(float x, float y) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = x;
    ev.motion.y = y;
    CHECK(SDL_PushEvent(&ev));
}

static void push_quit(void) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_QUIT;
    CHECK(SDL_PushEvent(&ev));
}

static void steps(App *app, int n) {
    for (int i = 0; i < n; i++)
        app_step(app, 0);
}

int main(void) {
    SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);

    // HiDPI first (its teardown calls SDL_Quit): forced scale 2 doubles
    // the rasterized font and chrome metrics, and a scaled frame renders
    // without errors.
    SDL_setenv_unsafe("FASTNOTE_SCALE", "2", 1);
    App hi;
    char herr[512];
    CHECK(app_init(&hi, NULL, NULL, herr, sizeof(herr)));
    if (failures == 0) {
        CHECK(hi.display_scale == 2.0f);
        CHECK(hi.base_font_px == 14);
        CHECK(hi.font.px == 28);
        CHECK(hi.m.menu_h == 56);
        CHECK(hi.m.status_h == 48);
        CHECK(hi.m.pad_x == 16);
        steps(&hi, 2);
    }
    app_quit(&hi);
    SDL_unsetenv_unsafe("FASTNOTE_SCALE");

    App app;
    char err[512];
    CHECK(app_init(&app, NULL, NULL, err, sizeof(err)));
    if (failures)
        return 1;
    steps(&app, 2); // initial frames render without errors

    // Typing via text-input events (the international-keyboard path).
    push_text("hello");
    steps(&app, 1);
    CHECK(app.tabs[app.cur].buf.len == 5);
    CHECK(memcmp(app.tabs[app.cur].buf.data, "hello", 5) == 0);

    // Return key makes a newline; keep typing.
    push_key(SDLK_RETURN, 0);
    push_text("world");
    steps(&app, 2);
    CHECK(app.tabs[app.cur].buf.nlines == 2);
    CHECK(app.tabs[app.cur].modified);

    // Home/End navigation.
    push_key(SDLK_HOME, 0);
    steps(&app, 1);
    CHECK(app.tabs[app.cur].cursor == 6);
    push_key(SDLK_END, 0);
    steps(&app, 1);
    CHECK(app.tabs[app.cur].cursor == 11);

    // Shift+Home selects "world".
    push_key(SDLK_HOME, SDL_KMOD_SHIFT);
    steps(&app, 1);
    CHECK(editor_has_selection(&app.tabs[app.cur]));
    char *sel = editor_selection_text(&app.tabs[app.cur]);
    CHECK(sel && strcmp(sel, "world") == 0);
    free(sel);

    // Ctrl+A selects everything.
    push_key(SDLK_A, SDL_KMOD_CTRL);
    steps(&app, 1);
    sel = editor_selection_text(&app.tabs[app.cur]);
    CHECK(sel && strcmp(sel, "hello\nworld") == 0);
    free(sel);

    // Escape clears the selection.
    push_key(SDLK_ESCAPE, 0);
    steps(&app, 1);
    CHECK(!editor_has_selection(&app.tabs[app.cur]));

    // Font size shortcuts.
    CHECK(app.font.px == 14);
    push_key(SDLK_EQUALS, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.font.px == 15);
    push_key(SDLK_0, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.font.px == 14);

    // Undo removes "world", redo restores it.
    push_key(SDLK_Z, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.tabs[app.cur].buf.nlines == 1);
    push_key(SDLK_Y, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.tabs[app.cur].buf.nlines == 2);

    // Menu: click "File", hover first row, activate it (New on a
    // modified doc -> confirm modal appears).
    push_click(20.0f, 10.0f);
    steps(&app, 1);
    CHECK(app.menu.open);
    float ih = (float)app.font.line_h + 6.0f;
    push_motion(50.0f, 28.0f + 4.0f + ih / 2.0f);
    steps(&app, 1);
    CHECK(app.menu.hover_item == 0);
    push_click(50.0f, 28.0f + 4.0f + ih / 2.0f);
    steps(&app, 1);
    CHECK(!app.menu.open);
    CHECK(ui_modal_is_open(&app.modal)); // unsaved changes confirm
    // Cancel the modal; the document must survive.
    push_key(SDLK_ESCAPE, 0);
    steps(&app, 1);
    CHECK(!ui_modal_is_open(&app.modal));
    CHECK(app.tabs[app.cur].buf.nlines == 2);

    // Mouse click in the editor area positions the cursor (2nd line start).
    // x must clear pad + line-number gutter (text starts at area_x).
    float gx = (float)(app.m.pad_x) +
               (float)ui_gutter_w(&app.font, &app.m, app.tabs[app.cur].buf.nlines) +
               4.0f;
    push_click(gx, (float)(app.m.menu_h + app.m.tab_h + app.font.line_h + 2));
    steps(&app, 1);
    CHECK(app.tabs[app.cur].cursor == 6);

    // Ctrl+arrows reach word navigation (not swallowed as shortcuts).
    push_key(SDLK_LEFT, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.tabs[app.cur].cursor == 0);
    push_key(SDLK_RIGHT, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.tabs[app.cur].cursor == 6);

    // Empty-line select-all: the selection rect must not wrap when the
    // selection ends before an empty line starts (renders under ASan).
    push_key(SDLK_HOME, SDL_KMOD_CTRL);
    steps(&app, 1);
    push_key(SDLK_RETURN, 0);
    steps(&app, 1);
    CHECK(app.tabs[app.cur].buf.nlines == 3);
    push_key(SDLK_A, SDL_KMOD_CTRL);
    steps(&app, 2);
    sel = editor_selection_text(&app.tabs[app.cur]);
    CHECK(sel && strcmp(sel, "\nhello\nworld") == 0);
    free(sel);

    // Project sidebar: scan, click-to-open (with unsaved confirm), toggle.
    {
        // Bad root is rejected without state.
        Project bad;
        project_init(&bad);
        CHECK(!project_open(&bad, "/nonexistent-fn-dir", NULL, 0));
        CHECK(!bad.has);
        project_quit(&bad);

        mkdir("/tmp/fn_proj", 0755);
        mkdir("/tmp/fn_proj/sub", 0755);
        FILE *pf = fopen("/tmp/fn_proj/a.txt", "w");
        CHECK(pf != NULL);
        if (pf) {
            fputs("AAA\n", pf);
            fclose(pf);
        }
        pf = fopen("/tmp/fn_proj/sub/b.txt", "w");
        CHECK(pf != NULL);
        if (pf) {
            fputs("BBB\n", pf);
            fclose(pf);
        }
        char perr[256];
        CHECK(project_open(&app.project, "/tmp/fn_proj", perr, sizeof(perr)));
        CHECK(app.project.visible);
        // Layout: depth-first, dirs first -> sub/, sub/b.txt, a.txt.
        CHECK(app.project.n == 3);
        CHECK(app.project.ents[0].is_dir);
        CHECK(strcmp(app.project.ents[1].rel, "sub/b.txt") == 0);
        CHECK(strcmp(app.project.ents[2].rel, "a.txt") == 0);
        steps(&app, 2); // renders the sidebar without errors

        int rh = project_row_h(&app.font, &app.m);
        // Row 0 is the sub/ directory: clicking collapses/expands it.
        float row0_y = (float)(app.m.menu_h + rh) + (float)rh / 2.0f;
        push_click(100.0f, row0_y);
        steps(&app, 1);
        CHECK(app.project.n == 2);
        CHECK(strcmp(app.project.ents[1].rel, "a.txt") == 0);
        push_click(100.0f, row0_y);
        steps(&app, 1);
        CHECK(app.project.n == 3);
        // Row i occupies [menu_h + rh*(i+1), menu_h + rh*(i+2)).
        // Clicking a file opens it in a FRESH tab (current tab keeps its
        // unsaved changes, so no confirm modal appears).
        size_t tabs_before = app.ntabs;
        float row2_y =
            (float)(app.m.menu_h + 3 * rh) + (float)rh / 2.0f;
        push_click(100.0f, row2_y);
        steps(&app, 1);
        CHECK(!ui_modal_is_open(&app.modal));
        CHECK(app.ntabs == tabs_before + 1);
        CHECK(app.cur == app.ntabs - 1);
        CHECK(app.tabs[app.cur].buf.len == 4);
        CHECK(memcmp(app.tabs[app.cur].buf.data, "AAA\n", 4) == 0);
        // Ctrl+B toggles the sidebar.
        push_key(SDLK_B, SDL_KMOD_CTRL);
        steps(&app, 1);
        CHECK(!app.project.visible);
        push_key(SDLK_B, SDL_KMOD_CTRL);
        steps(&app, 1);
        CHECK(app.project.visible);
        remove("/tmp/fn_proj/sub/b.txt");
        remove("/tmp/fn_proj/a.txt");
        rmdir("/tmp/fn_proj/sub");
        rmdir("/tmp/fn_proj");
    }

    // Highlight render path: a .c extension activates the C tokenizer.
    CHECK(editor_save_as(&app.tabs[app.cur], "/tmp/fn_hl.c", err, sizeof(err)));
    CHECK(app.tabs[app.cur].hl_lang == HLANG_C);
    steps(&app, 2);
    remove("/tmp/fn_hl.c");

    // Tabs: Ctrl+N appends an empty tab; typing goes there; Ctrl+Tab
    // cycles both directions.
    size_t tb0 = app.ntabs;
    push_key(SDLK_N, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.ntabs == tb0 + 1 && app.cur == app.ntabs - 1);
    CHECK(app.tabs[app.cur].buf.len == 0);
    push_text("tab2");
    steps(&app, 1);
    CHECK(app.tabs[app.cur].buf.len == 4);
    push_key(SDLK_TAB, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.cur == 0);
    push_key(SDLK_TAB, (SDL_Keymod)(SDL_KMOD_CTRL | SDL_KMOD_SHIFT));
    steps(&app, 1);
    CHECK(app.cur == app.ntabs - 1);
    sel = editor_selection_text(&app.tabs[app.cur]);
    CHECK(sel == NULL); // no selection in the fresh tab
    // Ctrl+W on the modified tab asks; cancel keeps it.
    push_key(SDLK_W, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(ui_modal_is_open(&app.modal));
    push_key(SDLK_ESCAPE, 0);
    steps(&app, 1);
    CHECK(!ui_modal_is_open(&app.modal));
    CHECK(app.ntabs == tb0 + 1);
    // Save, then Ctrl+W closes outright.
    CHECK(editor_save_as(&app.tabs[app.cur], "/tmp/fn_tab.txt", err,
                         sizeof(err)));
    push_key(SDLK_W, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.ntabs == tb0 && app.cur == app.ntabs - 1);
    remove("/tmp/fn_tab.txt");

    // Pathological single line stays renderable (bounded line walks).
    {
        size_t big = 200000;
        char *xs = malloc(big);
        CHECK(xs != NULL);
        if (xs) {
            memset(xs, 'x', big);
            CHECK(editor_insert(&app.tabs[app.cur], xs, big));
            free(xs);
            steps(&app, 2);
            // Appended at end of a.txt (no active selection to replace).
            CHECK(app.tabs[app.cur].buf.len == 4 + big);
        }
    }

    // Wheel event on a short document keeps scroll clamped.
    SDL_Event wev;
    memset(&wev, 0, sizeof(wev));
    wev.type = SDL_EVENT_MOUSE_WHEEL;
    wev.wheel.y = 3.0f;
    CHECK(SDL_PushEvent(&wev));
    steps(&app, 1);
    CHECK(app.tabs[app.cur].scroll_line == 0);

    // Save every remaining tab, then quit cleanly via the window path.
    for (size_t t = 0; t < app.ntabs; t++) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "/tmp/fn_q%zu.txt", t);
        CHECK(editor_save_as(&app.tabs[t], tmp, err, sizeof(err)));
    }
    push_quit();
    steps(&app, 1);
    CHECK(!app.running);

    app_quit(&app);
    for (size_t t = 0; t < 4; t++) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "/tmp/fn_q%zu.txt", t);
        remove(tmp);
    }
    remove("/tmp/fn_itest.txt");
    if (failures == 0)
        printf("all integration tests passed\n");
    return failures != 0;
}

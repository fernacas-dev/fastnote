// Integration test: drives the real App event loop with synthetic SDL
// events under the dummy video driver (ASan/UBSan instrumented).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

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
    CHECK(app.ed.buf.len == 5);
    CHECK(memcmp(app.ed.buf.data, "hello", 5) == 0);

    // Return key makes a newline; keep typing.
    push_key(SDLK_RETURN, 0);
    push_text("world");
    steps(&app, 2);
    CHECK(app.ed.buf.nlines == 2);
    CHECK(app.ed.modified);

    // Home/End navigation.
    push_key(SDLK_HOME, 0);
    steps(&app, 1);
    CHECK(app.ed.cursor == 6);
    push_key(SDLK_END, 0);
    steps(&app, 1);
    CHECK(app.ed.cursor == 11);

    // Shift+Home selects "world".
    push_key(SDLK_HOME, SDL_KMOD_SHIFT);
    steps(&app, 1);
    CHECK(editor_has_selection(&app.ed));
    char *sel = editor_selection_text(&app.ed);
    CHECK(sel && strcmp(sel, "world") == 0);
    free(sel);

    // Ctrl+A selects everything.
    push_key(SDLK_A, SDL_KMOD_CTRL);
    steps(&app, 1);
    sel = editor_selection_text(&app.ed);
    CHECK(sel && strcmp(sel, "hello\nworld") == 0);
    free(sel);

    // Escape clears the selection.
    push_key(SDLK_ESCAPE, 0);
    steps(&app, 1);
    CHECK(!editor_has_selection(&app.ed));

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
    CHECK(app.ed.buf.nlines == 1);
    push_key(SDLK_Y, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.ed.buf.nlines == 2);

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
    CHECK(app.ed.buf.nlines == 2);

    // Mouse click in the editor area positions the cursor (2nd line start).
    push_click(10.0f, (float)(28 + app.font.line_h + 2));
    steps(&app, 1);
    CHECK(app.ed.cursor == 6);

    // Ctrl+arrows reach word navigation (not swallowed as shortcuts).
    push_key(SDLK_LEFT, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.ed.cursor == 0);
    push_key(SDLK_RIGHT, SDL_KMOD_CTRL);
    steps(&app, 1);
    CHECK(app.ed.cursor == 6);

    // Empty-line select-all: the selection rect must not wrap when the
    // selection ends before an empty line starts (renders under ASan).
    push_key(SDLK_HOME, SDL_KMOD_CTRL);
    steps(&app, 1);
    push_key(SDLK_RETURN, 0);
    steps(&app, 1);
    CHECK(app.ed.buf.nlines == 3);
    push_key(SDLK_A, SDL_KMOD_CTRL);
    steps(&app, 2);
    sel = editor_selection_text(&app.ed);
    CHECK(sel && strcmp(sel, "\nhello\nworld") == 0);
    free(sel);

    // Pathological single line stays renderable (bounded line walks).
    {
        size_t big = 200000;
        char *xs = malloc(big);
        CHECK(xs != NULL);
        if (xs) {
            memset(xs, 'x', big);
            CHECK(editor_insert(&app.ed, xs, big));
            free(xs);
            steps(&app, 2);
            // Insert replaced the active selection: exactly big bytes.
            CHECK(app.ed.buf.len == big);
        }
    }

    // Wheel event on a short document keeps scroll clamped.
    SDL_Event wev;
    memset(&wev, 0, sizeof(wev));
    wev.type = SDL_EVENT_MOUSE_WHEEL;
    wev.wheel.y = 3.0f;
    CHECK(SDL_PushEvent(&wev));
    steps(&app, 1);
    CHECK(app.ed.scroll_line == 0);

    // Save to a temp path, then quit cleanly via the window path.
    CHECK(editor_save_as(&app.ed, "/tmp/fn_itest.txt", err, sizeof(err)));
    push_quit();
    steps(&app, 1);
    CHECK(!app.running);

    app_quit(&app);
    remove("/tmp/fn_itest.txt");
    if (failures == 0)
        printf("all integration tests passed\n");
    return failures != 0;
}

# FastNote — High-Performance Minimal Linux Text Editor

FastNote is an extremely lightweight graphical text editor for Linux,
written in C17 with SDL3 (window, input, clipboard, rendering) and
FreeType (glyph rasterization). No GTK/Qt, no tabs,
no plugins — just fast text editing.

## Dependencies

- C17 compiler (gcc/clang), CMake ≥ 3.16, pkg-config
- SDL3 development files
- FreeType development files

Ubuntu/Debian:

```bash
sudo apt install libsdl3-dev libfreetype-dev
```

A monospace font is auto-detected (`DejaVuSansMono` preferred). Override with:

```bash
FASTNOTE_FONT=/path/to/Mono.ttf fastnote
```

## Build

```bash
cmake -S . -B build
cmake --build build -j
./build/fastnote [file]
```

Run the tests (unit + headless integration, ASan/UBSan instrumented):

```bash
ctest --test-dir build --output-on-failure
```

Optional sanitizer build of the whole app:

```bash
cmake -S . -B build-asan -DFASTNOTE_SANITIZERS=ON
cmake --build build-asan -j
```

Compiler warnings: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.

## Usage

```
fastnote [options] [file]
  --font PATH   use PATH as the editor font
  --help        show help
  --version     show version
```

### Keyboard

| Action | Shortcut |
|---|---|
| New / Open / Save / Save As / Exit | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S / Ctrl+Q |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y |
| Cut / Copy / Paste / Select All | Ctrl+X / Ctrl+C / Ctrl+V / Ctrl+A |
| New tab / Close tab | Ctrl+N / Ctrl+W |
| Next / previous tab | Ctrl+Tab / Ctrl+Shift+Tab |
| Word jump | Ctrl+Left / Ctrl+Right |
| Doc start / end | Ctrl+Home / Ctrl+End |
| Page up / down | PageUp / PageDown |
| Extend selection | Shift + any navigation key |
| Font bigger / smaller / reset | Ctrl++ / Ctrl+- / Ctrl+0 |
| Toggle project sidebar | Ctrl+B |
| Close menu / clear selection | Esc |

Text entry goes through SDL text-input events, so UTF-8, layouts and
accented keys work. Window title shows `*` for modified documents; New,
Open and Exit ask Save/Discard/Cancel when unsaved changes exist.

## Tabs

One `Editor` per open document (`App.tabs`): each tab keeps its own text,
cursor, selection, scroll, undo history and highlight state. `Ctrl+N`
opens an empty tab (reusing the current one when it is an untouched
untitled document); `File → Open` and sidebar clicks open files in a fresh
tab the same way. The strip under the menu bar shows basenames (`*` when
modified, clipped with a per-tab `×`); click switches, `×`/`Ctrl+W` closes
with Save/Discard/Cancel when modified (closing the last tab resets it in
place). `Ctrl+Tab`/`Ctrl+Shift+Tab` cycle. Quitting with unsaved tabs
confirms them one by one (Save All semantics per tab: untitled tabs fall
back to the save dialog and the exit resumes afterwards).

## Architecture

```
src/
  main.c         CLI entry (file arg, --font/--help/--version)
  app.[hc]       App state, event-driven main loop, input, actions
  editor.[hc]    cursor, selection, word/page motions, undo/redo (SDL-free)
  text_buffer.[hc] contiguous buffer, incremental line index, UTF-8, load/save
  font.[hc]      FreeType loading, metrics, open-addressing glyph cache
  render.c       visible-lines-only frame renderer
  ui.[hc]        menu bar + dropdowns, modal dialogs, status bar
  file_dialog.[hc] isolation layer over SDL3 async file dialogs
  project.[hc]   project sidebar: folder scan, file list, open-on-click
  filetype.[hc]  language identification by file name (status bar)
  highlight.[hc] hand-rolled syntax highlighter (no dependencies)
  theme.h        centralized dark palette + layout constants
tests/
  test_main.c    unit tests (text buffer, UTF-8, editor, undo)
  test_app.c     integration test (synthetic SDL events, dummy video driver)
```

Performance decisions (all documented at the use site):

- Event-driven loop with `SDL_WaitEventTimeout`: ~zero idle CPU; renders
  only when dirty or on the 500 ms cursor-blink edge.
- Glyph cache: each codepoint rasterized once per font size.
- Only visible lines are rendered; line widths beyond the viewport are
  measured with an early-out cap, so pathological single-line files can't
  stall a frame (horizontal scroll clamps at 100 000 px).
- Text buffer grows exponentially; the line-start index is updated
  incrementally (binary search for offset→line).
- Undo history is bounded (2000 ops / 8 MB); consecutive typing coalesces.
- Files are normalized on load (CRLF→LF, invalid UTF-8→U+FFFD, BOM
  stripped) so malformed input can never crash the editor.

## Performance test plan

`scripts/perf_test.sh` automates this. Tiers:

| Tier | Size | Checks |
|---|---|---|
| Empty | 0 B | startup time, idle CPU (`top`), RSS |
| Small | ~1 KB | startup, typing latency (subjective) |
| Medium | ~1 MB | load time, scroll smoothness |
| Large | ~10 MB | load time, render time, memory |
| Very large | ~100 MB | graceful behavior, no catastrophic slowdown |

Method: `/usr/bin/time -v` for wall/RSS, `SDL_VIDEODRIVER=dummy timeout N`
for headless startup+render smoke, `top -p` for idle CPU. 100 MB files must
load and stay navigable; single-line monsters degrade gracefully via the
bounded measurement paths (see above).

## Notes

- File dialogs use SDL3's native dialogs (`SDL_ShowOpenFileDialog` /
  `SDL_ShowSaveFileDialog`), isolated behind `file_dialog.h` so the backend
  can be swapped without touching the rest of the app.
- Horizontal scrolling exists (Shift+wheel, cursor follow) but is minimal.
- Line numbers: the gutter shows right-aligned numbers for visible lines
  only (current line in foreground, rest dimmed); clicks on it are ignored.
- Language label: the status bar shows the detected language (`filetype.h`,
  by extension + known basenames like Makefile/Dockerfile, unknown →
  "Plain Text").

## Project sidebar

`File → Open Folder` (native folder dialog) loads a project: a 220 px
sidebar shows a tree (directories first, alphabetical, depth-indented,
folders with a `/` suffix and ▶/▼ triangles). Clicking a folder
collapses/expands its contents; clicking a file opens it — with the usual
Save/Discard/Cancel when the current document has unsaved changes. The
open file is highlighted; the wheel scrolls the tree when hovering it;
`View → Toggle Sidebar` (Ctrl+B) hides/shows it. The tree rescans on
folder open, window focus and successful save (collapses persist by path).
Hidden files, symlinks and unreadable subdirectories are skipped; caps of
20 000 entries, 1024 collapses and 64 depth keep stray mounts from
stalling the UI.

## Syntax highlighting

Minimal and dependency-free (`highlight.h/c`): hand-rolled per-line
tokenizers for C, C++, Java, JavaScript/TypeScript, Python, Ruby, Shell
and HTML/XML — keywords, comments, strings, numbers, preprocessor lines
and tags, in 6 theme colors. Only visible lines tokenize per frame, so
typing stays instant; multi-line constructs (`/* */`, Python triple
quotes, `<!-- -->`) thread one state byte across lines with per-line
states maintained parallel to the line index. Edits invalidate from the
edited line and recompute forward with a per-frame budget (3000 lines),
stopping early once the state rejoins a previously valid region — typical
keystrokes re-resolve in ~1 line, huge files converge progressively
without jank. Other languages and files over the 64 KB per-line kinds cap
render as plain text. `View → Toggle Highlight` disables it.

## HiDPI / display scaling

FastNote renders in physical pixels: the window is created with
`SDL_WINDOW_HIGH_PIXEL_DENSITY`, layout uses `SDL_GetWindowSizeInPixels()`,
and the font rasterizes at `base_size × display_scale`
(`SDL_GetWindowDisplayScale()`), so text stays crisp on 125/150/200 %
desktops instead of being upscaled blurry. Menu bar, status bar, padding
and cursor scale with the same factor (`UiMetrics` in `src/ui.h`); the
`N px` indicator and `Ctrl++/-/0` operate on the user-facing base size
(8–48). Moving the window to a monitor with a different scale
(`SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED`) re-applies automatically, and
mouse coordinates are converted from logical to framebuffer pixels.
Override for testing: `FASTNOTE_SCALE=2 fastnote`.

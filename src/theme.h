#ifndef FASTNOTE_THEME_H
#define FASTNOTE_THEME_H

#include <SDL3/SDL.h>

// Centralized color palette. Only this struct needs to change for theming.
typedef struct {
    SDL_Color background;
    SDL_Color foreground;
    SDL_Color cursor;
    SDL_Color selection;
    SDL_Color menu;
    SDL_Color border;
    SDL_Color status_bar;
    SDL_Color line_number; // gutter numbers (current line uses foreground)
    SDL_Color hl_keyword;
    SDL_Color hl_string;
    SDL_Color hl_comment;
    SDL_Color hl_number;
    SDL_Color hl_preproc;
    SDL_Color hl_tag;
} Theme;

static inline Theme theme_dark(void) {
    return (Theme){
        .background = {0x1E, 0x1E, 0x1E, 0xFF},
        .foreground = {0xD4, 0xD4, 0xD4, 0xFF},
        .cursor     = {0xFF, 0xFF, 0xFF, 0xFF},
        .selection  = {0x40, 0x40, 0x40, 0xFF},
        .menu       = {0x25, 0x25, 0x25, 0xFF},
        .border     = {0x30, 0x30, 0x30, 0xFF},
        .status_bar = {0x25, 0x25, 0x25, 0xFF},
        .line_number = {0x85, 0x85, 0x85, 0xFF},
        .hl_keyword = {0x56, 0x9C, 0xD6, 0xFF},
        .hl_string = {0xCE, 0x91, 0x78, 0xFF},
        .hl_comment = {0x6A, 0x99, 0x55, 0xFF},
        .hl_number = {0xB5, 0xCE, 0xA8, 0xFF},
        .hl_preproc = {0xC5, 0x86, 0xC0, 0xFF},
        .hl_tag = {0x4E, 0xC9, 0xB0, 0xFF},
    };
}

// Layout constants below are 1x base sizes; UiMetrics scales them by the
// display scale for HiDPI displays (see ui.h).
#define FN_MENU_H 28
#define FN_TAB_H 30
#define FN_STATUS_H 24
#define FN_PAD_X 8
#define FN_CURSOR_W 2

// Font policy.
#define FN_FONT_DEFAULT_PX 14
#define FN_FONT_MIN_PX 8
#define FN_FONT_MAX_PX 48

// Editor behavior.
#define FN_TAB_WIDTH_COLS 4
#define FN_BLINK_MS 500u
#define FN_WHEEL_LINES 3

#endif

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
    };
}

// Layout constants below are 1x base sizes; UiMetrics scales them by the
// display scale for HiDPI displays (see ui.h).
#define FN_MENU_H 28
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

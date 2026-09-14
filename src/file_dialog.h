#ifndef FASTNOTE_FILE_DIALOG_H
#define FASTNOTE_FILE_DIALOG_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

// Thin isolation layer over SDL3's async file dialogs. The rest of the
// application never touches SDL dialog APIs directly, so this backend can
// be replaced (e.g. with portal/zenity) without further changes.
//
// Results arrive as SDL user events (see event_type()); the event's data1
// carries a heap-allocated FileDialogResult the receiver must free
// (including the path string, if any).
typedef struct {
    char *path;    // NULL when the dialog was cancelled or failed
    bool is_save;  // true for save dialogs
    bool is_folder; // true for open-folder dialogs (path = directory)
} FileDialogResult;

// Registered SDL user-event type for results. 0 on failure.
uint32_t filedialog_event_type(void);
void filedialog_open(SDL_Window *win);
void filedialog_save(SDL_Window *win);
void filedialog_open_folder(SDL_Window *win);

#endif

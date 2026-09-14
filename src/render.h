#ifndef FASTNOTE_RENDER_H
#define FASTNOTE_RENDER_H

// Frame rendering. Takes an opaque App pointer to avoid a header cycle;
// render.c includes app.h for the full definition.
// Returns true when the frame is fully resolved; false when background
// highlight-state work remains and the caller should render again.
#include <stdbool.h>

struct App;
bool render_frame(struct App *app);

#endif

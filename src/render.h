#ifndef FASTNOTE_RENDER_H
#define FASTNOTE_RENDER_H

// Frame rendering. Takes an opaque App pointer to avoid a header cycle;
// render.c includes app.h for the full definition.
struct App;
void render_frame(struct App *app);

#endif

#ifndef FASTNOTE_FILETYPE_H
#define FASTNOTE_FILETYPE_H

// Language identification by file name (extension + a few well-known
// basenames). Display-only: FastNote has no syntax highlighting.
// Unknown or missing types report "Plain Text".
const char *filetype_of(const char *path);

#endif

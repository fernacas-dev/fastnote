#include <stdio.h>
#include <string.h>

#include "app.h"

static void usage(const char *prog) {
    printf("Usage: %s [options] [file]\n", prog);
    printf("\nMinimal, fast Linux text editor.\n\n");
    printf("Options:\n");
    printf("  --font PATH   use PATH as the editor font\n");
    printf("  --help        show this help\n");
    printf("  --version     show version\n");
}

int main(int argc, char **argv) {
    const char *open_path = NULL;
    const char *font_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            printf("FastNote 1.0.0\n");
            return 0;
        }
        if (strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
            font_path = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        } else {
            open_path = argv[i];
        }
    }

    App app;
    char err[512];
    if (!app_init(&app, open_path, font_path, err, sizeof(err))) {
        fprintf(stderr, "fastnote: %s\n", err);
        return 1;
    }
    int rc = app_run(&app);
    app_quit(&app);
    return rc;
}

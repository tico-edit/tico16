/*
 * tico - a tico/nano-style text editor for 16-bit DOS
 *
 * Command line handling, following the dos-hello16 conventions; the
 * editor itself is in editor.c.
 */

#include <stdio.h>
#include <string.h>
#include "tico.h"

static const char *progname = "tico";

static void usage(FILE *fp)
{
    fprintf(fp,
        "Usage: tico [OPTION]... [FILE]\n"
        "Edit FILE, or a new buffer, full screen.\n"
        "\n"
        "  -h, --help     display this help and exit\n"
        "      --version  output version information and exit\n");
}

int main(int argc, char **argv)
{
    const char *file = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0 ||
            strcmp(a, "-?") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "--version") == 0) {
            printf("tico (tico16) %s\n", VERSION);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "%s: unrecognized option '%s'\n", progname, a);
            fprintf(stderr, "Try 'tico --help' for more information.\n");
            return 1;
        } else if (file != NULL) {
            fprintf(stderr, "%s: extra operand '%s'\n", progname, a);
            fprintf(stderr, "Try 'tico --help' for more information.\n");
            return 1;
        } else {
            file = a;
        }
    }
    return editor_run(file);
}

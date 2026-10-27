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
    fputs(
        "Usage: tico [OPTION]... [FILE]\n"
        "Edit FILE, or a new buffer, full screen.\n"
        "\n"
        "  -h, --help     display this help and exit\n"
        "      --version  output version information and exit\n", fp);
}

/* "tico: MESSAGE 'ARG'" and the pointer to --help, on stderr */
static void bad_arg(const char *message, const char *arg)
{
    fputs(progname, stderr);
    fputs(message, stderr);
    fputs(arg, stderr);
    fputs("'\nTry 'tico --help' for more information.\n", stderr);
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
            fputs("tico (tico16) " VERSION "\n", stdout);
            return 0;
        } else if (a[0] == '-') {
            bad_arg(": unrecognized option '", a);
            return 1;
        } else if (file != NULL) {
            bad_arg(": extra operand '", a);
            return 1;
        } else {
            file = a;
        }
    }
    return editor_run(file);
}

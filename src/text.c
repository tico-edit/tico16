/*
 * text.c - reading and writing line breaks
 *
 * Reading follows nano 8.7.1's read_file (tico's convert_line_endings):
 *
 *  - a CR directly before an LF is dropped, and the file is DOS format
 *    when its first line break is such a CR LF;
 *  - a bare CR followed by another byte is a line break only on the
 *    first line, or once the file is known to be Mac format; elsewhere
 *    it is ordinary text (shown as ^M);
 *  - a CR ending the file is a line break.
 *
 * Writing puts the buffer's one line break after every line but the
 * last (the magic line), so a file that didn't end with a break gets
 * one, as in nano.
 *
 * Portable C, unit-tested on the build host.
 */

#include <stdlib.h>
#include "tico.h"

/*
 * The line being read accumulates in a scratch line (r->cur) and is
 * copied to an exact-sized line when it ends, so loaded text carries
 * no slack.
 */

void reader_start(reader *r, lines *ls)
{
    r->ls = ls;
    r->cur = line_new(NULL, 0);
    r->breaks = 0;
    r->format = FMT_NONE;
    r->failed = r->cur == NULL;
}

static int ends_with_cr(const line *l)
{
    return l->len > 0 && l->text[l->len - 1] == '\r';
}

static void end_line(reader *r)
{
    if (!lines_append(r->ls, r->cur->text, r->cur->len))
        r->failed = 1;
    r->cur->len = 0;
}

static void push(reader *r, char c)
{
    line *l = r->cur;

    if (l->len == l->cap) {
        /* grows with slack, so this is rare */
        if (!line_insert(l, l->len, &c, 1))
            r->failed = 1;
        return;
    }
    l->text[l->len++] = c;
}

int reader_feed(reader *r, const char *buf, unsigned len)
{
    unsigned i;
    char c;

    for (i = 0; i < len && !r->failed; i++) {
        c = buf[i];
        if (c == '\n') {
            if (ends_with_cr(r->cur)) {
                if (r->breaks == 0)
                    r->format = FMT_DOS;
                r->cur->len--;
            } else if (r->breaks == 0) {
                r->format = FMT_UNIX;
            }
            end_line(r);
            r->breaks++;
        } else if ((r->breaks == 0 || r->format == FMT_MAC) &&
                   ends_with_cr(r->cur)) {
            r->format = FMT_MAC;
            r->cur->len--;
            end_line(r);
            r->breaks++;
            push(r, c);
        } else {
            push(r, c);
        }
    }
    return !r->failed;
}

int reader_finish(reader *r)
{
    if (!r->failed && r->cur->len > 0) {
        if (ends_with_cr(r->cur)) {
            if (r->breaks == 0)
                r->format = FMT_MAC;
            r->cur->len--;
        }
        end_line(r);
    }
    line_free(r->cur);
    r->cur = NULL;
    /* the magic line, after the last terminated line */
    if (!r->failed && !lines_append(r->ls, NULL, 0))
        r->failed = 1;
    return !r->failed;
}

const char *format_eol(int format)
{
    switch (format) {
    case FMT_UNIX:
        return "\n";
    case FMT_MAC:
        return "\r";
    default:
        return "\r\n";
    }
}

int text_write(const lines *ls, int format, int ctrlz, text_out out,
               void *ctx)
{
    const char *eol = format_eol(format);
    unsigned eollen = format == FMT_UNIX || format == FMT_MAC ? 1 : 2;
    const line *l;

    for (l = ls->top; l != NULL && l != ls->bot; l = l->next) {
        if (l->len > 0 && !out(ctx, l->text, l->len))
            return 0;
        if (!out(ctx, eol, eollen))
            return 0;
    }
    /* the magic line is normally empty; write it if it somehow isn't */
    if (l != NULL && l->len > 0 && !out(ctx, l->text, l->len))
        return 0;
    if (ctrlz && !out(ctx, "\x1a", 1))
        return 0;
    return 1;
}

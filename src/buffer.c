/*
 * buffer.c - lines and lists of lines
 *
 * Portable C: no DOS calls, so the host unit tests build it too.  In
 * the compact model every pointer here is far, so each line's text is
 * its own allocation of up to LINE_MAX_LEN bytes and the buffer as a
 * whole can use all of conventional memory.
 */

#include <stdlib.h>
#include <string.h>
#include "tico.h"

/* Make room for need bytes, with slack for typing. 0 when out of room. */
static int line_reserve(line *l, unsigned need)
{
    unsigned cap;
    char *p;

    if (need <= l->cap)
        return 1;
    if (need > LINE_MAX_LEN)
        return 0;
    cap = need + 16;
    if (l->cap > 0 && need < LINE_MAX_LEN - need / 2)
        cap = need + need / 2;
    if (cap > LINE_MAX_LEN)
        cap = LINE_MAX_LEN;
    p = l->text ? realloc(l->text, cap) : malloc(cap);
    if (p == NULL)
        return 0;
    l->text = p;
    l->cap = cap;
    return 1;
}

line *line_new(const char *text, unsigned len)
{
    line *l = malloc(sizeof *l);

    if (l == NULL)
        return NULL;
    l->prev = l->next = NULL;
    l->text = NULL;
    l->len = l->cap = 0;
    if (len > 0) {
        /* exact fit: loaded lines are the bulk of memory */
        l->text = malloc(len);
        if (l->text == NULL) {
            free(l);
            return NULL;
        }
        memcpy(l->text, text, len);
        l->len = l->cap = len;
    }
    return l;
}

void line_free(line *l)
{
    if (l != NULL) {
        free(l->text);
        free(l);
    }
}

int line_insert(line *l, unsigned at, const char *text, unsigned len)
{
    if (len == 0)
        return 1;
    if (len > LINE_MAX_LEN - l->len || !line_reserve(l, l->len + len))
        return 0;
    memmove(l->text + at + len, l->text + at, l->len - at);
    memcpy(l->text + at, text, len);
    l->len += len;
    return 1;
}

void line_erase(line *l, unsigned at, unsigned len)
{
    if (len == 0)
        return;
    memmove(l->text + at, l->text + at + len, l->len - at - len);
    l->len -= len;
}

void lines_init(lines *ls)
{
    ls->top = ls->bot = NULL;
    ls->count = 0;
}

void lines_clear(lines *ls)
{
    line *l = ls->top, *next;

    while (l != NULL) {
        next = l->next;
        line_free(l);
        l = next;
    }
    lines_init(ls);
}

/* Link l in after `after' (NULL: at the top). */
void lines_link_after(lines *ls, line *after, line *l)
{
    l->prev = after;
    l->next = after ? after->next : ls->top;
    if (l->next)
        l->next->prev = l;
    else
        ls->bot = l;
    if (after)
        after->next = l;
    else
        ls->top = l;
    ls->count++;
}

void lines_unlink(lines *ls, line *l)
{
    if (l->prev)
        l->prev->next = l->next;
    else
        ls->top = l->next;
    if (l->next)
        l->next->prev = l->prev;
    else
        ls->bot = l->prev;
    l->prev = l->next = NULL;
    ls->count--;
}

int lines_append(lines *ls, const char *text, unsigned len)
{
    line *l = line_new(text, len);

    if (l == NULL)
        return 0;
    lines_link_after(ls, ls->bot, l);
    return 1;
}

/* Keep an empty last line (nano's magic line). */
int lines_ensure_magic(lines *ls)
{
    if (ls->bot != NULL && ls->bot->len == 0)
        return 1;
    return lines_append(ls, NULL, 0);
}

/* Break l in two at byte `at'. */
int lines_split(lines *ls, line *l, unsigned at)
{
    line *n = line_new(l->text + at, l->len - at);

    if (n == NULL)
        return 0;
    l->len = at;
    lines_link_after(ls, l, n);
    return 1;
}

/* Join the line after l onto l. */
int lines_join(lines *ls, line *l)
{
    line *n = l->next;

    if (!line_insert(l, l->len, n->text, n->len))
        return 0;
    lines_unlink(ls, n);
    line_free(n);
    return 1;
}

/* Append copies of src's lines to dst. */
int lines_copy(lines *dst, const lines *src)
{
    const line *l;

    for (l = src->top; l != NULL; l = l->next)
        if (!lines_append(dst, l->text, l->len))
            return 0;
    return 1;
}

/* Bytes of text, counting each line break as one (nano's char count). */
long lines_bytes(const lines *ls)
{
    const line *l;
    long n = 0;

    for (l = ls->top; l != NULL; l = l->next) {
        n += l->len;
        if (l->next)
            n++;
    }
    return n;
}

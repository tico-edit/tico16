/*
 * Host-native tests for buffer.c and text.c.
 *
 * The conversion cases mirror tico's convert_line_endings tests
 * (src/fileio.rs), with nano's final newline added on write.
 */

#include <stdio.h>
#include <string.h>
#include "../../src/tico.h"

static int pass, fail;

#define OUTMAX 4096

typedef struct {
    char buf[OUTMAX];
    unsigned len;
} sink;

static int to_sink(void *ctx, const char *buf, unsigned len)
{
    sink *s = ctx;

    if (s->len + len > OUTMAX)
        return 0;
    memcpy(s->buf + s->len, buf, len);
    s->len += len;
    return 1;
}

/* Load raw (n bytes), optionally fed one byte at a time. */
static int load(lines *ls, const char *raw, unsigned n, int bytewise)
{
    reader r;
    unsigned i;

    lines_init(ls);
    reader_start(&r, ls);
    if (bytewise) {
        for (i = 0; i < n; i++)
            reader_feed(&r, raw + i, 1);
    } else {
        reader_feed(&r, raw, n);
    }
    if (!reader_finish(&r))
        return -1;
    return r.format;
}

static void show(const char *s, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++) {
        if (s[i] == '\r')
            printf("\\r");
        else if (s[i] == '\n')
            printf("\\n");
        else
            putchar(s[i]);
    }
}

static void check_bytes(const char *what, const char *got, unsigned gotn,
                        const char *want, unsigned wantn)
{
    if (gotn == wantn && memcmp(got, want, gotn) == 0) {
        pass++;
        return;
    }
    fail++;
    printf("FAIL: %s: got \"", what);
    show(got, gotn);
    printf("\", want \"");
    show(want, wantn);
    printf("\"\n");
}

static void check_int(const char *what, long got, long want)
{
    if (got == want) {
        pass++;
    } else {
        fail++;
        printf("FAIL: %s: got %ld, want %ld\n", what, got, want);
    }
}

/*
 * conv: loading raw gives `format' and text that, written with plain
 * LFs, is `unix'; written back in its own format it is `back'.
 */
static void conv(const char *raw, int format, const char *unix_text,
                 const char *back)
{
    lines ls;
    sink s;
    int bytewise, got;

    for (bytewise = 0; bytewise < 2; bytewise++) {
        got = load(&ls, raw, strlen(raw), bytewise);
        check_int(raw, got, format);
        s.len = 0;
        text_write(&ls, FMT_UNIX, 0, to_sink, &s);
        check_bytes(raw, s.buf, s.len, unix_text, strlen(unix_text));
        s.len = 0;
        text_write(&ls, got, 0, to_sink, &s);
        check_bytes(raw, s.buf, s.len, back, strlen(back));
        check_int("magic line", ls.bot->len, 0);
        lines_clear(&ls);
    }
}

static void test_conversion(void)
{
    /* crlf is stripped and the first line break decides DOS format */
    conv("a\r\nb\r\n", FMT_DOS, "a\nb\n", "a\r\nb\r\n");
    conv("a\nb\r\n", FMT_UNIX, "a\nb\n", "a\nb\n");
    conv("a\r\r\n", FMT_MAC, "a\n\n", "a\r\r");
    conv("a\r\nb\r\r\n", FMT_DOS, "a\nb\r\n", "a\r\nb\r\r\n");
    conv("\r\n", FMT_DOS, "\n", "\r\n");
    /* bare CR line breaks make Mac format only from the first line */
    conv("a\rb\r", FMT_MAC, "a\nb\n", "a\rb\r");
    conv("a\rb\rc", FMT_MAC, "a\nb\nc\n", "a\rb\rc\r");
    conv("a\rb\r\nc", FMT_MAC, "a\nb\nc\n", "a\rb\rc\r");
    conv("a\nb\rc\n", FMT_UNIX, "a\nb\rc\n", "a\nb\rc\n");
    conv("a\r\nb\rc\r\n", FMT_DOS, "a\nb\rc\n", "a\r\nb\rc\r\n");
    conv("a\r", FMT_MAC, "a\n", "a\r");
    conv("\r", FMT_MAC, "\n", "\r");
    /* no line break at all: no format; writes DOS, adds the newline */
    conv("abc", FMT_NONE, "abc\n", "abc\r\n");
    conv("", FMT_NONE, "", "");
    conv("\n", FMT_UNIX, "\n", "\n");
    conv("\n\n", FMT_UNIX, "\n\n", "\n\n");
    conv("a\tb\001\377\n", FMT_UNIX, "a\tb\001\377\n", "a\tb\001\377\n");
}

static void test_nul_and_ctrlz(void)
{
    static const char raw[] = "a\0b\r\n\x1a" "c\r\n";
    lines ls;
    sink s;

    check_int("nul format", load(&ls, raw, sizeof raw - 1, 0), FMT_DOS);
    check_int("nul line len", ls.top->len, 3);
    s.len = 0;
    text_write(&ls, FMT_DOS, 1, to_sink, &s);
    check_bytes("nul+ctrlz", s.buf, s.len, "a\0b\r\n\x1a" "c\r\n\x1a", 10);
    lines_clear(&ls);
}

static void test_long_line(void)
{
    static char big[LINE_MAX_LEN + 10];
    lines ls;

    memset(big, 'x', sizeof big);
    check_int("max line", load(&ls, big, LINE_MAX_LEN, 0), FMT_NONE);
    check_int("max line len", ls.top->len, LINE_MAX_LEN);
    lines_clear(&ls);
    check_int("too long", load(&ls, big, LINE_MAX_LEN + 1, 0), -1);
    lines_clear(&ls);
}

static void test_edit(void)
{
    lines ls, copy;
    sink s;

    load(&ls, "hello world\n", 12, 0);
    lines_split(&ls, ls.top, 5);
    check_int("split count", ls.count, 3);
    line_erase(ls.top->next, 0, 1);
    line_insert(ls.top, 5, ",", 1);
    s.len = 0;
    text_write(&ls, FMT_UNIX, 0, to_sink, &s);
    check_bytes("split", s.buf, s.len, "hello,\nworld\n", 13);
    lines_join(&ls, ls.top);
    s.len = 0;
    text_write(&ls, FMT_UNIX, 0, to_sink, &s);
    check_bytes("join", s.buf, s.len, "hello,world\n", 12);
    check_int("bytes", lines_bytes(&ls), 12);
    lines_init(&copy);
    lines_copy(&copy, &ls);
    check_int("copy count", copy.count, ls.count);
    lines_clear(&copy);
    lines_clear(&ls);
}

int main(void)
{
    test_conversion();
    test_nul_and_ctrlz();
    test_long_line();
    test_edit();
    printf("unit: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}

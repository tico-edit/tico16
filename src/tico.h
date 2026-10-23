/*
 * tico16 - a tico/nano-style text editor for 16-bit DOS
 *
 * Shared declarations.  buffer.c and text.c are portable C so they can
 * be unit-tested on the build host; dos.c holds everything that talks
 * to DOS or the BIOS; editor.c is the editor itself.
 */

#ifndef TICO_H
#define TICO_H

#include <stddef.h>

#ifndef VERSION
#define VERSION "unknown"
#endif

/* ------------------------------------------------------------------ */
/* buffer.c: the text, as a doubly linked list of lines.               */

/*
 * One line of text, without its line break.  The bytes are arbitrary
 * (NULs included); text is NULL while cap is 0.
 */
typedef struct line {
    struct line *prev, *next;
    char *text;
    unsigned len, cap;
} line;

/*
 * A list of lines.  In the edit buffer the last line is always empty:
 * nano's "magic line", which stands for the end of the file, so the
 * file's bytes are every line but the last, each followed by a break.
 */
typedef struct {
    line *top, *bot;
    long count;
} lines;

/* Longest line the buffer can hold (one far allocation). */
#define LINE_MAX_LEN 65000u

line *line_new(const char *text, unsigned len);
void line_free(line *l);
int line_insert(line *l, unsigned at, const char *text, unsigned len);
void line_erase(line *l, unsigned at, unsigned len);

void lines_init(lines *ls);
void lines_clear(lines *ls);
void lines_link_after(lines *ls, line *after, line *l);
void lines_unlink(lines *ls, line *l);
int lines_append(lines *ls, const char *text, unsigned len);
int lines_ensure_magic(lines *ls);
int lines_split(lines *ls, line *l, unsigned at);
int lines_join(lines *ls, line *l);
int lines_copy(lines *dst, const lines *src);
long lines_bytes(const lines *ls);

/* ------------------------------------------------------------------ */
/* text.c: line-ending conversion, as nano 8.7.1 / tico do it.         */

enum { FMT_NONE, FMT_UNIX, FMT_DOS, FMT_MAC };

/*
 * Streaming reader.  Feed it the file's bytes in any number of pieces,
 * then finish it; it builds the buffer's lines (with the magic line).
 */
typedef struct {
    lines *ls;
    line *cur;          /* line being accumulated */
    long breaks;        /* line breaks seen so far */
    int format;         /* FMT_NONE until a break decides it */
    int failed;         /* out of memory, or a line too long */
} reader;

void reader_start(reader *r, lines *ls);
int reader_feed(reader *r, const char *buf, unsigned len);
int reader_finish(reader *r);

/* The bytes that end a line in a format (FMT_NONE writes DOS). */
const char *format_eol(int format);

/* Serialize: calls out(ctx, bytes, len) for each piece of the file. */
typedef int (*text_out)(void *ctx, const char *buf, unsigned len);
int text_write(const lines *ls, int format, int ctrlz, text_out out,
               void *ctx);

/* ------------------------------------------------------------------ */
/* dos.c: screen, keyboard and files.                                  */

/* Keys.  Plain bytes are 0x00-0xFF; others are coded above that. */
#define KEY_EXT(scan)  (0x100 | (scan))   /* BIOS scan code, no ASCII */
#define KEY_META(c)    (0x200 | (c))      /* Alt+key or Esc, key */

#define KEY_UP      KEY_EXT(0x48)
#define KEY_DOWN    KEY_EXT(0x50)
#define KEY_LEFT    KEY_EXT(0x4B)
#define KEY_RIGHT   KEY_EXT(0x4D)
#define KEY_HOME    KEY_EXT(0x47)
#define KEY_END     KEY_EXT(0x4F)
#define KEY_PGUP    KEY_EXT(0x49)
#define KEY_PGDN    KEY_EXT(0x51)
#define KEY_INS     KEY_EXT(0x52)
#define KEY_DEL     KEY_EXT(0x53)
#define KEY_CHOME   KEY_EXT(0x77)
#define KEY_CEND    KEY_EXT(0x75)
#define KEY_F1      KEY_EXT(0x3B)   /* F1-F10 are 0x3B-0x44 */
#define KEY_F10     KEY_EXT(0x44)
#define KEY_F11     KEY_EXT(0x85)
#define KEY_F12     KEY_EXT(0x86)
#define KEY_BSP     0x08
#define KEY_TAB     0x09
#define KEY_ENTER   0x0D
#define KEY_ESC     0x1B
#define CTRL(c)     ((c) & 0x1F)

/* Attributes. */
enum { ATTR_NORMAL, ATTR_REVERSE, ATTR_ERROR };

extern int scr_rows, scr_cols;

int scr_init(void);
void scr_done(void);
void scr_put(int row, int col, const char *s, int len, int attr);
void scr_fill(int row, int col, int len, int attr);
void scr_cursor(int row, int col);
void scr_scroll(int top, int bottom, int up);
void scr_beep(void);

int key_get(void);

/* File access.  Errors are reported as strings for the status line. */
enum { LOAD_OK, LOAD_NEW, LOAD_ERR };
int file_load(const char *name, lines *ls, int *format, int *ctrlz,
              const char **err);
int file_exists(const char *name);
int file_save(const char *name, const lines *ls, int format, int ctrlz,
              const char **err, int *kept_tmp);
void sys_init(void);
void sys_done(void);

/* ------------------------------------------------------------------ */
/* editor.c */

int editor_run(const char *name);

#endif

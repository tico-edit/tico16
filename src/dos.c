/*
 * dos.c - screen, keyboard, files and interrupt handlers (Open Watcom)
 *
 * The screen is written directly in text video memory; the BIOS only
 * sets the cursor and scrolls.  Keys come from INT 16h.  Files use the
 * DOS handle calls in binary mode.
 *
 * The test build (-DTICO_TEST) reads keys from the file named by
 * %TICOKEYS% instead of the keyboard, and appends screen dumps to the
 * file named by %TICOSCR%; see tests/run.sh.  With %TICOVRAM% set, each
 * dump also appends the raw text video memory (characters and
 * attributes) to that file, for making screenshots.
 */

#include <dos.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include "tico.h"

/* ------------------------------------------------------------------ */
/* screen */

int scr_rows, scr_cols;

static unsigned short __far *vram;
static unsigned char page;
static unsigned char attrs[3];
static unsigned short *saved;           /* the DOS screen, restored at exit */
static unsigned char saved_row, saved_col;

#define BIOS_BYTE(off)  (*(unsigned char __far *)MK_FP(0x40, (off)))
#define BIOS_WORD(off)  (*(unsigned short __far *)MK_FP(0x40, (off)))

int scr_init(void)
{
    union REGS r;
    int mode, mono;
    unsigned n;

    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    mode = r.h.al & 0x7F;
    mono = mode == 7;
    if (mode > 3 && !mono) {
        /* a graphics mode: switch to 80x25 text */
        mono = (BIOS_WORD(0x10) & 0x30) == 0x30;
        r.h.ah = 0x00;
        r.h.al = mono ? 7 : 3;
        int86(0x10, &r, &r);
    }
    scr_cols = BIOS_WORD(0x4A);
    page = BIOS_BYTE(0x62);

    /* EGA and later keep the row count in the BIOS data area */
    r.h.ah = 0x12;
    r.h.bl = 0x10;
    int86(0x10, &r, &r);
    scr_rows = r.h.bl != 0x10 ? BIOS_BYTE(0x84) + 1 : 25;

    vram = MK_FP(mono ? 0xB000 : 0xB800, BIOS_WORD(0x4E));
    attrs[ATTR_NORMAL] = 0x07;
    attrs[ATTR_REVERSE] = 0x70;
    attrs[ATTR_ERROR] = mono ? 0x70 : 0x4F;

    r.h.ah = 0x03;
    r.h.bh = page;
    int86(0x10, &r, &r);
    saved_row = r.h.dh;
    saved_col = r.h.dl;
    n = scr_rows * scr_cols;
    saved = malloc(n * sizeof *saved);
    if (saved != NULL)
        _fmemcpy(saved, vram, n * sizeof *saved);
    return scr_cols >= 20 && scr_rows >= 6;
}

void scr_put(int row, int col, const char *s, int len, int attr)
{
    unsigned short __far *p = vram + row * scr_cols + col;
    unsigned short a = (unsigned short)attrs[attr] << 8;

    if (len > scr_cols - col)
        len = scr_cols - col;
    while (len-- > 0)
        *p++ = a | (unsigned char)*s++;
}

void scr_fill(int row, int col, int len, int attr)
{
    unsigned short __far *p = vram + row * scr_cols + col;
    unsigned short cell = ((unsigned short)attrs[attr] << 8) | ' ';

    if (len > scr_cols - col)
        len = scr_cols - col;
    while (len-- > 0)
        *p++ = cell;
}

void scr_cursor(int row, int col)
{
    union REGS r;

    r.h.ah = 0x02;
    r.h.bh = page;
    r.h.dh = row;
    r.h.dl = col;
    int86(0x10, &r, &r);
}

/* Scroll rows top..bottom by one line, up or down, blanking the new row. */
void scr_scroll(int top, int bottom, int up)
{
    union REGS r;

    r.h.ah = up ? 0x06 : 0x07;
    r.h.al = 1;
    r.h.bh = attrs[ATTR_NORMAL];
    r.h.ch = top;
    r.h.cl = 0;
    r.h.dh = bottom;
    r.h.dl = scr_cols - 1;
    int86(0x10, &r, &r);
}

void scr_beep(void)
{
    union REGS r;

    r.h.ah = 0x0E;
    r.h.al = 7;
    r.h.bh = page;
    int86(0x10, &r, &r);
}

#ifdef TICO_TEST
static void dump_screen(const char *why);
#endif

void scr_done(void)
{
#ifdef TICO_TEST
    dump_screen("exit");
#endif
    if (saved != NULL) {
        _fmemcpy(vram, saved, scr_rows * scr_cols * sizeof *saved);
        free(saved);
        saved = NULL;
    }
    scr_cursor(saved_row, saved_col);
}

/* ------------------------------------------------------------------ */
/* keyboard */

/* Alt+letter scan codes, from 0x10 */
static const char alt_letters[] =
    "QWERTYUIOP\0\0\0\0ASDFGHJKL\0\0\0\0\0ZXCVBNM";

/* Translate an INT 16h key (AH scan, AL ASCII); -1 for nothing. */
static int translate(unsigned ax)
{
    unsigned char al = ax & 0xFF, ah = ax >> 8;

    if (ax == 0)
        return -1;                      /* Ctrl+Break */
    if (al == 0xE0 && ah != 0)
        return KEY_EXT(ah);             /* grey keys, enhanced BIOS */
    if (al != 0)
        return al;
    if (ah >= 0x10 && ah < 0x10 + sizeof alt_letters - 1 &&
        alt_letters[ah - 0x10] != 0)
        return KEY_META(alt_letters[ah - 0x10]);
    if (ah >= 0x78 && ah <= 0x80)
        return KEY_META('1' + ah - 0x78);
    if (ah == 0x81)
        return KEY_META('0');
    if (ah == 0x2B)
        return KEY_META('\\');
    if (ah == 0x35)
        return KEY_META('/');
    return KEY_EXT(ah);
}

#ifndef TICO_TEST

int key_get(void)
{
    static int enhanced = -1;
    union REGS r;
    int k;

    if (enhanced < 0)
        enhanced = (BIOS_BYTE(0x96) & 0x10) != 0;
    do {
        r.h.ah = enhanced ? 0x10 : 0x00;
        int86(0x16, &r, &r);
        k = translate(r.x.ax);
    } while (k < 0);
    return k;
}

#else /* TICO_TEST */

#include <libc16.h>
#include <stdio.h>

/*
 * The key script: bytes are typed as they are, except that newlines
 * are ignored and {NAME} names a key: {^X} (Ctrl), {M-x} (Alt), {Up},
 * {Down}, {Left}, {Right}, {Home}, {End}, {PgUp}, {PgDn}, {Ins}, {Del},
 * {^Home}, {^End}, {Bsp}, {Tab}, {Enter}, {Esc}, {F1}..{F12}, {Brk},
 * {lb} for a literal '{', {xHH} for a byte, and {Dump} to append the
 * screen to %TICOSCR%.  When the script runs out the editor stops with
 * exit status 3.
 */

static char script[8192];
static unsigned script_len, script_pos;
static int script_loaded;

static const struct {
    const char *name;
    unsigned ax;
} named_keys[] = {
    {"Up", 0x4800}, {"Down", 0x5000}, {"Left", 0x4B00}, {"Right", 0x4D00},
    {"Home", 0x4700}, {"End", 0x4F00}, {"PgUp", 0x4900}, {"PgDn", 0x5100},
    {"Ins", 0x5200}, {"Del", 0x5300}, {"^Home", 0x7700}, {"^End", 0x7500},
    {"Bsp", 0x0E08}, {"Tab", 0x0F09}, {"Enter", 0x1C0D}, {"Esc", 0x011B},
    {"Brk", 0x0000}, {"lb", '{'},
    {"F1", 0x3B00}, {"F2", 0x3C00}, {"F3", 0x3D00}, {"F4", 0x3E00},
    {"F5", 0x3F00}, {"F6", 0x4000}, {"F7", 0x4100}, {"F8", 0x4200},
    {"F9", 0x4300}, {"F10", 0x4400}, {"F11", 0x8500}, {"F12", 0x8600},
    {"M-6", 0x7D00}, {"M-\\", 0x2B00}, {"M-/", 0x3500},
};

static void dump_screen(const char *why)
{
    const char *path = getenv("TICOSCR");
    const char *raw = getenv("TICOVRAM");
    union REGS r;
    FILE *fp;
    int row, col, end;
    unsigned short __far *p;

    r.h.ah = 0x03;
    r.h.bh = page;
    int86(0x10, &r, &r);
    if (path != NULL && (fp = fopen(path, "ab")) != NULL) {
        char num[12];

        fputs("=== ", fp);
        fputs(why, fp);
        fputs(" cursor ", fp);
        fputs(libc16_ltoa(r.h.dh, num, 10), fp);
        fputc(' ', fp);
        fputs(libc16_ltoa(r.h.dl, num, 10), fp);
        fputs("\r\n", fp);
        for (row = 0; row < scr_rows; row++) {
            p = vram + row * scr_cols;
            for (end = scr_cols; end > 0 && (p[end - 1] & 0xFF) == ' ';
                 end--)
                ;
            for (col = 0; col < end; col++)
                fputc(p[col] & 0xFF, fp);
            fputs("\r\n", fp);
        }
        fclose(fp);
    }
    /* raw: rows, columns, cursor row and column, then the cells */
    if (raw != NULL && (fp = fopen(raw, "ab")) != NULL) {
        fputc(scr_rows, fp);
        fputc(scr_cols, fp);
        fputc(r.h.dh, fp);
        fputc(r.h.dl, fp);
        fwrite(vram, sizeof *vram, scr_rows * scr_cols, fp);
        fclose(fp);
    }
}

static void load_script(void)
{
    const char *path = getenv("TICOKEYS");
    FILE *fp;

    script_loaded = 1;
    if (path == NULL || (fp = fopen(path, "rb")) == NULL)
        return;
    script_len = fread(script, 1, sizeof script, fp);
    fclose(fp);
}

static void script_end(void)
{
    scr_done();
    sys_done();
    exit(3);
}

/* The next key from the script, as INT 16h would report it. */
static unsigned script_key(void)
{
    char name[16];
    unsigned i, n;
    char c;

    for (;;) {
        if (script_pos >= script_len)
            script_end();
        c = script[script_pos++];
        if (c == '\r' || c == '\n')
            continue;
        if (c != '{')
            return (unsigned char)c;
        for (n = 0; script_pos < script_len && script[script_pos] != '}';
             script_pos++)
            if (n < sizeof name - 1)
                name[n++] = script[script_pos];
        script_pos++;
        name[n] = 0;
        if (strcmp(name, "Dump") == 0) {
            dump_screen("dump");
            continue;
        }
        if (n == 2 && name[0] == '^')
            return CTRL(name[1]);
        if (n == 3 && name[0] == 'x')
            return (unsigned)strtoul(name + 1, NULL, 16);
        for (i = 0; i < sizeof named_keys / sizeof named_keys[0]; i++)
            if (strcmp(name, named_keys[i].name) == 0)
                return named_keys[i].ax;
        if (n == 3 && name[0] == 'M' && name[1] == '-') {
            /* other Alt keys arrive as Esc, key */
            script_pos -= 2;
            script[script_pos] = name[2];
            script[script_pos + 1] = '\n';
            return 0x011B;
        }
        /* unknown name: ignore it */
    }
}

int key_get(void)
{
    int k;

    if (!script_loaded)
        load_script();
    do
        k = translate(script_key());
    while (k < 0);
    return k;
}

#endif /* TICO_TEST */

/* ------------------------------------------------------------------ */
/* interrupts */

static void (__interrupt __far *old_1b)(void);

static void __interrupt __far ignore_break(void)
{
}

/* libc16 calls a _harderr handler __cdecl (its docs/spec/dos.md) */
static int __cdecl __far fail_critical(unsigned deverr, unsigned errcode,
                                       unsigned __far *devhdr)
{
    (void)deverr;
    (void)errcode;
    (void)devhdr;
    return _HARDERR_FAIL;
}

void sys_init(void)
{
    /* DOS restores 23h and 24h from the PSP at exit; 1Bh is ours to undo */
    _dos_setvect(0x23, ignore_break);
    old_1b = _dos_getvect(0x1B);
    _dos_setvect(0x1B, ignore_break);
    _harderr(fail_critical);
}

void sys_done(void)
{
    if (old_1b != NULL) {
        _dos_setvect(0x1B, old_1b);
        old_1b = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* files */

static char errbuf[200];

static const char *dos_error(unsigned code)
{
    switch (code) {
    case 2:
    case 3:
        return "No such file or directory";
    case 4:
        return "Too many open files";
    case 5:
        return "Permission denied";
    case 8:
        return "Not enough memory";
    case 15:
        return "No such device";
    case 19:
        return "Read-only file system";
    default:
        return "Input/output error";
    }
}

static unsigned dos_unlink(const char *name)
{
    union REGS r;
    struct SREGS s;

    segread(&s);
    r.h.ah = 0x41;
    s.ds = FP_SEG(name);
    r.x.dx = FP_OFF(name);
    intdosx(&r, &r, &s);
    return r.x.cflag ? r.x.ax : 0;
}

static unsigned dos_rename(const char *from, const char *to)
{
    union REGS r;
    struct SREGS s;

    segread(&s);
    r.h.ah = 0x56;
    s.ds = FP_SEG(from);
    r.x.dx = FP_OFF(from);
    s.es = FP_SEG(to);
    r.x.di = FP_OFF(to);
    intdosx(&r, &r, &s);
    return r.x.cflag ? r.x.ax : 0;
}

#define IOBUF 4096
static char iobuf[IOBUF];

int file_exists(const char *name)
{
    unsigned attr;

    return _dos_getfileattr(name, &attr) == 0;
}

int file_load(const char *name, lines *ls, int *format, int *ctrlz,
              const char **err)
{
    unsigned attr, code, got;
    int h;
    reader r;
    int pending = -1;                   /* held-back last byte */

    *ctrlz = 0;
    *format = FMT_NONE;
    code = _dos_getfileattr(name, &attr);
    if (code == 2 || code == 3)
        return LOAD_NEW;
    if (code == 0 && (attr & _A_SUBDIR)) {
        *err = "Is a directory";
        return LOAD_ERR;
    }
    if (code == 0)
        code = _dos_open(name, O_RDONLY, &h);
    if (code != 0) {
        *err = dos_error(code);
        return LOAD_ERR;
    }
    reader_start(&r, ls);
    /*
     * Feed everything but the file's last byte as it comes; a Ctrl+Z
     * in that position is kept aside rather than read as text.
     */
    for (;;) {
        code = _dos_read(h, iobuf, IOBUF, &got);
        if (code != 0 || got == 0)
            break;
        if (pending >= 0) {
            char c = (char)pending;
            reader_feed(&r, &c, 1);
        }
        pending = (unsigned char)iobuf[got - 1];
        if (!reader_feed(&r, iobuf, got - 1))
            break;
    }
    _dos_close(h);
    if (code == 0 && pending >= 0) {
        if (pending == 0x1A) {
            *ctrlz = 1;
        } else {
            char c = (char)pending;
            reader_feed(&r, &c, 1);
        }
    }
    if (!reader_finish(&r) || code != 0) {
        lines_clear(ls);
        *err = code != 0 ? dos_error(code) : "Not enough memory";
        return LOAD_ERR;
    }
    *format = r.format;
    return LOAD_OK;
}

typedef struct {
    int handle;
    unsigned fill;
    unsigned code;      /* DOS error, or 0xFFFF for a full disk */
} writer;

static int flush(writer *w)
{
    unsigned put;

    if (w->fill == 0 || w->code != 0)
        return w->code == 0;
    w->code = _dos_write(w->handle, iobuf, w->fill, &put);
    if (w->code == 0 && put != w->fill)
        w->code = 0xFFFF;
    w->fill = 0;
    return w->code == 0;
}

static int write_out(void *ctx, const char *buf, unsigned len)
{
    writer *w = ctx;
    unsigned n;

    while (len > 0) {
        n = IOBUF - w->fill;
        if (n > len)
            n = len;
        memcpy(iobuf + w->fill, buf, n);
        w->fill += n;
        buf += n;
        len -= n;
        if (w->fill == IOBUF && !flush(w))
            return 0;
    }
    return 1;
}

/* NAME.EXT -> NAME.$$$ in the same directory; 0 if the name is too long. */
static int temp_name(const char *name, char *tmp, unsigned size)
{
    const char *base = name, *p;
    const char *dot = NULL;
    unsigned n;

    for (p = name; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    for (p = base; *p; p++)
        if (*p == '.')
            dot = p;
    n = dot ? (unsigned)(dot - name) : (unsigned)strlen(name);
    if (n + 5 > size)
        return 0;
    memcpy(tmp, name, n);
    strcpy(tmp + n, ".$$$");
    if (stricmp(tmp, name) == 0)
        strcpy(tmp + n, ".$$~");
    return 1;
}

static const char *save_error(const char *what, unsigned code)
{
    strcpy(errbuf, what);
    strcat(errbuf, code == 0xFFFF ? "No space left on device"
                                  : dos_error(code));
    return errbuf;
}

/*
 * Write to NAME.$$$, then delete NAME and rename NAME.$$$ to NAME.
 * DOS can't rename over an existing file, so this is not atomic:
 * between the two steps the text is only in NAME.$$$, and if the
 * rename fails it stays there (*kept_tmp is set and the error says so).
 */
int file_save(const char *name, const lines *ls, int format, int ctrlz,
              const char **err, int *kept_tmp)
{
    static char tmp[140];
    unsigned attr, code;
    int exists;
    writer w;

    *kept_tmp = 0;
    code = _dos_getfileattr(name, &attr);
    exists = code == 0;
    if (exists && (attr & _A_SUBDIR)) {
        *err = "Is a directory";
        return 0;
    }
    if (exists && (attr & _A_RDONLY)) {
        *err = "Permission denied";
        return 0;
    }
    if (!temp_name(name, tmp, sizeof tmp)) {
        *err = "File name too long";
        return 0;
    }
    code = _dos_creat(tmp, _A_NORMAL, &w.handle);
    if (code != 0) {
        *err = dos_error(code);
        return 0;
    }
    w.fill = 0;
    w.code = 0;
    text_write(ls, format, ctrlz, write_out, &w);
    flush(&w);
    code = _dos_close(w.handle);
    if (w.code == 0 && code != 0)
        w.code = code;
    if (w.code != 0) {
        dos_unlink(tmp);
        *err = save_error("", w.code);
        return 0;
    }
    if (exists && (code = dos_unlink(name)) != 0) {
        dos_unlink(tmp);
        *err = dos_error(code);
        return 0;
    }
    if ((code = dos_rename(tmp, name)) != 0) {
        *kept_tmp = 1;
        strcpy(errbuf, dos_error(code));
        strcat(errbuf, "; text saved in ");
        strncat(errbuf, tmp, sizeof errbuf - strlen(errbuf) - 1);
        *err = errbuf;
        return 0;
    }
    if (exists)
        _dos_setfileattr(name, (attr & (_A_HIDDEN | _A_SYSTEM)) | _A_ARCH);
    return 1;
}

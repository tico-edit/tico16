/*
 * editor.c - the editor: commands, prompts and drawing
 *
 * Behavior follows tico (and through it nano 8.7.1); the comments name
 * the tico/nano function a piece mirrors where that helps.
 *
 * Screen layout, as in tico without `emptyline': the title bar on row
 * 0, the text on rows 1..text_rows, the status/prompt line below it
 * and the two-line shortcut bar at the bottom.
 */

#include <stdio.h>
#include <string.h>
#include "tico.h"

#define TABSIZE 8
#define NAMEMAX 128

static lines buf;
static line *cur;               /* cursor line */
static long cur_no;             /* its number, from 1 */
static unsigned cur_x;          /* byte offset in the line */
static long want;               /* nano's placewewant: the goal column */
static long top_no;             /* line number on the top text row */
static int text_rows;

static char filename[NAMEMAX];  /* empty: an unnamed buffer */
static int format;
static int ctrlz;               /* trailing Ctrl+Z, kept out of the text */
static int modified;

static lines cutbuf;            /* text as lines; "a\n" is {"a", ""} */
static int cut_run;             /* the last command was a line cut */

static char status[100];
static int status_attr;
static int status_count;        /* keystrokes until it is wiped */

static int quitting;
static int meta_pending;        /* Esc seen: the next key is Meta */

/* prompts */
enum { P_NONE, P_WRITE, P_EXIT, P_DIFFNAME, P_OVERWRITE };
static int prompt;
static char prompt_label[NAMEMAX + 40];
static char answer[NAMEMAX];
static unsigned answer_len, answer_x;
static int exiting;             /* the write was asked for by ^X */

/* drawing state: what is on the screen, and what needs redrawing */
static int full_redraw = 1;
static long redraw_from;        /* redraw lines numbered from here on */
static long dirty[3];           /* and these lines */
static long drawn_top_no, drawn_cur_no, drawn_left;
static int redraw_title = 1, redraw_status = 1, redraw_bar = 1;

#define NONE 0x7FFFFFFFL

/* ------------------------------------------------------------------ */
/* columns */

static int char_width(unsigned char c, long col)
{
    if (c == '\t')
        return TABSIZE - (int)(col % TABSIZE);
    if (c < 0x20 || c == 0x7F)
        return 2;
    return 1;
}

/* Display column of byte x of l. */
static long disp_col(const line *l, unsigned x)
{
    long col = 0;
    unsigned i;

    for (i = 0; i < x; i++)
        col += char_width(l->text[i], col);
    return col;
}

/* nano's actual_x: the byte at or just before display column goal. */
static unsigned actual_x(const line *l, long goal)
{
    long col = 0, next;
    unsigned i;

    for (i = 0; i < l->len; i++) {
        next = col + char_width(l->text[i], col);
        if (next > goal)
            break;
        col = next;
    }
    return i;
}

/* nano 8.7.1's get_page_start (tico's scroll_horizontal_to_cursor). */
static long page_start(long column)
{
    int width = scr_cols;

    if (column == 0 || column + 2 < width)
        return 0;
    if (width > 8)
        return column - 6 - (column - 6) % (width - 8);
    return column - (width - 2);
}

/* ------------------------------------------------------------------ */
/* status line */

static void set_status_at(const char *msg, int attr, int count)
{
    strncpy(status, msg, sizeof status - 1);
    status[sizeof status - 1] = 0;
    status_attr = attr;
    status_count = count;
    redraw_status = 1;
}

static void set_status(const char *msg)
{
    set_status_at(msg, ATTR_REVERSE, 20);
}

static void set_alert(const char *msg)
{
    set_status_at(msg, ATTR_ERROR, 20);
    scr_beep();
}

static void clear_status(void)
{
    if (status[0]) {
        status[0] = 0;
        redraw_status = 1;
    }
}

/* ------------------------------------------------------------------ */
/* marking things for redraw */

static void mark_line(long n)
{
    int i;

    for (i = 0; i < 3; i++)
        if (dirty[i] == n)
            return;
    for (i = 0; i < 3; i++)
        if (dirty[i] == NONE) {
            dirty[i] = n;
            return;
        }
    if (n < redraw_from)
        redraw_from = n;
}

static void mark_from(long n)
{
    if (n < redraw_from)
        redraw_from = n;
}

static void set_modified(void)
{
    if (!modified) {
        modified = 1;
        redraw_title = 1;
    }
}

/* ------------------------------------------------------------------ */
/* moving */

static void update_want(void)
{
    want = disp_col(cur, cur_x);
}

static void go_up(void)
{
    if (cur->prev) {
        cur = cur->prev;
        cur_no--;
        cur_x = actual_x(cur, want);
    }
}

static void go_down(void)
{
    if (cur->next) {
        cur = cur->next;
        cur_no++;
        cur_x = actual_x(cur, want);
    }
}

static void go_left(void)
{
    if (cur_x > 0) {
        cur_x--;
    } else if (cur->prev) {
        cur = cur->prev;
        cur_no--;
        cur_x = cur->len;
    }
    update_want();
}

static void go_right(void)
{
    if (cur_x < cur->len) {
        cur_x++;
    } else if (cur->next) {
        cur = cur->next;
        cur_no++;
        cur_x = 0;
    }
    update_want();
}

static void go_first_line(void)
{
    cur = buf.top;
    cur_no = 1;
    cur_x = 0;
    want = 0;
}

static void go_last_line(void)
{
    cur = buf.bot;
    cur_no = buf.count;
    cur_x = 0;
    want = 0;
}

/*
 * nano's do_page_up/do_page_down: two lines short of a screenful,
 * keeping the cursor on the same screen row and aiming for the same
 * column; without that many lines left, to the start of the first line
 * or the end of the last.
 */
static void go_page(int forward)
{
    long mustmove = text_rows < 3 ? 1 : text_rows - 2;
    long row = cur_no - top_no, i;

    if (!forward && cur_no - 1 < mustmove) {
        go_first_line();
        return;
    }
    if (forward && buf.count - cur_no < mustmove) {
        cur = buf.bot;
        cur_no = buf.count;
        cur_x = cur->len;
        update_want();
        return;
    }
    for (i = 0; i < mustmove; i++) {
        cur = forward ? cur->next : cur->prev;
        cur_no += forward ? 1 : -1;
    }
    cur_x = actual_x(cur, want);
    top_no = cur_no - row;
}

/* ------------------------------------------------------------------ */
/* editing */

static void out_of_memory(void)
{
    set_alert("Out of memory");
}

static void insert_byte(char c)
{
    if (!line_insert(cur, cur_x, &c, 1)) {
        out_of_memory();
        return;
    }
    cur_x++;
    update_want();
    set_modified();
    mark_line(cur_no);
    /* typing on the magic line makes a new one */
    if (cur == buf.bot && !lines_ensure_magic(&buf))
        out_of_memory();
}

static void do_enter(void)
{
    if (!lines_split(&buf, cur, cur_x)) {
        out_of_memory();
        return;
    }
    mark_from(cur_no);
    cur = cur->next;
    cur_no++;
    cur_x = 0;
    want = 0;
    set_modified();
}

/* nano's do_deletion(FORWARD). */
static void do_delete(void)
{
    if (cur_x < cur->len) {
        line_erase(cur, cur_x, 1);
        mark_line(cur_no);
    } else if (cur->next) {
        /* don't eat the magic line from the end of a non-empty line */
        if (cur->next == buf.bot && cur_x != 0)
            return;
        if (!lines_join(&buf, cur)) {
            out_of_memory();
            return;
        }
        mark_from(cur_no);
    } else {
        return;
    }
    set_modified();
}

static void do_backspace(void)
{
    if (cur_x > 0) {
        cur_x--;
        update_want();
        do_delete();
    } else if (cur->prev) {
        go_left();
        do_delete();
    }
}

static void cut_to_cutbuf(line *l)
{
    lines_unlink(&buf, l);
    lines_link_after(&cutbuf, cutbuf.bot->prev, l);
}

/* ^K: nano's cut_text for a whole line; ^K's in a row add up. */
static void do_cut(void)
{
    line *l = cur;

    if (cur->next == NULL) {
        set_status("Nothing was cut");
        return;
    }
    if (!cut_run) {
        lines_clear(&cutbuf);
        if (!lines_append(&cutbuf, NULL, 0)) {
            out_of_memory();
            return;
        }
    }
    cur = cur->next;
    cut_to_cutbuf(l);
    cur_x = 0;
    want = 0;
    cut_run = 1;
    clear_status();
    mark_from(cur_no);
    set_modified();
}

/* M-6: tico's do_copy, the current line. */
static void do_copy(void)
{
    lines_clear(&cutbuf);
    if (!lines_append(&cutbuf, cur->text, cur->len) ||
        !lines_append(&cutbuf, NULL, 0)) {
        lines_clear(&cutbuf);
        out_of_memory();
        return;
    }
    set_status("Copied");
}

/* ^U: insert the cut buffer at the cursor. */
static void do_paste(void)
{
    lines add;
    line *first, *last, *l, *next;
    long n;

    if (cutbuf.top == NULL)
        return;
    first = cutbuf.top;
    last = cutbuf.bot;
    if (first == last) {
        if (!line_insert(cur, cur_x, first->text, first->len)) {
            out_of_memory();
            return;
        }
        cur_x += first->len;
        mark_line(cur_no);
    } else {
        /*
         * Build the new lines first: copies of the middle ones, then the
         * last one followed by the text after the cursor.  Only then
         * change the cursor line, so running out of memory changes
         * nothing.
         */
        lines_init(&add);
        for (l = first->next; l != last; l = l->next)
            if (!lines_append(&add, l->text, l->len))
                break;
        if (l != last || !lines_append(&add, last->text, last->len) ||
            !line_insert(add.bot, add.bot->len, cur->text + cur_x,
                         cur->len - cur_x) ||
            !line_insert(cur, cur_x, first->text, first->len)) {
            lines_clear(&add);
            out_of_memory();
            return;
        }
        cur->len = cur_x + first->len;
        mark_from(cur_no);
        n = add.count;
        for (l = add.top; l != NULL; l = next) {
            next = l->next;
            l->prev = l->next = NULL;
            lines_link_after(&buf, cur, l);
            cur = l;
        }
        cur_no += n;
        cur_x = last->len;
    }
    update_want();
    set_modified();
    if (!lines_ensure_magic(&buf))
        out_of_memory();
    set_status("Pasted");
}

static int digits(long n)
{
    int d = 1;

    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

/* ^C: nano's report_cursor_position. */
static void do_location(void)
{
    char msg[100];
    const line *l;
    long column = disp_col(cur, cur_x) + 1;
    long fullwidth = disp_col(cur, cur->len) + 1;
    long sum = cur_x, total = lines_bytes(&buf);

    for (l = buf.top; l != cur; l = l->next)
        sum += l->len + 1;
    sprintf(msg, "line %*ld/%ld (%2d%%), col %2ld/%2ld (%3d%%), "
            "char %*ld/%ld (%2d%%)",
            digits(buf.count), cur_no, buf.count,
            (int)(100 * cur_no / buf.count),
            column, fullwidth, (int)(100 * column / fullwidth),
            digits(total), sum, total,
            total > 0 ? (int)(100 * sum / total) : 0);
    set_status(msg);
}

/* ------------------------------------------------------------------ */
/* prompts */

static void open_prompt(int kind, const char *label, const char *given)
{
    prompt = kind;
    strcpy(prompt_label, label);
    strncpy(answer, given, sizeof answer - 1);
    answer[sizeof answer - 1] = 0;
    answer_len = answer_x = strlen(answer);
    redraw_status = redraw_bar = 1;
}

static void close_prompt(void)
{
    prompt = P_NONE;
    redraw_status = redraw_bar = 1;
}

static void ask_filename(void)
{
    const char *tag = format == FMT_UNIX ? ""
                    : format == FMT_MAC ? " [Mac Format]" : " [DOS Format]";

    sprintf(prompt_label, "Write to File%s", tag);
    open_prompt(P_WRITE, prompt_label, answer);
}

static void finish_write(void)
{
    const char *err;
    char msg[NAMEMAX + 100];
    int kept;

    close_prompt();
    if (!file_save(answer, &buf, format, ctrlz, &err, &kept)) {
        sprintf(msg, "Error writing %s: %s", answer, err);
        set_alert(msg);
        return;
    }
    strcpy(filename, answer);
    modified = 0;
    redraw_title = 1;
    sprintf(msg, buf.count - 1 == 1 ? "Wrote %ld line" : "Wrote %ld lines",
            buf.count - 1);
    set_status(msg);
    if (exiting)
        quitting = 1;
}

/* tico's check_write_answer, without the on-disk-change check. */
static void check_answer(void)
{
    int exists = file_exists(answer);
    char label[NAMEMAX + 40];

    if (filename[0] == 0 ? exists : stricmp(answer, filename) != 0) {
        if (filename[0] != 0) {
            open_prompt(P_DIFFNAME, "Save file under DIFFERENT NAME? ",
                        answer);
            return;
        }
        if (exists) {
            sprintf(label, "File \"%.*s\" exists; OVERWRITE? ",
                    scr_cols - 26 > 0 ? scr_cols - 26 : 0, answer);
            open_prompt(P_OVERWRITE, label, answer);
            return;
        }
    }
    finish_write();
}

/* ^O (withprompt) and ^S: nano's write_it_out. */
static void write_out(int for_exit, int withprompt)
{
    exiting = for_exit;
    if (!withprompt && filename[0]) {
        strcpy(answer, filename);
        check_answer();
        return;
    }
    strcpy(answer, filename);
    ask_filename();
}

static void do_exit(void)
{
    if (modified)
        open_prompt(P_EXIT, "Save modified buffer? ", "");
    else
        quitting = 1;
}

static void cancelled(void)
{
    close_prompt();
    set_status("Cancelled");
}

static void yes_no_key(int k)
{
    char label[NAMEMAX + 40];

    if (k == 'y' || k == 'Y') {
        switch (prompt) {
        case P_EXIT:
            close_prompt();
            write_out(1, 1);
            break;
        case P_DIFFNAME:
            if (file_exists(answer)) {
                sprintf(label, "File \"%.*s\" exists; OVERWRITE? ",
                        scr_cols - 26 > 0 ? scr_cols - 26 : 0, answer);
                open_prompt(P_OVERWRITE, label, answer);
            } else {
                finish_write();
            }
            break;
        case P_OVERWRITE:
            finish_write();
            break;
        }
    } else if (k == 'n' || k == 'N') {
        if (prompt == P_EXIT) {
            close_prompt();
            quitting = 1;
        } else {
            /* back to the filename, as nano does */
            ask_filename();
        }
    } else if (k == CTRL('C') || k == KEY_ESC) {
        cancelled();
    }
}

static void text_prompt_key(int k)
{
    switch (k) {
    case KEY_ENTER:
        if (answer_len == 0) {
            cancelled();
        } else {
            close_prompt();
            check_answer();
        }
        return;
    case CTRL('C'):
    case KEY_ESC:
        cancelled();
        return;
    case KEY_LEFT:
        if (answer_x > 0)
            answer_x--;
        break;
    case KEY_RIGHT:
        if (answer_x < answer_len)
            answer_x++;
        break;
    case KEY_HOME:
    case CTRL('A'):
        answer_x = 0;
        break;
    case KEY_END:
    case CTRL('E'):
        answer_x = answer_len;
        break;
    case KEY_BSP:
        if (answer_x == 0)
            break;
        answer_x--;
        /* fall through */
    case KEY_DEL:
    case CTRL('D'):
        if (answer_x < answer_len) {
            memmove(answer + answer_x, answer + answer_x + 1,
                    answer_len - answer_x);
            answer_len--;
        }
        break;
    default:
        if (k >= 0x20 && k <= 0xFF && k != 0x7F &&
            answer_len < sizeof answer - 1) {
            memmove(answer + answer_x + 1, answer + answer_x,
                    answer_len - answer_x + 1);
            answer[answer_x++] = (char)k;
            answer_len++;
        }
        break;
    }
    redraw_status = 1;
}

/* ------------------------------------------------------------------ */
/* keys */

static void unbound(int k)
{
    char msg[40];

    if (k < 0x20)
        sprintf(msg, "Unbound key: ^%c", k == 0 ? '@' : k + 0x40);
    else if (k >= KEY_META(0))
        sprintf(msg, "Unbound key: M-%c", k & 0xFF);
    else if (k >= KEY_F1 && k <= KEY_F10)
        sprintf(msg, "Unbound key: F%d", k - KEY_F1 + 1);
    else if (k == KEY_F11 || k == KEY_F12)
        sprintf(msg, "Unbound key: F%d", k == KEY_F11 ? 11 : 12);
    else
        strcpy(msg, "Unbound key");
    set_status_at(msg, ATTR_ERROR, 1);
}

static void edit_key(int k)
{
    if (k != CTRL('K') && k != KEY_EXT(0x43))
        cut_run = 0;

    switch (k) {
    case KEY_UP:
    case CTRL('P'):
        go_up();
        break;
    case KEY_DOWN:
    case CTRL('N'):
        go_down();
        break;
    case KEY_LEFT:
        go_left();
        break;
    case KEY_RIGHT:
        go_right();
        break;
    case KEY_HOME:
    case CTRL('A'):
        cur_x = 0;
        update_want();
        break;
    case KEY_END:
    case CTRL('E'):
        cur_x = cur->len;
        update_want();
        break;
    case KEY_PGUP:
    case CTRL('Y'):
        go_page(0);
        break;
    case KEY_PGDN:
    case CTRL('V'):
        go_page(1);
        break;
    case KEY_CHOME:
    case KEY_META('\\'):
        go_first_line();
        break;
    case KEY_CEND:
    case KEY_META('/'):
        go_last_line();
        break;
    case KEY_ENTER:
        do_enter();
        break;
    case KEY_TAB:
        insert_byte('\t');
        break;
    case KEY_BSP:
        do_backspace();
        break;
    case KEY_DEL:
    case CTRL('D'):
        do_delete();
        break;
    case CTRL('K'):
    case KEY_EXT(0x43):                 /* F9 */
        do_cut();
        break;
    case CTRL('U'):
    case KEY_EXT(0x44):                 /* F10 */
        do_paste();
        break;
    case KEY_META('6'):
    case KEY_META('^'):
        do_copy();
        break;
    case CTRL('C'):
    case KEY_F11:
        do_location();
        break;
    case CTRL('O'):
    case KEY_EXT(0x3D):                 /* F3 */
        write_out(0, 1);
        break;
    case CTRL('S'):
        write_out(0, 0);
        break;
    case CTRL('X'):
    case KEY_EXT(0x3C):                 /* F2 */
        do_exit();
        break;
    default:
        if (k >= 0x20 && k <= 0xFF && k != 0x7F)
            insert_byte((char)k);
        else
            unbound(k);
        break;
    }
}

static void handle_key(int k)
{
    if (prompt == P_WRITE) {
        text_prompt_key(k);
        return;
    }
    if (prompt != P_NONE) {
        yes_no_key(k);
        return;
    }
    /* nano's blank_it_when_expired */
    if (status_count > 0 && --status_count == 0)
        clear_status();
    if (meta_pending) {
        meta_pending = 0;
        if (k == KEY_ESC)
            return;
        if (k >= 'a' && k <= 'z')
            k -= 'a' - 'A';
        if (k < 0x100)
            k = KEY_META(k);
    } else if (k == KEY_ESC) {
        meta_pending = 1;
        return;
    }
    edit_key(k);
}

/* ------------------------------------------------------------------ */
/* drawing */

static char row_text[256];
static unsigned char row_attr[256];

static void put_row(int row, int len)
{
    int i, start;

    for (i = 0; i < len; i = start) {
        start = i;
        while (i < len && row_attr[i] == row_attr[start])
            i++;
        scr_put(row, start, row_text + start, i - start, row_attr[start]);
        start = i;
    }
    if (len < scr_cols)
        scr_fill(row, len, scr_cols - len, ATTR_NORMAL);
}

/* One text row: line l from display column left, with nano's < > marks. */
static void draw_line(int row, const line *l, long left)
{
    long col = 0, right = left + scr_cols;
    unsigned i;
    int w, j, n = 0, more = 0;
    unsigned char c;

    for (i = 0; l != NULL && i < l->len; i++) {
        c = l->text[i];
        w = char_width(c, col);
        if (col + w > left) {
            if (col >= right) {
                more = 1;
                break;
            }
            for (j = 0; j < w; j++) {
                if (col + j < left || col + j >= right) {
                    if (col + j >= right)
                        more = 1;
                    continue;
                }
                if (c == '\t')
                    row_text[n] = ' ';
                else if (c < 0x20 || c == 0x7F)
                    row_text[n] = j == 0 ? '^' : c ^ 0x40;
                else
                    row_text[n] = c;
                row_attr[n++] = ATTR_NORMAL;
            }
        }
        col += w;
    }
    if (left > 0) {
        row_text[0] = '<';
        row_attr[0] = ATTR_REVERSE;
        if (n == 0)
            n = 1;
    }
    if (more) {
        row_text[scr_cols - 1] = '>';
        row_attr[scr_cols - 1] = ATTR_REVERSE;
        n = scr_cols;
    }
    put_row(row, n);
}

/* tico's title_bar_text, for one buffer and no state flags; the version
 * on the left is tico16's own. */
static void draw_title(void)
{
    char upperleft[24];
    const char *path = filename[0] ? filename : "New Buffer";
    const char *state = modified ? "Modified" : "";
    int cols = scr_cols, pluglen = modified ? 0 : 9;
    int verlen, pathlen, pathwidth, statelen, offset, keep, n;

    sprintf(upperleft, "tico16 %s", VERSION);
    memset(row_text, ' ', cols);
    memset(row_attr, ATTR_REVERSE, cols);
    verlen = strlen(upperleft) + 3;
    pathlen = pathwidth = strlen(path);
    statelen = strlen(state) + 2;
    if (statelen > 2)
        pathlen++;
    if (verlen + pathlen + pluglen + statelen <= cols) {
        memcpy(row_text + 2, upperleft, verlen - 3);
    } else {
        verlen = 2;
        if (verlen + pathlen + pluglen + statelen > cols)
            pluglen = 0;
        if (verlen + pathlen + pluglen + statelen > cols) {
            verlen = 0;
            statelen -= 2;
        }
    }
    offset = 0;
    if (verlen > 0) {
        n = cols - (verlen + pluglen + statelen + pathlen);
        offset = verlen + (n > 0 ? n / 2 : 0);
    }
    if (pathlen + pluglen + statelen <= cols) {
        n = pathwidth;
        if (offset + n > cols)
            n = cols - offset;
        memcpy(row_text + offset, path, n);
    } else if (5 + statelen <= cols) {
        keep = pathwidth - (3 + pathlen + statelen - cols);
        if (keep < 0)
            keep = 0;
        memcpy(row_text + offset, "...", 3);
        n = keep;
        if (offset + 3 + n > cols)
            n = cols - offset - 3;
        if (n > 0)
            memcpy(row_text + offset + 3, path + pathwidth - keep, n);
    }
    if (statelen > 2 && statelen <= cols)
        memcpy(row_text + cols - statelen, state, strlen(state));
    put_row(0, cols);
}

/* tico's render_status_line. */
static void draw_status(void)
{
    int row = text_rows + 1, cols = scr_cols;
    int len, start, pad, n, plen;

    if (prompt != P_NONE) {
        plen = strlen(prompt_label);
        memset(row_text, ' ', cols);
        memset(row_attr, ATTR_REVERSE, cols);
        n = plen < cols ? plen : cols;
        memcpy(row_text, prompt_label, n);
        if (prompt == P_WRITE && n + 2 <= cols) {
            /* the label, ": ", and the answer, scrolled to show the cursor */
            row_text[n++] = ':';
            row_text[n++] = ' ';
            start = plen + 2 + (int)answer_x - (cols - 1);
            start = start > 0 ? start : 0;
            len = answer_len - start;
            if (len > cols - n)
                len = cols - n;
            if (len > 0)
                memcpy(row_text + n, answer + start, len);
        }
        put_row(row, cols);
        return;
    }
    if (status[0] == 0) {
        scr_fill(row, 0, cols, ATTR_NORMAL);
        return;
    }
    len = strlen(status);
    if (len > cols)
        len = cols;
    start = (cols - len) / 2;
    if (start > 1) {
        pad = start - 2;
        n = sprintf(row_text + pad, "[ %.*s ]", len, status);
    } else {
        pad = start;
        n = sprintf(row_text + pad, "%.*s", len, status);
    }
    if (pad + n > cols)
        n = cols - pad;
    memset(row_text, ' ', pad);
    memset(row_attr, ATTR_NORMAL, pad);
    memset(row_attr + pad, status_attr, n);
    put_row(row, pad + n);
}

typedef struct {
    const char *key, *desc;
} shortcut;

/* tico's SHORTCUT_PRIORITY order, implemented entries only */
static const shortcut main_keys[] = {
    {"^X", "Exit"}, {"^O", "Write Out"}, {"^K", "Cut"}, {"^U", "Paste"},
    {"^C", "Location"}, {"M-6", "Copy"},
};
static const shortcut write_keys[] = {
    {"^C", "Cancel"},
};
static const shortcut yes_no_keys[] = {
    {"Y", "Yes"}, {"N", "No"}, {"", ""}, {"^C", "Cancel"},
};

/* tico's render_shortcut_bar and shortcut_bar_layout. */
static void draw_bar(void)
{
    const shortcut *keys;
    int count, maxk = 2, maxd = 4, width, pairs, r, c, idx, n, kl, dl;

    if (prompt == P_NONE) {
        keys = main_keys;
        count = sizeof main_keys / sizeof main_keys[0];
    } else if (prompt == P_WRITE) {
        keys = write_keys;
        count = sizeof write_keys / sizeof write_keys[0];
    } else {
        keys = yes_no_keys;
        count = sizeof yes_no_keys / sizeof yes_no_keys[0];
    }
    for (idx = 0; idx < count; idx++) {
        if (idx == 0 || (int)strlen(keys[idx].key) > maxk)
            maxk = strlen(keys[idx].key);
        if (idx == 0 || (int)strlen(keys[idx].desc) > maxd)
            maxd = strlen(keys[idx].desc);
    }
    width = maxk + 1 + maxd + 2;
    pairs = scr_cols / width;
    if (pairs < 1)
        pairs = 1;
    if (pairs > (count + 1) / 2)
        pairs = (count + 1) / 2;
    for (r = 0; r < 2; r++) {
        n = 0;
        for (c = 0; c < pairs && n < scr_cols; c++) {
            idx = c * 2 + r;
            memset(row_text + n, ' ', width);
            memset(row_attr + n, ATTR_NORMAL, width);
            if (idx < count && keys[idx].key[0]) {
                kl = strlen(keys[idx].key);
                dl = strlen(keys[idx].desc);
                memcpy(row_text + n, keys[idx].key, kl);
                memset(row_attr + n, ATTR_REVERSE, kl);
                memcpy(row_text + n + kl + 1, keys[idx].desc, dl);
            }
            n += width;
        }
        if (n > scr_cols)
            n = scr_cols;
        put_row(text_rows + 2 + r, n);
    }
}

static int needs_draw(long n)
{
    return n >= redraw_from || n == dirty[0] || n == dirty[1] ||
           n == dirty[2];
}

static void refresh(void)
{
    long left, n;
    const line *l;
    int row;

    /* keep the cursor on screen: nano's smooth scrolling */
    if (cur_no < top_no)
        top_no = cur_no;
    else if (cur_no >= top_no + text_rows)
        top_no = cur_no - text_rows + 1;
    if (top_no < 1)
        top_no = 1;

    left = page_start(disp_col(cur, cur_x));
    if (!full_redraw && top_no != drawn_top_no) {
        if (top_no == drawn_top_no + 1) {
            scr_scroll(1, text_rows, 1);
            mark_line(top_no + text_rows - 1);
        } else if (top_no == drawn_top_no - 1) {
            scr_scroll(1, text_rows, 0);
            mark_line(top_no);
        } else {
            full_redraw = 1;
        }
    }
    /* only the cursor line scrolls sideways */
    if (cur_no != drawn_cur_no) {
        if (drawn_left != 0)
            mark_line(drawn_cur_no);
        if (left != 0)
            mark_line(cur_no);
    } else if (left != drawn_left) {
        mark_line(cur_no);
    }

    l = cur;
    for (n = cur_no; n > top_no; n--)
        l = l->prev;
    for (row = 0; row < text_rows; row++, n++) {
        if (full_redraw || needs_draw(n))
            draw_line(row + 1, l, n == cur_no ? left : 0);
        if (l != NULL)
            l = l->next;
    }

    if (full_redraw || redraw_title)
        draw_title();
    if (full_redraw || redraw_status)
        draw_status();
    if (full_redraw || redraw_bar)
        draw_bar();

    drawn_top_no = top_no;
    drawn_cur_no = cur_no;
    drawn_left = left;
    full_redraw = redraw_title = redraw_status = redraw_bar = 0;
    redraw_from = NONE;
    dirty[0] = dirty[1] = dirty[2] = NONE;

    if (prompt != P_NONE) {
        n = strlen(prompt_label);
        if (prompt == P_WRITE) {
            n += 2 + answer_x;
            if (n > scr_cols - 1)
                n = scr_cols - 1;
        }
        scr_cursor(text_rows + 1, n < scr_cols ? (int)n : scr_cols - 1);
    } else {
        scr_cursor((int)(cur_no - top_no) + 1,
                   (int)(disp_col(cur, cur_x) - left));
    }
}

/* ------------------------------------------------------------------ */

int editor_run(const char *name)
{
    char msg[60];
    const char *err;
    int state;

    lines_init(&buf);
    lines_init(&cutbuf);
    format = FMT_DOS;
    state = LOAD_NEW;
    if (name != NULL) {
        if (strlen(name) >= sizeof filename) {
            fprintf(stderr, "tico: %s: File name too long\n", name);
            return 1;
        }
        strcpy(filename, name);
        state = file_load(name, &buf, &format, &ctrlz, &err);
        if (state == LOAD_ERR) {
            fprintf(stderr, "tico: %s: %s\n", name, err);
            return 1;
        }
    }
    if (state == LOAD_OK) {
        sprintf(msg, buf.count - 1 == 1 ? "Read %ld line%s" : "Read %ld lines%s",
                buf.count - 1,
                format == FMT_DOS ? " (converted from DOS format)" :
                format == FMT_MAC ? " (converted from Mac format)" : "");
        set_status(msg);
        /* no line break to go by: write DOS, like a new buffer */
        if (format == FMT_NONE)
            format = FMT_DOS;
    } else {
        format = FMT_DOS;
        if (!lines_append(&buf, NULL, 0)) {
            fprintf(stderr, "tico: Not enough memory\n");
            return 1;
        }
        if (name != NULL)
            set_status("New File");
    }

    sys_init();
    if (!scr_init()) {
        scr_done();
        sys_done();
        fprintf(stderr, "tico: screen too small\n");
        return 1;
    }
    text_rows = scr_rows - 4;
    cur = buf.top;
    cur_no = top_no = 1;
    redraw_from = NONE;
    dirty[0] = dirty[1] = dirty[2] = NONE;

    while (!quitting) {
        refresh();
        handle_key(key_get());
    }

    scr_done();
    sys_done();
    return 0;
}

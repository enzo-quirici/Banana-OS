#include "editor.h"
#include "../kernel/terminal.h"
#include "../kernel/keyboard.h"
#include "../kernel/fs.h"
#include "../kernel/gui.h"
#include "../kernel/timer.h"
#include "../kernel/types.h"

#define ED_MAX_LINES  64
#define ED_LINE_LEN   79
#define ED_ROWS       21

static char  lines[ED_MAX_LINES][ED_LINE_LEN];
static int   nlines;
static int   cx, cy;
static int   scroll;
static int   dirty;
static char  filename[FS_NAME_LEN];

/* helpers */
static int k_strlen(const char* s) { int n=0; while(s[n]) n++; return n; }
static void k_strcpy(char* d, const char* s, int max) {
    int i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]='\0';
}
static void k_memmove(char* d, const char* s, int n) {
    if (d < s) { for(int i=0;i<n;i++) d[i]=s[i]; }
    else       { for(int i=n-1;i>=0;i--) d[i]=s[i]; }
}

/* buffer → file */
static void buf_to_file(fs_file_t* f) {
    int pos = 0;
    for (int i = 0; i < nlines && pos < FS_CONTENT_LEN - 2; i++) {
        int l = k_strlen(lines[i]);
        for (int j = 0; j < l && pos < FS_CONTENT_LEN - 2; j++)
            f->content[pos++] = lines[i][j];
        if (i < nlines - 1) f->content[pos++] = '\n';
    }
    f->content[pos] = '\0';
}

/* file → buffer */
static void file_to_buf(const char* src) {
    nlines = 0;
    int col = 0;
    for (int i = 0; src[i] && nlines < ED_MAX_LINES; i++) {
        if (src[i] == '\n') {
            lines[nlines][col] = '\0';
            nlines++;
            col = 0;
        } else if (col < ED_LINE_LEN - 1) {
            lines[nlines][col++] = src[i];
        }
    }
    lines[nlines][col] = '\0';
    nlines++;
    if (nlines == 0) { lines[0][0] = '\0'; nlines = 1; }
}

/* draw */
static void draw(void) {
    terminal_clear();

    /* title bar */
    terminal_setcolor(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    terminal_write("  GNU nano 0.1  (Banana Edition)    File: ");
    terminal_write(filename);
    if (dirty) terminal_write(" [Modified]");

    int used = 42 + k_strlen(filename) + (dirty ? 11 : 0);
    for (int i = used; i < 80; i++) terminal_putchar(' ');

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_putchar('\n');

    /* text */
    for (int row = 0; row < ED_ROWS; row++) {
        int li = scroll + row;
        if (li < nlines) terminal_write(lines[li]);
        terminal_putchar('\n');
    }

    /* status */
    terminal_setcolor(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);

    char lbuf[8]; char cbuf[8];
    char* ls = u32_to_str((uint32_t)(cy+1), lbuf, sizeof(lbuf));
    char* cs = u32_to_str((uint32_t)(cx+1), cbuf, sizeof(cbuf));

    terminal_write("  Line ");
    terminal_write(ls);
    terminal_write(", Col ");
    terminal_write(cs);

    for (int i = 14 + k_strlen(ls) + k_strlen(cs); i < 80; i++)
        terminal_putchar(' ');

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_putchar('\n');

    /* shortcuts */
    terminal_setcolor(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    terminal_write("^X Exit  ^O/^S Save  ^K Cut line  ^U Paste");
    for (int i = 46; i < 80; i++) terminal_putchar(' ');
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* ✅ FIXED CURSOR POSITION */
    int screen_row = 2 + (cy - scroll);

    if (screen_row < 1) screen_row = 1;
    if (screen_row > ED_ROWS) screen_row = ED_ROWS;

    int screen_col = cx;
    if (screen_col < 0) screen_col = 0;
    if (screen_col > 79) screen_col = 79;

    terminal_set_cursor((size_t)screen_row, (size_t)screen_col);
}

/* clamp */
static void clamp(void) {
    if (cy < 0) cy = 0;
    if (cy >= nlines) cy = nlines - 1;

    int ll = k_strlen(lines[cy]);
    if (cx < 0) cx = 0;
    if (cx > ll) cx = ll;

    if (cy < scroll) scroll = cy;
    if (cy >= scroll + ED_ROWS) scroll = cy - ED_ROWS + 1;
}

/* cut buffer */
static char cut_buf[ED_LINE_LEN];

static void save_file(fs_file_t* f) {
    buf_to_file(f);
    dirty = 0;
    draw();
    terminal_setcolor(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    terminal_write("  Saved. Press any key...");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    while (!keyboard_try_getchar()) { gui_poll(); timer_sleep_ms(10); }
    draw();
}

void editor_open(const char* fname) {
    k_strcpy(filename, fname, FS_NAME_LEN);

    int idx = fs_find_file(fname);
    if (idx < 0) idx = fs_create(fname);
    if (idx < 0) {
        terminal_write_color("editor: cannot open\n",
                             VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        return;
    }

    fs_file_t* f = fs_get_file(idx);
    file_to_buf(f->content);

    cx = 0; cy = 0; scroll = 0; dirty = 0;
    cut_buf[0] = '\0';

    draw();

    while (1) {
        gui_poll();
        char c = keyboard_try_getchar();
        if (!c) { timer_sleep_ms(10); continue; }

        if (c == 24) { /* Ctrl+X */
            if (dirty) {
                terminal_setcolor(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
                terminal_write("\n  Save modified buffer? (Y/N): ");
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                char ans = 0;
                while (!ans) { gui_poll(); ans = keyboard_try_getchar(); if (!ans) timer_sleep_ms(10); }
                if (ans == 'y' || ans == 'Y') buf_to_file(f);
            }
            terminal_clear();
            return;
        }

        if (c == 15 || c == 19) { /* Ctrl+O or Ctrl+S */
            save_file(f);
            continue;
        }

        if (c == 11) { /* Ctrl+K */
            k_strcpy(cut_buf, lines[cy], ED_LINE_LEN);
            if (nlines > 1) {
                for (int i = cy; i < nlines - 1; i++)
                    k_strcpy(lines[i], lines[i+1], ED_LINE_LEN);
                nlines--;
            } else lines[0][0] = '\0';

            clamp();
            dirty = 1;
            draw();
            continue;
        }

        if (c == 21) { /* Ctrl+U */
            if (cut_buf[0] && nlines < ED_MAX_LINES) {
                for (int i = nlines; i > cy; i--)
                    k_strcpy(lines[i], lines[i-1], ED_LINE_LEN);
                k_strcpy(lines[cy], cut_buf, ED_LINE_LEN);
                nlines++;
                cy++;
                clamp();
                dirty = 1;
                draw();
            }
            continue;
        }

        if (c == 27) { /* arrows */
            char c2 = keyboard_getchar();
            if (c2 == '[') {
                char c3 = keyboard_getchar();
                if (c3 == 'A') cy--;
                if (c3 == 'B') cy++;
                if (c3 == 'C') cx++;
                if (c3 == 'D') cx--;
                clamp();
                draw();
            }
            continue;
        }

        if (c == '\n') {
            if (nlines >= ED_MAX_LINES) continue;

            char rest[ED_LINE_LEN];
            k_strcpy(rest, lines[cy] + cx, ED_LINE_LEN);
            lines[cy][cx] = '\0';

            for (int i = nlines; i > cy + 1; i--)
                k_strcpy(lines[i], lines[i-1], ED_LINE_LEN);

            nlines++;
            cy++;
            k_strcpy(lines[cy], rest, ED_LINE_LEN);
            cx = 0;

            clamp();
            dirty = 1;
            draw();
            continue;
        }

        if (c == '\b') {
            if (cx > 0) {
                char* ln = lines[cy];
                int ll = k_strlen(ln);
                k_memmove(ln + cx - 1, ln + cx, ll - cx + 1);
                cx--;
            } else if (cy > 0) {
                int prev_len = k_strlen(lines[cy-1]);
                int cur_len  = k_strlen(lines[cy]);

                if (prev_len + cur_len < ED_LINE_LEN - 1) {
                    k_strcpy(lines[cy-1] + prev_len, lines[cy], ED_LINE_LEN - prev_len);
                    for (int i = cy; i < nlines - 1; i++)
                        k_strcpy(lines[i], lines[i+1], ED_LINE_LEN);
                    nlines--;
                    cy--;
                    cx = prev_len;
                }
            }
            clamp();
            dirty = 1;
            draw();
            continue;
        }

        if (c >= 32 && c < 127) {
            char* ln = lines[cy];
            int ll = k_strlen(ln);

            if (ll < ED_LINE_LEN - 1) {
                k_memmove(ln + cx + 1, ln + cx, ll - cx + 1);
                ln[cx] = c;
                cx++;
            }

            clamp();
            dirty = 1;
            draw();
        }
    }
}
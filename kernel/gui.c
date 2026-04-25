#include "gui.h"
#include "terminal.h"
#include "timer.h"
#include "sysinfo.h"
#include "keyboard.h"
#include "gfx.h"
#include "fb.h"
#include "usb.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

#define TASKBAR_ROW (VGA_HEIGHT - 1)

static int g_menu_open = 0;
static int g_menu_sel = 0; /* 0=About, 1=Terminal, 2=Quit GUI */
static uint32_t g_last_clock_sec = (uint32_t)-1;
static int g_gui_enabled = 0; /* like startx: default off */
static int g_about_open = 0;
typedef struct {
    int open;
    int vt;
    int x, y, w, h;
    int dragging;
    int drag_dx, drag_dy;
} term_win_t;

#define TERM_WIN_MAX 4
static term_win_t g_terms[TERM_WIN_MAX] = {
    {0, 0, 140,  90, 520, 340, 0, 0, 0},
    {0, 0, 180, 120, 520, 340, 0, 0, 0},
    {0, 0, 220, 150, 520, 340, 0, 0, 0},
    {0, 0, 260, 180, 520, 340, 0, 0, 0},
};

/* z-order: back -> front */
static int g_term_order[TERM_WIN_MAX] = {0, 1, 2, 3};

static void menu_activate(void);

static uint32_t vga_color_rgb(uint8_t c) {
    static const uint32_t pal[16] = {
        0x00000000u, 0x000000AAu, 0x0000AA00u, 0x0000AAAAu,
        0x00AA0000u, 0x00AA00AAu, 0x00AA5500u, 0x00AAAAAAu,
        0x00555555u, 0x005555FFu, 0x0055FF55u, 0x0055FFFFu,
        0x00FF5555u, 0x00FF55FFu, 0x00FFFF55u, 0x00FFFFFFu,
    };
    return pal[c & 0x0F];
}

static void clamp_win(const fb_info_t* fi, term_win_t* w) {
    if (!fi) return;
    if (!w) return;
    if (w->w < 220) w->w = 220;
    if (w->h < 160) w->h = 160;
    if (w->x < 0) w->x = 0;
    if (w->y < 0) w->y = 0;
    if (w->x + w->w > (int)fi->width)  w->x = (int)fi->width - w->w;
    if (w->y + w->h > (int)fi->height) w->y = (int)fi->height - w->h;
    if (w->x < 0) w->x = 0;
    if (w->y < 0) w->y = 0;
}

static void bring_term_front(int idx) {
    int pos = -1;
    for (int i = 0; i < TERM_WIN_MAX; i++) {
        if (g_term_order[i] == idx) { pos = i; break; }
    }
    if (pos < 0) return;
    for (int i = pos; i < TERM_WIN_MAX - 1; i++) g_term_order[i] = g_term_order[i + 1];
    g_term_order[TERM_WIN_MAX - 1] = idx;
}

static int open_new_terminal(void) {
    for (int i = 0; i < TERM_WIN_MAX; i++) {
        if (!g_terms[i].open) {
            int vt = terminal_vt_alloc();
            if (vt < 0) return -1;
            g_terms[i].open = 1;
            g_terms[i].vt = vt;
            g_terms[i].dragging = 0;
            bring_term_front(i);
            terminal_vt_set_active(vt);
            return i;
        }
    }
    /* none free: focus the frontmost */
    bring_term_front(g_term_order[TERM_WIN_MAX - 1]);
    return g_term_order[TERM_WIN_MAX - 1];
}

static void draw_terminal_window(const fb_info_t* fi, const term_win_t* win) {
    if (!win || !win->open) return;
    if (!fi) return;

    term_win_t w = *win;
    clamp_win(fi, &w);

    int title_h = 20;
    int pad = 6;

    /* frame + title */
    gfx_fill_rect(w.x, w.y, w.w, w.h, 0x00202020u);
    gfx_fill_rect(w.x, w.y, w.w, title_h, 0x004040A0u);
    gfx_draw_text(w.x + 8, w.y + 6, "Terminal", 0x00FFFFFFu, 0x004040A0u);

    /* close button */
    int bx = w.x + w.w - 28;
    gfx_fill_rect(bx, w.y + 2, 24, 16, 0x00AA0000u);
    gfx_draw_text(bx + 8, w.y + 6, "X", 0x00FFFFFFu, 0x00AA0000u);

    /* client area */
    int cx = w.x + pad;
    int cy = w.y + title_h + pad;
    int cw = w.w - pad * 2;
    int ch = w.h - title_h - pad * 2;
    gfx_fill_rect(cx, cy, cw, ch, 0x00000000u);

    const char* chars;
    const uint8_t* cols;
    int tw, th, stride;
    terminal_vt_get_buffer(win->vt, &chars, &cols, &tw, &th, &stride);

    int max_cols = cw / 8;
    int max_rows = ch / 8;
    if (max_cols > tw) max_cols = tw;
    if (max_rows > th) max_rows = th;

    for (int y = 0; y < max_rows; y++) {
        for (int x = 0; x < max_cols; x++) {
            int idx = y * stride + x;
            uint8_t color = cols[idx];
            uint8_t fg = color & 0x0F;
            uint8_t bg = (color >> 4) & 0x0F;
            gfx_draw_char(cx + x * 8, cy + y * 8, chars[idx],
                          vga_color_rgb(fg), vga_color_rgb(bg));
        }
    }
}

static void k_memset(char* p, char v, int n) {
    for (int i = 0; i < n; i++) p[i] = v;
}

static void u32_to_2dig(uint32_t v, char out[2]) {
    out[0] = (char)('0' + ((v / 10u) % 10u));
    out[1] = (char)('0' + (v % 10u));
}

static void format_clock(char out[9]) {
    uint32_t sec = timer_ticks() / 100u;
    uint32_t h = (sec / 3600u) % 24u;
    uint32_t m = (sec / 60u) % 60u;
    uint32_t s = sec % 60u;

    u32_to_2dig(h, &out[0]);
    out[2] = ':';
    u32_to_2dig(m, &out[3]);
    out[5] = ':';
    u32_to_2dig(s, &out[6]);
    out[8] = '\0';
}

static void draw_taskbar(void) {
    /* background bar */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        terminal_putentryat(' ', VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY, TASKBAR_ROW, x);
    }

    terminal_write_at(" [Start] ", VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY, TASKBAR_ROW, 0);

    char clk[9];
    format_clock(clk);
    size_t clk_len = 8;
    size_t clk_col = (VGA_WIDTH > (clk_len + 1)) ? (VGA_WIDTH - (clk_len + 1)) : 0;
    terminal_write_at(clk, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY, TASKBAR_ROW, clk_col);
}

static void draw_menu(void) {
    if (!g_menu_open) return;

    const size_t menu_x = 0;
    const size_t menu_y = TASKBAR_ROW - 3;
    const size_t menu_w = 20;
    const size_t menu_h = 3;

    /* box background */
    for (size_t y = 0; y < menu_h; y++) {
        for (size_t x = 0; x < menu_w; x++) {
            terminal_putentryat(' ', VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY, menu_y + y, menu_x + x);
        }
    }

    /* items */
    for (int i = 0; i < 3; i++) {
        uint8_t fg = VGA_COLOR_WHITE;
        uint8_t bg = VGA_COLOR_DARK_GREY;
        if (g_menu_sel == i) { fg = VGA_COLOR_WHITE; bg = VGA_COLOR_BLUE; }

        const char* item = (i == 0) ? " About app" : (i == 1) ? " Terminal" : " Quit GUI";
        terminal_write_at(item,
                          fg, bg, menu_y + (size_t)i, menu_x + 1);

        /* fill the rest of the row in highlight color for clean look */
        for (size_t x = 0; x < menu_w - 2; x++) {
            terminal_putentryat(' ', fg, bg, menu_y + (size_t)i, menu_x + 1 + x);
        }
        terminal_write_at(item,
                          fg, bg, menu_y + (size_t)i, menu_x + 1);
    }

    /* hint row */
    terminal_write_at(" Enter = open", VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY, menu_y + 3, menu_x + 1);
}

static void menu_close_redraw(void) {
    g_menu_open = 0;
    draw_taskbar();
    /* clear menu area */
    for (size_t y = 0; y < 3; y++) {
        for (size_t x = 0; x < 20; x++) {
            terminal_putentryat(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK, (TASKBAR_ROW - 3) + y, x);
        }
    }
    draw_taskbar();
}

static void show_about(void) {
    size_t saved_r, saved_c;
    terminal_get_cursor(&saved_r, &saved_c);

    terminal_clear();

    const sysinfo_t* si = sysinfo_get();
    terminal_write_color("Banana OS 0.2 - About app\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeln("----------------------------------------");
    terminal_write_color("Version: ", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_writeln("0.2");
    terminal_write_color("Display: ", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_writeln("VGA text 80x25 + basic GUI taskbar");
    terminal_write_color("CPU: ", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_writeln(si->cpu_brand[0] ? si->cpu_brand : "Unknown");

    terminal_write_color("Uptime: ", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    {
        uint32_t sec = timer_ticks() / 100u;
        uint32_t h = sec / 3600u;
        uint32_t m = (sec % 3600u) / 60u;
        uint32_t s = sec % 60u;
        char b[32];
        k_memset(b, 0, (int)sizeof(b));
        /* simple formatting without stdlib */
        b[0] = (char)('0' + ((h / 10u) % 10u));
        b[1] = (char)('0' + (h % 10u));
        b[2] = 'h';
        b[3] = ' ';
        b[4] = (char)('0' + ((m / 10u) % 10u));
        b[5] = (char)('0' + (m % 10u));
        b[6] = 'm';
        b[7] = ' ';
        b[8] = (char)('0' + ((s / 10u) % 10u));
        b[9] = (char)('0' + (s % 10u));
        b[10] = 's';
        b[11] = '\0';
        terminal_writeln(b);
    }

    terminal_writeln("");
    terminal_write_color("Press any key to return...\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);

    /* wait for a key, keep clock updating */
    while (1) {
        gui_poll();
        char c = keyboard_try_getchar();
        if (c) break;
        timer_sleep_ms(10);
    }

    terminal_clear();
    terminal_set_cursor(saved_r, saved_c);
    draw_taskbar();
}

void gui_init(void) {
    if (gfx_available()) {
        /* framebuffer desktop is opt-in via startx */
        return;
    }

    terminal_set_reserved_bottom(1);
    draw_taskbar();
}

void gui_poll(void) {
    if (gfx_available()) {
        if (!g_gui_enabled) return;
        /* framebuffer desktop loop (throttled + backbuffer to avoid flicker) */
        const fb_info_t* fi = fb_info();
        if (!fi || fi->width == 0 || fi->height == 0) return;

        /* backbuffer sized for requested mode (800x600) */
        static uint32_t backbuf[800u * 600u];
        static int bb_inited = 0;
        static int mx = 100, my = 100;
        static int last_mx = -1, last_my = -1;
        static uint32_t last_sec = (uint32_t)-1;
        static int last_open = -1, last_sel = -1;
        static uint32_t last_frame_tick = 0;
        static int prev_left = 0;

        if (!bb_inited) {
            fb_set_backbuffer(backbuf, 800u, 600u);
            bb_inited = 1;
            last_mx = -1; last_my = -1;
            last_sec = (uint32_t)-1;
            last_open = -1; last_sel = -1;
        }

        mouse_state_t ms = mouse_read();
        mx += ms.dx;
        my -= ms.dy;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx > (int)fi->width - 1) mx = (int)fi->width - 1;
        if (my > (int)fi->height - 1) my = (int)fi->height - 1;

        uint32_t sec = timer_ticks() / 100u;

        /* mouse click handling (rising edge) */
        int left = ms.btn_left ? 1 : 0;
        int click = (left && !prev_left);
        prev_left = left;

        int bar_h = 28;
        int bar_y = (int)fi->height - bar_h;

        /* Start button hitbox around "[Start]" text */
        int start_x = 8;
        int start_w = 7 * 8; /* "[Start]" */
        int quit_w = 6 * 8;  /* "[Quit]" */
        int clk_w_for_hit = 8 * 8;
        int clk_x_for_hit = (int)fi->width - clk_w_for_hit - 8;
        int quit_x = clk_x_for_hit - quit_w - 12; /* quit before clock */
        if (quit_x < (start_x + start_w + 12)) quit_x = start_x + start_w + 12;

        if (click) {
            if (g_about_open) {
                g_about_open = 0;
            }

            /* click on taskbar Start */
            if (my >= bar_y && mx >= start_x && mx < (start_x + start_w)) {
                g_menu_open = !g_menu_open;
                if (g_menu_open) g_menu_sel = 0;
            }

            /* click on Quit GUI button */
            if (my >= bar_y && mx >= quit_x && mx < (quit_x + quit_w)) {
                gui_set_enabled(0);
            }

            /* terminal windows hit testing (front-to-back) */
            for (int oi = TERM_WIN_MAX - 1; oi >= 0; oi--) {
                int wi = g_term_order[oi];
                term_win_t* w = &g_terms[wi];
                if (!w->open) continue;

                int title_h = 20;
                if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) {
                    bring_term_front(wi);
                    terminal_vt_set_active(w->vt);

                    int close_x = w->x + w->w - 28;
                    if (mx >= close_x && mx < close_x + 24 && my >= w->y + 2 && my < w->y + 18) {
                        w->open = 0;
                        terminal_vt_free(w->vt);
                        w->vt = 0;
                    } else if (my < w->y + title_h) {
                        w->dragging = 1;
                        w->drag_dx = mx - w->x;
                        w->drag_dy = my - w->y;
                    }
                    break;
                }
            }

            /* click in menu items */
            if (g_menu_open) {
                int menu_w = 220;
                int menu_h = 112;
                int menu_x = 8;
                int menu_y = (int)fi->height - bar_h - menu_h - 8;

                int item_x0 = menu_x + 8;
                int item_x1 = menu_x + menu_w - 8;

                int item0_y0 = menu_y + 8;
                int item0_y1 = item0_y0 + 24;
                int item1_y0 = menu_y + 36;
                int item1_y1 = item1_y0 + 24;
                int item2_y0 = menu_y + 64;
                int item2_y1 = item2_y0 + 24;

                if (mx >= item_x0 && mx < item_x1) {
                    if (my >= item0_y0 && my < item0_y1) {
                        g_menu_sel = 0;
                        menu_activate();
                    } else if (my >= item1_y0 && my < item1_y1) {
                        g_menu_sel = 1;
                        menu_activate();
                    } else if (my >= item2_y0 && my < item2_y1) {
                        g_menu_sel = 2;
                        menu_activate();
                    }
                }
            }
        }

        /* throttle to ~30 fps max */
        uint32_t now_tick = timer_ticks();
        if ((int32_t)(now_tick - last_frame_tick) < 3 && /* <30ms */
            mx == last_mx && my == last_my &&
            sec == last_sec &&
            g_menu_open == last_open && g_menu_sel == last_sel) {
            return;
        }
        last_frame_tick = now_tick;

        /* redraw desktop (without cursor) into backbuffer */
        gfx_clear(0x00303070u);

        /* desktop shortcuts */
        {
            int icon_w = 120, icon_h = 44;
            int sx = 24, sy = 24;

            gfx_fill_rect(sx, sy, icon_w, icon_h, 0x00202020u);
            gfx_draw_text(sx + 12, sy + 16, "About", 0x00FFFFFFu, 0x00202020u);

            gfx_fill_rect(sx, sy + icon_h + 12, icon_w, icon_h, 0x00202020u);
            gfx_draw_text(sx + 12, sy + icon_h + 12 + 16, "Terminal", 0x00FFFFFFu, 0x00202020u);
            gfx_fill_rect(sx, sy + (icon_h + 12) * 2, icon_w, icon_h, 0x00202020u);
            gfx_draw_text(sx + 12, sy + (icon_h + 12) * 2 + 16, "Quit GUI", 0x00FFFFFFu, 0x00202020u);

            /* click shortcuts */
            if (click && !g_about_open && !g_menu_open) {
                if (mx >= sx && mx < (sx + icon_w) && my >= sy && my < (sy + icon_h)) {
                    g_about_open = 1;
                } else if (mx >= sx && mx < (sx + icon_w) && my >= (sy + icon_h + 12) && my < (sy + icon_h + 12 + icon_h)) {
                    open_new_terminal();
                } else if (mx >= sx && mx < (sx + icon_w) && my >= (sy + (icon_h + 12) * 2) && my < (sy + (icon_h + 12) * 2 + icon_h)) {
                    gui_set_enabled(0);
                }
            }
        }

        /* clock (uptime as HH:MM:SS) */
        uint32_t h = (sec / 3600u) % 24u;
        uint32_t m = (sec / 60u) % 60u;
        uint32_t s = sec % 60u;
        char clk[9];
        clk[0] = (char)('0' + ((h / 10u) % 10u));
        clk[1] = (char)('0' + (h % 10u));
        clk[2] = ':';
        clk[3] = (char)('0' + ((m / 10u) % 10u));
        clk[4] = (char)('0' + (m % 10u));
        clk[5] = ':';
        clk[6] = (char)('0' + ((s / 10u) % 10u));
        clk[7] = (char)('0' + (s % 10u));
        clk[8] = '\0';

        int clk_w = 8 * 8;
        int clk_x = (int)fi->width - clk_w - 8; /* clock stays at far right */

        /* taskbar */
        gfx_fill_rect(0, bar_y, (int)fi->width, bar_h, 0x00202020u);
        gfx_draw_text(start_x, bar_y + 10, "[Start]", 0x00FFFFFFu, 0x00202020u);
        gfx_draw_text(quit_x, bar_y + 10, "[Quit]", 0x00FFFFFFu, 0x00202020u);
        gfx_draw_text(clk_x, bar_y + 10, clk, 0x00FFFFFFu, 0x00202020u);

        /* start menu (very basic) */
        if (g_menu_open) {
            int menu_w = 220;
            int menu_h = 112;
            int menu_x = 8;
            int menu_y = (int)fi->height - bar_h - menu_h - 8;
            gfx_fill_rect(menu_x, menu_y, menu_w, menu_h, 0x00303030u);

            uint32_t sel_bg0 = (g_menu_sel == 0) ? 0x004040A0u : 0x00303030u;
            uint32_t sel_bg1 = (g_menu_sel == 1) ? 0x004040A0u : 0x00303030u;
            uint32_t sel_bg2 = (g_menu_sel == 2) ? 0x004040A0u : 0x00303030u;
            gfx_fill_rect(menu_x + 8, menu_y + 8, menu_w - 16, 24, sel_bg0);
            gfx_fill_rect(menu_x + 8, menu_y + 36, menu_w - 16, 24, sel_bg1);
            gfx_fill_rect(menu_x + 8, menu_y + 64, menu_w - 16, 24, sel_bg2);
            gfx_draw_text(menu_x + 16, menu_y + 14, "About app", 0x00FFFFFFu, sel_bg0);
            gfx_draw_text(menu_x + 16, menu_y + 42, "Terminal", 0x00FFFFFFu, sel_bg1);
            gfx_draw_text(menu_x + 16, menu_y + 70, "Quit GUI", 0x00FFFFFFu, sel_bg2);
        }

        if (g_about_open) {
            int mw = 420, mh = 160;
            int mx0 = ((int)fi->width - mw) / 2;
            int my0 = ((int)fi->height - mh) / 2;
            gfx_fill_rect(mx0, my0, mw, mh, 0x00303030u);
            gfx_fill_rect(mx0, my0, mw, 20, 0x004040A0u);
            gfx_draw_text(mx0 + 8, my0 + 6, "About Banana OS 0.2", 0x00FFFFFFu, 0x004040A0u);
            gfx_draw_text(mx0 + 16, my0 + 40, "Banana OS 0.2", 0x00FFFFFFu, 0x00303030u);
            gfx_draw_text(mx0 + 16, my0 + 56, "Desktop: basic framebuffer GUI", 0x00FFFFFFu, 0x00303030u);
            gfx_draw_text(mx0 + 16, my0 + 80, "Click anywhere to close", 0x00AAAAAAu, 0x00303030u);
        }

        /* drag any terminal windows while holding left */
        for (int i = 0; i < TERM_WIN_MAX; i++) {
            if (!g_terms[i].open) continue;
            if (left && g_terms[i].dragging) {
                g_terms[i].x = mx - g_terms[i].drag_dx;
                g_terms[i].y = my - g_terms[i].drag_dy;
                clamp_win(fi, &g_terms[i]);
            }
            if (!left) g_terms[i].dragging = 0;
        }

        /* draw terminal windows back-to-front */
        for (int oi = 0; oi < TERM_WIN_MAX; oi++) {
            term_win_t* w = &g_terms[g_term_order[oi]];
            draw_terminal_window(fi, w);
        }

        /* push backbuffer to framebuffer once per frame */
        fb_present();

        /* mouse cursor */
        for (int cy = 0; cy < 10; cy++) {
            for (int cx = 0; cx < 6; cx++) {
                if (cx == 0 || cy == 0 || cx == cy / 2) {
                    fb_putpixel_direct(mx + cx, my + cy, 0x00FFFFFFu);
                }
            }
        }

        last_mx = mx; last_my = my;
        last_sec = sec;
        last_open = g_menu_open;
        last_sel = g_menu_sel;

        return;
    }

    uint32_t sec = timer_ticks() / 100u;
    if (sec != g_last_clock_sec) {
        g_last_clock_sec = sec;
        draw_taskbar();
        draw_menu();
    } else if (g_menu_open) {
        /* keep menu visible even if other code redraws */
        draw_menu();
    }
}

void gui_set_enabled(int enabled) {
    g_gui_enabled = enabled ? 1 : 0;
    g_menu_open = 0;
    g_about_open = 0;
    for (int i = 0; i < TERM_WIN_MAX; i++) {
        if (g_terms[i].open) terminal_vt_free(g_terms[i].vt);
        g_terms[i].open = 0;
        g_terms[i].vt = 0;
    }

    if (gfx_available()) {
        if (g_gui_enabled) terminal_set_mode(TERMINAL_MODE_SUSPENDED);
        else {
            terminal_vt_set_active(0);
            terminal_set_mode(TERMINAL_MODE_FRAMEBUFFER);
            terminal_fb_set_scale(1); /* console stays small */
            terminal_clear();
        }
    }
}

int gui_is_enabled(void) {
    return g_gui_enabled;
}

static void menu_activate(void) {
    if (g_menu_sel == 0) {
        if (gfx_available()) {
            g_menu_open = 0;
            g_about_open = 1;
            return;
        } else {
            menu_close_redraw();
            show_about();
        }
        return;
    }
    if (g_menu_sel == 1) {
        if (gfx_available()) {
            g_menu_open = 0;
            g_about_open = 0;
            open_new_terminal();
        } else {
            menu_close_redraw();
        }
        return; /* "Terminal" just closes the menu */
    }
    if (g_menu_sel == 2) {
        gui_set_enabled(0);
        return;
    }
}

int gui_handle_arrow(char esc_code) {
    if (!g_menu_open) return 0;
    if (esc_code == 'A') { /* up */
        if (g_menu_sel > 0) g_menu_sel--;
        draw_menu();
        return 1;
    }
    if (esc_code == 'B') { /* down */
        if (g_menu_sel < 2) g_menu_sel++;
        draw_menu();
        return 1;
    }
    return 1; /* swallow other arrows while menu is open */
}

int gui_handle_key(char c) {
    if (gfx_available() && !g_gui_enabled) return 0;
    /* Ctrl+T toggles Start menu */
    if (c == 20) {
        g_menu_open = !g_menu_open;
        if (g_menu_open) {
            g_menu_sel = 0;
            draw_taskbar();
            draw_menu();
        } else {
            menu_close_redraw();
        }
        return 1;
    }

    if (!g_menu_open) return 0;

    if (c == '\n') {
        menu_activate();
        return 1;
    }
    if (c == 27) { /* ESC */
        menu_close_redraw();
        return 1;
    }

    return 1; /* swallow typing while menu is open */
}


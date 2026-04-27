#include "process.h"
#include "timer.h"

static process_info_t g_procs[PROC_MAX];
static int g_count = 0;
static int g_running_idx = 0;
static uint32_t g_last_switch_tick = 0;
static uint32_t g_last_cpu_window_tick = 0;
static uint32_t g_window_ticks[PROC_MAX];
static uint32_t g_sleep_until_tick[PROC_MAX];

static void set_name(char* dst, const char* src, int maxlen) {
    int i = 0;
    while (i < maxlen - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void add_proc(uint32_t pid, const char* name, proc_state_t st) {
    if (g_count >= PROC_MAX) return;
    g_procs[g_count].pid = pid;
    set_name(g_procs[g_count].name, name, PROC_NAME_MAX);
    g_procs[g_count].state = st;
    g_procs[g_count].cpu_pct = 0;
    g_procs[g_count].ticks_total = 0;
    g_count++;
}

void process_init(void) {
    g_count = 0;
    add_proc(0, "kernel-idle", PROC_RUNNING);
    add_proc(1, "banana-sh", PROC_READY);
    add_proc(2, "netd", PROC_READY);
    add_proc(3, "sysmon", PROC_SLEEPING);

    g_running_idx = 0;
    g_last_switch_tick = timer_ticks();
    g_last_cpu_window_tick = g_last_switch_tick;
    for (int i = 0; i < PROC_MAX; i++) {
        g_window_ticks[i] = 0;
        g_sleep_until_tick[i] = 0;
    }
    g_sleep_until_tick[3] = g_last_switch_tick + 20; /* sysmon wakes periodically */
}

void process_poll(void) {
    timer_poll();
    uint32_t now = timer_ticks();
    if (g_count == 0) return;

    for (int i = 0; i < g_count; i++) {
        if (g_procs[i].state == PROC_SLEEPING && g_sleep_until_tick[i] != 0 &&
            (int32_t)(now - g_sleep_until_tick[i]) >= 0) {
            g_procs[i].state = PROC_READY;
            g_sleep_until_tick[i] = 0;
        }
    }

    if (g_running_idx >= 0 && g_running_idx < g_count) {
        g_procs[g_running_idx].ticks_total++;
        g_window_ticks[g_running_idx]++;
    }

    if ((int32_t)(now - g_last_switch_tick) >= 4) {
        int prev = g_running_idx;
        int next = -1;
        for (int step = 1; step <= g_count; step++) {
            int cand = (prev + step) % g_count;
            if (g_procs[cand].state == PROC_READY || g_procs[cand].state == PROC_RUNNING) {
                next = cand;
                break;
            }
        }

        if (g_procs[prev].state == PROC_RUNNING) {
            if (prev == 3) {
                g_procs[prev].state = PROC_SLEEPING;
                g_sleep_until_tick[prev] = now + 20; /* sleep ~200ms */
            } else {
                g_procs[prev].state = PROC_READY;
            }
        }

        if (next >= 0 && g_procs[next].state != PROC_UNUSED) {
            g_running_idx = next;
            g_procs[g_running_idx].state = PROC_RUNNING;
        }
        g_last_switch_tick = now;
    }

    if ((int32_t)(now - g_last_cpu_window_tick) >= 100) {
        uint32_t total = 0;
        for (int i = 0; i < g_count; i++) total += g_window_ticks[i];
        if (total == 0) total = 1;
        for (int i = 0; i < g_count; i++) {
            g_procs[i].cpu_pct = (g_window_ticks[i] * 100u) / total;
            g_window_ticks[i] = 0;
        }
        g_last_cpu_window_tick = now;
    }
}

int process_count(void) {
    return g_count;
}

void process_snapshot(process_info_t* out, int max_count) {
    int n = (g_count < max_count) ? g_count : max_count;
    for (int i = 0; i < n; i++) out[i] = g_procs[i];
}

const char* process_state_str(proc_state_t s) {
    if (s == PROC_RUNNING) return "running";
    if (s == PROC_READY) return "ready";
    if (s == PROC_SLEEPING) return "sleep";
    return "unused";
}

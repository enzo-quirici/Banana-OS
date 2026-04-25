#include "process.h"
#include "timer.h"

static process_info_t g_procs[PROC_MAX];
static int g_count = 0;
static int g_running_idx = 0;
static uint32_t g_last_switch_tick = 0;

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
}

void process_poll(void) {
    uint32_t now = timer_ticks();
    if (g_count == 0) return;

    if (g_running_idx >= 0 && g_running_idx < g_count) {
        g_procs[g_running_idx].ticks_total++;
    }

    if ((int32_t)(now - g_last_switch_tick) >= 15) {
        int prev = g_running_idx;
        g_running_idx = (g_running_idx + 1) % g_count;

        if (g_procs[prev].state == PROC_RUNNING)
            g_procs[prev].state = (prev == 3) ? PROC_SLEEPING : PROC_READY;

        if (g_procs[g_running_idx].state != PROC_UNUSED)
            g_procs[g_running_idx].state = PROC_RUNNING;

        if ((now % 100) < 20) g_procs[3].state = PROC_READY;
        g_last_switch_tick = now;
    }

    uint32_t total = 0;
    for (int i = 0; i < g_count; i++) total += g_procs[i].ticks_total;
    if (total == 0) total = 1;
    for (int i = 0; i < g_count; i++)
        g_procs[i].cpu_pct = (g_procs[i].ticks_total * 100u) / total;
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

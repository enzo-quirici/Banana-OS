#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"

#define PROC_MAX 8
#define PROC_NAME_MAX 24

typedef enum {
    PROC_UNUSED = 0,
    PROC_RUNNING,
    PROC_READY,
    PROC_SLEEPING
} proc_state_t;

typedef struct {
    uint32_t pid;
    char name[PROC_NAME_MAX];
    proc_state_t state;
    uint32_t cpu_pct;
    uint32_t ticks_total;
} process_info_t;

void process_init(void);
void process_poll(void);
int process_count(void);
void process_snapshot(process_info_t* out, int max_count);
const char* process_state_str(proc_state_t s);

#endif

#ifndef SYSINFO_H
#define SYSINFO_H

#include "types.h"

typedef struct {
    char     cpu_vendor[13];    /* e.g. "GenuineIntel" */
    char     cpu_brand[49];     /* full CPU name string */
    uint32_t mem_kb;            /* total memory in KiB from multiboot */
} sysinfo_t;

void sysinfo_init(uint32_t mb_info_addr);
void sysinfo_init_mb2(uint32_t mb2_info_addr);
const sysinfo_t* sysinfo_get(void);

#endif

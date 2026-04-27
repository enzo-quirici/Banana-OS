#ifndef TIMER_H
#define TIMER_H

#include "types.h"

void     timer_init(void);          /* program PIT channel 0 at ~100 Hz */
uint32_t timer_ticks(void);         /* ticks since init */
void     timer_poll(void);          /* update ticks from PIT wraps */
void     timer_sleep_ms(uint32_t ms);

#endif

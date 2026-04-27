#include "timer.h"
#include "types.h"

/*
 * PIT channel 0, mode 2 (rate generator), ~100 Hz.
 * We poll PIT wraps instead of using IRQs.
 */

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_HZ    100
#define PIT_BASE  1193182u

// ✅ global tick counter (must be at file scope)
static volatile uint32_t _ticks = 0;
static uint16_t pit_divisor = 0;
static uint16_t last_pit_value = 0;

static uint16_t pit_read(void);

/* ────────────────────────────────────────────── */
/* Low-level port I/O                            */
/* ────────────────────────────────────────────── */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(val), "Nd"(port)
    );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile (
        "inb %1, %0"
        : "=a"(ret)
        : "Nd"(port)
    );
    return ret;
}

/* ────────────────────────────────────────────── */
/* Timer init                                    */
/* ────────────────────────────────────────────── */
void timer_init(void) {
    uint32_t divisor = PIT_BASE / PIT_HZ;

    uint8_t lo = divisor & 0xFF;
    uint8_t hi = (divisor >> 8) & 0xFF;

    // channel 0, lobyte/hibyte, mode 2, binary
    outb(PIT_CMD, 0x34);
    outb(PIT_CH0, lo);
    outb(PIT_CH0, hi);

    pit_divisor = (uint16_t)divisor;
    _ticks = 0;
    last_pit_value = pit_read();
}

/* ────────────────────────────────────────────── */
/* Read PIT counter                              */
/* ────────────────────────────────────────────── */
static uint16_t pit_read(void) {
    // latch channel 0
    outb(PIT_CMD, 0x00);

    uint8_t lo = inb(PIT_CH0);
    uint8_t hi = inb(PIT_CH0);

    return ((uint16_t)hi << 8) | lo;
}

/* ────────────────────────────────────────────── */
/* Public API                                    */
/* ────────────────────────────────────────────── */
uint32_t timer_ticks(void) {
    return _ticks;
}

void timer_poll(void) {
    if (pit_divisor == 0) return;
    uint16_t cur = pit_read();
    /* mode 2 downcounter reload appears as cur > last */
    if (cur > last_pit_value) _ticks++;
    last_pit_value = cur;
}

/*
 * Sleep based on PIT wraps (stable in VMs and real hardware).
 * At 100 Hz, one wrap ~= 10 ms.
 */
void timer_sleep_ms(uint32_t ms) {
    if (ms == 0) return;

    if (pit_divisor == 0) {
        /* timer_init() not called: fail safe with a tiny spin */
        for (volatile uint32_t i = 0; i < (ms * 1000u); i++) __asm__ volatile("nop");
        return;
    }

    uint32_t target_wraps = (ms * PIT_HZ + 999u) / 1000u; /* ceil(ms / 10ms) */
    if (target_wraps == 0) target_wraps = 1;

    uint32_t start = timer_ticks();
    while ((timer_ticks() - start) < target_wraps) {
        timer_poll();
    }
}
#include "rtc.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port));
    return v;
}

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int bcd_to_bin(int v) {
    return (v & 0x0F) + ((v >> 4) * 10);
}

static int rtc_read_raw(rtc_datetime_t* out, uint8_t* reg_b) {
    if (!out || !reg_b) return -1;

    out->second = cmos_read(0x00);
    out->minute = cmos_read(0x02);
    out->hour   = cmos_read(0x04);
    out->day    = cmos_read(0x07);
    out->month  = cmos_read(0x08);
    out->year   = cmos_read(0x09);
    *reg_b      = cmos_read(0x0B);
    return 0;
}

void rtc_init(void) {
    /* No setup needed for basic RTC reads. */
}

int rtc_read_datetime(rtc_datetime_t* out) {
    if (!out) return -1;

    rtc_datetime_t a, b;
    uint8_t reg_b = 0;
    int guard = 10000;

    while (guard-- > 0) {
        while (cmos_read(0x0A) & 0x80) { /* update-in-progress */ }
        rtc_read_raw(&a, &reg_b);
        while (cmos_read(0x0A) & 0x80) { }
        rtc_read_raw(&b, &reg_b);

        if (a.second == b.second &&
            a.minute == b.minute &&
            a.hour   == b.hour &&
            a.day    == b.day &&
            a.month  == b.month &&
            a.year   == b.year) {
            break;
        }
    }
    if (guard <= 0) return -1;

    /* Convert from BCD if needed (reg B bit 2 = 1 means binary mode). */
    if ((reg_b & 0x04) == 0) {
        b.second = (uint8_t)bcd_to_bin(b.second);
        b.minute = (uint8_t)bcd_to_bin(b.minute);
        b.hour   = (uint8_t)(bcd_to_bin(b.hour & 0x7F) | (b.hour & 0x80));
        b.day    = (uint8_t)bcd_to_bin(b.day);
        b.month  = (uint8_t)bcd_to_bin(b.month);
        b.year   = (uint16_t)bcd_to_bin((int)b.year);
    }

    /* Convert 12h mode to 24h if needed (reg B bit 1 = 1 means 24h mode). */
    if ((reg_b & 0x02) == 0) {
        int pm = (b.hour & 0x80) ? 1 : 0;
        int h = b.hour & 0x7F;
        if (pm && h < 12) h += 12;
        if (!pm && h == 12) h = 0;
        b.hour = (uint8_t)h;
    } else {
        b.hour &= 0x7F;
    }

    b.year = (uint16_t)(2000u + (b.year % 100u)); /* best effort */
    *out = b;
    return 0;
}

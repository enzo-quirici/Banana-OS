#ifndef TYPES_H
#define TYPES_H

/* Freestanding types - no system headers needed */

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef unsigned int       size_t;
typedef signed int         ptrdiff_t;

/* ✅ ADD THIS (required for pointer casts) */
typedef unsigned int       uintptr_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

/* uint32 → decimal string, returns pointer into buf */
static inline char* u32_to_str(uint32_t n, char* buf, int buflen) {
    buf[buflen-1] = '\0';
    int i = buflen - 2;
    if (n == 0) {
        buf[i--] = '0';
    } else {
        while (n > 0 && i >= 0) {
            buf[i--] = '0' + (n % 10);
            n /= 10;
        }
    }
    return &buf[i+1];
}

#endif
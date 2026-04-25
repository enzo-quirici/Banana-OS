#include "sysinfo.h"
#include "types.h"

/* Multiboot info structure (partial) */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;   /* KiB below 1MB  */
    uint32_t mem_upper;   /* KiB above 1MB  */
    /* ... rest unused */
} __attribute__((packed)) mb_info_t;

static sysinfo_t info;

static void cpuid(uint32_t leaf,
                  uint32_t* eax, uint32_t* ebx,
                  uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf)
        : 
    );
}

static void store_str(char* dst, uint32_t val) {
    dst[0] = (val)       & 0xFF;
    dst[1] = (val >>  8) & 0xFF;
    dst[2] = (val >> 16) & 0xFF;
    dst[3] = (val >> 24) & 0xFF;
}

void sysinfo_init(uint32_t mb_info_addr) {
    uint32_t eax, ebx, ecx, edx;

    /* ── CPU vendor ─────────────────────────────────────────────── */
    cpuid(0, &eax, &ebx, &ecx, &edx);
    store_str(info.cpu_vendor + 0, ebx);
    store_str(info.cpu_vendor + 4, edx);
    store_str(info.cpu_vendor + 8, ecx);
    info.cpu_vendor[12] = '\0';

    /* ── CPU brand string (leaves 0x80000002-4) ─────────────────── */
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004) {
        uint32_t* p = (uint32_t*)info.cpu_brand;
        for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
            cpuid(leaf, &eax, &ebx, &ecx, &edx);
            *p++ = eax; *p++ = ebx; *p++ = ecx; *p++ = edx;
        }
        info.cpu_brand[48] = '\0';
        /* trim leading spaces */
        char* s = info.cpu_brand;
        while (*s == ' ') s++;
        if (s != info.cpu_brand) {
            char tmp[49];
            int i = 0;
            while (s[i]) { tmp[i] = s[i]; i++; }
            tmp[i] = '\0';
            for (i = 0; tmp[i]; i++) info.cpu_brand[i] = tmp[i];
            info.cpu_brand[i] = '\0';
        }
    } else {
        /* fallback */
        const char* fb = "Whatever your hypervisor gives you";
        int i = 0;
        while (fb[i]) { info.cpu_brand[i] = fb[i]; i++; }
        info.cpu_brand[i] = '\0';
    }

    /* ── Memory from Multiboot ──────────────────────────────────── */
    if (mb_info_addr) {
        mb_info_t* mb = (mb_info_t*)mb_info_addr;
        if (mb->flags & 0x1) {
            /* mem_upper is KiB above 1MB; add 1024 for the first MB */
            info.mem_kb = mb->mem_upper + 1024;
        } else {
            info.mem_kb = 0;
        }
    } else {
        info.mem_kb = 0;
    }
}

/* ── Multiboot2 memory info ─────────────────────────────────────── */
typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed)) mb2_info_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_t;

typedef struct {
    uint32_t type;   /* 4 */
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __attribute__((packed)) mb2_tag_basic_mem_t;

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;      /* 1 = available RAM */
    uint32_t reserved;
} __attribute__((packed)) mb2_mmap_entry_t;

typedef struct {
    uint32_t type;       /* 6 */
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    /* entries[] */
} __attribute__((packed)) mb2_tag_mmap_t;

static uint32_t align_up8(uint32_t v) {
    return (v + 7u) & ~7u;
}

void sysinfo_init_mb2(uint32_t mb2_info_addr) {
    /* Keep CPU fields from cpuid() logic */
    sysinfo_init(0);

    info.mem_kb = 0;
    if (!mb2_info_addr) return;

    mb2_info_t* mb2 = (mb2_info_t*)mb2_info_addr;
    uint32_t total = mb2->total_size;
    uint32_t off = 8;

    while (off + 8 <= total) {
        mb2_tag_t* tag = (mb2_tag_t*)(uintptr_t)(mb2_info_addr + off);
        if (tag->type == 0) break;

        if (tag->type == 6 && tag->size >= sizeof(mb2_tag_mmap_t)) {
            mb2_tag_mmap_t* mm = (mb2_tag_mmap_t*)tag;
            if (mm->entry_size >= sizeof(mb2_mmap_entry_t)) {
                uint64_t max_end = 0;
                uint32_t pos = sizeof(mb2_tag_mmap_t);
                while (pos + mm->entry_size <= mm->size) {
                    mb2_mmap_entry_t* e = (mb2_mmap_entry_t*)((uintptr_t)mm + pos);
                    if (e->type == 1) {
                        uint64_t end = e->base_addr + e->length;
                        if (end > max_end) max_end = end;
                    }
                    pos += mm->entry_size;
                }
                if (max_end > 0) {
                    info.mem_kb = (uint32_t)(max_end / 1024u);
                    return;
                }
            }
        }

        if (tag->type == 4 && tag->size >= sizeof(mb2_tag_basic_mem_t)) {
            mb2_tag_basic_mem_t* mem = (mb2_tag_basic_mem_t*)tag;
            /* mem_upper is KiB above 1MB; add 1024 for the first MB */
            info.mem_kb = mem->mem_upper + 1024u;
            return;
        }

        off += align_up8(tag->size);
    }
}

const sysinfo_t* sysinfo_get(void) {
    return &info;
}

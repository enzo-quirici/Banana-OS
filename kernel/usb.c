#include "usb.h"
#include "terminal.h"
#include "types.h"

/* ── I/O helpers ─────────────────────────────────────────────────── */
static inline uint8_t  inb (uint16_t p){ uint8_t  v; __asm__ volatile("inb  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw (uint16_t p){ uint16_t v; __asm__ volatile("inw  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint32_t inl (uint16_t p){ uint32_t v; __asm__ volatile("inl  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void     outb(uint16_t p, uint8_t  v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void     outl(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }

static char usb_status_buf[96];
static int usb_xhci_found = 0, usb_xhci_handoff_ok = 0;
static int usb_ehci_found = 0, usb_ehci_handoff_ok = 0;
static int usb_uhci_found = 0, usb_ohci_found = 0;

/* ── PCI helpers ─────────────────────────────────────────────────── */
#define PCI_ADDR  0xCF8
#define PCI_DATA  0xCFC

static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)fn  <<  8)
                  | (off & 0xFC);
    outl(PCI_ADDR, addr);
    return inl(PCI_DATA);
}

static void pci_write(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t val) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)fn  <<  8)
                  | (off & 0xFC);
    outl(PCI_ADDR, addr);
    outl(PCI_DATA, val);
}

static void pci_enable_usb_decode(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t cmd = pci_read(bus, dev, fn, 0x04);
    cmd |= 0x00000007u; /* I/O space + memory space + bus master */
    pci_write(bus, dev, fn, 0x04, cmd);
}

/* ── xHCI legacy handoff ─────────────────────────────────────────── */
/*
 * Scan all PCI buses for an xHCI controller (class 0x0C, sub 0x03, prog 0x30).
 * If found, read the xHCI Extended Capabilities pointer, find the
 * USB Legacy Support Capability (cap ID 1), set the OS Owned bit,
 * and wait for BIOS to release it.  After that the BIOS SMI handler
 * keeps routing USB HID → PS/2 port 0x60 for us.
 */
static void __attribute__((unused)) xhci_handoff(uint8_t bus, uint8_t dev, uint8_t fn) {
    /*
     * BAR0 holds xHCI MMIO base and may be 32-bit or 64-bit memory BAR.
     * In this kernel we only use 32-bit identity-mapped addresses.
     */
    uint32_t bar0_lo = pci_read(bus, dev, fn, 0x10);
    if (!(bar0_lo & 0x1) && ((bar0_lo & 0x6) == 0x4)) {
        /* 64-bit BAR: include upper dword if present */
        uint32_t bar0_hi = pci_read(bus, dev, fn, 0x14);
        if (bar0_hi != 0) return; /* MMIO above 4 GiB is not reachable here */
    }
    uint32_t bar0 = bar0_lo & ~0xFu;
    if (!bar0) return;

    /* HCCPARAMS1 is at offset 0x10 in the capability registers */
    volatile uint32_t* base = (volatile uint32_t*)(uintptr_t)bar0;
    uint32_t hccparams1 = base[4];  /* offset 0x10 / 4 */
    uint32_t xecp_off   = (hccparams1 >> 16) & 0xFFFF;
    if (!xecp_off) return;

    volatile uint32_t* xecp = base + xecp_off;
    /* Walk extended capability list */
    for (int iter = 0; iter < 32; iter++) {
        uint32_t cap = *xecp;
        uint8_t  id  = cap & 0xFF;
        if (id == 1) {
            /* USB Legacy Support cap found */
            /* Set OS Owned Semaphore (bit 24) */
            *xecp = cap | (1u << 24);
            /* Wait for BIOS Owned (bit 16) to clear */
            for (int t = 0; t < 100000; t++) {
                if (!(*xecp & (1u << 16))) break;
                /* small spin delay */
                for (int d = 0; d < 100; d++)
                    __asm__ volatile("pause");
            }
            usb_xhci_handoff_ok++;
            return;
        }
        uint8_t next = (cap >> 8) & 0xFF;
        if (!next) break;
        xecp += next;
    }
}

/* ── EHCI legacy handoff ─────────────────────────────────────────── */
/*
 * EHCI exposes an "Extended Capabilities Pointer" (EECP) in HCCPARAMS.
 * If the USB Legacy Support capability is present, set OS Owned semaphore
 * and wait for BIOS Owned to clear so firmware SMI can hand over cleanly.
 */
static void __attribute__((unused)) ehci_handoff(uint8_t bus, uint8_t dev, uint8_t fn) {
    /* BAR0 can be 32-bit memory BAR for EHCI operational registers. */
    uint32_t bar0 = pci_read(bus, dev, fn, 0x10) & ~0xFu;
    if (!bar0) return;

    volatile uint32_t* base = (volatile uint32_t*)(uintptr_t)bar0;
    /* EHCI HCCPARAMS at offset 0x08 */
    uint32_t hccparams = base[2];
    uint8_t eecp = (uint8_t)((hccparams >> 8) & 0xFF);
    if (!eecp) return;

    /*
     * USBLEGSUP is a PCI config dword at EECP.
     * bit16 = BIOS Owned, bit24 = OS Owned
     */
    uint32_t legsup = pci_read(bus, dev, fn, eecp);
    if (!(legsup & (1u << 24))) {
        legsup |= (1u << 24);
        pci_write(bus, dev, fn, eecp, legsup);
    }

    for (int t = 0; t < 100000; t++) {
        uint32_t now = pci_read(bus, dev, fn, eecp);
        if (!(now & (1u << 16))) break;
        for (int d = 0; d < 100; d++) __asm__ volatile("pause");
    }

    /*
     * Disable legacy USB SMI sources that can steal interrupts/events.
     * USBLEGCTLSTS is at EECP + 4 for EHCI.
     */
    pci_write(bus, dev, fn, (uint8_t)(eecp + 4), 0);
    usb_ehci_handoff_ok++;
}

void usb_init(void) {
    usb_xhci_found = usb_xhci_handoff_ok = 0;
    usb_ehci_found = usb_ehci_handoff_ok = 0;
    usb_uhci_found = usb_ohci_found = 0;

    /*
     * Scan PCI for USB controllers.
     *
     * IMPORTANT:
     * This kernel currently uses PS/2 scancodes only (port 0x60) and does not
     * implement native EHCI/xHCI transfers. Therefore we keep BIOS/firmware
     * legacy emulation in control instead of claiming OS ownership.
     *
     * On QEMU with `-device usb-kbd`, this keeps keyboard events translated
     * to PS/2 so the existing keyboard driver continues to work.
     */
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            for (uint32_t fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) { if (fn==0) break; continue; }
                uint32_t cc = pci_read(bus, dev, fn, 8) >> 8;
                if ((cc & 0xFFFF00) == 0x0C0300) {
                    pci_enable_usb_decode((uint8_t)bus, (uint8_t)dev, (uint8_t)fn);
                }
                /* class=0x0C serial bus, sub=0x03 USB, prog=0x30 xHCI */
                if (cc == 0x0C0330) {
                    usb_xhci_found++;
                    /* preserve BIOS-owned legacy emulation for now */
                }
                /* class=0x0C serial bus, sub=0x03 USB, prog=0x20 EHCI */
                if (cc == 0x0C0320) {
                    usb_ehci_found++;
                    /* preserve BIOS-owned legacy emulation for now */
                }
                if (cc == 0x0C0300) usb_uhci_found++;
                if (cc == 0x0C0310) usb_ohci_found++;
                if (fn == 0) {
                    uint32_t hdr = pci_read(bus, dev, fn, 0x0C);
                    if (!((hdr >> 16) & 0x80)) break; /* not multi-function */
                }
            }
        }
    }
    /*
     * After xHCI handoff, the BIOS continues to translate USB HID
     * reports to PS/2 scancodes through port 0x60.  Our existing
     * PS/2 keyboard driver therefore works for USB keyboards with
     * zero extra code.
     */
}

const char* usb_status(void) {
    int p = 0;
    char nbuf[16];
    const char* s;
    const char* a = "USB: xHCI ";
    const char* b = " EHCI ";
    const char* c = " UHCI ";
    const char* d = " OHCI ";
    const char* e = " (BIOS legacy preserved)";

    for (int i = 0; a[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = a[i];
    s = u32_to_str((uint32_t)usb_xhci_handoff_ok, nbuf, sizeof(nbuf));
    for (int i = 0; s[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = s[i];
    if (p < (int)sizeof(usb_status_buf) - 1) usb_status_buf[p++] = '/';
    s = u32_to_str((uint32_t)usb_xhci_found, nbuf, sizeof(nbuf));
    for (int i = 0; s[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = s[i];

    for (int i = 0; b[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = b[i];
    s = u32_to_str((uint32_t)usb_ehci_handoff_ok, nbuf, sizeof(nbuf));
    for (int i = 0; s[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = s[i];
    if (p < (int)sizeof(usb_status_buf) - 1) usb_status_buf[p++] = '/';
    s = u32_to_str((uint32_t)usb_ehci_found, nbuf, sizeof(nbuf));
    for (int i = 0; s[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = s[i];

    for (int i = 0; c[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = c[i];
    s = u32_to_str((uint32_t)usb_uhci_found, nbuf, sizeof(nbuf));
    for (int i = 0; s[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = s[i];

    for (int i = 0; d[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = d[i];
    s = u32_to_str((uint32_t)usb_ohci_found, nbuf, sizeof(nbuf));
    for (int i = 0; s[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = s[i];

    for (int i = 0; e[i] && p < (int)sizeof(usb_status_buf) - 1; i++) usb_status_buf[p++] = e[i];
    usb_status_buf[p < (int)sizeof(usb_status_buf) ? p : (int)sizeof(usb_status_buf) - 1] = '\0';
    return usb_status_buf;
}

/* ─────────────────────────────────────────────────────────────────
 * PS/2 Mouse (AUX port)
 * The mouse is connected to the 8042 AUX channel.
 * We send the "enable" command and then poll.
 * ───────────────────────────────────────────────────────────────── */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

static int mouse_enabled = 0;
static uint8_t mouse_pkt[3];
static int mouse_pkt_i = 0;

static void ps2_wait_write(void) {
    int t = 100000;
    while ((inb(PS2_STATUS) & 0x02) && t--);
}
static void ps2_wait_read(void) {
    int t = 100000;
    while (!(inb(PS2_STATUS) & 0x01) && t--);
}
static uint8_t ps2_mouse_read(void) {
    ps2_wait_read();
    return inb(PS2_DATA);
}
static void ps2_mouse_write(uint8_t val) {
    ps2_wait_write();
    outb(PS2_CMD, 0xD4);   /* next byte goes to AUX port */
    ps2_wait_write();
    outb(PS2_DATA, val);
}

void mouse_init(void) {
    /* Enable AUX port */
    ps2_wait_write();
    outb(PS2_CMD, 0xA8);

    /* Enable AUX interrupt in 8042 command byte */
    ps2_wait_write();
    outb(PS2_CMD, 0x20);           /* read command byte */
    ps2_wait_read();
    uint8_t cb = inb(PS2_DATA);
    cb |= 0x02;                    /* enable IRQ12 (AUX) */
    cb &= ~0x20;                   /* clear "disable mouse" bit */
    ps2_wait_write();
    outb(PS2_CMD, 0x60);
    ps2_wait_write();
    outb(PS2_DATA, cb);

    /* Reset mouse */
    ps2_mouse_write(0xFF);
    ps2_mouse_read();  /* ACK */
    ps2_mouse_read();  /* 0xAA */
    ps2_mouse_read();  /* 0x00 */

    /* Set defaults + enable */
    ps2_mouse_write(0xF6);  /* set defaults */
    ps2_mouse_read();       /* ACK */
    ps2_mouse_write(0xF4);  /* enable */
    ps2_mouse_read();       /* ACK */

    mouse_enabled = 1;
}

static mouse_state_t last_mouse = {0,0,0,0,0};

void mouse_on_aux_byte(uint8_t b) {
    if (!mouse_enabled) return;

    if (mouse_pkt_i == 0) {
        /* validate sync bit */
        if (!(b & 0x08)) return;
        mouse_pkt[0] = b;
        mouse_pkt_i = 1;
        return;
    }
    if (mouse_pkt_i == 1) {
        mouse_pkt[1] = b;
        mouse_pkt_i = 2;
        return;
    }
    if (mouse_pkt_i == 2) {
        mouse_pkt[2] = b;
        mouse_pkt_i = 3;
        return;
    }
}

static int mouse_pkt_ready(void) {
    return mouse_pkt_i >= 3;
}

static void mouse_consume_ready(void) {
    uint8_t b0 = mouse_pkt[0];
    uint8_t b1 = mouse_pkt[1];
    uint8_t b2 = mouse_pkt[2];

    last_mouse.btn_left   = b0 & 0x01;
    last_mouse.btn_right  = (b0 >> 1) & 0x01;
    last_mouse.btn_middle = (b0 >> 2) & 0x01;

    last_mouse.dx = (int)b1 - ((b0 & 0x10) ? 256 : 0);
    last_mouse.dy = (int)b2 - ((b0 & 0x20) ? 256 : 0);

    mouse_pkt_i = 0;
}

/*
 * Non-blocking mouse read: if a full 3-byte packet is available in
 * the 8042 output buffer (bit 5 of status = AUX data), consume it.
 */
mouse_state_t mouse_read(void) {
    if (!mouse_enabled) return last_mouse;

    /* If no new packet arrives, deltas must be 0 (avoid cursor drift). */
    last_mouse.dx = 0;
    last_mouse.dy = 0;

    if (mouse_pkt_ready()) {
        mouse_consume_ready();
        return last_mouse;
    }

    /* Check if data is from AUX (bit 5 set) and available (bit 0 set) */
    uint8_t st = inb(PS2_STATUS);
    if (!((st & 0x01) && (st & 0x20))) return last_mouse;

    /* Drain as many AUX bytes as available into the packet assembler */
    while (1) {
        st = inb(PS2_STATUS);
        if (!((st & 0x01) && (st & 0x20))) break;
        uint8_t b = inb(PS2_DATA);
        mouse_on_aux_byte(b);
        if (mouse_pkt_ready()) break;
    }

    if (mouse_pkt_ready()) {
        mouse_consume_ready();
    }

    return last_mouse;
}

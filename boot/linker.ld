/* Banana OS 0.3 linker script */
ENTRY(_start)

SECTIONS {
    . = 0x100000;       /* Load kernel at 1MB */

    .text BLOCK(4K) : ALIGN(4K) {
        *(.multiboot)
        *(.multiboot2)
        *(.text)
    }

    .rodata BLOCK(4K) : ALIGN(4K) {
        *(.rodata)
    }

    .data BLOCK(4K) : ALIGN(4K) {
        *(.data)
    }

    .bss BLOCK(4K) : ALIGN(4K) {
        *(COMMON)
        *(.bss)
    }
}

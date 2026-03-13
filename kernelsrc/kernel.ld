ENTRY(_start)

SECTIONS {
    . = 0x7C00;          /* Load kernel at 1MB */

    .text   : { *(.text)   }
    .rodata : { *(.rodata) }
    .data   : { *(.data)   }
    .bss    : {
        _bss_start = .;
        *(.bss)
        *(COMMON)
        _bss_end = .;
    }
    _kernel_end = .;
}
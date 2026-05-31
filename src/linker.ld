ENTRY(kernel_main)          // the first symbol the CPU will execute

SECTIONS {
    . = 0x10000;            // load address: start at physical 0x10000

    .text : {               // code section
        *(.text.boot)
        *(.text)            // include .text from all object files
    }

    .rodata : {             // read-only data (string literals, const globals)
        *(.rodata)
    }

    .data : {               // initialised read-write data
        *(.data)
    }

    .bss : {                // uninitialised data (zeroed at startup)
        *(.bss)
        *(COMMON)
    }
}

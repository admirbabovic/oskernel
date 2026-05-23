/* ===========================================================================
   kernel.c  —  Minimal C kernel entry point
   ===========================================================================
   This is where the bootloader jumps to after switching to protected mode.
   The linker script (linker.ld) ensures kernel_main() is placed at 0x10000.

   IMPORTANT: We do NOT have a C runtime (libc) here.  No malloc, no printf,
   no constructors.  We have raw memory and a flat 32-bit address space.
   =========================================================================== */

/* VGA text mode buffer lives at physical address 0xB8000.
   In 80x25 text mode each cell is 2 bytes:
     byte 0: ASCII character code
     byte 1: colour attribute (high nibble = background, low nibble = foreground)
   Common colour codes: 0x0F = white on black, 0x0A = bright green on black */

#define VGA_BUFFER ((unsigned short*)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

/* Simple VGA helper: write a string at (row, col) with the given colour */
static void vga_write(const char* str, int row, int col, unsigned char colour) {
    int i;
    for (i = 0; str[i] != '\0'; i++) {
        int index = row * VGA_COLS + col + i;
        VGA_BUFFER[index] = (unsigned short)str[i] | ((unsigned short)colour << 8);
    }
}

/* Clear the entire screen to black */
static void vga_clear(void) {
    int i;
    for (i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        VGA_BUFFER[i] = 0x0F20; /* space character, white on black */
    }
}

/* ---------------------------------------------------------------------------
   kernel_main — the C kernel entry point
   ---------------------------------------------------------------------------
   The bootloader jumps directly to this function's address (0x10000).
   No name mangling occurs in C so no extern "C" is needed. */
void kernel_main(void) {
    vga_clear();
    vga_write("Kernel loaded successfully!", 0, 0, 0x0A); /* bright green */
    vga_write("Welcome to MyOS",             2, 0, 0x0F); /* white */
    vga_write("Running in 32-bit protected mode", 3, 0, 0x0F);

    /* Hang forever — a real kernel would set up interrupts, scheduler, etc. */
    for (;;) {
        __asm__ volatile("hlt");
    }
}
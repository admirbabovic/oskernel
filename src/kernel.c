/*
 * kernel.c — MyOS Kernel
 *
 * A bare-metal x86 kernel that runs in 32-bit protected mode.
 * Features:
 *   - VGA text-mode driver (colours, cursor control, scrolling)
 *   - GDT already set up by the bootloader
 *   - IDT with 256 entries wired to ISR stubs
 *   - PIC remapping (IRQs 0-15 → INT 0x20-0x2F)
 *   - Timer interrupt (IRQ 0) — ticks counter
 *   - Keyboard interrupt (IRQ 1) — US-QWERTY scancode → ASCII
 *   - CPU-exception handlers (divide-by-zero, page fault, etc.)
 *   - Simple shell (type commands, press Enter)
 *   - Built-in commands: help, clear, about, uptime, reboot, halt, echo
 *   - Animated boot banner drawn with VGA colours
 */

/* =========================================================================
 * 0.  COMPILER BUILT-INS & TYPE DEFINITIONS
 * ========================================================================= */

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;

typedef uint32_t size_t;

#define NULL ((void*)0)

/* =========================================================================
 * 1.  PORT I/O HELPERS
 * ========================================================================= */

/*
 * outb — write one byte to an x86 I/O port.
 * The "volatile" asm prevents the compiler reordering or removing the
 * instruction.  "N" means the port must be a compile-time constant 0-255;
 * "d" means the DX register for wider ranges.
 */
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %1, %0" : : "dN"(port), "a"(value));
}

/* inb — read one byte from an x86 I/O port. */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

/* io_wait — tiny delay by writing to an unused port; used after PIC writes. */
static inline void io_wait(void) {
    outb(0x80, 0);
}

/* =========================================================================
 * 2.  VGA TEXT-MODE DRIVER
 * ========================================================================= */

/*
 * The VGA text-mode frame-buffer lives at physical address 0xB8000.
 * Each cell is 2 bytes: [attribute | character].
 * Attribute byte: bits 7-4 = background colour, bits 3-0 = foreground colour.
 */
#define VGA_ADDRESS  0xB8000
#define VGA_COLS     80
#define VGA_ROWS     25

/* Standard VGA colour indices */
typedef enum {
    VGA_BLACK         = 0,
    VGA_BLUE          = 1,
    VGA_GREEN         = 2,
    VGA_CYAN          = 3,
    VGA_RED           = 4,
    VGA_MAGENTA       = 5,
    VGA_BROWN         = 6,
    VGA_LIGHT_GREY    = 7,
    VGA_DARK_GREY     = 8,
    VGA_LIGHT_BLUE    = 9,
    VGA_LIGHT_GREEN   = 10,
    VGA_LIGHT_CYAN    = 11,
    VGA_LIGHT_RED     = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW        = 14,
    VGA_WHITE         = 15,
} vga_color_t;

/* Combine foreground + background into a single attribute byte */
static inline uint8_t vga_attr(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

/* Pack character + attribute into a 16-bit VGA cell */
static inline uint16_t vga_cell(char c, uint8_t attr) {
    return (uint16_t)((uint16_t)attr << 8 | (uint8_t)c);
}

/* Global terminal state */
static uint16_t *vga_buf    = (uint16_t *)VGA_ADDRESS;
static int       term_col   = 0;   /* current cursor column  */
static int       term_row   = 0;   /* current cursor row     */
static uint8_t   term_attr  = 0;   /* current attribute byte */

/* vga_set_color — update the active foreground/background pair */
void vga_set_color(vga_color_t fg, vga_color_t bg) {
    term_attr = vga_attr(fg, bg);
}

/* vga_clear — fill every cell with spaces using the current attribute */
void vga_clear(void) {
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_buf[r * VGA_COLS + c] = vga_cell(' ', term_attr);
    term_col = 0;
    term_row = 0;
}

/*
 * vga_update_cursor — move the hardware text cursor.
 * The VGA CRTC registers 0x0E/0x0F hold the 16-bit cursor position.
 * We reach them through the CRTC index port (0x3D4) and data port (0x3D5).
 */
void vga_update_cursor(void) {
    uint16_t pos = (uint16_t)(term_row * VGA_COLS + term_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/*
 * vga_scroll — shift every row up by one line, blank the last row.
 * Called when the cursor moves past row 24.
 */
static void vga_scroll(void) {
    /* Copy row N to row N-1 */
    for (int r = 1; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_buf[(r - 1) * VGA_COLS + c] = vga_buf[r * VGA_COLS + c];

    /* Blank the last row */
    for (int c = 0; c < VGA_COLS; c++)
        vga_buf[(VGA_ROWS - 1) * VGA_COLS + c] = vga_cell(' ', term_attr);

    /* Move cursor to last row, same column */
    term_row = VGA_ROWS - 1;
}

/*
 * vga_putchar — place a single character at the current cursor position,
 * handling newline, carriage-return, backspace and automatic scrolling.
 */
void vga_putchar(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
    } else if (c == '\r') {
        term_col = 0;
    } else if (c == '\b') {
        /* Backspace: erase the character to the left */
        if (term_col > 0) {
            term_col--;
            vga_buf[term_row * VGA_COLS + term_col] = vga_cell(' ', term_attr);
        }
    } else {
        vga_buf[term_row * VGA_COLS + term_col] = vga_cell(c, term_attr);
        term_col++;
        if (term_col >= VGA_COLS) {
            term_col = 0;
            term_row++;
        }
    }

    if (term_row >= VGA_ROWS)
        vga_scroll();

    vga_update_cursor();
}

/* vga_puts — write a NUL-terminated string */
void vga_puts(const char *s) {
    while (*s)
        vga_putchar(*s++);
}

/* vga_putchar_at — write a character directly at row/col without moving cursor */
void vga_putchar_at(char c, uint8_t attr, int row, int col) {
    vga_buf[row * VGA_COLS + col] = vga_cell(c, attr);
}

/* =========================================================================
 * 3.  MINIMAL STRING / NUMBER UTILITIES  (no libc available)
 * ========================================================================= */

size_t k_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int k_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int k_strncmp(const char *a, const char *b, size_t n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    if (n == (size_t)-1) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *k_strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

/* k_itoa — convert unsigned 32-bit integer to decimal string in buf */
void k_itoa(uint32_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int i = 0;
    while (val) { tmp[i++] = '0' + val % 10; val /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* k_memset — fill n bytes of dst with value c */
void *k_memset(void *dst, int c, size_t n) {
    unsigned char *p = (unsigned char *)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

/* =========================================================================
 * 4.  GLOBAL DESCRIPTOR TABLE  (minimal re-declaration for reference)
 * ========================================================================= */
/*
 * The GDT was already set up by boot.asm.  We only need its layout to
 * understand segment selectors (0x08 = code, 0x10 = data).  No code needed.
 */

/* =========================================================================
 * 5.  INTERRUPT DESCRIPTOR TABLE  (IDT)
 * ========================================================================= */

/*
 * An IDT gate (interrupt descriptor) is 8 bytes:
 *   offset_lo  [15:0]  — lower 16 bits of ISR address
 *   selector   [31:16] — code segment selector (0x08)
 *   zero       [39:32] — always 0
 *   type_attr  [47:40] — gate type + DPL + present bit
 *   offset_hi  [63:48] — upper 16 bits of ISR address
 */
typedef struct __attribute__((packed)) {
    uint16_t offset_lo;   /* ISR address bits 15:0  */
    uint16_t selector;    /* Code segment selector  */
    uint8_t  zero;        /* Always 0               */
    uint8_t  type_attr;   /* Gate type / DPL / P    */
    uint16_t offset_hi;   /* ISR address bits 31:16 */
} idt_entry_t;

/*
 * IDTR — the 6-byte register value loaded with LIDT.
 * limit = sizeof(idt) - 1, base = address of idt array.
 */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

/* 256 IDT entries — one per possible interrupt/exception vector */
static idt_entry_t idt[256];
static idt_ptr_t   idt_ptr;

/*
 * idt_set_gate — fill one IDT entry.
 * handler : address of the ISR function
 * selector: code segment (0x08 from the GDT)
 * flags   : 0x8E = 32-bit interrupt gate, kernel privilege
 */
static void idt_set_gate(uint8_t num, uint32_t handler,
                         uint16_t selector, uint8_t flags) {
    idt[num].offset_lo = (uint16_t)(handler & 0xFFFF);
    idt[num].selector  = selector;
    idt[num].zero      = 0;
    idt[num].type_attr = flags;
    idt[num].offset_hi = (uint16_t)((handler >> 16) & 0xFFFF);
}

/* =========================================================================
 * 6.  ISR STUBS — generic C-level exception/interrupt handler
 * ========================================================================= */

/*
 * The CPU pushes these registers on the stack before calling our ISR.
 * We manually push the rest in the stub so we get a consistent frame.
 */
typedef struct __attribute__((packed)) {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pusha order  */
    uint32_t int_no, err_code;                         /* pushed by stub */
    uint32_t eip, cs, eflags, useresp, ss;            /* pushed by CPU  */
} registers_t;

/*
 * Forward-declare the assembly stubs.  They are defined later using
 * __attribute__((naked)) so we control every push/pop exactly.
 */
#define ISR_NOERR(n) void isr##n(void);
#define ISR_ERR(n)   void isr##n(void);
#define IRQ(n, v)    void irq##n(void);

/* CPU exceptions 0–31 */
ISR_NOERR(0)  ISR_NOERR(1)  ISR_NOERR(2)  ISR_NOERR(3)
ISR_NOERR(4)  ISR_NOERR(5)  ISR_NOERR(6)  ISR_NOERR(7)
ISR_ERR(8)    ISR_NOERR(9)  ISR_ERR(10)   ISR_ERR(11)
ISR_ERR(12)   ISR_ERR(13)   ISR_ERR(14)   ISR_NOERR(15)
ISR_NOERR(16) ISR_ERR(17)   ISR_NOERR(18) ISR_NOERR(19)
ISR_NOERR(20) ISR_NOERR(21) ISR_NOERR(22) ISR_NOERR(23)
ISR_NOERR(24) ISR_NOERR(25) ISR_NOERR(26) ISR_NOERR(27)
ISR_NOERR(28) ISR_NOERR(29) ISR_ERR(30)   ISR_NOERR(31)

/* Hardware IRQs 0–15 mapped to vectors 0x20-0x2F */
IRQ(0, 32)  IRQ(1, 33)  IRQ(2, 34)  IRQ(3, 35)
IRQ(4, 36)  IRQ(5, 37)  IRQ(6, 38)  IRQ(7, 39)
IRQ(8, 40)  IRQ(9, 41)  IRQ(10, 42) IRQ(11, 43)
IRQ(12, 44) IRQ(13, 45) IRQ(14, 46) IRQ(15, 47)

/*
 * isr_handler — called by all ISR stubs for CPU exceptions.
 * Prints a panic message and halts.
 */
static const char *exception_names[] = {
    "Division By Zero",         /* 0  */
    "Debug",                    /* 1  */
    "Non Maskable Interrupt",   /* 2  */
    "Breakpoint",               /* 3  */
    "Into Detected Overflow",   /* 4  */
    "Out of Bounds",            /* 5  */
    "Invalid Opcode",           /* 6  */
    "No Coprocessor",           /* 7  */
    "Double Fault",             /* 8  */
    "Coprocessor Segment Over", /* 9  */
    "Bad TSS",                  /* 10 */
    "Segment Not Present",      /* 11 */
    "Stack Fault",              /* 12 */
    "General Protection Fault", /* 13 */
    "Page Fault",               /* 14 */
    "Unknown Interrupt",        /* 15 */
    "FPU Fault",                /* 16 */
    "Alignment Check",          /* 17 */
    "Machine Check",            /* 18 */
    "SIMD Fault",               /* 19 */
};

void isr_handler(registers_t *regs) {
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_puts("\n\n  *** KERNEL PANIC ***\n");
    vga_puts("  Exception: ");
    if (regs->int_no < 20)
        vga_puts(exception_names[regs->int_no]);
    else
        vga_puts("Reserved");
    vga_puts("\n  System halted.\n");
    __asm__ volatile ("cli; hlt");
}

/* =========================================================================
 * 7.  PIC — PROGRAMMABLE INTERRUPT CONTROLLER  (8259A)
 * ========================================================================= */

/*
 * The legacy 8259A PIC fires IRQs 0-7 at INT 0x08-0x0F by default,
 * which clashes with CPU exception vectors.  We remap them to 0x20-0x2F.
 *
 * Master PIC: command 0x20, data 0x21
 * Slave  PIC: command 0xA0, data 0xA1
 */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20  /* End-Of-Interrupt command */

static void pic_remap(void) {
    /* Save current masks */
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    /* Start init sequence in cascade mode (ICW1) */
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();

    /* ICW2: new vector offsets */
    outb(PIC1_DATA, 0x20); io_wait(); /* IRQ0 → INT 32 (0x20) */
    outb(PIC2_DATA, 0x28); io_wait(); /* IRQ8 → INT 40 (0x28) */

    /* ICW3: master has slave on IRQ2; slave knows its cascade identity */
    outb(PIC1_DATA, 4); io_wait();  /* bit 2 = IRQ2 line        */
    outb(PIC2_DATA, 2); io_wait();  /* cascade identity = 2      */

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    /* Restore masks */
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

/* Send End-Of-Interrupt to the appropriate PIC(s) */
static void pic_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

/* =========================================================================
 * 8.  TIMER (IRQ 0)
 * ========================================================================= */

static volatile uint32_t timer_ticks = 0;

/*
 * irq_handler_timer — called every time the PIT fires.
 * The default PIT rate is ~18.2 Hz; we just count ticks here.
 */
void irq_handler_timer(void) {
    timer_ticks++;
    pic_eoi(0);
}

/* uptime_seconds — approximate seconds since boot (18 ticks ≈ 1 s) */
uint32_t uptime_seconds(void) {
    return timer_ticks / 18;
}

/* =========================================================================
 * 9.  KEYBOARD (IRQ 1) — US-QWERTY SCANCODE SET 1
 * ========================================================================= */

#define KB_DATA 0x60  /* PS/2 keyboard data port */

/* Scancode set 1 → ASCII (lowercase, unshifted).  0 = no printable char. */
static const char kb_map[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', /* 0x00–0x09 */
    '9', '0', '-', '=', '\b', '\t',                    /* 0x0A–0x0F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', /* 0x10–0x19 */
    '[', ']', '\n', 0,                                  /* 0x1A–0x1D */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', /* 0x1E–0x27 */
    '\'', '`', 0, '\\',                                 /* 0x28–0x2B */
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', /* 0x2C–0x35 */
    0, '*', 0, ' ',                                     /* 0x36–0x39 */
    /* remainder all 0 */
};

/* Shifted scancode → ASCII */
static const char kb_map_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, '*', 0, ' ',
};

/* Key state */
static int kb_shift = 0;     /* non-zero while shift is held */

/*
 * Ring-buffer (circular queue) for keyboard input.
 * The interrupt handler writes here; the shell reads from here.
 */
#define KB_BUF_SIZE 256
static volatile char     kb_buf[KB_BUF_SIZE];
static volatile uint32_t kb_head = 0;  /* write index */
static volatile uint32_t kb_tail = 0;  /* read  index */

static void kb_buf_push(char c) {
    uint32_t next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {          /* drop if full */
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}

/* kb_getchar — blocking read: spin until a character is available */
char kb_getchar(void) {
    while (kb_head == kb_tail)
        __asm__ volatile ("hlt"); /* sleep until next interrupt */
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}

/*
 * irq_handler_keyboard — called on every key press/release.
 * Scancode bit 7 = 1 means key released; bit 7 = 0 means key pressed.
 */
void irq_handler_keyboard(void) {
    uint8_t scancode = inb(KB_DATA);

    /* Detect shift press/release (scancodes 0x2A, 0x36 and their breaks) */
    if (scancode == 0x2A || scancode == 0x36) { kb_shift = 1; }
    else if (scancode == 0xAA || scancode == 0xB6) { kb_shift = 0; }
    else if (!(scancode & 0x80)) {
        /* Key-pressed event (not a key-release) */
        char c = kb_shift ? kb_map_shift[scancode] : kb_map[scancode];
        if (c) kb_buf_push(c);
    }

    pic_eoi(1);
}

/* =========================================================================
 * 10. IRQ DISPATCH — called by all IRQ stubs
 * ========================================================================= */

void irq_handler(registers_t *regs) {
    switch (regs->int_no - 32) {   /* subtract PIC offset to get IRQ number */
        case 0:  irq_handler_timer();    break;
        case 1:  irq_handler_keyboard(); break;
        /* IRQs 2–15 are unhandled; still must send EOI */
        default: pic_eoi((uint8_t)(regs->int_no - 32)); break;
    }
}

/* =========================================================================
 * 11. ISR/IRQ STUB IMPLEMENTATIONS  (naked functions = no function prologue)
 * ========================================================================= */

/*
 * Each stub must:
 *   1. Push a fake error code (if the CPU doesn't) to keep the frame uniform.
 *   2. Push the interrupt number.
 *   3. Save all general-purpose registers (pusha).
 *   4. Save the data-segment register and reload DS to kernel's data segment.
 *   5. Call the C handler.
 *   6. Restore everything in reverse and return with IRET.
 */

#define STUB_ISR_NOERR(n) \
__attribute__((naked)) void isr##n(void) { \
    __asm__ volatile ( \
        "cli\n" \
        "push $0\n"        /* fake error code */ \
        "push $" #n "\n"   /* interrupt number */ \
        "pusha\n" \
        "mov %%ds, %%ax\n" \
        "push %%eax\n" \
        "mov $0x10, %%ax\n" \
        "mov %%ax, %%ds\n" \
        "mov %%ax, %%es\n" \
        "mov %%ax, %%fs\n" \
        "mov %%ax, %%gs\n" \
        "mov %%esp, %%eax\n" \
        "push %%eax\n" \
        "call isr_handler\n" \
        "pop %%eax\n" \
        "pop %%eax\n" \
        "mov %%ax, %%ds\n" \
        "mov %%ax, %%es\n" \
        "mov %%ax, %%fs\n" \
        "mov %%ax, %%gs\n" \
        "popa\n" \
        "add $8, %%esp\n" /* pop int_no + err_code */ \
        "iret\n" \
        : : : "memory" \
    ); \
}

#define STUB_ISR_ERR(n) \
__attribute__((naked)) void isr##n(void) { \
    __asm__ volatile ( \
        "cli\n" \
        /* CPU already pushed error code */ \
        "push $" #n "\n" \
        "pusha\n" \
        "mov %%ds, %%ax\n" \
        "push %%eax\n" \
        "mov $0x10, %%ax\n" \
        "mov %%ax, %%ds\n" \
        "mov %%ax, %%es\n" \
        "mov %%ax, %%fs\n" \
        "mov %%ax, %%gs\n" \
        "mov %%esp, %%eax\n" \
        "push %%eax\n" \
        "call isr_handler\n" \
        "pop %%eax\n" \
        "pop %%eax\n" \
        "mov %%ax, %%ds\n" \
        "mov %%ax, %%es\n" \
        "mov %%ax, %%fs\n" \
        "mov %%ax, %%gs\n" \
        "popa\n" \
        "add $8, %%esp\n" \
        "iret\n" \
        : : : "memory" \
    ); \
}

#define STUB_IRQ(n, vec) \
__attribute__((naked)) void irq##n(void) { \
    __asm__ volatile ( \
        "cli\n" \
        "push $0\n" \
        "push $" #vec "\n" \
        "pusha\n" \
        "mov %%ds, %%ax\n" \
        "push %%eax\n" \
        "mov $0x10, %%ax\n" \
        "mov %%ax, %%ds\n" \
        "mov %%ax, %%es\n" \
        "mov %%ax, %%fs\n" \
        "mov %%ax, %%gs\n" \
        "mov %%esp, %%eax\n" \
        "push %%eax\n" \
        "call irq_handler\n" \
        "pop %%eax\n" \
        "pop %%eax\n" \
        "mov %%ax, %%ds\n" \
        "mov %%ax, %%es\n" \
        "mov %%ax, %%fs\n" \
        "mov %%ax, %%gs\n" \
        "popa\n" \
        "add $8, %%esp\n" \
        "iret\n" \
        : : : "memory" \
    ); \
}

/* Instantiate all stubs */
STUB_ISR_NOERR(0)  STUB_ISR_NOERR(1)  STUB_ISR_NOERR(2)  STUB_ISR_NOERR(3)
STUB_ISR_NOERR(4)  STUB_ISR_NOERR(5)  STUB_ISR_NOERR(6)  STUB_ISR_NOERR(7)
STUB_ISR_ERR(8)    STUB_ISR_NOERR(9)  STUB_ISR_ERR(10)   STUB_ISR_ERR(11)
STUB_ISR_ERR(12)   STUB_ISR_ERR(13)   STUB_ISR_ERR(14)   STUB_ISR_NOERR(15)
STUB_ISR_NOERR(16) STUB_ISR_ERR(17)   STUB_ISR_NOERR(18) STUB_ISR_NOERR(19)
STUB_ISR_NOERR(20) STUB_ISR_NOERR(21) STUB_ISR_NOERR(22) STUB_ISR_NOERR(23)
STUB_ISR_NOERR(24) STUB_ISR_NOERR(25) STUB_ISR_NOERR(26) STUB_ISR_NOERR(27)
STUB_ISR_NOERR(28) STUB_ISR_NOERR(29) STUB_ISR_ERR(30)   STUB_ISR_NOERR(31)

STUB_IRQ(0,  32)  STUB_IRQ(1,  33)  STUB_IRQ(2,  34)  STUB_IRQ(3,  35)
STUB_IRQ(4,  36)  STUB_IRQ(5,  37)  STUB_IRQ(6,  38)  STUB_IRQ(7,  39)
STUB_IRQ(8,  40)  STUB_IRQ(9,  41)  STUB_IRQ(10, 42)  STUB_IRQ(11, 43)
STUB_IRQ(12, 44)  STUB_IRQ(13, 45)  STUB_IRQ(14, 46)  STUB_IRQ(15, 47)

/* =========================================================================
 * 12. IDT INSTALLATION
 * ========================================================================= */

static void idt_install(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    k_memset(&idt, 0, sizeof(idt));

    /* CPU exceptions 0–19 */
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    /* Hardware IRQs 0–15 → vectors 32–47 */
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    /* Load the IDT register */
    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
}

/* =========================================================================
 * 13. BOOT SPLASH SCREEN
 * ========================================================================= */

/*
 * draw_banner — draws a colourful ASCII-art banner directly to the VGA buffer.
 * We bypass vga_puts() so we can set per-character colours.
 */
static void draw_banner(void) {
    /* Full-screen fill: dark blue background */
    uint8_t bg = vga_attr(VGA_LIGHT_GREY, VGA_BLUE);
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_buf[r * VGA_COLS + c] = vga_cell(' ', bg);

    /* -- Row 1: top border -- */
    uint8_t border_attr = vga_attr(VGA_YELLOW, VGA_BLUE);
    for (int c = 0; c < VGA_COLS; c++)
        vga_putchar_at('=', border_attr, 1, c);

    /* -- Rows 3-7: logo text -- */
    const char *logo[] = {
        "  888b     d888          .d88888b.   .d8888b.  ",
        "  8888b   d8888         d88P\" \"Y88b d88P  Y88b ",
        "  88888b.d88888         888     888 Y88b.      ",
        "  888Y88888P888 888  888888     888  \"Y888b.   ",
        "  888 Y888P 888 888  888888     888     \"Y88b. ",
        "  888  Y8P  888 888  888888     888       \"888 ",
        "  888   \"   888 Y88b 888Y88b. .d88P Y88b  d88P ",
        "  888       888  \"Y88888 \"Y88888P\"   \"Y8888P\"  ",
    };
    uint8_t logo_attr = vga_attr(VGA_LIGHT_CYAN, VGA_BLUE);
    for (int i = 0; i < 8; i++) {
        const char *line = logo[i];
        int col = 15;
        for (int j = 0; line[j]; j++, col++)
            if (col < VGA_COLS)
                vga_putchar_at(line[j], logo_attr, 3 + i, col);
    }

    /* -- Row 12: tagline -- */
    const char *tag = "[ A hobby OS kernel — built from scratch ]";
    uint8_t tag_attr = vga_attr(VGA_YELLOW, VGA_BLUE);
    int tag_col = (VGA_COLS - (int)k_strlen(tag)) / 2;
    for (int i = 0; tag[i]; i++)
        vga_putchar_at(tag[i], tag_attr, 12, tag_col + i);

    /* -- Row 14: version / arch line -- */
    const char *ver = "MyOS v1.0  |  x86 32-bit Protected Mode  |  GCC Toolchain";
    uint8_t ver_attr = vga_attr(VGA_WHITE, VGA_BLUE);
    int ver_col = (VGA_COLS - (int)k_strlen(ver)) / 2;
    for (int i = 0; ver[i]; i++)
        vga_putchar_at(ver[i], ver_attr, 14, ver_col + i);

    /* -- Row 16: feature flags -- */
    const char *feat = "  IDT  |  PIC  |  Timer IRQ  |  PS/2 Keyboard  |  Shell  ";
    uint8_t feat_attr = vga_attr(VGA_LIGHT_GREEN, VGA_BLUE);
    int feat_col = (VGA_COLS - (int)k_strlen(feat)) / 2;
    for (int i = 0; feat[i]; i++)
        vga_putchar_at(feat[i], feat_attr, 16, feat_col + i);

    /* -- Row 18: bottom border -- */
    for (int c = 0; c < VGA_COLS; c++)
        vga_putchar_at('=', border_attr, 18, c);

    /* -- Row 20: prompt -- */
    const char *press = "Press any key to continue...";
    uint8_t press_attr = vga_attr(VGA_LIGHT_MAGENTA, VGA_BLUE);
    int press_col = (VGA_COLS - (int)k_strlen(press)) / 2;
    for (int i = 0; press[i]; i++)
        vga_putchar_at(press[i], press_attr, 20, press_col + i);

    /* -- Decorative corners and side bars -- */
    uint8_t corner_attr = vga_attr(VGA_LIGHT_RED, VGA_BLUE);
    vga_putchar_at('*', corner_attr, 1,  0);
    vga_putchar_at('*', corner_attr, 1,  VGA_COLS - 1);
    vga_putchar_at('*', corner_attr, 18, 0);
    vga_putchar_at('*', corner_attr, 18, VGA_COLS - 1);
}

/* busy-loop delay so the banner stays visible even without a keypress read */
static void delay_ticks(uint32_t ticks) {
    uint32_t start = timer_ticks;
    while (timer_ticks - start < ticks)
        __asm__ volatile ("hlt");
}

/* =========================================================================
 * 14. SHELL
 * ========================================================================= */

#define CMD_BUF_SIZE 128

static char cmd_buf[CMD_BUF_SIZE];
static int  cmd_len = 0;

/* Print the prompt */
static void shell_prompt(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("myos");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("@kernel");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts(":~$ ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

/* ---- Built-in command implementations ---- */

static void cmd_help(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\nMyOS Built-in Commands\n");
    vga_puts("----------------------\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  help    ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Show this help message\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  clear   ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Clear the screen\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  about   ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- About this OS\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  uptime  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Seconds since boot\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  echo    ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Print text  (e.g. echo hello world)\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  color   ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Demo all 16 VGA colours\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  reboot  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Restart the machine\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  halt    ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Halt the CPU\n\n");
}

static void cmd_about(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n  MyOS — A Minimal x86 Kernel\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("  Written in C and x86 assembly.\n");
    vga_puts("  Architecture : IA-32 (32-bit protected mode)\n");
    vga_puts("  Boot         : Stage-1 BIOS bootloader (MBR)\n");
    vga_puts("  Video        : VGA text mode 80x25\n");
    vga_puts("  Interrupts   : 8259A PIC, IDT with 256 gates\n");
    vga_puts("  Timer        : PIT IRQ0 (18.2 Hz)\n");
    vga_puts("  Keyboard     : PS/2 IRQ1, US-QWERTY\n\n");
}

static void cmd_uptime(void) {
    char buf[16];
    uint32_t secs = uptime_seconds();
    k_itoa(secs, buf);
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("\n  Uptime: ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts(buf);
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts(" seconds\n\n");
}

static void cmd_color(void) {
    vga_puts("\n  VGA colour palette:\n  ");
    const char *names[] = {
        "BLK","BLU","GRN","CYN","RED","MAG","BRN","LGY",
        "DGY","LBL","LGN","LCY","LRD","LMG","YEL","WHT"
    };
    for (int i = 0; i < 16; i++) {
        vga_set_color((vga_color_t)i, VGA_BLACK);
        vga_puts(names[i]);
        vga_putchar(' ');
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("\n\n");
}

/*
 * cmd_echo — print everything after "echo " on the command line.
 * We walk past the first token (the command word itself).
 */
static void cmd_echo(const char *line) {
    /* skip "echo" */
    const char *p = line + 4;
    /* skip any spaces */
    while (*p == ' ') p++;
    vga_putchar('\n');
    vga_puts(p);
    vga_putchar('\n');
    vga_putchar('\n');
}

/*
 * cmd_reboot — triple-fault reboot trick:
 * load an IDT with a zero limit, then trigger a software interrupt.
 * The CPU will triple-fault and reset.
 */
static void cmd_reboot(void) {
    vga_puts("\n  Rebooting...\n");
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);          /* pulse the reset line via PS/2 controller */
    __asm__ volatile ("hlt");
}

/*
 * shell_run — read a line from the keyboard, dispatch to a command handler.
 */
static void shell_run(void) {
    while (1) {
        shell_prompt();
        cmd_len = 0;

        /* Read characters until Enter */
        while (1) {
            char c = kb_getchar();
            if (c == '\n') {
                vga_putchar('\n');
                break;
            } else if (c == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    vga_putchar('\b');
                }
            } else if (cmd_len < CMD_BUF_SIZE - 1) {
                cmd_buf[cmd_len++] = c;
                vga_putchar(c);
            }
        }
        cmd_buf[cmd_len] = '\0';

        /* Dispatch */
        if (cmd_len == 0) {
            /* empty line — just re-prompt */
        } else if (k_strcmp(cmd_buf, "help") == 0) {
            cmd_help();
        } else if (k_strcmp(cmd_buf, "clear") == 0) {
            vga_set_color(VGA_WHITE, VGA_BLACK);
            vga_clear();
        } else if (k_strcmp(cmd_buf, "about") == 0) {
            cmd_about();
        } else if (k_strcmp(cmd_buf, "uptime") == 0) {
            cmd_uptime();
        } else if (k_strcmp(cmd_buf, "color") == 0) {
            cmd_color();
        } else if (k_strcmp(cmd_buf, "reboot") == 0) {
            cmd_reboot();
        } else if (k_strcmp(cmd_buf, "halt") == 0) {
            vga_puts("\n  CPU halted. Power off the machine.\n");
            __asm__ volatile ("cli; hlt");
        } else if (k_strncmp(cmd_buf, "echo", 4) == 0
                   && (cmd_buf[4] == ' ' || cmd_buf[4] == '\0')) {
            cmd_echo(cmd_buf);
        } else {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_puts("\n  Unknown command: '");
            vga_puts(cmd_buf);
            vga_puts("'  (type 'help' for a list)\n\n");
            vga_set_color(VGA_WHITE, VGA_BLACK);
        }
    }
}

/* =========================================================================
 * 15. KERNEL ENTRY POINT
 * ========================================================================= */

__attribute__((section(".text.boot"))) void kernel_main(void) {
    /*
     * 1. Remap the PIC so hardware IRQs don't overlap CPU exception vectors.
     *    Must happen BEFORE installing the IDT.
     */
    pic_remap();

    /*
     * 2. Install all 256 IDT gates and load the IDTR register.
     */
    idt_install();

    /*
     * 3. Enable hardware interrupts.  From this point the timer and keyboard
     *    ISRs will fire asynchronously.
     */
    __asm__ volatile ("sti");

    /*
     * 4. Draw the animated boot banner.
     *    Timer interrupts are now ticking so delay_ticks() works.
     */
    draw_banner();
    delay_ticks(18 * 2);        /* show splash for ~2 seconds */
    kb_getchar();               /* also wait for a key press */

    /*
     * 5. Switch to the shell screen.
     */
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("MyOS kernel booted successfully.\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("Type 'help' for a list of commands.\n\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /*
     * 6. Enter the interactive shell loop — never returns.
     */
    shell_run();

    /* Should never reach here */
    __asm__ volatile ("cli; hlt");
}
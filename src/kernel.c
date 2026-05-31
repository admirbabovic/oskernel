// COMPILER BUILT-INS & TYPE DEFINITIONS

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;

typedef uint32_t size_t;

#define NULL ((void*)0)

// I/O PORT HELPERS

static inline void outb(uint16_t port, uint8_t value) { // write one byte to an x86 I/O port
    __asm__ volatile ("outb %1, %0" : : "dN"(port), "a"(value));
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) { // read one byte from an x86 I/O port
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

static inline void io_wait(void) { // tiny delay by writing to an unused port
    outb(0x80, 0);
}


// VGA TEXT-MODE DRIVER

#define VGA_ADDRESS  0xB8000 // physical place in memory where frame-buffer lives
#define VGA_COLS     80
#define VGA_ROWS     25

// Standard VGA colour indices
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

// Combines foreground and background into a single attribute byte
static inline uint8_t vga_attr(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

// Packs character and attribute into a 16-bit VGA cell
static inline uint16_t vga_cell(char c, uint8_t attr) {
    return (uint16_t)((uint16_t)attr << 8 | (uint8_t)c);
}

static uint16_t *vga_buf    = (uint16_t *)VGA_ADDRESS;
static int       term_col   = 0;   // current cursor column
static int       term_row   = 0;   // current cursor row
static uint8_t   term_attr  = 0;   // current attribute byte

void vga_set_color(vga_color_t fg, vga_color_t bg) { // update the active foreground/background pair
    term_attr = vga_attr(fg, bg);
}

void vga_clear(void) { // fill every cell with spaces using the current attribute
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_buf[r * VGA_COLS + c] = vga_cell(' ', term_attr);
    term_col = 0;
    term_row = 0;
}

void vga_update_cursor(void) { // move the hardware text cursor
    uint16_t pos = (uint16_t)(term_row * VGA_COLS + term_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll(void) { // shift every row up by one line, blank the last row
    for (int r = 1; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_buf[(r - 1) * VGA_COLS + c] = vga_buf[r * VGA_COLS + c];

    for (int c = 0; c < VGA_COLS; c++)
        vga_buf[(VGA_ROWS - 1) * VGA_COLS + c] = vga_cell(' ', term_attr);

    term_row = VGA_ROWS - 1;
}

void vga_putchar(char c) { // place a single character at the current cursor position
    if (c == '\n') {
        term_col = 0;
        term_row++;
    } else if (c == '\r') {
        term_col = 0;
    } else if (c == '\b') {
        if (term_col > 0) { // Backspace erases the character to the left
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

void vga_puts(const char *s) { // write a NUL-terminated string
    while (*s)
        vga_putchar(*s++);
}

void vga_putchar_at(char c, uint8_t attr, int row, int col) { // writes a character directly at row/col without moving cursor
    vga_buf[row * VGA_COLS + col] = vga_cell(c, attr);
}


// MINIMAL STRING / NUMBER UTILITIES

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

void k_itoa(uint32_t val, char *buf) { // convert unsigned 32-bit integer to decimal string in buf
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int i = 0;
    while (val) { tmp[i++] = '0' + val % 10; val /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

void *k_memset(void *dst, int c, size_t n) { // fill n bytes of dst with value c
    unsigned char *p = (unsigned char *)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

// INTERRUPT DESCRIPTOR TABLE  (IDT)

typedef struct __attribute__((packed)) {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_hi;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

static idt_entry_t idt[256]; // one IDT entry per possible interrupt/exception vector
static idt_ptr_t   idt_ptr;

// fill one IDT entry
static void idt_set_gate(uint8_t num, uint32_t handler,
                         uint16_t selector, uint8_t flags) {
    idt[num].offset_lo = (uint16_t)(handler & 0xFFFF); // lower 16 bits of ISR address (0-15)
    idt[num].selector  = selector; // code segment selector (0x08) (16-31)
    idt[num].zero      = 0; // always 0 (32-39)
    idt[num].type_attr = flags; // gate type / DPL / present bit (40-47)
    idt[num].offset_hi = (uint16_t)((handler >> 16) & 0xFFFF); // upper 16 bits of ISR address (48-63)
}

// ISR STUBS - C-level exception/interrupt handler

typedef struct __attribute__((packed)) {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;    // pusha order
    uint32_t int_no, err_code;  // pushed by stub
    uint32_t eip, cs, eflags, useresp, ss;  // pushed by CPU
} registers_t;

#define ISR_NOERR(n) void isr##n(void);
#define ISR_ERR(n)   void isr##n(void);
#define IRQ(n, v)    void irq##n(void);

// CPU exceptions 0–31
ISR_NOERR(0)  ISR_NOERR(1)  ISR_NOERR(2)  ISR_NOERR(3)
ISR_NOERR(4)  ISR_NOERR(5)  ISR_NOERR(6)  ISR_NOERR(7)
ISR_ERR(8)    ISR_NOERR(9)  ISR_ERR(10)   ISR_ERR(11)
ISR_ERR(12)   ISR_ERR(13)   ISR_ERR(14)   ISR_NOERR(15)
ISR_NOERR(16) ISR_ERR(17)   ISR_NOERR(18) ISR_NOERR(19)
ISR_NOERR(20) ISR_NOERR(21) ISR_NOERR(22) ISR_NOERR(23)
ISR_NOERR(24) ISR_NOERR(25) ISR_NOERR(26) ISR_NOERR(27)
ISR_NOERR(28) ISR_NOERR(29) ISR_ERR(30)   ISR_NOERR(31)

// Hardware IRQs 0–15 mapped to vectors 0x20-0x2F
IRQ(0, 32)  IRQ(1, 33)  IRQ(2, 34)  IRQ(3, 35)
IRQ(4, 36)  IRQ(5, 37)  IRQ(6, 38)  IRQ(7, 39)
IRQ(8, 40)  IRQ(9, 41)  IRQ(10, 42) IRQ(11, 43)
IRQ(12, 44) IRQ(13, 45) IRQ(14, 46) IRQ(15, 47)

static const char *exception_names[] = {
    "Division By Zero",         // 0
    "Debug",                    // 1
    "Non Maskable Interrupt",   // 2
    "Breakpoint",               // 3
    "Into Detected Overflow",   // 4
    "Out of Bounds",            // 5
    "Invalid Opcode",           // 6
    "No Coprocessor",           // 7
    "Double Fault",             // 8
    "Coprocessor Segment Over", // 9
    "Bad TSS",                  // 10
    "Segment Not Present",      // 11
    "Stack Fault",              // 12
    "General Protection Fault", // 13
    "Page Fault",               // 14
    "Unknown Interrupt",        // 15
    "FPU Fault",                // 16
    "Alignment Check",          // 17
    "Machine Check",            // 18
    "SIMD Fault",               // 19
};

void isr_handler(registers_t *regs) { // called by all ISR stubs for CPU exceptions - prints a panic message and halts
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

// PIC - PROGRAMMABLE INTERRUPT CONTROLLER

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20  // End-Of-Interrupt command

static void pic_remap(void) {
    // Save current masks
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    // Start init sequence in cascade mode
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();

    // ICW2: new vector offsets
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();

    // ICW3
    outb(PIC1_DATA, 4); io_wait();
    outb(PIC2_DATA, 2); io_wait();

    // ICW4: 8086 mode
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    // Restore masks
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

// Send End-Of-Interrupt to the appropriate PIC
static void pic_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

// TIMER (IRQ 0) - The default PIT rate is ~18.2 Hz

static volatile uint32_t timer_ticks = 0;

void irq_handler_timer(void) { // called every time the PIT fires
    timer_ticks++;
    pic_eoi(0);
}

// uptime_seconds - approximate seconds since boot (18 ticks ~1s)
uint32_t uptime_seconds(void) {
    return timer_ticks / 18;
}

// KEYBOARD (IRQ 1) - US-QWERTY SCANCODE SET 1

#define KB_DATA 0x60  // PS/2 keyboard data port

// Scancode set 1 = ASCII (lowercase, unshifted) and  0 = no printable char.
static const char kb_map[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', /* 0x00–0x09 */
    '9', '0', '-', '=', '\b', '\t',                    /* 0x0A–0x0F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', /* 0x10–0x19 */
    '[', ']', '\n', 0,                                  /* 0x1A–0x1D */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', /* 0x1E–0x27 */
    '\'', '`', 0, '\\',                                 /* 0x28–0x2B */
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', /* 0x2C–0x35 */
    0, '*', 0, ' ',                                     /* 0x36–0x39 */
    0, 0, 0, 0, 0, 0, 0, 0,                           /* 0x3A-0x41 */
    0, 0, 0, 0, 0, 0,                                 /* 0x42-0x47 */
     '\x11', 0, 0, 0, 0, 0, 0, 0, '\x12',              /* 0x48-0x50 (Up=0x48, Down=0x50) */
};

// Shifted scancode - ASCII
static const char kb_map_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|', '\\',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    '\x11', 0, 0, 0, 0, 0, 0, 0, '\x12',
};

// Key state
static int kb_shift = 0;

// Ring-buffer (circular queue) for keyboard input.
#define KB_BUF_SIZE 256
static volatile char     kb_buf[KB_BUF_SIZE];
static volatile uint32_t kb_head = 0;  // write index
static volatile uint32_t kb_tail = 0;  // read  index

static void kb_buf_push(char c) {
    uint32_t next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}

char kb_getchar(void) { // blocking read - spin until a character is available
    while (kb_head == kb_tail)
        __asm__ volatile ("hlt");
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}

void irq_handler_keyboard(void) { // called on every key press/release
    uint8_t scancode = inb(KB_DATA);

    // Detect shift press/release
    if (scancode == 0x2A || scancode == 0x36) { kb_shift = 1; }
    else if (scancode == 0xAA || scancode == 0xB6) { kb_shift = 0; }
    else if (!(scancode & 0x80)) {
        char c = kb_shift ? kb_map_shift[scancode] : kb_map[scancode];
        if (c) kb_buf_push(c);
    }

    pic_eoi(1);
}

// IRQ DISPATCH - called by all IRQ stubs

void irq_handler(registers_t *regs) {
    switch (regs->int_no - 32) {   // subtract PIC offset to get IRQ number
        case 0:  irq_handler_timer();    break;
        case 1:  irq_handler_keyboard(); break;

        default: pic_eoi((uint8_t)(regs->int_no - 32)); break;
    }
}

// ISR/IRQ STUB IMPLEMENTATIONS

#define STUB_ISR_NOERR(n) \
__attribute__((naked)) void isr##n(void) { \
    __asm__ volatile ( \
        "cli\n" \
        "push $0\n" \
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

#define STUB_ISR_ERR(n) \
__attribute__((naked)) void isr##n(void) { \
    __asm__ volatile ( \
        "cli\n" \
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

// Instantiate all stubs
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

// IDT INSTALLATION

static void idt_install(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    k_memset(&idt, 0, sizeof(idt));

    // CPU exceptions 0–19
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

    // Hardware IRQs 0–15 - vectors 32–47
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

    // Load the IDT register
    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
}

// BOOT SPLASH SCREEN

int current_theme = 0;

static void draw_banner(void) {

    uint8_t c_bg     = current_theme ? VGA_BLACK      : VGA_WHITE;
    uint8_t c_fill   = current_theme ? VGA_BLACK      : VGA_LIGHT_GREY;
    uint8_t c_border = current_theme ? VGA_WHITE      : VGA_DARK_GREY;
    uint8_t c_logo   = current_theme ? VGA_LIGHT_BLUE : VGA_BLUE;
    uint8_t c_text   = current_theme ? VGA_WHITE      : VGA_DARK_GREY;
    uint8_t c_dev    = current_theme ? VGA_LIGHT_RED  : VGA_RED;
    uint8_t c_prompt = current_theme ? VGA_LIGHT_GREY : VGA_MAGENTA;

    uint8_t bg = vga_attr(c_fill, c_bg); // Full-screen fill
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_buf[r * VGA_COLS + c] = vga_cell(' ', bg);

    // Row 1: top border
    uint8_t border_attr = vga_attr(c_border, c_bg);
    for (int c = 0; c < VGA_COLS; c++)
        vga_putchar_at('=', border_attr, 1, c);

    // Rows 3-7: logo text
    const char *logo[] = {
        "    /$$$$$$   /$$$$$$   ",
        "   /$$__  $$ /$$__  $$  ",
        "  | $$  | $$| $$  |__/  ",
        "  | $$  | $$|  $$$$$$   ",
        "  | $$  | $$ |____  $$  ",
        "  | $$  | $$ /$$  | $$  ",
        "  |  $$$$$$/|  $$$$$$/  ",
        "   \\______/  \\______/ ",
    };

    uint8_t logo_attr = vga_attr(c_logo, c_bg);
    for (int i = 0; i < 8; i++) {
        const char *line = logo[i];
        int col = 27;
        for (int j = 0; line[j]; j++, col++)
            if (col < VGA_COLS)
                vga_putchar_at(line[j], logo_attr, 3 + i, col);
    }

    // Row 12: OS project tagline
    const char *tag = "[ OS kernel built built in C for CS304 course project ]";
    uint8_t tag_attr = vga_attr(c_text, c_bg);
    int tag_col = (VGA_COLS - (int)k_strlen(tag)) / 2;
    for (int i = 0; tag[i]; i++)
        vga_putchar_at(tag[i], tag_attr, 12, tag_col + i);

    // Row 14: Current version and build features
    const char *ver = "OS v1.0.5  |  x86 32-bit Protected Mode  |  GCC Toolchain";
    uint8_t ver_attr = vga_attr(c_text, c_bg);
    int ver_col = (VGA_COLS - (int)k_strlen(ver)) / 2;
    for (int i = 0; ver[i]; i++)
        vga_putchar_at(ver[i], ver_attr, 14, ver_col + i);

    // Rows 16-17: Project team members
    const char *dev_frstln = "                            Dev team:                               ";
    uint8_t dev_frstln_attr = vga_attr(c_dev, c_bg);
    int dev_frstln_col = (VGA_COLS - (int)k_strlen(dev_frstln)) / 2;
    for (int i = 0; dev_frstln[i]; i++)
        vga_putchar_at(dev_frstln[i], dev_frstln_attr, 16, dev_frstln_col + i);

    const char *dev_scndln = "Amar Kucevic, Vedad Halilovic, Faris Skula, Admir Babovic, Ali Murtic";
    uint8_t dev_scndln_attr = vga_attr(c_dev, c_bg);
    int dev_scndln_col = (VGA_COLS - (int)k_strlen(dev_scndln)) / 2;
    for (int i = 0; dev_scndln[i]; i++)
        vga_putchar_at(dev_scndln[i], dev_scndln_attr, 17, dev_scndln_col + i);

    // Row 19: Bottom border
    for (int c = 0; c < VGA_COLS; c++)
        vga_putchar_at('=', border_attr, 19, c);

    // Row 21: Theme switcher text
    char *toggle = "Press SPACE to toggle dark theme";
    if (current_theme == 1) {
        toggle = "Press SPACE to toggle light theme";
    }
    uint8_t toggle_attr = vga_attr(c_prompt, c_bg);
    int toggle_col = (VGA_COLS - (int)k_strlen(toggle)) / 2;
    for (int i = 0; toggle[i]; i++)
        vga_putchar_at(toggle[i], toggle_attr, 21, toggle_col + i);

    // Row 22: Proceed to shell text
    const char *press = "Press any other key to continue to shell...";
    uint8_t press_attr = vga_attr(c_prompt, c_bg);
    int press_col = (VGA_COLS - (int)k_strlen(press)) / 2;
    for (int i = 0; press[i]; i++)
        vga_putchar_at(press[i], press_attr, 22, press_col + i);
}

static void delay_ticks(uint32_t ticks) {
    uint32_t start = timer_ticks;
    while (timer_ticks - start < ticks)
        __asm__ volatile ("hlt");
}

static void show_loading_animation(void) {
    int bar_width = 50; // Width of the loading bar in characters
    int row = 14;       // Place in the middle of the screen
    int start_col = (VGA_COLS - bar_width - 2) / 2;

    uint8_t c_bg      = current_theme ? VGA_BLACK      : VGA_WHITE;
    uint8_t c_bracket = current_theme ? VGA_WHITE      : VGA_BLACK;
    uint8_t c_fill    = current_theme ? VGA_RED        : VGA_BLUE;
    uint8_t c_empty   = current_theme ? VGA_LIGHT_GREY : VGA_DARK_GREY;
    uint8_t c_text    = current_theme ? VGA_WHITE      : VGA_BLACK;

    uint8_t attr_bracket = vga_attr(c_bracket, c_bg);
    uint8_t attr_fill    = vga_attr(c_fill, c_bg);
    uint8_t attr_empty   = vga_attr(c_empty, c_bg);
    uint8_t attr_text    = vga_attr(c_text, c_bg);

    uint8_t bg_fill = vga_attr(c_bg, c_bg);
    for (int r = 0; r < VGA_ROWS; r++) {
        for (int c = 0; c < VGA_COLS; c++) {
            vga_buf[r * VGA_COLS + c] = vga_cell(' ', bg_fill);
        }
    }

    char *msg = "Initializing OS...";
    int msg_col = (VGA_COLS - (int)k_strlen(msg)) / 2;
    for (int i = 0; msg[i]; i++) {
        vga_putchar_at(msg[i], attr_text, row - 2, msg_col + i);
    }

    // Draw the initial empty loading bar
    vga_putchar_at('[', attr_bracket, row, start_col);
    for (int i = 0; i < bar_width; i++) {
        // '-' character for empty space
        vga_putchar_at('-', attr_empty, row, start_col + 1 + i);
    }
    vga_putchar_at(']', attr_bracket, row, start_col + 1 + bar_width);

    // Animate the bar filling up
    for (int i = 0; i < bar_width; i++) {
        vga_putchar_at('\xDB', attr_fill, row, start_col + 1 + i);

        // Calculate progress percentage to determine speed
        int percentage = (i * 100) / bar_width;

        if (percentage < 50) {
            delay_ticks(1);   // 1 tick * 55ms 
        } else if (percentage < 70) {
            msg = "Starting terminal session...";
            msg_col = (VGA_COLS - (int)k_strlen(msg)) / 2;
            for (int i = 0; msg[i]; i++) {
                vga_putchar_at(msg[i], attr_text, row - 2, msg_col + i);
            }
            delay_ticks(3);   // 3 ticks * 55ms
        } else if (percentage < 90) {
            delay_ticks(4);  // 4 ticks * 55ms
        } else {
            msg = "      Almost there...       ";
            msg_col = (VGA_COLS - (int)k_strlen(msg)) / 2;
            for (int i = 0; msg[i]; i++) {
                vga_putchar_at(msg[i], attr_text, row - 2, msg_col + i);
            }
            delay_ticks(9);  // 9 ticks * 55ms
        }
    }

    msg = "         Welcome...         ";
    int shell_msg_col = (VGA_COLS - (int)k_strlen(msg)) / 2;
    for (int i = 0; msg[i]; i++) {
        vga_putchar_at(msg[i], attr_text, row - 2, shell_msg_col + i);
    }
    delay_ticks(18 * 1);
}

static void show_boot_menu(void) {
    draw_banner();

    while (1) {
        uint8_t key = kb_getchar(); 

        if (key == ' ') {
            current_theme = !current_theme; 
            draw_banner(); 
        } 
        else if (key != 0) {
            break; 
        }
    }
}

// SHELL

#define CMD_BUF_SIZE 128
#define HISTORY_MAX 20

static char cmd_buf[CMD_BUF_SIZE];
static int  cmd_len = 0;

// History buffer
static char history[HISTORY_MAX][CMD_BUF_SIZE];
static int history_count = 0;
static int history_idx = 0;

// Print the prompt
static void shell_prompt(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("[root");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("@kernel ");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("~]$ ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

// Built-in command implementations
static void cmd_help(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\nBuilt-in Commands\n");
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
    vga_puts("  colors  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Demo all 16 VGA colours\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  reboot  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Restart the machine\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  exit    ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Shut down the machine\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  halt    ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("- Halt the CPU\n\n");
}

static void cmd_about(void) {
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("  Kernel written in C and x86 assembly.\n");
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
    vga_set_color(VGA_CYAN, VGA_BLACK);
    vga_puts(buf);
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts(" seconds\n\n");
}

static void cmd_colors(void) {
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

static void cmd_echo(const char *line) { // print everything after "echo " on the command line
    // skip "echo"
    const char *p = line + 4;
    // skip any spaces
    while (*p == ' ') p++;
    vga_putchar('\n');
    vga_puts(p);
    vga_putchar('\n');
    vga_putchar('\n');
}

static void cmd_reboot(void) { // triple-fault reboot
    // load an IDT with a zero limit, then trigger a software interrupt, cpu triple-faults and resets
    vga_puts("\n  Rebooting...\n");
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    __asm__ volatile ("hlt");
}

static void cmd_exit(void) { // shutdown QEMU emulator
    vga_puts("\n  Shutting down...\n");

    // QEMU ACPI shutdown
    outw(0x604, 0x2000);

    // Fallback 1 - try the older QEMU debug exit port
    outw(0xB004, 0x2000);

    // Fallback 2 - triple fault, load a null IDT and trigger an interrupt
    __asm__ volatile (
        "lidt (0)\n"
        "int $3\n"
    );

    __asm__ volatile ("hlt");
}

static void shell_run(void) { // read a line from the keyboard, dispatch to a command handler
    while (1) {
        shell_prompt();
        cmd_len = 0;
        history_idx = history_count;

        // Read characters until 'Enter'
        while (1) {
            char c = kb_getchar();

            if (c == '\n') {
                vga_putchar('\n');

                // null-terminating a command before saving in history
                cmd_buf[cmd_len] = '\0';

                if (cmd_len > 0) {
                    k_strcpy(history[history_count % HISTORY_MAX], cmd_buf);
                    history_count++;
                }
                break;
            } else if (c == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    vga_putchar('\b');
                }
            } else if (c == '\t') {
                // Tab command completion
                if (cmd_len > 0) {
                    const char *commands[] = {"help", "clear", "about", "uptime", "colors", "reboot", "exit", "halt", "echo"};
                    int num_cmds = 9;
                    int match_idx = -1;
                    int matches = 0;

                    // Searching for all the commands that begin with current input
                    for (int i = 0; i < num_cmds; i++) {
                        if (k_strncmp(commands[i], cmd_buf, cmd_len) == 0) {
                            match_idx = i;
                            matches++;
                        }
                    }

                    // Autocomplete if there is 1 match
                    if (matches == 1) {
                        const char *match = commands[match_idx];
                        while (cmd_len < (int)k_strlen(match)) {
                            char mc = match[cmd_len];
                            cmd_buf[cmd_len++] = mc;
                            vga_putchar(mc);
                        }
                    }
                }
            } else if (c == '\x11') {
                // Up arrow
                int oldest = (history_count > HISTORY_MAX) ? (history_count - HISTORY_MAX) : 0;
                if (history_idx > oldest) {
                    while (cmd_len > 0) { vga_putchar('\b'); cmd_len--; }
                    history_idx--;
                    k_strcpy(cmd_buf, history[history_idx % HISTORY_MAX]);
                    cmd_len = k_strlen(cmd_buf);
                    vga_puts(cmd_buf);
                }
            } else if (c == '\x12') {
                // Down arrow
                if (history_idx < history_count) {
                    while (cmd_len > 0) { vga_putchar('\b'); cmd_len--; }
                    history_idx++;
                    if (history_idx == history_count) {
                        cmd_buf[0] = '\0';
                        cmd_len = 0;
                    } else {
                        k_strcpy(cmd_buf, history[history_idx % HISTORY_MAX]);
                        cmd_len = k_strlen(cmd_buf);
                        vga_puts(cmd_buf);
                    }
                }
            } else if (c >= 32 && c <= 126 && cmd_len < CMD_BUF_SIZE - 1) {
                cmd_buf[cmd_len++] = c;
                vga_putchar(c);
            }
        }
        cmd_buf[cmd_len] = '\0';

        if (cmd_len == 0) {
        } else if (k_strcmp(cmd_buf, "help") == 0) {
            cmd_help();
        } else if (k_strcmp(cmd_buf, "clear") == 0) {
            vga_set_color(VGA_WHITE, VGA_BLACK);
            vga_clear();
        } else if (k_strcmp(cmd_buf, "about") == 0) {
            cmd_about();
        } else if (k_strcmp(cmd_buf, "uptime") == 0) {
            cmd_uptime();
        } else if (k_strcmp(cmd_buf, "colors") == 0) {
            cmd_colors();
        } else if (k_strcmp(cmd_buf, "reboot") == 0) {
            cmd_reboot();
        } else if (k_strcmp(cmd_buf, "exit") == 0) {
            cmd_exit();
        } else if (k_strcmp(cmd_buf, "halt") == 0) {
            vga_puts("\n  CPU halted. Power off the machine.\n");
            __asm__ volatile ("cli; hlt");
        } else if (k_strncmp(cmd_buf, "echo", 4) == 0
                   && (cmd_buf[4] == ' ' || cmd_buf[4] == '\0')) {
            cmd_echo(cmd_buf);
        } else {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_puts("\n  Unknown command: ");
            vga_puts(cmd_buf);
            vga_puts(" (type 'help' for a list)\n\n");
            vga_set_color(VGA_WHITE, VGA_BLACK);
        }
    }
}

// KERNEL ENTRY POINT

__attribute__((section(".text.boot"))) void kernel_main(void) {
    pic_remap();

    idt_install();

    __asm__ volatile ("sti");

    show_boot_menu();
    show_loading_animation();

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("OS kernel booted successfully.\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("Type 'help' for a list of commands.\n\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    shell_run();

    __asm__ volatile ("cli; hlt");
}
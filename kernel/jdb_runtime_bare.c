// Freestanding jdBasic runtime.
//
// Implements the symbols a kernel-profile program actually calls, measured by
// counting call sites in the emitted IR. Output goes to the VGA text buffer,
// memory comes from a bump allocator that never frees. The hosted runtime in
// src/jdb_runtime.cpp is a separate implementation of the same ABI; the two
// never link into the same image.

typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long long size_t;

#define NULL ((void*)0)

// ── Port I/O ────────────────────────────────────────────────

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

// ── Serial mirror ───────────────────────────────────────────
//
// Every character written to the screen also goes to COM1, so the image can be
// checked without a framebuffer.

#define COM1 0x3F8

static int serial_ready = 0;

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    serial_ready = 1;
}

static void serial_putc(char c) {
    if (!serial_ready) serial_init();
    while ((inb(COM1 + 5) & 0x20) == 0) { }
    outb(COM1, (uint8_t)c);
}

// ── VGA text output ─────────────────────────────────────────

#define VGA_MEM    ((volatile uint16_t*)0xB8000)
#define VGA_COLS   80
#define VGA_ROWS   25
#define VGA_ATTR   0x0F00

static int vga_pos = 0;

static void vga_scroll(void) {
    for (int i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
        VGA_MEM[i] = VGA_MEM[i + VGA_COLS];
    for (int i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
        VGA_MEM[i] = VGA_ATTR | ' ';
    vga_pos = (VGA_ROWS - 1) * VGA_COLS;
}

static void vga_putc(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    if (c == '\n') {
        vga_pos = (vga_pos / VGA_COLS + 1) * VGA_COLS;
    } else {
        VGA_MEM[vga_pos] = VGA_ATTR | (uint8_t)c;
        vga_pos++;
    }
    if (vga_pos >= VGA_ROWS * VGA_COLS) vga_scroll();
}

static void vga_puts(const char* s) {
    while (s && *s) vga_putc(*s++);
}

void vga_clear(void) {
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++) VGA_MEM[i] = VGA_ATTR | ' ';
    vga_pos = 0;
}

// ── Bump allocator ──────────────────────────────────────────
//
// Nothing is ever freed. The kernel profile has no garbage collector and no
// long-running allocation churn, so a monotonically rising pointer over a
// fixed region is the whole memory manager.

#define HEAP_BASE  0x00400000ULL
#define HEAP_LIMIT 0x00800000ULL

static uint64_t heap_ptr = HEAP_BASE;

static void* bump_alloc(size_t n) {
    n = (n + 15) & ~15ULL;
    if (heap_ptr + n >= HEAP_LIMIT) return NULL;
    void* p = (void*)heap_ptr;
    heap_ptr += n;
    return p;
}

// ── Compiler-emitted memory helpers ─────────────────────────
//
// The backend lowers struct copies and array zeroing to these even under
// -ffreestanding, so they must exist.

void* memset(void* dst, int c, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

static size_t bare_strlen(const char* s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

// ── Number formatting ───────────────────────────────────────

static void put_i64(int64_t v) {
    char buf[24];
    int i = 0;
    if (v < 0) { vga_putc('-'); v = -v; }
    if (v == 0) { vga_putc('0'); return; }
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) vga_putc(buf[--i]);
}

// ── Runtime ABI ─────────────────────────────────────────────

struct JdbArray {
    double* data;
    int64_t length;
    int32_t flags;
    int8_t* elem_tags;
};

// The handle a compiled program threads through the bridge entry points. In
// the kernel profile nothing dereferences it, so a null handle is enough.
void* jdrt_init(void)                                { return NULL; }
void  jdrt_shutdown(void* h)                         { (void)h; }
void  jdrt_set_event_dispatcher(void* h, void* fn)   { (void)h; (void)fn; }

// Registered as the dispatcher above, so it has to exist even though the
// kernel profile raises no events.
void jdrt_dispatch_event(void* h, void* name, void* args, int32_t n) {
    (void)h; (void)name; (void)args; (void)n;
}

void* jdrt_last_error(void* h)                       { (void)h; return NULL; }
void  jdrt_clear_last_error(void* h)                 { (void)h; }
int64_t jdrt_frame_begin(void* h)                    { (void)h; return 0; }
void  jdrt_frame_end(void* h, int64_t mark)          { (void)h; (void)mark; }

void  jdb_runtime_set_handle(void* h)                { (void)h; }
void  jdb_set_args(int32_t argc, void* argv)         { (void)argc; (void)argv; }

static int64_t err_code = 0;

int64_t jdb_err_code(void)                           { return err_code; }
void    jdb_err_set(const char* msg, int64_t code)   { (void)msg; err_code = code; }

void jdb_throw_uncaught(void) {
    vga_puts("\nPANIC: uncaught error\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

void jdb_print_int(int64_t v)     { put_i64(v); }
void jdb_print_nl(void)           { vga_putc('\n'); }
void jdb_print_space(void)        { vga_putc(' '); }
void jdb_print_str(const char* s) { vga_puts(s); }
void jdb_print_bool(int64_t v)    { vga_puts(v ? "TRUE" : "FALSE"); }

void jdb_print_double(double d) {
    if (d < 0) { vga_putc('-'); d = -d; }
    int64_t whole = (int64_t)d;
    put_i64(whole);
    double frac = d - (double)whole;
    if (frac <= 0.0) return;
    vga_putc('.');
    for (int i = 0; i < 6; i++) {
        frac *= 10.0;
        int digit = (int)frac;
        vga_putc((char)('0' + digit));
        frac -= (double)digit;
    }
}

char* jdb_str_concat(const char* a, const char* b) {
    size_t la = bare_strlen(a), lb = bare_strlen(b);
    char* r = (char*)bump_alloc(la + lb + 1);
    if (!r) return NULL;
    memcpy(r, a, la);
    memcpy(r + la, b, lb);
    r[la + lb] = '\0';
    return r;
}

struct JdbArray* jdb_array_new(int64_t size) {
    struct JdbArray* arr = (struct JdbArray*)bump_alloc(sizeof(struct JdbArray));
    if (!arr) return NULL;
    arr->data = (double*)bump_alloc((size_t)size * sizeof(double));
    arr->length = size;
    arr->flags = 0;
    arr->elem_tags = NULL;
    if (arr->data) memset(arr->data, 0, (size_t)size * sizeof(double));
    return arr;
}

void jdb_array_set(struct JdbArray* arr, int64_t idx, double val) {
    if (arr && arr->data && idx >= 0 && idx < arr->length) arr->data[idx] = val;
}

double jdb_array_get(struct JdbArray* arr, int64_t idx) {
    if (!arr || !arr->data || idx < 0 || idx >= arr->length) return 0.0;
    return arr->data[idx];
}

int64_t jdb_array_len(struct JdbArray* arr) {
    return arr ? arr->length : 0;
}

// Only the plain-double case: the kernel profile has no string, nested or
// tagged element arrays.
void jdb_print_array_elem(struct JdbArray* arr, int64_t idx) {
    if (!arr || !arr->data || idx < 0 || idx >= arr->length) return;
    jdb_print_double(arr->data[idx]);
}

// ── SYS.* ring-0 primitives ─────────────────────────────────

int64_t jdb_sys_inb(int64_t port) {
    return (int64_t)inb((uint16_t)port);
}

void jdb_sys_outb(int64_t port, int64_t val) {
    outb((uint16_t)port, (uint8_t)val);
}

int64_t jdb_sys_peekb(int64_t addr) {
    return (int64_t)*(volatile uint8_t*)(uint64_t)addr;
}

void jdb_sys_pokeb(int64_t addr, int64_t val) {
    *(volatile uint8_t*)(uint64_t)addr = (uint8_t)val;
}

int64_t jdb_sys_peekw(int64_t addr) {
    return (int64_t)*(volatile uint16_t*)(uint64_t)addr;
}

void jdb_sys_pokew(int64_t addr, int64_t val) {
    *(volatile uint16_t*)(uint64_t)addr = (uint16_t)val;
}

int64_t jdb_sys_peek(int64_t addr) {
    return (int64_t)*(volatile uint32_t*)(uint64_t)addr;
}

void jdb_sys_poke(int64_t addr, int64_t val) {
    *(volatile uint32_t*)(uint64_t)addr = (uint32_t)val;
}

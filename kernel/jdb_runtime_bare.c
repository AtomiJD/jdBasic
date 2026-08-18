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

#define HEAP_BASE  0x01000000ULL
#define HEAP_LIMIT 0x04000000ULL

static uint64_t heap_ptr = HEAP_BASE;

static void panic(const char* msg);

static void* bump_alloc(size_t n) {
    n = (n + 15) & ~15ULL;
    if (heap_ptr + n >= HEAP_LIMIT) panic("out of memory");
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

// The compiler registers every SUB whose name carries an event suffix, so a
// name ending in _LOAD is enough to pull this in even with no ON statement
// anywhere. Nothing dispatches events here.
void jdrt_register_event_handler(void* name, void* fn) {
    (void)name; (void)fn;
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

static void panic(const char* msg) {
    vga_puts("\nPANIC: ");
    vga_puts(msg);
    vga_putc('\n');
    for (;;) __asm__ volatile ("cli; hlt");
}

void jdb_throw_uncaught(void) { panic("uncaught error"); }

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

// Flags bit 0 marks pointer elements, bit 1 marks them strings; the pointer
// itself rides in the element's double as a bit pattern.
void jdb_array_set_string_elems(struct JdbArray* arr) {
    if (arr) arr->flags |= 3;
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

// ── Recursion guard ─────────────────────────────────────────
//
// Emitted around every user FUNC and SUB. The 64 KiB boot stack is the real
// budget here, so the limit is lower than the hosted runtime's 256.

#define MAX_RECURSION 128

static int64_t rec_depth = 0;

int32_t jdb_recursion_enter(void) {
    if (rec_depth >= MAX_RECURSION) {
        jdb_err_set("Stack overflow: recursion too deep", 1);
        return 1;
    }
    rec_depth++;
    return 0;
}

void    jdb_recursion_leave(void)        { if (rec_depth > 0) rec_depth--; }
int64_t jdb_recursion_depth(void)        { return rec_depth; }
void    jdb_recursion_reset_to(int64_t d){ rec_depth = d; }

// ── Math the backend lowers to libm ─────────────────────────
//
// Exact for magnitudes inside the 64-bit integer range, which is everything
// the kernel profile computes with. Larger values are returned unchanged
// rather than approximated.

#define INT64_SCALE 9.2233720368547758e18

double trunc(double x) {
    if (x >= INT64_SCALE || x <= -INT64_SCALE) return x;
    return (double)(int64_t)x;
}

double floor(double x) {
    double t = trunc(x);
    return (x < 0.0 && t != x) ? t - 1.0 : t;
}

double ceil(double x) {
    double t = trunc(x);
    return (x > 0.0 && t != x) ? t + 1.0 : t;
}

double fabs(double x) { return x < 0.0 ? -x : x; }

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    return x - trunc(x / y) * y;
}

// ── Conversions ─────────────────────────────────────────────

int64_t jdb_cint(double x) { return (int64_t)(int32_t)x; }
int64_t jdb_clng(double x) { return (int64_t)x; }
int64_t jdb_cbool(double x) { return x != 0.0 ? 1 : 0; }

// ── Strings ─────────────────────────────────────────────────

int64_t jdb_len_str(const char* s) { return (int64_t)bare_strlen(s); }

int64_t jdb_byteat(const char* s, int64_t idx) {
    if (!s || idx < 0 || idx >= (int64_t)bare_strlen(s)) return 0;
    return (int64_t)(uint8_t)s[idx];
}

int64_t jdb_asc(const char* s) {
    return (s && s[0]) ? (int64_t)(uint8_t)s[0] : 0;
}

static char* bare_strdup(const char* s, int64_t n) {
    char* r = (char*)bump_alloc((size_t)n + 1);
    if (!r) return NULL;
    if (n > 0) memcpy(r, s, (size_t)n);
    r[n] = '\0';
    return r;
}

int64_t jdb_str_eq(const char* a, const char* b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    size_t la = bare_strlen(a), lb = bare_strlen(b);
    if (la != lb) return 0;
    for (size_t i = 0; i < la; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int64_t jdb_str_ne(const char* a, const char* b) { return jdb_str_eq(a, b) ? 0 : 1; }

int64_t jdb_str_cmp(const char* a, const char* b) {
    const unsigned char* x = (const unsigned char*)(a ? a : "");
    const unsigned char* y = (const unsigned char*)(b ? b : "");
    while (*x && *x == *y) { x++; y++; }
    return (int64_t)*x - (int64_t)*y;
}

// 0-based, -1 when absent.
int64_t jdb_instr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return -1;
    int64_t hlen = (int64_t)bare_strlen(haystack);
    int64_t nlen = (int64_t)bare_strlen(needle);
    if (nlen == 0) return 0;
    if (nlen > hlen) return -1;
    for (int64_t i = 0; i + nlen <= hlen; i++) {
        int64_t k = 0;
        while (k < nlen && haystack[i + k] == needle[k]) k++;
        if (k == nlen) return i;
    }
    return -1;
}

char* jdb_mid_lax(const char* s, int64_t start, int64_t length) {
    if (!s) return bare_strdup("", 0);
    int64_t slen = (int64_t)bare_strlen(s);
    if (start < 0) start = 0;
    if (start > slen) return bare_strdup("", 0);
    if (length < 0 || start + length > slen) length = slen - start;
    return bare_strdup(s + start, length);
}

char* jdb_mid(const char* s, int64_t start, int64_t length) {
    if (!s) return bare_strdup("", 0);
    int64_t slen = (int64_t)bare_strlen(s);
    if (start < 0 || start > slen) {
        jdb_err_set("MID: index out of range", 1);
        return bare_strdup("", 0);
    }
    if (length < 0 || start + length > slen) length = slen - start;
    return bare_strdup(s + start, length);
}

char* jdb_left(const char* s, int64_t n) { return jdb_mid_lax(s, 0, n); }

char* jdb_right(const char* s, int64_t n) {
    if (!s) return bare_strdup("", 0);
    int64_t slen = (int64_t)bare_strlen(s);
    if (n >= slen) return bare_strdup(s, slen);
    return jdb_mid_lax(s, slen - n, n);
}

static char* int_to_text(int64_t v, char* end) {
    char* p = end;
    *--p = '\0';
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
    if (u == 0) *--p = '0';
    while (u) { *--p = (char)('0' + (u % 10)); u /= 10; }
    if (neg) *--p = '-';
    return p;
}

char* jdb_int_to_str(int64_t v) {
    char buf[24];
    char* p = int_to_text(v, buf + sizeof(buf));
    return bare_strdup(p, (int64_t)bare_strlen(p));
}

// Whole numbers render without decimals, everything else with six places.
// The hosted runtime uses %g here; without a formatter that is the closest
// behaviour worth carrying into ring 0.
char* jdb_double_to_str(double val) {
    char buf[64];
    int n = 0;
    if (val == trunc(val) && fabs(val) < 1e15) {
        char t[24];
        char* p = int_to_text((int64_t)val, t + sizeof(t));
        while (*p && n < 60) buf[n++] = *p++;
    } else {
        double a = val;
        if (a < 0.0) { buf[n++] = '-'; a = -a; }
        int64_t whole = (int64_t)trunc(a);
        char t[24];
        char* p = int_to_text(whole, t + sizeof(t));
        while (*p && n < 50) buf[n++] = *p++;
        buf[n++] = '.';
        double frac = a - (double)whole;
        for (int i = 0; i < 6 && n < 62; i++) {
            frac *= 10.0;
            int digit = (int)frac;
            buf[n++] = (char)('0' + digit);
            frac -= (double)digit;
        }
    }
    buf[n] = '\0';
    return bare_strdup(buf, n);
}

// Registered as the length path for VM-handle values. The kernel profile has
// no VM, and the tag dispatch only reaches this arm for handles, so nothing
// real can land here.
int64_t jdrt_val_length(void* h, int64_t val) {
    (void)h; (void)val;
    return 0;
}

char* jdb_chr(int64_t code) {
    char* r = (char*)bump_alloc(2);
    if (!r) return NULL;
    r[0] = (char)(uint8_t)code;
    r[1] = '\0';
    return r;
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

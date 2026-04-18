// jdb_runtime.cpp — C runtime for jdBasic native-compiled executables.
// These functions are called by LLVM-generated code.
// Compiled separately into jdb_runtime.obj and linked into generated .exe files.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <string>
#include <regex>
#include <vector>

#include "jdb_tags.h"

#ifdef _WIN32
#include <windows.h>
#endif

static auto g_start_time = std::chrono::steady_clock::now();

// Forward declare JdbArray for use in C++ functions
struct JdbArray;
extern "C" JdbArray* jdb_array_new(int64_t size);

extern "C" {

// ── I/O ─────────────────────────────────────────────────────

void jdb_print_int(int64_t val) {
    printf("%lld", (long long)val);
}

void jdb_print_double(double val) {
    printf("%g", val);
}

void jdb_print_str(const char* val) {
    printf("%s", val);
}

void jdb_print_nl() {
    printf("\n");
}

// Trace logging: writes to stderr with immediate flush so we can find crash sites.
void jdb_trace(const char* file, int64_t line) {
    if (file && *file)
        fprintf(stderr, "[TRACE] %s:%lld\n", file, (long long)line);
    else
        fprintf(stderr, "[TRACE] line %lld\n", (long long)line);
    fflush(stderr);
}

void jdb_print_space() {
    printf(" ");
}

// ── Exception state (THROW/TRY/CATCH) ───────────────────────
// Generated code branches to its catch-block on throw; we only
// store the message/code here so ERRMSG$/ERR can read them in
// the CATCH body.

static char    g_err_msg[512] = "";
static int64_t g_err_code     = 0;
// Shadow copies that persist through a catch body. The per-statement err
// check needs g_err_code to go to zero on catch entry or it would keep
// re-tripping; user code inside the catch still expects ERR / ERRMSG$
// to return the caught values. Reads fall back to these shadows.
static char    g_last_msg[512] = "";
static int64_t g_last_code     = 0;

void jdb_err_set(const char* msg, int64_t code) {
    if (msg) {
        strncpy(g_err_msg, msg, sizeof(g_err_msg) - 1);
        g_err_msg[sizeof(g_err_msg) - 1] = '\0';
        strncpy(g_last_msg, msg, sizeof(g_last_msg) - 1);
        g_last_msg[sizeof(g_last_msg) - 1] = '\0';
    } else {
        g_err_msg[0] = '\0';
        g_last_msg[0] = '\0';
    }
    g_err_code = code;
    g_last_code = code;
}

void jdb_err_clear() {
    g_err_msg[0] = '\0';
    g_err_code = 0;
    g_last_msg[0] = '\0';
    g_last_code = 0;
}

const char* jdb_err_msg() {
    const char* src = g_err_msg[0] ? g_err_msg : g_last_msg;
    return _strdup(src);
}

// Raw err_code — used by the per-stmt propagation check in codegen,
// which MUST read only the live value or it would loop forever on a
// caught error (the shadow would keep it non-zero after soft-clear).
int64_t jdb_err_code() {
    return g_err_code;
}

// User-visible err_code — falls back to the shadow so a catch body
// sees the caught code even after the soft-clear has run.
int64_t jdb_err_code_visible() {
    return g_err_code ? g_err_code : g_last_code;
}

// Called when a THROW escapes all TRY handlers — mirrors the
// interpreter's unhandled exception behavior.
void jdb_throw_uncaught() {
    fprintf(stderr, "Unhandled exception: %s\n", g_err_msg);
    fflush(stderr);
    exit(1);
}

// Recursion guard — each user FUNC/SUB bumps a thread-local depth
// counter at entry and decrements it on normal return. Hitting the
// limit sets the error state and returns 1 so the codegen branch can
// propagate out instead of letting the OS stack overflow kill the exe.
static thread_local int64_t g_rec_depth = 0;
static const int64_t JDB_MAX_RECURSION = 256;

int32_t jdb_recursion_enter() {
    if (g_rec_depth >= JDB_MAX_RECURSION) {
        jdb_err_set("Stack overflow: recursion too deep", 1);
        return 1;
    }
    g_rec_depth++;
    return 0;
}

// Soft clear used by a native catch block on entry: zeroes only the
// error code so the per-statement propagation check inside the catch
// body doesn't re-trip, while leaving g_err_msg intact so ERRMSG$ can
// still return the caught message. The real err_clear runs when the
// catch body finishes.
void jdb_err_code_clear() {
    g_err_code = 0;
}

void jdb_recursion_leave() {
    if (g_rec_depth > 0) g_rec_depth--;
}

// After a caught error the counter may be stale because the unwind
// path skipped the paired leaves. The catch block resets to the depth
// the caller was at so later recursion can run to the full limit.
void jdb_recursion_reset_to(int64_t depth) {
    if (depth >= 0) g_rec_depth = depth;
}

int64_t jdb_recursion_depth() {
    return g_rec_depth;
}

// OS.FEATURE for the native-compiled runtime. Mirrors the VM-side
// function: returns 1 for capabilities the current binary advertises.
// Always reports NATIVEC=1 (we are running compiled code) and the
// inverse for INTERPRETER. Optional features are off here because
// jdb_runtime.cpp is built without those headers.
int64_t jdb_os_feature(const char* name) {
    if (!name) return 0;
    char up[64]; size_t n = strlen(name);
    if (n >= sizeof(up)) n = sizeof(up) - 1;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        up[i] = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
    }
    up[n] = '\0';
    if (strcmp(up, "NATIVEC")     == 0) return 1;
    if (strcmp(up, "INTERPRETER") == 0) return 0;
    if (strcmp(up, "LLVMC")       == 0) return 1;
    return 0;
}

// ── String operations ───────────────────────────────────────

char* jdb_str_concat(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    char* r = (char*)malloc(la + lb + 1);
    memcpy(r, a, la);
    memcpy(r + la, b, lb + 1);
    return r;
}

// Locale state: 0 = C (default), 1 = de_DE (dot thousand sep, comma decimal)
static int g_locale = 0;

void jdb_setlocale(const char* name) {
    if (!name) { g_locale = 0; return; }
    if (strstr(name, "de") || strstr(name, "DE")) g_locale = 1;
    else g_locale = 0;
}

// Format integer with thousand separators based on locale.
static void format_int_grouped(int64_t val, char sep, char* out, size_t out_size) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)val);
    bool neg = (buf[0] == '-');
    const char* digits = neg ? buf + 1 : buf;
    size_t dlen = strlen(digits);
    size_t oi = 0;
    if (neg && oi < out_size - 1) out[oi++] = '-';
    for (size_t i = 0; i < dlen && oi < out_size - 1; i++) {
        if (i > 0 && (dlen - i) % 3 == 0) {
            if (oi < out_size - 1) out[oi++] = sep;
        }
        out[oi++] = digits[i];
    }
    out[oi] = '\0';
}

char* jdb_int_to_str(int64_t val) {
    char buf[64];
    if (g_locale == 1) format_int_grouped(val, '.', buf, sizeof(buf));
    else snprintf(buf, sizeof(buf), "%lld", (long long)val);
    return _strdup(buf);
}

char* jdb_double_to_str(double val) {
    char buf[128];
    // Whole-number doubles print without decimals (matches interpreter).
    if (val == (int64_t)val && fabs(val) < 1e15) {
        if (g_locale == 1) format_int_grouped((int64_t)val, '.', buf, sizeof(buf));
        else snprintf(buf, sizeof(buf), "%lld", (long long)(int64_t)val);
    } else {
        snprintf(buf, sizeof(buf), "%g", val);
        if (g_locale == 1) {
            // Replace dot decimal with comma
            for (char* p = buf; *p; p++) if (*p == '.') { *p = ','; break; }
        }
    }
    return _strdup(buf);
}

// ── Math builtins ───────────────────────────────────────────

double jdb_abs(double x)       { return fabs(x); }
double jdb_sqr(double x)       { return sqrt(x); }
double jdb_sin(double x)       { return sin(x); }
double jdb_cos(double x)       { return cos(x); }
double jdb_tan(double x)       { return tan(x); }
double jdb_asin(double x)      { return asin(x); }
double jdb_acos(double x)      { return acos(x); }
double jdb_atan(double x)      { return atan(x); }
double jdb_log(double x)       { return log(x); }
double jdb_log10(double x)     { return log10(x); }
double jdb_exp(double x)       { return exp(x); }
double jdb_floor(double x)     { return floor(x); }
double jdb_ceil(double x)      { return ceil(x); }
double jdb_pow(double x, double y) { return pow(x, y); }
int64_t jdb_int(double x)      { return (int64_t)x; }
double jdb_val(const char* s)   { return atof(s); }
double jdb_rnd()                { return (double)rand() / RAND_MAX; }

// Extended math
double jdb_sinh(double x)      { return sinh(x); }
double jdb_cosh(double x)      { return cosh(x); }
double jdb_tanh(double x)      { return tanh(x); }
double jdb_atan2(double y, double x) { return atan2(y, x); }
double jdb_round(double x)     { return round(x); }
double jdb_round_p(double x, double places) {
    double m = pow(10.0, places);
    return round(x * m) / m;
}
double jdb_trunc(double x)     { return trunc(x); }
double jdb_sign(double x)      { return (x > 0) ? 1.0 : (x < 0) ? -1.0 : 0.0; }
double jdb_clamp(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
double jdb_fac(double n) {
    int64_t k = (int64_t)n;
    if (k <= 1) return 1.0;
    double r = 1.0;
    for (int64_t i = 2; i <= k; i++) r *= i;
    return r;
}
double jdb_fmod(double x, double y) { return fmod(x, y); }
double jdb_min2(double a, double b) { return a < b ? a : b; }
double jdb_max2(double a, double b) { return a > b ? a : b; }
double jdb_pi() { return 3.14159265358979323846; }
double jdb_e()  { return 2.71828182845904523536; }

// ── System ──────────────────────────────────────────────────

double jdb_tick() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now - g_start_time).count();
}

void jdb_sleep(int64_t ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
}

void jdb_randomseed(int64_t seed) {
    srand((unsigned int)seed);
}

// ── Arrays ──────────────────────────────────────────────────

struct JdbArray {
    double* data;
    int64_t length;
    int32_t flags;  // bit 0: elements are nested array ptrs (2D+)
};

JdbArray* jdb_array_new(int64_t size) {
    auto* arr = (JdbArray*)malloc(sizeof(JdbArray));
    arr->data = (double*)calloc(size, sizeof(double));
    arr->length = size;
    arr->flags = 0;
    return arr;
}

void jdb_array_set(JdbArray* arr, int64_t idx, double val) {
    if (idx >= 0 && idx < arr->length)
        arr->data[idx] = val;
}

double jdb_array_get(JdbArray* arr, int64_t idx) {
    if (idx >= 0 && idx < arr->length)
        return arr->data[idx];
    return 0.0;
}

int64_t jdb_array_len(JdbArray* arr) {
    return arr ? arr->length : 0;
}

JdbArray* jdb_iota(int64_t n) {
    auto* arr = jdb_array_new(n);
    for (int64_t i = 0; i < n; i++)
        arr->data[i] = (double)(i + 1);  // IOTA starts at 1
    return arr;
}

// IOTA(n, start, step) — n elements starting at `start` stepping by `step`.
JdbArray* jdb_iota3(double n, double start, double step) {
    int64_t cnt = (int64_t)n;
    if (cnt < 0) cnt = 0;
    auto* arr = jdb_array_new(cnt);
    for (int64_t i = 0; i < cnt; i++)
        arr->data[i] = start + step * (double)i;
    return arr;
}

// POP: remove last element, return its value (numeric as double, string as ptr
// via union). Caller knows the type via array flags. Returns 0 on empty.
double jdb_array_pop(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    double v = arr->data[arr->length - 1];
    arr->length--;
    return v;
}

// POP returning a string-typed element (array flags indicate string storage).
char* jdb_array_pop_str(JdbArray* arr) {
    if (!arr || arr->length == 0) return _strdup("");
    union { double d; int64_t i; } u;
    u.d = arr->data[arr->length - 1];
    arr->length--;
    const char* s = (const char*)(intptr_t)u.i;
    return _strdup(s ? s : "");
}

double jdb_mean(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    double sum = 0.0;
    for (int64_t i = 0; i < arr->length; i++)
        sum += arr->data[i];
    return sum / arr->length;
}

double jdb_stdev(JdbArray* arr) {
    if (!arr || arr->length <= 1) return 0.0;
    double avg = jdb_mean(arr);
    double sum_sq = 0.0;
    for (int64_t i = 0; i < arr->length; i++) {
        double d = arr->data[i] - avg;
        sum_sq += d * d;
    }
    return sqrt(sum_sq / arr->length);  // population stdev (matches VM)
}

// ── Array operations ────────────────────────────────────────

double jdb_array_sum(JdbArray* arr) {
    if (!arr) return 0.0;
    double s = 0.0;
    for (int64_t i = 0; i < arr->length; i++) s += arr->data[i];
    return s;
}

double jdb_array_product(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    double s = 1.0;
    for (int64_t i = 0; i < arr->length; i++) s *= arr->data[i];
    return s;
}

double jdb_array_min(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    double m = arr->data[0];
    for (int64_t i = 1; i < arr->length; i++) if (arr->data[i] < m) m = arr->data[i];
    return m;
}

double jdb_array_max(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    double m = arr->data[0];
    for (int64_t i = 1; i < arr->length; i++) if (arr->data[i] > m) m = arr->data[i];
    return m;
}

int64_t jdb_array_any(JdbArray* arr) {
    if (!arr) return 0;
    for (int64_t i = 0; i < arr->length; i++) if (arr->data[i] != 0.0) return 1;
    return 0;
}

int64_t jdb_array_all(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0;
    for (int64_t i = 0; i < arr->length; i++) if (arr->data[i] == 0.0) return 0;
    return 1;
}

JdbArray* jdb_array_reverse(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    for (int64_t i = 0; i < arr->length; i++)
        r->data[i] = arr->data[arr->length - 1 - i];
    return r;
}

JdbArray* jdb_array_sort(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    memcpy(r->data, arr->data, arr->length * sizeof(double));
    // Simple insertion sort
    for (int64_t i = 1; i < r->length; i++) {
        double key = r->data[i];
        int64_t j = i - 1;
        while (j >= 0 && r->data[j] > key) { r->data[j+1] = r->data[j]; j--; }
        r->data[j+1] = key;
    }
    return r;
}

JdbArray* jdb_array_append(JdbArray* arr, double val) {
    int64_t newlen = arr ? arr->length + 1 : 1;
    auto* r = jdb_array_new(newlen);
    if (arr) memcpy(r->data, arr->data, arr->length * sizeof(double));
    r->data[newlen - 1] = val;
    return r;
}

int64_t jdb_array_count(JdbArray* arr, double val) {
    if (!arr) return 0;
    int64_t c = 0;
    for (int64_t i = 0; i < arr->length; i++) if (arr->data[i] == val) c++;
    return c;
}

int64_t jdb_array_indexof(JdbArray* arr, double val) {
    if (!arr) return -1;
    for (int64_t i = 0; i < arr->length; i++) if (arr->data[i] == val) return i;
    return -1;
}

// IN operator on arrays: returns 1 if needle is a member, else 0.
int64_t jdb_array_has_str(JdbArray* arr, const char* needle) {
    if (!arr || !needle) return 0;
    for (int64_t i = 0; i < arr->length; i++) {
        union { double d; int64_t i; } u; u.d = arr->data[i];
        const char* s = (const char*)(intptr_t)u.i;
        if (s && strcmp(s, needle) == 0) return 1;
    }
    return 0;
}

int64_t jdb_array_has_num(JdbArray* arr, double val) {
    if (!arr) return 0;
    for (int64_t i = 0; i < arr->length; i++) if (arr->data[i] == val) return 1;
    return 0;
}

JdbArray* jdb_array_unique(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    int64_t n = 0;
    for (int64_t i = 0; i < arr->length; i++) {
        bool found = false;
        for (int64_t j = 0; j < n; j++) if (r->data[j] == arr->data[i]) { found = true; break; }
        if (!found) r->data[n++] = arr->data[i];
    }
    r->length = n;
    return r;
}

JdbArray* jdb_array_cumsum(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    double s = 0;
    for (int64_t i = 0; i < arr->length; i++) { s += arr->data[i]; r->data[i] = s; }
    return r;
}

JdbArray* jdb_array_cumprod(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    double s = 1;
    for (int64_t i = 0; i < arr->length; i++) { s *= arr->data[i]; r->data[i] = s; }
    return r;
}

JdbArray* jdb_array_take(JdbArray* arr, int64_t n) {
    if (!arr || n <= 0) return jdb_array_new(0);
    if (n > arr->length) n = arr->length;
    auto* r = jdb_array_new(n);
    memcpy(r->data, arr->data, n * sizeof(double));
    return r;
}

JdbArray* jdb_array_drop(JdbArray* arr, int64_t n) {
    if (!arr || n >= arr->length) return jdb_array_new(0);
    if (n < 0) n = 0;
    int64_t newlen = arr->length - n;
    auto* r = jdb_array_new(newlen);
    memcpy(r->data, arr->data + n, newlen * sizeof(double));
    return r;
}

JdbArray* jdb_array_diff(JdbArray* a, JdbArray* b) {
    if (!a) return jdb_array_new(0);
    auto* r = jdb_array_new(a->length);
    int64_t n = 0;
    for (int64_t i = 0; i < a->length; i++) {
        bool found = false;
        if (b) for (int64_t j = 0; j < b->length; j++) if (b->data[j] == a->data[i]) { found = true; break; }
        if (!found) r->data[n++] = a->data[i];
    }
    r->length = n;
    return r;
}

JdbArray* jdb_array_flatten(JdbArray* arr) {
    // For 1D arrays, flatten is identity
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    memcpy(r->data, arr->data, arr->length * sizeof(double));
    return r;
}

JdbArray* jdb_array_shuffle(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    memcpy(r->data, arr->data, arr->length * sizeof(double));
    for (int64_t i = r->length - 1; i > 0; i--) {
        int64_t j = rand() % (i + 1);
        double tmp = r->data[i]; r->data[i] = r->data[j]; r->data[j] = tmp;
    }
    return r;
}

double jdb_array_median(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    auto* sorted = jdb_array_sort(arr);
    double result;
    int64_t n = sorted->length;
    if (n % 2 == 1) result = sorted->data[n/2];
    else result = (sorted->data[n/2-1] + sorted->data[n/2]) / 2.0;
    free(sorted->data); free(sorted);
    return result;
}

double jdb_array_variance(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    double avg = jdb_mean(arr);
    double ss = 0.0;
    for (int64_t i = 0; i < arr->length; i++) {
        double d = arr->data[i] - avg;
        ss += d * d;
    }
    return ss / arr->length;
}

JdbArray* jdb_linspace(double start, double stop, int64_t n) {
    if (n <= 0) return jdb_array_new(0);
    auto* r = jdb_array_new(n);
    if (n == 1) { r->data[0] = start; return r; }
    double step = (stop - start) / (n - 1);
    for (int64_t i = 0; i < n; i++) r->data[i] = start + i * step;
    return r;
}

JdbArray* jdb_zeros(int64_t n) { return jdb_array_new(n); }

JdbArray* jdb_ones(int64_t n) {
    auto* r = jdb_array_new(n);
    for (int64_t i = 0; i < n; i++) r->data[i] = 1.0;
    return r;
}

JdbArray* jdb_range(int64_t start, int64_t stop, int64_t step) {
    if (step == 0) step = 1;
    int64_t n = (stop - start + step - 1) / step;
    if (n <= 0) return jdb_array_new(0);
    auto* r = jdb_array_new(n);
    for (int64_t i = 0; i < n; i++) r->data[i] = (double)(start + i * step);
    return r;
}

JdbArray* jdb_grade(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    // Fill with indices
    for (int64_t i = 0; i < arr->length; i++) r->data[i] = (double)i;
    // Sort indices by arr values (insertion sort)
    for (int64_t i = 1; i < r->length; i++) {
        double key_idx = r->data[i];
        double key_val = arr->data[(int64_t)key_idx];
        int64_t j = i - 1;
        while (j >= 0 && arr->data[(int64_t)r->data[j]] > key_val) {
            r->data[j+1] = r->data[j]; j--;
        }
        r->data[j+1] = key_idx;
    }
    return r;
}

// ── Array Arithmetic (element-wise, recursive for 2D+) ──
// Uses flags bit 0 to know if elements are nested array pointers.
// Mirrors the interpreter's array_arithmetic: recurse on nested arrays,
// scalar_binop on leaf elements. Supports broadcasting.

static inline JdbArray* decode_inner(double val) {
    union { double d; int64_t i; } u; u.d = val;
    return (JdbArray*)(intptr_t)u.i;
}
static inline double encode_inner(JdbArray* arr) {
    union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)arr;
    return u.d;
}

static inline double scalar_op(double a, double b, int op) {
    switch (op) {
        case 0: return a + b;
        case 1: return a - b;
        case 2: return a * b;
        case 3: return b != 0 ? a / b : 0;
        default: return 0;
    }
}
static inline double scalar_cmp(double a, double b, int op) {
    switch (op) {
        case 0: return a == b ? 1.0 : 0.0;
        case 1: return a != b ? 1.0 : 0.0;
        case 2: return (a && b) ? 1.0 : 0.0;
        case 3: return (a || b) ? 1.0 : 0.0;
        default: return 0;
    }
}

// arr OP arr (recursive for nested arrays)
static JdbArray* arr_binop(JdbArray* a, JdbArray* b, int op) {
    if (!a || !b) return jdb_array_new(0);
    int64_t n = a->length < b->length ? a->length : b->length;
    auto* r = jdb_array_new(n);
    bool nested = (a->flags & 1) || (b->flags & 1);
    if (nested) {
        r->flags |= 1;
        for (int64_t i = 0; i < n; i++) {
            auto* inner = arr_binop(decode_inner(a->data[i]), decode_inner(b->data[i]), op);
            r->data[i] = encode_inner(inner);
        }
    } else {
        for (int64_t i = 0; i < n; i++)
            r->data[i] = scalar_op(a->data[i], b->data[i], op);
    }
    return r;
}

// arr OP scalar (recursive for nested arrays, broadcasts scalar to all leaves)
static JdbArray* arr_scalar_op(JdbArray* a, double s, int op, bool scalar_left) {
    if (!a) return jdb_array_new(0);
    auto* r = jdb_array_new(a->length);
    if (a->flags & 1) {
        r->flags |= 1;
        for (int64_t i = 0; i < a->length; i++) {
            auto* inner = arr_scalar_op(decode_inner(a->data[i]), s, op, scalar_left);
            r->data[i] = encode_inner(inner);
        }
    } else {
        for (int64_t i = 0; i < a->length; i++) {
            double av = a->data[i];
            r->data[i] = scalar_left ? scalar_op(s, av, op) : scalar_op(av, s, op);
        }
    }
    return r;
}

// arr CMP scalar (recursive)
static JdbArray* arr_cmp_scalar(JdbArray* a, double s, int op) {
    if (!a) return jdb_array_new(0);
    auto* r = jdb_array_new(a->length);
    if (a->flags & 1) {
        r->flags |= 1;
        for (int64_t i = 0; i < a->length; i++) {
            auto* inner = arr_cmp_scalar(decode_inner(a->data[i]), s, op);
            r->data[i] = encode_inner(inner);
        }
    } else {
        for (int64_t i = 0; i < a->length; i++)
            r->data[i] = scalar_cmp(a->data[i], s, op);
    }
    return r;
}

// arr CMP arr (recursive)
static JdbArray* arr_cmp_arr(JdbArray* a, JdbArray* b, int op) {
    if (!a || !b) return jdb_array_new(0);
    int64_t n = a->length < b->length ? a->length : b->length;
    auto* r = jdb_array_new(n);
    bool nested = (a->flags & 1) || (b->flags & 1);
    if (nested) {
        r->flags |= 1;
        for (int64_t i = 0; i < n; i++) {
            auto* inner = arr_cmp_arr(decode_inner(a->data[i]), decode_inner(b->data[i]), op);
            r->data[i] = encode_inner(inner);
        }
    } else {
        for (int64_t i = 0; i < n; i++)
            r->data[i] = scalar_cmp(a->data[i], b->data[i], op);
    }
    return r;
}

// op: 0=add,1=sub,2=mul,3=div; is_arr_b: whether b is an array
JdbArray* jdb_array_binop(JdbArray* a, JdbArray* b, int32_t op) {
    return arr_binop(a, b, op);
}
JdbArray* jdb_array_scalar_op(JdbArray* a, double s, int32_t op, int32_t scalar_left) {
    return arr_scalar_op(a, s, op, scalar_left != 0);
}
JdbArray* jdb_array_cmp_scalar(JdbArray* a, double s, int32_t op) {
    return arr_cmp_scalar(a, s, op);
}
JdbArray* jdb_array_cmp_arr(JdbArray* a, JdbArray* b, int32_t op) {
    return arr_cmp_arr(a, b, op);
}

// Mark array as containing nested array pointers (2D+)
void jdb_array_set_nested(JdbArray* arr) {
    if (arr) arr->flags |= 1;
}

// Mark array as containing string elements (ptr + bit 1 so print/cmp know)
void jdb_array_set_string_elems(JdbArray* arr) {
    if (arr) arr->flags |= 3;  // bit 0 (ptr) + bit 1 (string)
}

// String * int → repeat: "-" * 5 → "-----"
char* jdb_str_repeat(const char* s, int64_t n) {
    if (!s || n <= 0) return _strdup("");
    size_t slen = strlen(s);
    char* out = (char*)malloc(slen * (size_t)n + 1);
    for (int64_t i = 0; i < n; i++) memcpy(out + i * slen, s, slen);
    out[slen * n] = '\0';
    return out;
}

// ── Maps / Objects (string-keyed) ──────────────────────────────
// Simple linear-scan map; fine for the small maps used in tests.
// Layout: { count, capacity, char** keys, double* values, int32* tags }
// Tag values follow JdTag enum (jdb_tags.h): F64, STR, ARR, NATIVE_MAP.

struct JdbMap {
    int64_t count;
    int64_t capacity;
    char** keys;
    double* values;
    int32_t* tags;
};

JdbMap* jdb_map_new() {
    auto* m = (JdbMap*)malloc(sizeof(JdbMap));
    m->count = 0;
    m->capacity = 0;
    m->keys = nullptr;
    m->values = nullptr;
    m->tags = nullptr;
    return m;
}

// ── Event System (ON / RAISEEVENT) ──────────────────────────
// Handlers have signature void (*)(JdbArray*). RAISEEVENT packs
// the supplied scalar arg into a fresh one-element string array
// and invokes the registered handler synchronously.

}  // close extern "C" so we can use std::unordered_map below

#include <unordered_map>

typedef void (*JdbEventHandler)(JdbArray*);
static std::unordered_map<std::string, JdbEventHandler> g_event_handlers;

extern "C" {

void jdb_event_on(const char* name, void* handler) {
    if (!name || !handler) return;
    g_event_handlers[name] = (JdbEventHandler)handler;
}

void jdb_event_raise_str(const char* name, const char* arg) {
    if (!name) return;
    auto it = g_event_handlers.find(name);
    if (it == g_event_handlers.end() || !it->second) return;
    auto* arr = jdb_array_new(1);
    arr->flags |= 2;  // string-flag
    union { double d; int64_t i; } u;
    u.i = (int64_t)(intptr_t)_strdup(arg ? arg : "");
    arr->data[0] = u.d;
    it->second(arr);
}

static void map_grow(JdbMap* m) {
    int64_t newcap = m->capacity ? m->capacity * 2 : 8;
    m->keys = (char**)realloc(m->keys, newcap * sizeof(char*));
    m->values = (double*)realloc(m->values, newcap * sizeof(double));
    m->tags = (int32_t*)realloc(m->tags, newcap * sizeof(int32_t));
    m->capacity = newcap;
}

static int64_t map_find(JdbMap* m, const char* key) {
    if (!m || !key) return -1;
    for (int64_t i = 0; i < m->count; i++) {
        if (m->keys[i] && strcmp(m->keys[i], key) == 0) return i;
    }
    return -1;
}

void jdb_map_set_f64(JdbMap* m, const char* key, double val) {
    if (!m || !key) return;
    int64_t idx = map_find(m, key);
    if (idx < 0) {
        if (m->count == m->capacity) map_grow(m);
        idx = m->count++;
        m->keys[idx] = _strdup(key);
    }
    m->values[idx] = val;
    m->tags[idx] = JD_TAG_F64;
}

void jdb_map_set_str(JdbMap* m, const char* key, const char* val) {
    if (!m || !key) return;
    int64_t idx = map_find(m, key);
    if (idx < 0) {
        if (m->count == m->capacity) map_grow(m);
        idx = m->count++;
        m->keys[idx] = _strdup(key);
    }
    union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)_strdup(val ? val : "");
    m->values[idx] = u.d;
    m->tags[idx] = JD_TAG_STR;
}

// Store an already-punned f64 value with an explicit tag. Used when the
// setter knows the value is actually a map/array ptr punned as f64 — the
// default f64 setter would stomp the tag back to F64 and lose the
// type identity needed by the tagged getter.
//
// For tag=STR, `val` holds a punned char* (pointer bits). We strdup the
// referenced string so the map owns its copy (matches jdb_map_set_str).
void jdb_map_set_tagged(JdbMap* m, const char* key, double val, int32_t tag) {
    if (!m || !key) return;
    int64_t idx = map_find(m, key);
    if (idx < 0) {
        if (m->count == m->capacity) map_grow(m);
        idx = m->count++;
        m->keys[idx] = _strdup(key);
    }
    if (tag == JD_TAG_STR) {
        union { double d; int64_t i; } u; u.d = val;
        const char* s = (const char*)(intptr_t)u.i;
        union { int64_t i; double d; } out; out.i = (int64_t)(intptr_t)_strdup(s ? s : "");
        m->values[idx] = out.d;
    } else {
        m->values[idx] = val;
    }
    m->tags[idx] = tag;
}

double jdb_map_get_f64(JdbMap* m, const char* key) {
    int64_t idx = map_find(m, key);
    if (idx < 0) return 0;
    // String value: caller asked for an f64, so we refuse rather than
    // pun the pointer bits. Use jdb_map_get_str when the field is a string.
    if (m->tags[idx] == JD_TAG_STR) {
        return 0;
    }
    return m->values[idx];
}

char* jdb_map_get_str(JdbMap* m, const char* key) {
    int64_t idx = map_find(m, key);
    if (idx < 0) return _strdup("");
    if (m->tags[idx] == JD_TAG_STR) {
        union { double d; int64_t i; } u; u.d = m->values[idx];
        const char* s = (const char*)(intptr_t)u.i;
        return _strdup(s ? s : "");
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", m->values[idx]);
    return _strdup(buf);
}

int64_t jdb_map_has(JdbMap* m, const char* key) {
    return map_find(m, key) >= 0 ? 1 : 0;
}

// Used by the codegen for nested-map / array-typed fields. The caller is
// responsible for knowing the real type — the map itself doesn't expose
// per-field tags through this entry point.
void* jdb_map_get_obj(JdbMap* m, const char* key) {
    int64_t idx = map_find(m, key);
    if (idx < 0) return nullptr;
    union { double d; int64_t i; } u; u.d = m->values[idx];
    return (void*)(intptr_t)u.i;
}

// Forward decl — the unified dispatcher that picks between this and
// jdrt_obj_get_tagged based on val_tag.
int32_t jdb_tagged_get(int64_t val_bits, int32_t val_tag, const char* key, int64_t* out_val);

// Tag-aware counterpart of jdb_map_get_* — returns (tag, bits) so the
// caller can handle numbers, strings, and pointer-punned sub-maps/arrays
// without knowing the field's type in advance.
int32_t jdb_map_get_tagged(JdbMap* m, const char* key, int64_t* out_val) {
    *out_val = 0;
    int64_t idx = map_find(m, key);
    if (idx < 0) return 0;
    union { double d; int64_t i; } u;
    u.d = m->values[idx];
    int32_t t = m->tags[idx];
    if (t == JD_TAG_STR) {
        const char* s = (const char*)(intptr_t)u.i;
        *out_val = (int64_t)(intptr_t)_strdup(s ? s : "");
        return JD_TAG_STR;
    }
    *out_val = u.i;
    return (t == JD_TAG_ARR || t == JD_TAG_NATIVE_MAP || t == JD_TAG_VM_HANDLE) ? t : JD_TAG_F64;
}

// String - String → remove all occurrences: "abcabc" - "bc" → "aa"
char* jdb_str_sub(const char* a, const char* b) {
    if (!a) return _strdup("");
    if (!b || !*b) return _strdup(a);
    size_t alen = strlen(a), blen = strlen(b);
    char* out = (char*)malloc(alen + 1);
    size_t oi = 0;
    for (size_t i = 0; i < alen; ) {
        if (i + blen <= alen && memcmp(a + i, b, blen) == 0) {
            i += blen;
        } else {
            out[oi++] = a[i++];
        }
    }
    out[oi] = '\0';
    return out;
}

// ── Generic native vectorization ──────────────────────────────
//
// Native array_apply: apply a scalar fn to each element of an array.
// Used for SIN(arr), ABS(arr), UCASE$(arr), LEFT$(arr, n), etc. instead
// of dispatching via VM bridge. The fn pointer type is encoded by suffix:
//   _ff:  double(double)                 — SIN, COS, ABS, SQR, etc.
//   _ii:  int64(int64)                   — (rare, but e.g. BITNOT)
//   _ss:  char*(const char*)             — UCASE$, LCASE$, TRIM$, REVERSE$
//   _sfi: char*(const char*, int64)      — LEFT$, RIGHT$
//   _sfii: char*(const char*, int64, int64) — MID$
//   _ifs: int64(const char*)             — LEN$, ASC
//   _ffi: double(double, int64)          — ROUND(x, decimals)

typedef double (*fn_ff)(double);
typedef int64_t (*fn_ii)(int64_t);
typedef char* (*fn_ss)(const char*);
typedef char* (*fn_sfi)(const char*, int64_t);
typedef char* (*fn_sfii)(const char*, int64_t, int64_t);
typedef int64_t (*fn_ifs)(const char*);

// Numeric → numeric element-wise. Handles nested arrays recursively.
JdbArray* jdb_array_apply_ff(JdbArray* arr, void* fnp) {
    if (!arr) return jdb_array_new(0);
    fn_ff fn = (fn_ff)fnp;
    auto* r = jdb_array_new(arr->length);
    if (arr->flags & 1) {
        // ptr elements (nested arrays)
        r->flags |= 1;
        for (int64_t i = 0; i < arr->length; i++) {
            union { double d; int64_t i; } u; u.d = arr->data[i];
            JdbArray* inner = (JdbArray*)(intptr_t)u.i;
            JdbArray* res = jdb_array_apply_ff(inner, fnp);
            union { int64_t i; double d; } ur; ur.i = (int64_t)(intptr_t)res;
            r->data[i] = ur.d;
        }
    } else {
        for (int64_t i = 0; i < arr->length; i++)
            r->data[i] = fn(arr->data[i]);
    }
    return r;
}

// String → string element-wise. Array must have bit 1 set (string elems).
JdbArray* jdb_array_apply_ss(JdbArray* arr, void* fnp) {
    if (!arr) return jdb_array_new(0);
    fn_ss fn = (fn_ss)fnp;
    auto* r = jdb_array_new(arr->length);
    r->flags |= 3;  // ptr + string
    bool has_string = (arr->flags & 2) != 0;
    for (int64_t i = 0; i < arr->length; i++) {
        if (has_string) {
            union { double d; int64_t i; } u; u.d = arr->data[i];
            const char* elem = (const char*)(intptr_t)u.i;
            char* res = fn(elem ? elem : "");
            union { int64_t i; double d; } ur; ur.i = (int64_t)(intptr_t)res;
            r->data[i] = ur.d;
        } else {
            // Element is numeric, caller mistake — return empty string
            char* empty = _strdup("");
            union { int64_t i; double d; } ur; ur.i = (int64_t)(intptr_t)empty;
            r->data[i] = ur.d;
        }
    }
    return r;
}

// String → int element-wise (LEN$, ASC).
JdbArray* jdb_array_apply_ifs(JdbArray* arr, void* fnp) {
    if (!arr) return jdb_array_new(0);
    fn_ifs fn = (fn_ifs)fnp;
    auto* r = jdb_array_new(arr->length);
    bool has_string = (arr->flags & 2) != 0;
    for (int64_t i = 0; i < arr->length; i++) {
        if (has_string) {
            union { double d; int64_t i; } u; u.d = arr->data[i];
            const char* elem = (const char*)(intptr_t)u.i;
            r->data[i] = (double)fn(elem ? elem : "");
        } else {
            r->data[i] = 0;
        }
    }
    return r;
}

// String + int → string element-wise (LEFT$, RIGHT$ with scalar count).
JdbArray* jdb_array_apply_sfi(JdbArray* arr, int64_t n, void* fnp) {
    if (!arr) return jdb_array_new(0);
    fn_sfi fn = (fn_sfi)fnp;
    auto* r = jdb_array_new(arr->length);
    r->flags |= 3;
    bool has_string = (arr->flags & 2) != 0;
    for (int64_t i = 0; i < arr->length; i++) {
        if (has_string) {
            union { double d; int64_t i; } u; u.d = arr->data[i];
            const char* elem = (const char*)(intptr_t)u.i;
            char* res = fn(elem ? elem : "", n);
            union { int64_t i; double d; } ur; ur.i = (int64_t)(intptr_t)res;
            r->data[i] = ur.d;
        } else {
            char* empty = _strdup("");
            union { int64_t i; double d; } ur; ur.i = (int64_t)(intptr_t)empty;
            r->data[i] = ur.d;
        }
    }
    return r;
}

// String + int + int → string element-wise (MID$).
JdbArray* jdb_array_apply_sfii(JdbArray* arr, int64_t a, int64_t b, void* fnp) {
    if (!arr) return jdb_array_new(0);
    fn_sfii fn = (fn_sfii)fnp;
    auto* r = jdb_array_new(arr->length);
    r->flags |= 3;
    bool has_string = (arr->flags & 2) != 0;
    for (int64_t i = 0; i < arr->length; i++) {
        if (has_string) {
            union { double d; int64_t i; } u; u.d = arr->data[i];
            const char* elem = (const char*)(intptr_t)u.i;
            char* res = fn(elem ? elem : "", a, b);
            union { int64_t i; double d; } ur; ur.i = (int64_t)(intptr_t)res;
            r->data[i] = ur.d;
        } else {
            char* empty = _strdup("");
            union { int64_t i; double d; } ur; ur.i = (int64_t)(intptr_t)empty;
            r->data[i] = ur.d;
        }
    }
    return r;
}

// double + int → double (ROUND(arr, 2))
JdbArray* jdb_array_apply_ffi(JdbArray* arr, int64_t n, void* fnp) {
    if (!arr) return jdb_array_new(0);
    double (*fn)(double, int64_t) = (double(*)(double, int64_t))fnp;
    auto* r = jdb_array_new(arr->length);
    if (arr->flags & 1) {
        r->flags |= 1;
        for (int64_t i = 0; i < arr->length; i++) {
            union { double d; int64_t i; } u; u.d = arr->data[i];
            JdbArray* inner = (JdbArray*)(intptr_t)u.i;
            JdbArray* res = jdb_array_apply_ffi(inner, n, fnp);
            union { int64_t i; double d; } ur; ur.i = (int64_t)(intptr_t)res;
            r->data[i] = ur.d;
        }
    } else {
        for (int64_t i = 0; i < arr->length; i++)
            r->data[i] = fn(arr->data[i], n);
    }
    return r;
}

// Returns 1 if array flags indicate nested/ptr elements, else 0
int32_t jdb_array_is_nested(JdbArray* arr) {
    return (arr && (arr->flags & 1)) ? 1 : 0;
}

// Print an element of an array: uses flags bits to decide format.
// Bit 0: ptr element. Bit 1: element is a string (vs nested array).
void jdb_print_array_elem(JdbArray* arr, int64_t idx) {
    if (!arr || idx < 0 || idx >= arr->length) return;
    double val = arr->data[idx];
    bool has_ptr = (arr->flags & 1) != 0;
    bool has_string = (arr->flags & 2) != 0;
    if (has_string) {
        // String-flag alone is enough — element is a ptr-encoded char*.
        union { double d; int64_t i; } u; u.d = val;
        const char* s = (const char*)(intptr_t)u.i;
        if (s) printf("%s", s);
    } else if (has_ptr) {
        // Nested array — print as [e0, e1, ...]
        union { double d; int64_t i; } u; u.d = val;
        JdbArray* inner = (JdbArray*)(intptr_t)u.i;
        if (!inner) return;
        printf("[");
        for (int64_t i = 0; i < inner->length; i++) {
            if (i > 0) printf(", ");
            jdb_print_array_elem(inner, i);
        }
        printf("]");
    } else {
        if (val == (int64_t)val)
            printf("%lld", (long long)(int64_t)val);
        else
            printf("%g", val);
    }
}

// Element-wise concat: each string in arr + scalar suffix (or prefix if scalar_left).
// Array elements are ptr-encoded strings (from SPLIT/etc.) or will be encoded.
JdbArray* jdb_array_str_concat(JdbArray* arr, const char* s, int32_t scalar_left) {
    if (!arr || !s) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    r->flags |= 1; // contains ptr elements (strings)
    for (int64_t i = 0; i < arr->length; i++) {
        // Decode element as string ptr
        union { double d; int64_t i; } u; u.d = arr->data[i];
        const char* elem = (const char*)(intptr_t)u.i;
        if (!elem) elem = "";
        size_t ls = strlen(s), le = strlen(elem);
        char* out = (char*)malloc(ls + le + 1);
        if (scalar_left) { memcpy(out, s, ls); memcpy(out + ls, elem, le); }
        else             { memcpy(out, elem, le); memcpy(out + le, s, ls); }
        out[ls + le] = 0;
        union { int64_t i; double d; } ur; ur.i = (int64_t)(intptr_t)out;
        r->data[i] = ur.d;
    }
    return r;
}

// Get array element as a pointer (decodes f64-encoded ptr back).
// Used when array has flags bit 0 set (contains pointers/strings).
void* jdb_array_get_ptr(JdbArray* arr, int64_t idx) {
    if (!arr || idx < 0 || idx >= arr->length) return nullptr;
    union { double d; int64_t i; } u; u.d = arr->data[idx];
    return (void*)(intptr_t)u.i;
}

// LEN that returns shape array for nested (2D+) arrays, otherwise scalar length.
// Mirrors interpreter behavior: LEN(2d_arr) returns [rows, cols, ...].
// For 1D arrays returns a scalar-wrapped value (caller decodes).
// Returns: ptr to JdbArray holding the shape, OR ptr encoding a single i64.
// Caller uses flags to tell the difference: nested=shape array, non-nested=1D scalar wrap.
JdbArray* jdb_array_len_shape(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    // Non-nested: return a single-element array with the length
    if (!(arr->flags & 1)) {
        auto* r = jdb_array_new(1);
        r->data[0] = (double)arr->length;
        return r;
    }
    // Nested: walk structure to build shape
    auto* r = jdb_array_new(0);
    JdbArray* cur = arr;
    while (cur && cur->length > 0) {
        // Append current length
        union { int64_t i; double d; } u; u.i = 0;
        double val = (double)cur->length;
        // grow
        int64_t new_len = r->length + 1;
        double* new_data = (double*)calloc(new_len, sizeof(double));
        for (int64_t i = 0; i < r->length; i++) new_data[i] = r->data[i];
        new_data[r->length] = val;
        free(r->data);
        r->data = new_data;
        r->length = new_len;
        // Descend if nested
        if ((cur->flags & 1) && cur->length > 0) {
            union { double d; int64_t i; } v; v.d = cur->data[0];
            cur = (JdbArray*)(intptr_t)v.i;
        } else {
            cur = nullptr;
        }
    }
    return r;
}

// ── OS.ARGS ─────────────────────────────────────────────────

static int g_argc = 0;
static char** g_argv = nullptr;

void jdb_set_args(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
}

// OS.ARGS() returns a "string array" — each element stores the argv pointer
// as a double (via memcpy). When indexed and passed to VAL/PRINT, the
// pointer is recovered via jdb_array_get_str.
JdbArray* jdb_os_args() {
    auto* arr = jdb_array_new(g_argc);
    for (int i = 0; i < g_argc; i++) {
        // Store pointer as double via memcpy (avoids UB from bitcast)
        double d;
        intptr_t p = (intptr_t)g_argv[i];
        memcpy(&d, &p, sizeof(d));
        arr->data[i] = d;
    }
    return arr;
}

// Get string pointer from an OS.ARGS array element
const char* jdb_array_get_str(JdbArray* arr, int64_t idx) {
    if (!arr || idx < 0 || idx >= arr->length) return "";
    intptr_t p;
    memcpy(&p, &arr->data[idx], sizeof(p));
    return (const char*)p;
}

// VAL for a pointer encoded as double
double jdb_val_ptr(double encoded_ptr) {
    intptr_t p;
    memcpy(&p, &encoded_ptr, sizeof(p));
    const char* s = (const char*)p;
    return atof(s);
}

// ── FORMAT$ ─────────────────────────────────────────────────
// Supports: {} for auto, {:.Nf} for fixed-point, {:d} for integer
// Up to 8 arguments (variadic via double array)

char* jdb_format(const char* fmt, JdbArray* args) {
    char result[4096];
    int rp = 0;
    int arg_idx = 0;
    const char* p = fmt;

    while (*p && rp < 4090) {
        if (*p == '{') {
            if (*(p+1) == '{') { result[rp++] = '{'; p += 2; continue; }
            // Find closing }
            const char* end = strchr(p, '}');
            if (!end) { result[rp++] = *p++; continue; }

            // Extract format spec (between { and })
            char spec[64] = {0};
            int slen = (int)(end - p - 1);
            if (slen > 0 && slen < 63) memcpy(spec, p+1, slen);

            double val = (args && arg_idx < args->length) ? args->data[arg_idx++] : 0.0;

            char tmp[128];
            if (spec[0] == ':' && spec[1] == '.' && spec[strlen(spec)-1] == 'f') {
                int prec = atoi(spec + 2);
                snprintf(tmp, sizeof(tmp), "%.*f", prec, val);
            } else if (spec[0] == ':' && spec[strlen(spec)-1] == 'd') {
                snprintf(tmp, sizeof(tmp), "%lld", (long long)(int64_t)val);
            } else {
                snprintf(tmp, sizeof(tmp), "%g", val);
            }

            int tlen = (int)strlen(tmp);
            if (rp + tlen < 4090) { memcpy(result + rp, tmp, tlen); rp += tlen; }

            p = end + 1;
        } else if (*p == '}' && *(p+1) == '}') {
            result[rp++] = '}'; p += 2;
        } else {
            result[rp++] = *p++;
        }
    }
    result[rp] = '\0';
    return _strdup(result);
}

// Simple FORMAT$ with individual args (up to 4)
char* jdb_format1(const char* fmt, double a1) {
    auto* arr = jdb_array_new(1); arr->data[0] = a1;
    char* r = jdb_format(fmt, arr); free(arr->data); free(arr); return r;
}
char* jdb_format2(const char* fmt, double a1, double a2) {
    auto* arr = jdb_array_new(2); arr->data[0] = a1; arr->data[1] = a2;
    char* r = jdb_format(fmt, arr); free(arr->data); free(arr); return r;
}
char* jdb_format3(const char* fmt, double a1, double a2, double a3) {
    auto* arr = jdb_array_new(3); arr->data[0] = a1; arr->data[1] = a2; arr->data[2] = a3;
    char* r = jdb_format(fmt, arr); free(arr->data); free(arr); return r;
}
char* jdb_format4(const char* fmt, double a1, double a2, double a3, double a4) {
    auto* arr = jdb_array_new(4); arr->data[0] = a1; arr->data[1] = a2; arr->data[2] = a3; arr->data[3] = a4;
    char* r = jdb_format(fmt, arr); free(arr->data); free(arr); return r;
}

// ── String Builtins ─────────────────────────────────────────

// Forward decl: resolved against jdbrt.dll at link time.
// Returns the true byte length of binary strings (with embedded nulls),
// or -1 if the string isn't registered as binary (then use strlen).
#ifdef _WIN32
__declspec(dllimport)
#endif
int64_t jdrt_strlen(const char* s);

int64_t jdb_len_str(const char* s) {
    if (!s) return 0;
    int64_t blen = jdrt_strlen(s);
    if (blen >= 0) return blen;
    return (int64_t)strlen(s);
}

// Strict MID — matches VM's substr-based register_native("MID", ...).
// start past the end is an error so TRY/CATCH can observe it (the
// crash_test relies on this).
char* jdb_mid(const char* s, int64_t start, int64_t length) {
    if (!s) return _strdup("");
    int64_t slen = (int64_t)strlen(s);
    if (start < 0 || start > slen) {
        jdb_err_set("MID: index out of range", 1);
        return _strdup("");
    }
    if (length < 0 || start + length > slen) length = slen - start;
    char* r = (char*)malloc(length + 1);
    memcpy(r, s + start, length);
    r[length] = '\0';
    return r;
}

// Lenient MID$ — matches VM's register_native("MID$", ...). Out-of-range
// start returns an empty string instead of erroring; lots of jdBasic
// programs (dialog wrappers, parsers) rely on this to scan past the
// end of a string without bounds-checking.
char* jdb_mid_lax(const char* s, int64_t start, int64_t length) {
    if (!s) return _strdup("");
    int64_t slen = (int64_t)strlen(s);
    if (start < 0) start = 0;
    if (start > slen) return _strdup("");
    if (length < 0 || start + length > slen) length = slen - start;
    char* r = (char*)malloc(length + 1);
    memcpy(r, s + start, length);
    r[length] = '\0';
    return r;
}

char* jdb_left(const char* s, int64_t n) {
    return jdb_mid(s, 0, n);
}

char* jdb_right(const char* s, int64_t n) {
    if (!s) return _strdup("");
    int64_t slen = (int64_t)strlen(s);
    if (n >= slen) return _strdup(s);
    return jdb_mid(s, slen - n, n);
}

char* jdb_upper(const char* s) {
    if (!s) return _strdup("");
    char* r = _strdup(s);
    for (char* p = r; *p; p++) *p = (char)toupper((unsigned char)*p);
    return r;
}

char* jdb_lower(const char* s) {
    if (!s) return _strdup("");
    char* r = _strdup(s);
    for (char* p = r; *p; p++) *p = (char)tolower((unsigned char)*p);
    return r;
}

char* jdb_trim(const char* s) {
    if (!s) return _strdup("");
    while (*s && isspace((unsigned char)*s)) s++;
    int64_t len = (int64_t)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) len--;
    char* r = (char*)malloc(len + 1);
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

char* jdb_chr(int64_t code) {
    char buf[2] = { (char)code, '\0' };
    return _strdup(buf);
}

int64_t jdb_asc(const char* s) {
    return (s && *s) ? (unsigned char)s[0] : 0;
}

int64_t jdb_instr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return -1;
    const char* p = strstr(haystack, needle);
    return p ? (int64_t)(p - haystack) : -1;
}

char* jdb_replace(const char* src, const char* find, const char* rep) {
    if (!src || !find || !rep) return _strdup(src ? src : "");
    size_t flen = strlen(find), rlen = strlen(rep), slen = strlen(src);
    if (flen == 0) return _strdup(src);
    // Count occurrences
    int count = 0;
    const char* p = src;
    while ((p = strstr(p, find))) { count++; p += flen; }
    // Build result
    size_t nlen = slen + count * (rlen - flen);
    char* r = (char*)malloc(nlen + 1);
    char* rp = r;
    p = src;
    while (*p) {
        if (strncmp(p, find, flen) == 0) {
            memcpy(rp, rep, rlen); rp += rlen; p += flen;
        } else {
            *rp++ = *p++;
        }
    }
    *rp = '\0';
    return r;
}

char* jdb_str(double val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", val);
    return _strdup(buf);
}

char* jdb_space(int64_t n) {
    if (n <= 0) return _strdup("");
    char* r = (char*)malloc(n + 1);
    memset(r, ' ', n);
    r[n] = '\0';
    return r;
}

int64_t jdb_str_eq(const char* a, const char* b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0 ? 1 : 0;
}

int64_t jdb_str_ne(const char* a, const char* b) {
    return !jdb_str_eq(a, b);
}

char* jdb_ltrim(const char* s) {
    if (!s) return _strdup("");
    while (*s && isspace((unsigned char)*s)) s++;
    return _strdup(s);
}

char* jdb_rtrim(const char* s) {
    if (!s) return _strdup("");
    int64_t len = (int64_t)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) len--;
    char* r = (char*)malloc(len + 1);
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

int64_t jdb_startswith(const char* s, const char* prefix) {
    if (!s || !prefix) return 0;
    return strncmp(s, prefix, strlen(prefix)) == 0 ? 1 : 0;
}

int64_t jdb_endswith(const char* s, const char* suffix) {
    if (!s || !suffix) return 0;
    size_t slen = strlen(s), plen = strlen(suffix);
    if (plen > slen) return 0;
    return strcmp(s + slen - plen, suffix) == 0 ? 1 : 0;
}

char* jdb_hex(int64_t val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llX", (unsigned long long)val);
    return _strdup(buf);
}

char* jdb_bin(int64_t val) {
    char buf[66];
    if (val == 0) return _strdup("0");
    int pos = 0;
    uint64_t u = (uint64_t)val;
    // Find highest bit
    int bits = 0;
    for (uint64_t t = u; t; t >>= 1) bits++;
    for (int i = bits - 1; i >= 0; i--)
        buf[pos++] = (u & (1ULL << i)) ? '1' : '0';
    buf[pos] = '\0';
    return _strdup(buf);
}

char* jdb_oct(int64_t val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llo", (unsigned long long)val);
    return _strdup(buf);
}

char* jdb_join_arr(JdbArray* arr, const char* delim) {
    if (!arr || arr->length == 0) return _strdup("");
    bool is_str = (arr->flags & 2) != 0;
    size_t dlen = delim ? strlen(delim) : 0;
    // First pass: compute needed length
    size_t total = 0;
    for (int64_t i = 0; i < arr->length; i++) {
        if (i > 0) total += dlen;
        if (is_str) {
            union { double d; int64_t i; } u; u.d = arr->data[i];
            const char* s = (const char*)(intptr_t)u.i;
            total += s ? strlen(s) : 0;
        } else {
            total += 32;  // upper bound for a formatted number
        }
    }
    char* out = (char*)malloc(total + 1);
    size_t pos = 0;
    for (int64_t i = 0; i < arr->length; i++) {
        if (i > 0 && delim) { memcpy(out + pos, delim, dlen); pos += dlen; }
        if (is_str) {
            union { double d; int64_t i; } u; u.d = arr->data[i];
            const char* s = (const char*)(intptr_t)u.i;
            size_t sl = s ? strlen(s) : 0;
            if (sl) memcpy(out + pos, s, sl);
            pos += sl;
        } else {
            pos += snprintf(out + pos, total + 1 - pos, "%g", arr->data[i]);
        }
    }
    out[pos] = '\0';
    return out;
}

// SPLIT returns array of doubles (each is a pointer-encoded string)
JdbArray* jdb_split(const char* src, const char* delim) {
    if (!src || !delim) return jdb_array_new(0);
    // Count parts
    int count = 1;
    size_t dlen = strlen(delim);
    const char* p = src;
    while ((p = strstr(p, delim))) { count++; p += dlen; }
    auto* arr = jdb_array_new(count);
    p = src;
    for (int i = 0; i < count; i++) {
        const char* next = strstr(p, delim);
        size_t plen = next ? (size_t)(next - p) : strlen(p);
        char* part = (char*)malloc(plen + 1);
        memcpy(part, p, plen);
        part[plen] = '\0';
        // Store pointer as double
        intptr_t ptr = (intptr_t)part;
        memcpy(&arr->data[i], &ptr, sizeof(double));
        p = next ? next + dlen : p + plen;
    }
    return arr;
}

char* jdb_insert_str(const char* target, const char* insert, int64_t pos) {
    if (!target) return _strdup(insert ? insert : "");
    if (!insert) return _strdup(target);
    size_t tlen = strlen(target), ilen = strlen(insert);
    if (pos < 0) pos = 0;
    if (pos > (int64_t)tlen) pos = (int64_t)tlen;
    char* r = (char*)malloc(tlen + ilen + 1);
    memcpy(r, target, pos);
    memcpy(r + pos, insert, ilen);
    memcpy(r + pos + ilen, target + pos, tlen - pos + 1);
    return r;
}

// ── File I/O ────────────────────────────────────────────────

char* jdb_txtreader(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return _strdup("");
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

void jdb_txtwriter(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (f) { fputs(content, f); fclose(f); }
}

void jdb_txtwriter_append(const char* path, const char* content) {
    FILE* f = fopen(path, "ab");
    if (f) { fputs(content, f); fclose(f); }
}

char* jdb_pwd() {
    char buf[4096];
#ifdef _WIN32
    GetCurrentDirectoryA(sizeof(buf), buf);
#else
    getcwd(buf, sizeof(buf));
#endif
    return _strdup(buf);
}

// Return the resulting current directory as a fresh string. The parser
// wraps `CD "..."` in a PRINT to show the path; matching that contract.
char* jdb_cd(const char* path) {
#ifdef _WIN32
    if (path && *path) {
        if (!SetCurrentDirectoryA(path)) {
            jdb_err_set("CD: cannot change directory", 1);
        }
    }
    char buf[MAX_PATH];
    DWORD n = GetCurrentDirectoryA(MAX_PATH, buf);
    return _strdup(n > 0 ? buf : "");
#else
    if (path && *path) {
        if (chdir(path) != 0) {
            jdb_err_set("CD: cannot change directory", 1);
        }
    }
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) buf[0] = 0;
    return strdup(buf);
#endif
}

void jdb_mkdir_native(const char* path) {
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
}

void jdb_kill(const char* path) {
    remove(path);
}

int64_t jdb_file_exists(const char* path) {
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

// ── Date/Time ───────────────────────────────────────────────

// NOW returns current date/time as ISO string for consistency with CVDATE.
char* jdb_now() {
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    return _strdup(buf);
}

// Epoch version retained for legacy callers
double jdb_now_epoch() {
    return (double)time(NULL);
}

char* jdb_date_str(double epoch) {
    time_t t = (time_t)epoch;
    struct tm* tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
    return _strdup(buf);
}

char* jdb_time_str(double epoch) {
    time_t t = (time_t)epoch;
    struct tm* tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    return _strdup(buf);
}

// Date accessors: accept either epoch (f64) or ISO string.
// Since we can't overload in C, we use heuristic: very small values (< 10000)
// are treated as invalid; otherwise treated as epoch. For strings, use
// jdb_year_str etc. (called via string-tagged CVDATE result).
int64_t jdb_year(double epoch) {
    time_t t = (time_t)epoch;
    return localtime(&t)->tm_year + 1900;
}
int64_t jdb_month(double epoch) {
    time_t t = (time_t)epoch;
    return localtime(&t)->tm_mon + 1;
}
int64_t jdb_day(double epoch) {
    time_t t = (time_t)epoch;
    return localtime(&t)->tm_mday;
}
int64_t jdb_hour(double epoch) {
    time_t t = (time_t)epoch;
    return localtime(&t)->tm_hour;
}
int64_t jdb_minute(double epoch) {
    time_t t = (time_t)epoch;
    return localtime(&t)->tm_min;
}
int64_t jdb_second(double epoch) {
    time_t t = (time_t)epoch;
    return localtime(&t)->tm_sec;
}
int64_t jdb_weekday(double epoch) {
    time_t t = (time_t)epoch;
    return localtime(&t)->tm_wday;
}

// String-based date accessors: take ISO date string
int64_t jdb_year_str(const char* s) {
    int y, m, d, hr = 0, mn = 0, sc = 0;
    if (!s || sscanf(s, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hr, &mn, &sc) < 3) return 0;
    return y;
}
int64_t jdb_month_str(const char* s) {
    int y, m, d, hr = 0, mn = 0, sc = 0;
    if (!s || sscanf(s, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hr, &mn, &sc) < 3) return 0;
    return m;
}
int64_t jdb_day_str(const char* s) {
    int y, m, d, hr = 0, mn = 0, sc = 0;
    if (!s || sscanf(s, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hr, &mn, &sc) < 3) return 0;
    return d;
}
int64_t jdb_hour_str(const char* s) {
    int y, m, d, hr = 0, mn = 0, sc = 0;
    if (!s || sscanf(s, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hr, &mn, &sc) < 4) return 0;
    return hr;
}
int64_t jdb_minute_str(const char* s) {
    int y, m, d, hr = 0, mn = 0, sc = 0;
    if (!s || sscanf(s, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hr, &mn, &sc) < 5) return 0;
    return mn;
}
int64_t jdb_second_str(const char* s) {
    int y, m, d, hr = 0, mn = 0, sc = 0;
    if (!s || sscanf(s, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hr, &mn, &sc) < 6) return 0;
    return sc;
}

char* jdb_format_date(double epoch, const char* fmt) {
    time_t t = (time_t)epoch;
    struct tm* tm = localtime(&t);
    char buf[256];
    strftime(buf, sizeof(buf), fmt, tm);
    return _strdup(buf);
}

// ── System ──────────────────────────────────────────────────

char* jdb_getenv(const char* name) {
    const char* val = getenv(name);
    return _strdup(val ? val : "");
}

// IIF (inline if): returns a or b based on condition
double jdb_iif(int64_t cond, double a, double b) {
    return cond ? a : b;
}

// ISNUM, ISSTR, ISARR — type checking (simplified for native)
int64_t jdb_isnum(double val) { (void)val; return 1; }
int64_t jdb_isstr(const char* val) { (void)val; return 1; }

// ── Codec ───────────────────────────────────────────────────

// Base64 encoding
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* jdb_base64_encode(const char* input) {
    if (!input) return _strdup("");
    size_t len = strlen(input);
    size_t olen = 4 * ((len + 2) / 3);
    char* out = (char*)malloc(olen + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t a = (uint8_t)input[i];
        uint32_t b = (i+1 < len) ? (uint8_t)input[i+1] : 0;
        uint32_t c = (i+2 < len) ? (uint8_t)input[i+2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i+1 < len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i+2 < len) ? b64_table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    return out;
}

char* jdb_base64_decode(const char* input) {
    if (!input) return _strdup("");
    size_t len = strlen(input);
    size_t olen = len / 4 * 3;
    char* out = (char*)malloc(olen + 1);
    auto b64_val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 0;
    };
    size_t j = 0;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t a = b64_val(input[i]), b2 = b64_val(input[i+1]);
        uint32_t c = b64_val(input[i+2]), d = b64_val(input[i+3]);
        uint32_t triple = (a << 18) | (b2 << 12) | (c << 6) | d;
        out[j++] = (triple >> 16) & 0xFF;
        if (input[i+2] != '=') out[j++] = (triple >> 8) & 0xFF;
        if (input[i+3] != '=') out[j++] = triple & 0xFF;
    }
    out[j] = '\0';
    return out;
}

char* jdb_uuid() {
    char buf[40];
    auto r4 = []() -> uint16_t { return (uint16_t)(rand() & 0xFFFF); };
    snprintf(buf, sizeof(buf), "%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
        r4(), r4(), r4(), (r4() & 0x0FFF) | 0x4000,
        (r4() & 0x3FFF) | 0x8000, r4(), r4(), r4());
    return _strdup(buf);
}

// ── Date Helpers ────────────────────────────────────────────
// In the native runtime, dates are stored as ISO strings ("YYYY-MM-DD HH:MM:SS")
// to match the interpreter's string-conversion behavior. Functions that receive
// "date" arguments accept ISO strings; functions that return dates produce them.

static bool parse_iso_date(const char* s, struct tm* out) {
    if (!s) return false;
    int y, m, d, hr = 0, mn = 0, sc = 0;
    int n = sscanf(s, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hr, &mn, &sc);
    if (n < 3) return false;
    out->tm_year = y - 1900;
    out->tm_mon = m - 1;
    out->tm_mday = d;
    out->tm_hour = hr;
    out->tm_min = mn;
    out->tm_sec = sc;
    out->tm_isdst = -1;
    return true;
}

static char* format_iso_date(const struct tm* tm, bool include_time) {
    char buf[32];
    if (include_time) {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    }
    return _strdup(buf);
}

// CVDATE: parse ISO string, return normalized ISO string (always with time).
char* jdb_cvdate(const char* datestr) {
    struct tm t = {0};
    if (!parse_iso_date(datestr, &t)) return _strdup("");
    return format_iso_date(&t, true);
}

// CVDATE for numeric input — interpret as Unix epoch seconds and format
// as a local-time ISO string (matches the interpreter's behaviour).
char* jdb_cvdate_num(double epoch_secs) {
    time_t t = (time_t)epoch_secs;
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return format_iso_date(&tm, true);
}

// CVDATE on an array — element-wise. Numeric elements use jdb_cvdate_num,
// existing string elements pass through jdb_cvdate. Returns a string-flagged
// JdbArray so existing print / index paths Just Work.
JdbArray* jdb_cvdate_arr(JdbArray* in) {
    if (!in) return jdb_array_new(0);
    bool src_is_str = (in->flags & 2) != 0;
    auto* out = jdb_array_new(in->length);
    out->flags |= 2;  // marks element storage as string ptrs
    for (int64_t i = 0; i < in->length; i++) {
        char* s;
        if (src_is_str) {
            union { double d; int64_t i; } u; u.d = in->data[i];
            const char* in_s = (const char*)(intptr_t)u.i;
            s = jdb_cvdate(in_s);
        } else {
            s = jdb_cvdate_num(in->data[i]);
        }
        union { double d; int64_t i; } u; u.i = (int64_t)(intptr_t)s;
        out->data[i] = u.d;
    }
    return out;
}

// DATEADD: add num units of part ("Y","M","D","H","N","S") to an ISO date string.
// Returns a new ISO date string.
char* jdb_dateadd(const char* part, double amount, const char* date_str) {
    struct tm tm = {0};
    if (!parse_iso_date(date_str, &tm)) return _strdup("");
    int64_t n = (int64_t)amount;
    char p = part ? toupper((unsigned char)part[0]) : 'D';
    switch (p) {
        case 'Y': tm.tm_year += (int)n; break;
        case 'M': tm.tm_mon  += (int)n; break;
        case 'D': tm.tm_mday += (int)n; break;
        case 'H': tm.tm_hour += (int)n; break;
        case 'N': tm.tm_min  += (int)n; break;
        case 'S': tm.tm_sec  += (int)n; break;
    }
    mktime(&tm);  // normalize
    return format_iso_date(&tm, true);
}

// DATEDIFF: difference between two ISO date strings in units of part.
double jdb_datediff(const char* part, const char* date1, const char* date2) {
    struct tm tm1 = {0}, tm2 = {0};
    if (!parse_iso_date(date1, &tm1) || !parse_iso_date(date2, &tm2)) return 0;
    double diff = difftime(mktime(&tm2), mktime(&tm1));
    char p = part ? toupper((unsigned char)part[0]) : 'S';
    switch (p) {
        case 'S': return diff;
        case 'N': return diff / 60.0;
        case 'H': return diff / 3600.0;
        case 'D': return diff / 86400.0;
    }
    return diff;
}

// Vectorized DATEDIFF: scalar start, array of end-dates (ISO strings).
// Returns a JdbArray of f64 differences.
JdbArray* jdb_datediff_vec(const char* part, const char* date1, JdbArray* dates) {
    if (!dates) return jdb_array_new(0);
    auto* r = jdb_array_new(dates->length);
    for (int64_t i = 0; i < dates->length; i++) {
        // dates->data[i] is a ptr-encoded string
        union { double d; int64_t i; } u; u.d = dates->data[i];
        const char* date2 = (const char*)(intptr_t)u.i;
        r->data[i] = jdb_datediff(part, date1, date2);
    }
    return r;
}

} // end extern "C" — regex functions need C++ linkage internally

// ── Regex (C++ internally, extern "C" interface) ────────────

static int64_t regex_match_impl(const char* text, const char* pattern) {
    try {
        return std::regex_search(std::string(text), std::regex(pattern)) ? 1 : 0;
    } catch (...) { return 0; }
}

static char* regex_replace_impl(const char* text, const char* pattern, const char* replacement) {
    try {
        std::string result = std::regex_replace(std::string(text), std::regex(pattern), std::string(replacement));
        return _strdup(result.c_str());
    } catch (...) { return _strdup(text ? text : ""); }
}

static JdbArray* regex_findall_impl(const char* text, const char* pattern) {
    std::vector<double> positions;
    try {
        std::string s(text);
        std::regex re(pattern);
        auto begin = std::sregex_iterator(s.begin(), s.end(), re);
        auto end2 = std::sregex_iterator();
        for (auto it = begin; it != end2; ++it)
            positions.push_back((double)it->position());
    } catch (...) {}
    auto* arr = jdb_array_new((int64_t)positions.size());
    for (size_t i = 0; i < positions.size(); i++) arr->data[i] = positions[i];
    return arr;
}

extern "C" {
int64_t jdb_regex_match(const char* text, const char* pattern) { return regex_match_impl(text, pattern); }
char* jdb_regex_replace(const char* text, const char* pattern, const char* replacement) { return regex_replace_impl(text, pattern, replacement); }
JdbArray* jdb_regex_findall(const char* text, const char* pattern) { return regex_findall_impl(text, pattern); }

// ── SHA-256 ─────────────────────────────────────────────────
// Minimal self-contained SHA-256 implementation

static uint32_t sha_rotr(uint32_t x, int n) { return (x >> n) | (x << (32-n)); }
static uint32_t sha_ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static uint32_t sha_maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static uint32_t sha_sig0(uint32_t x) { return sha_rotr(x,2)^sha_rotr(x,13)^sha_rotr(x,22); }
static uint32_t sha_sig1(uint32_t x) { return sha_rotr(x,6)^sha_rotr(x,11)^sha_rotr(x,25); }
static uint32_t sha_gam0(uint32_t x) { return sha_rotr(x,7)^sha_rotr(x,18)^(x>>3); }
static uint32_t sha_gam1(uint32_t x) { return sha_rotr(x,17)^sha_rotr(x,19)^(x>>10); }

char* jdb_sha256(const char* input) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};

    size_t len = input ? strlen(input) : 0;
    // Pad message
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    uint8_t* msg = (uint8_t*)calloc(padded_len, 1);
    if (input) memcpy(msg, input, len);
    msg[len] = 0x80;
    uint64_t bits = len * 8;
    for (int i = 0; i < 8; i++) msg[padded_len - 1 - i] = (uint8_t)(bits >> (i * 8));

    // Process blocks
    for (size_t offset = 0; offset < padded_len; offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (msg[offset+i*4]<<24)|(msg[offset+i*4+1]<<16)|(msg[offset+i*4+2]<<8)|msg[offset+i*4+3];
        for (int i = 16; i < 64; i++)
            w[i] = sha_gam1(w[i-2]) + w[i-7] + sha_gam0(w[i-15]) + w[i-16];
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + sha_sig1(e) + sha_ch(e,f,g) + K[i] + w[i];
            uint32_t t2 = sha_sig0(a) + sha_maj(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    free(msg);

    char* result = (char*)malloc(65);
    for (int i = 0; i < 8; i++) snprintf(result + i*8, 9, "%08x", h[i]);
    return result;
}

// TYPEOF on compiled code: the type is usually known at compile time and
// codegen picks the matching stub. NaN on an f64 means the EXITFUNC
// sentinel was returned, which surfaces as "NONE".
char* jdb_typeof_f64(double v) {
    if (v != v) return _strdup("NONE");
    return _strdup("FLOAT64");
}

// Both NATIVE_MAP and VM_HANDLE surface as "OBJECT" — user-facing type
// language doesn't distinguish native-runtime maps from VM Values.
char* jdb_typeof_tag(int64_t tag) {
    switch ((JdTag)tag) {
        case JdTag::I64:        return _strdup("INT64");
        case JdTag::F64:        return _strdup("FLOAT64");
        case JdTag::STR:        return _strdup("STRING");
        case JdTag::ARR:        return _strdup("ARRAY");
        case JdTag::NATIVE_MAP: return _strdup("OBJECT");
        case JdTag::FUNCREF:    return _strdup("FUNCREF");
        case JdTag::VM_HANDLE:  return _strdup("OBJECT");
        case JdTag::RUNTIME:
        default:                return _strdup("UNKNOWN");
    }
}

// ── FRMV$ (format array as string) ──────────────────────────

char* jdb_frmv(JdbArray* arr) {
    if (!arr || arr->length == 0) return _strdup("[]");
    bool is_str = (arr->flags & 2) != 0;
    char buf[8192] = "[";
    int pos = 1;
    for (int64_t i = 0; i < arr->length && pos < 8180; i++) {
        if (i > 0) { buf[pos++] = ','; buf[pos++] = ' '; }
        if (is_str) {
            union { double d; int64_t i; } u; u.d = arr->data[i];
            const char* s = (const char*)(intptr_t)u.i;
            pos += snprintf(buf + pos, 8190 - pos, "%s", s ? s : "");
        } else {
            pos += snprintf(buf + pos, 8190 - pos, "%g", arr->data[i]);
        }
    }
    buf[pos++] = ']'; buf[pos] = '\0';
    return _strdup(buf);
}

// ── PACK$/UNPACK (binary data) ──────────────────────────────

char* jdb_pack(const char* fmt, JdbArray* values) {
    if (!fmt || !values) return _strdup("");
    size_t len = 0;
    // Calculate size
    for (const char* p = fmt; *p; p++) {
        switch (*p) { case 'b': len += 1; break; case 's': len += 2; break;
                      case 'i': case 'f': len += 4; break; case 'd': case 'l': len += 8; break; }
    }
    char* buf = (char*)calloc(len + 1, 1);
    size_t off = 0; int64_t vi = 0;
    for (const char* p = fmt; *p && vi < values->length; p++) {
        int64_t v = (int64_t)values->data[vi++];
        switch (*p) {
            case 'b': buf[off] = (char)v; off += 1; break;
            case 's': *(int16_t*)(buf+off) = (int16_t)v; off += 2; break;
            case 'i': *(int32_t*)(buf+off) = (int32_t)v; off += 4; break;
            case 'l': *(int64_t*)(buf+off) = v; off += 8; break;
            case 'f': *(float*)(buf+off) = (float)values->data[vi-1]; off += 4; break;
            case 'd': *(double*)(buf+off) = values->data[vi-1]; off += 8; break;
        }
    }
    // Return as string (binary data, length tracked by caller)
    char* result = (char*)malloc(len);
    memcpy(result, buf, len);
    free(buf);
    return result;
}

// ── Misc ────────────────────────────────────────────────────

double jdb_cdbl(double x) { return x; }
char* jdb_tostr(double x) { return jdb_str(x); }
double jdb_tonum(const char* s) { return s ? atof(s) : 0.0; }

int64_t jdb_byteat(const char* s, int64_t idx) {
    if (!s || idx < 0 || idx >= (int64_t)strlen(s)) return 0;
    return (uint8_t)s[idx];
}

// OS info
char* jdb_os_getos() {
#ifdef _WIN32
    return _strdup("win32");
#elif __linux__
    return _strdup("linux");
#elif __APPLE__
    return _strdup("macOS");
#else
    return _strdup("unknown");
#endif
}

char* jdb_os_hostname() {
    char buf[256] = {0};
#ifdef _WIN32
    DWORD size = sizeof(buf);
    GetComputerNameA(buf, &size);
#else
    gethostname(buf, sizeof(buf));
#endif
    return _strdup(buf);
}

} // end extern "C" for UDT section

// ── UDT (User-Defined Types) ────────────────────────────────
#include <unordered_map>

struct JdbObject {
    std::unordered_map<std::string, double> num_fields;
    std::unordered_map<std::string, char*> str_fields;
    char* type_name;
};

extern "C" {

JdbObject* jdb_udt_new(const char* type_name) {
    auto* obj = new JdbObject();
    obj->type_name = _strdup(type_name ? type_name : "");
    return obj;
}

void jdb_udt_set_f64(JdbObject* obj, const char* field, double val) {
    if (obj) obj->num_fields[field] = val;
}

double jdb_udt_get_f64(JdbObject* obj, const char* field) {
    if (obj) {
        auto it = obj->num_fields.find(field);
        if (it != obj->num_fields.end()) return it->second;
    }
    return 0.0;
}

void jdb_udt_set_str(JdbObject* obj, const char* field, const char* val) {
    if (obj) obj->str_fields[field] = _strdup(val ? val : "");
}

const char* jdb_udt_get_str(JdbObject* obj, const char* field) {
    if (obj && field) {
        if (strcmp(field, "__TYPE__") == 0) {
            // Return uppercase copy of the stored type name (interpreter-compat).
            const char* tn = obj->type_name ? obj->type_name : "";
            size_t n = strlen(tn);
            char* out = (char*)malloc(n + 1);
            for (size_t i = 0; i < n; i++) {
                char c = tn[i];
                out[i] = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
            }
            out[n] = '\0';
            return out;
        }
        auto it = obj->str_fields.find(field);
        if (it != obj->str_fields.end()) return it->second;
    }
    return "";
}

// ── Higher-order Array Functions (with native function pointers) ─

// Function pointer type: double fn(double)
typedef double (*JdbMapFn)(double);
typedef double (*JdbReduceFn)(double, double);

JdbArray* jdb_select_fn(JdbMapFn fn, JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    for (int64_t i = 0; i < arr->length; i++)
        r->data[i] = fn(arr->data[i]);
    return r;
}

JdbArray* jdb_filter_fn(JdbMapFn fn, JdbArray* arr) {
    // Predicate returns double: non-zero = true
    if (!arr) return jdb_array_new(0);
    int64_t count = 0;
    for (int64_t i = 0; i < arr->length; i++)
        if (fn(arr->data[i]) != 0.0) count++;
    auto* r = jdb_array_new(count);
    int64_t j = 0;
    for (int64_t i = 0; i < arr->length; i++)
        if (fn(arr->data[i]) != 0.0) r->data[j++] = arr->data[i];
    return r;
}

double jdb_reduce_fn(JdbReduceFn fn, JdbArray* arr, double init) {
    if (!arr) return init;
    double acc = init;
    for (int64_t i = 0; i < arr->length; i++)
        acc = fn(acc, arr->data[i]);
    return acc;
}

} // extern "C"

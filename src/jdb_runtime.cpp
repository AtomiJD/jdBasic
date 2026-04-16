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
void jdb_trace(int64_t line) {
    fprintf(stderr, "[TRACE] line %lld\n", (long long)line);
    fflush(stderr);
}

void jdb_print_space() {
    printf(" ");
}

// ── String operations ───────────────────────────────────────

char* jdb_str_concat(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    char* r = (char*)malloc(la + lb + 1);
    memcpy(r, a, la);
    memcpy(r + la, b, lb + 1);
    return r;
}

char* jdb_int_to_str(int64_t val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)val);
    return _strdup(buf);
}

char* jdb_double_to_str(double val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", val);
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

int64_t jdb_len_str(const char* s) {
    return s ? (int64_t)strlen(s) : 0;
}

char* jdb_mid(const char* s, int64_t start, int64_t length) {
    if (!s) return _strdup("");
    int64_t slen = (int64_t)strlen(s);
    if (start < 0) start = 0;
    if (start >= slen) return _strdup("");
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
    // Join numeric array as strings
    char buf[4096] = {0};
    int pos = 0;
    for (int64_t i = 0; i < arr->length && pos < 4080; i++) {
        if (i > 0 && delim) { int dl = (int)strlen(delim); memcpy(buf+pos, delim, dl); pos += dl; }
        pos += snprintf(buf+pos, 4090-pos, "%g", arr->data[i]);
    }
    return _strdup(buf);
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

void jdb_cd(const char* path) {
#ifdef _WIN32
    SetCurrentDirectoryA(path);
#else
    chdir(path);
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

double jdb_now() {
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

// ── Date Add/Diff ───────────────────────────────────────────

double jdb_dateadd(const char* part, double amount, double epoch) {
    time_t t = (time_t)epoch;
    struct tm* tm = localtime(&t);
    if (!tm) return epoch;
    struct tm copy = *tm;
    int64_t n = (int64_t)amount;
    char p = part ? toupper((unsigned char)part[0]) : 'D';
    switch (p) {
        case 'Y': copy.tm_year += (int)n; break;
        case 'M': copy.tm_mon += (int)n; break;
        case 'D': copy.tm_mday += (int)n; break;
        case 'H': copy.tm_hour += (int)n; break;
        case 'N': copy.tm_min += (int)n; break;  // N=minutes
        case 'S': copy.tm_sec += (int)n; break;
    }
    return (double)mktime(&copy);
}

double jdb_datediff(const char* part, double epoch1, double epoch2) {
    double diff = epoch2 - epoch1;
    char p = part ? toupper((unsigned char)part[0]) : 'S';
    switch (p) {
        case 'S': return diff;
        case 'N': return diff / 60.0;
        case 'H': return diff / 3600.0;
        case 'D': return diff / 86400.0;
    }
    return diff;
}

double jdb_cvdate(const char* datestr) {
    // Parse YYYY-MM-DD [HH:MM:SS]
    struct tm t = {0};
    if (datestr) {
        int y, m, d, hr = 0, mn = 0, sc = 0;
        if (sscanf(datestr, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hr, &mn, &sc) >= 3) {
            t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
            t.tm_hour = hr; t.tm_min = mn; t.tm_sec = sc;
        }
    }
    return (double)mktime(&t);
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

// ── TYPEOF ──────────────────────────────────────────────────
// In native compiled code, types are known at compile time.
// These are placeholder functions called with a type tag.
char* jdb_typeof_tag(int64_t tag) {
    switch (tag) {
        case 0: return _strdup("INT64");
        case 1: return _strdup("FLOAT64");
        case 2: return _strdup("STRING");
        case 3: return _strdup("ARRAY");
        default: return _strdup("UNKNOWN");
    }
}

// ── FRMV$ (format array as string) ──────────────────────────

char* jdb_frmv(JdbArray* arr) {
    if (!arr || arr->length == 0) return _strdup("[]");
    char buf[8192] = "[";
    int pos = 1;
    for (int64_t i = 0; i < arr->length && pos < 8180; i++) {
        if (i > 0) { buf[pos++] = ','; buf[pos++] = ' '; }
        pos += snprintf(buf + pos, 8190 - pos, "%g", arr->data[i]);
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
    if (obj) {
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

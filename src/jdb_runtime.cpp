// jdb_runtime.cpp - C runtime for jdBasic native-compiled executables.
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
#include "jdb_encoding.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
// POSIX has strdup; MSVC provides the underscore-prefixed _strdup. Map one
// to the other so the call sites don't need a forest of #ifdefs.
#define _strdup strdup
#endif

static auto g_start_time = std::chrono::steady_clock::now();

// Forward declare JdbArray for use in C++ functions
struct JdbArray;
extern "C" JdbArray* jdb_array_new(int64_t size);

extern "C" {

static inline JdbArray* decode_inner(double val);
static void flatten_into(JdbArray* arr, std::vector<double>& out);

// ── I/O ─────────────────────────────────────────────────────

void jdb_print_int(int64_t val) {
    printf("%lld", (long long)val);
}

// Format a double the way the interpreter's value.h::format_float_locale
// does: %.6f then trim trailing zeros and a trailing decimal point. This
// keeps interp/native PRINT outputs aligned (1e8 → "100000000", 1e-10 → "0",
// 1e20 → "100000000000000000000", 1.5 → "1.5").
static int jdb_format_double(char* out, int cap, double val) {
    int n = snprintf(out, cap, "%.6f", val);
    if (n <= 0 || n >= cap) return n;
    // Strip trailing zeros (only inside / after the fractional part).
    int dot = -1;
    for (int i = 0; i < n; i++) if (out[i] == '.') { dot = i; break; }
    if (dot >= 0) {
        int end = n - 1;
        while (end > dot && out[end] == '0') end--;
        if (end == dot) end--;  // drop the dot itself
        out[end + 1] = '\0';
        n = end + 1;
    }
    return n;
}

void jdb_print_double(double val) {
    char buf[64];
    jdb_format_double(buf, sizeof(buf), val);
    fputs(buf, stdout);
}

void jdb_print_str(const char* val) {
    printf("%s", val);
}

void jdb_print_bool(int64_t val) {
    printf("%s", val ? "TRUE" : "FALSE");
}

void jdb_print_nl() {
    printf("\n");
    fflush(stdout);
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

// Raw err_code - used by the per-stmt propagation check in codegen,
// which MUST read only the live value or it would loop forever on a
// caught error (the shadow would keep it non-zero after soft-clear).
int64_t jdb_err_code() {
    return g_err_code;
}

// User-visible err_code - falls back to the shadow so a catch body
// sees the caught code even after the soft-clear has run.
int64_t jdb_err_code_visible() {
    return g_err_code ? g_err_code : g_last_code;
}

// Called when a THROW escapes all TRY handlers - mirrors the
// interpreter's unhandled exception behavior.
void jdb_throw_uncaught() {
    fprintf(stderr, "Unhandled exception: %s\n", g_err_msg);
    fflush(stderr);
    exit(1);
}

// Recursion guard - each user FUNC/SUB bumps a thread-local depth
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
// Optional build-time features (HTTP, GFX, IMGUI, ...) reflect the
// macros passed when compiling this DLL via build_rt.bat.
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
#ifdef HTTP
    if (strcmp(up, "HTTP")        == 0) return 1;
#endif
#ifdef COM
    if (strcmp(up, "COM")         == 0) return 1;
#endif
#ifdef GFX
    if (strcmp(up, "GFX")         == 0) return 1;
#endif
#ifdef IMGUI
    if (strcmp(up, "IMGUI")       == 0) return 1;
#endif
#ifdef USE_SERIAL
    if (strcmp(up, "SERIAL")      == 0) return 1;
#endif
#ifdef LLM
    if (strcmp(up, "LLM")         == 0) return 1;
#endif
#ifdef ONNX
    if (strcmp(up, "ONNX")        == 0) return 1;
#endif
#ifdef SQLITE
    if (strcmp(up, "SQLITE")      == 0) return 1;
#endif
#ifdef PYTHON
    if (strcmp(up, "PYTHON")      == 0) return 1;
#endif
    return 0;
}

// ── String operations ───────────────────────────────────────

// Forward decl: resolved against jdbrt.dll at link time.
// Returns the true byte length of binary strings (with embedded nulls),
// or -1 if the string isn't registered as binary (then use strlen).
#ifdef _WIN32
__declspec(dllimport)
#endif
int64_t jdrt_strlen(const char* s);

#ifdef _WIN32
__declspec(dllimport)
#endif
void jdrt_register_binary(const char* s, int64_t n);

// Honours the binary-length registry so buffers carrying embedded 0x00
// (CHR$(0), BINREADER$ / PACK$ content and anything sliced out of them)
// report their real size instead of stopping at the first NUL.
static inline int64_t jdb_str_blen(const char* s) {
    if (!s) return 0;
    int64_t blen = jdrt_strlen(s);
    if (blen >= 0) return blen;
    return (int64_t)strlen(s);
}

char* jdb_str_concat(const char* a, const char* b) {
    int64_t la = jdb_str_blen(a), lb = jdb_str_blen(b);
    char* r = (char*)malloc((size_t)(la + lb + 1));
    if (la) memcpy(r, a, (size_t)la);
    if (lb) memcpy(r + la, b, (size_t)lb);
    r[la + lb] = '\0';
    // Carry the length forward when the result holds an interior NUL, so the
    // next concat, LEN or BINWRITER sees all of it rather than the strlen
    // prefix. This is what lets a RIFF header be built with CHR$(0).
    if (la + lb != (int64_t)strlen(r)) jdrt_register_binary(r, la + lb);
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
double jdb_random2(double lo, double hi) { return lo + (double)rand() / RAND_MAX * (hi - lo); }

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
    int32_t flags;       // bit 0: elements are nested array ptrs (2D+)
                         // bit 1: elements are string ptrs
                         // bit 2: elements are bool (TRUE/FALSE rendering)
                         // bit 3: per-element tags array present (elem_tags)
    int8_t* elem_tags;   // optional per-element JdTag (NULL when not used).
                         // Allocated only by jdb_array_append_tagged so the
                         // common no-tags case stays cheap.
};

JdbArray* jdb_array_new(int64_t size) {
    auto* arr = (JdbArray*)malloc(sizeof(JdbArray));
    arr->data = (double*)calloc(size, sizeof(double));
    arr->length = size;
    arr->flags = 0;
    arr->elem_tags = nullptr;
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

// Fancy/vector indexing: arr[indices] returns a new array with the
// elements of arr at the positions given by indices (each position
// is interpreted as an int64). Preserves arr's flag bits so nested-
// pointer arrays stay flagged on the gathered result.
JdbArray* jdb_array_gather(JdbArray* arr, JdbArray* idx) {
    if (!arr || !idx) return jdb_array_new(0);
    auto* r = jdb_array_new(idx->length);
    for (int64_t i = 0; i < idx->length; i++) {
        union { double d; int64_t v; } u; u.d = idx->data[i];
        // Indices arrive as plain f64s (IOTA, INT(...), arithmetic
        // results); double→int64 conversion is the right read here.
        int64_t k = (int64_t)idx->data[i]; (void)u;
        if (k >= 0 && k < arr->length) r->data[i] = arr->data[k];
        else                            r->data[i] = 0.0;
    }
    r->flags = arr->flags;
    return r;
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

// IOTA(n, start, step) - n elements starting at `start` stepping by `step`.
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

// Statistics walk leaves, not cells: on a matrix the top-level cells are
// inner-array pointers, and summing those bits as numbers is meaningless.
double jdb_mean(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    std::vector<double> flat;
    flatten_into(arr, flat);
    if (flat.empty()) return 0.0;
    double sum = 0.0;
    for (double d : flat) sum += d;
    return sum / (double)flat.size();
}

double jdb_stdev(JdbArray* arr) {
    if (!arr || arr->length <= 1) return 0.0;
    std::vector<double> flat;
    flatten_into(arr, flat);
    if (flat.size() < 2) return 0.0;
    double avg = 0.0;
    for (double d : flat) avg += d;
    avg /= (double)flat.size();
    double sum_sq = 0.0;
    for (double d : flat) { double x = d - avg; sum_sq += x * x; }
    return sqrt(sum_sq / (double)flat.size());  // population stdev (matches VM)
}

// ── Array operations ────────────────────────────────────────

double jdb_array_sum(JdbArray* arr) {
    if (!arr) return 0.0;
    double s = 0.0;
    if (arr->flags & 1) {
        for (int64_t i = 0; i < arr->length; i++)
            s += jdb_array_sum(decode_inner(arr->data[i]));
    } else {
        for (int64_t i = 0; i < arr->length; i++) s += arr->data[i];
    }
    return s;
}

// Reduce a matrix along one axis. dim 0 walks down the rows and yields one
// value per column; dim 1 walks across each row. Mirrors reduce_along_axis
// in the interpreter; the op codes match JdbReduceOp there.
//   0 sum  1 min  2 max  3 product  4 mean
//   5 median  6 variance  7 stdev  8 any  9 all
JdbArray* jdb_array_reduce_axis(JdbArray* arr, int64_t dim, int32_t op) {
    if (!arr || arr->length == 0 || !(arr->flags & 1)) return jdb_array_new(0);
    int64_t rows = arr->length;
    int64_t cols = 0;
    for (int64_t r = 0; r < rows; r++) {
        JdbArray* row = decode_inner(arr->data[r]);
        if (row && row->length > cols) cols = row->length;
    }
    auto cell = [&](int64_t r, int64_t c) -> double {
        JdbArray* row = decode_inner(arr->data[r]);
        return (row && c < row->length) ? row->data[c] : 0.0;
    };
    auto reduce_lane = [&](const std::vector<double>& v) -> double {
        if (v.empty()) return 0.0;
        switch (op) {
            case 1: { double m = v[0]; for (double d : v) if (d < m) m = d; return m; }
            case 2: { double m = v[0]; for (double d : v) if (d > m) m = d; return m; }
            case 3: { double s = 1.0; for (double d : v) s *= d; return s; }
            case 8: { for (double d : v) if (d != 0.0) return 1.0; return 0.0; }
            case 9: { for (double d : v) if (d == 0.0) return 0.0; return 1.0; }
            case 5: {
                std::vector<double> s = v;
                std::sort(s.begin(), s.end());
                size_t n = s.size();
                return (n % 2) ? s[n/2] : (s[n/2-1] + s[n/2]) / 2.0;
            }
            default: break;
        }
        double sum = 0.0;
        for (double d : v) sum += d;
        if (op == 0) return sum;
        double mean = sum / (double)v.size();
        if (op == 4) return mean;
        if (v.size() < 2) return 0.0;
        double ss = 0.0;
        for (double d : v) { double x = d - mean; ss += x * x; }
        double var = ss / (double)v.size();
        return (op == 7) ? sqrt(var) : var;
    };
    int64_t out_len = (dim == 0) ? cols : rows;
    int64_t lane_len = (dim == 0) ? rows : cols;
    auto* out = jdb_array_new(out_len);
    std::vector<double> lane;
    for (int64_t i = 0; i < out_len; i++) {
        lane.clear();
        for (int64_t k = 0; k < lane_len; k++)
            lane.push_back((dim == 0) ? cell(k, i) : cell(i, k));
        out->data[i] = reduce_lane(lane);
    }
    if (op == 8 || op == 9) out->flags |= 4;  // render as TRUE/FALSE
    return out;
}

double jdb_array_product(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    double s = 1.0;
    if (arr->flags & 1) {
        for (int64_t i = 0; i < arr->length; i++)
            s *= jdb_array_product(decode_inner(arr->data[i]));
    } else {
        for (int64_t i = 0; i < arr->length; i++) s *= arr->data[i];
    }
    return s;
}

double jdb_array_min(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    if (arr->flags & 1) {
        double m = jdb_array_min(decode_inner(arr->data[0]));
        for (int64_t i = 1; i < arr->length; i++) {
            double v = jdb_array_min(decode_inner(arr->data[i]));
            if (v < m) m = v;
        }
        return m;
    }
    double m = arr->data[0];
    for (int64_t i = 1; i < arr->length; i++) if (arr->data[i] < m) m = arr->data[i];
    return m;
}

double jdb_array_max(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    if (arr->flags & 1) {
        double m = jdb_array_max(decode_inner(arr->data[0]));
        for (int64_t i = 1; i < arr->length; i++) {
            double v = jdb_array_max(decode_inner(arr->data[i]));
            if (v > m) m = v;
        }
        return m;
    }
    double m = arr->data[0];
    for (int64_t i = 1; i < arr->length; i++) if (arr->data[i] > m) m = arr->data[i];
    return m;
}

int64_t jdb_array_any(JdbArray* arr) {
    if (!arr) return 0;
    if (arr->flags & 1) {
        for (int64_t i = 0; i < arr->length; i++)
            if (jdb_array_any(decode_inner(arr->data[i]))) return 1;
        return 0;
    }
    for (int64_t i = 0; i < arr->length; i++) if (arr->data[i] != 0.0) return 1;
    return 0;
}

// Dot product on flat double buffers - bypasses the bridge's
// jdbarray_to_value conversion which was making native DOT 13× slower
// than the interpreter (the conversion alone allocates 2*N Value's
// before the loop even starts). Bench at 4M elements: 91ms → ~7ms.
double jdb_array_dot(JdbArray* a, JdbArray* b) {
    if (!a || !b) return 0.0;
    int64_t n = a->length < b->length ? a->length : b->length;
    double s = 0.0;
    for (int64_t i = 0; i < n; i++) s += a->data[i] * b->data[i];
    return s;
}

// Per-function array specializations for unary math. The previous path
// (jdb_array_apply_ff with a function-pointer callback) prevented
// inlining and SIMD vectorisation. Baking the scalar function in lets
// the compiler inline std::sin / std::exp / etc. and emit a tight loop
// the optimiser can vectorise on /O2 (or better with /fp:fast or
// /arch:AVX2). The flags-bit-0 (nested array) case still recurses,
// matching apply_ff's behaviour.
#define JDB_ARRAY_FF(NAME, SCALAR_FN)                                    \
    JdbArray* jdb_array_##NAME(JdbArray* arr) {                          \
        if (!arr) return jdb_array_new(0);                               \
        auto* r = jdb_array_new(arr->length);                            \
        if (arr->flags & 1) {                                            \
            r->flags |= 1;                                               \
            for (int64_t i = 0; i < arr->length; i++) {                  \
                union { double d; int64_t i; } u; u.d = arr->data[i];    \
                JdbArray* inner = (JdbArray*)(intptr_t)u.i;              \
                JdbArray* res = jdb_array_##NAME(inner);                 \
                union { int64_t i; double d; } ur;                       \
                ur.i = (int64_t)(intptr_t)res;                           \
                r->data[i] = ur.d;                                       \
            }                                                            \
        } else {                                                         \
            for (int64_t i = 0; i < arr->length; i++)                    \
                r->data[i] = SCALAR_FN(arr->data[i]);                    \
        }                                                                \
        return r;                                                        \
    }

JDB_ARRAY_FF(sin,   sin)
JDB_ARRAY_FF(cos,   cos)
JDB_ARRAY_FF(tan,   tan)
JDB_ARRAY_FF(asin,  asin)
JDB_ARRAY_FF(acos,  acos)
JDB_ARRAY_FF(atan,  atan)
JDB_ARRAY_FF(sinh,  sinh)
JDB_ARRAY_FF(cosh,  cosh)
JDB_ARRAY_FF(tanh,  tanh)
JDB_ARRAY_FF(exp,   exp)
JDB_ARRAY_FF(log,   log)
JDB_ARRAY_FF(log10, log10)
JDB_ARRAY_FF(sqr,   sqrt)        // SQR is sqrt in jdBasic
JDB_ARRAY_FF(abs,   fabs)
JDB_ARRAY_FF(floor, floor)
JDB_ARRAY_FF(ceil,  ceil)
JDB_ARRAY_FF(round, round)
JDB_ARRAY_FF(trunc, trunc)

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
    r->flags = arr->flags;  // preserve string-elem / nested-arr dispatch
    return r;
}

JdbArray* jdb_array_sort(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    memcpy(r->data, arr->data, arr->length * sizeof(double));
    // Simple insertion sort. NB: when called on a string-array the sort
    // key becomes the punned-pointer bits, which is effectively random.
    // Codegen should route string sorts to a dedicated _str variant when
    // we add one - for now we still preserve the flag so reads after the
    // sort still see strings.
    for (int64_t i = 1; i < r->length; i++) {
        double key = r->data[i];
        int64_t j = i - 1;
        while (j >= 0 && r->data[j] > key) { r->data[j+1] = r->data[j]; j--; }
        r->data[j+1] = key;
    }
    r->flags = arr->flags;
    return r;
}

JdbArray* jdb_array_append(JdbArray* arr, double val) {
    int64_t newlen = arr ? arr->length + 1 : 1;
    auto* r = jdb_array_new(newlen);
    if (arr) memcpy(r->data, arr->data, arr->length * sizeof(double));
    r->data[newlen - 1] = val;
    if (arr) {
        r->flags = arr->flags;  // preserve element-type marker
        if (arr->elem_tags) {
            r->elem_tags = (int8_t*)malloc(newlen);
            memcpy(r->elem_tags, arr->elem_tags, arr->length);
            r->elem_tags[newlen - 1] = 0;  // unknown / inherit-from-flags
        }
    }
    return r;
}

// Append a value carrying its own JdTag. Allocates the per-element tags
// array on first tagged append and sets the JD_ARR_FLAG_TAGGED bit.
// Used by ARRAY_LITERAL codegen when an element is RUNTIME-tagged
// (e.g. `[m{"name"}, m{"age"}, m{"email"}]` from map-of-mixed-types) -
// without per-cell tags the consumer can't tell strings from numbers.
JdbArray* jdb_array_append_tagged(JdbArray* arr, double val, int32_t tag) {
    int64_t newlen = arr ? arr->length + 1 : 1;
    auto* r = jdb_array_new(newlen);
    if (arr) memcpy(r->data, arr->data, arr->length * sizeof(double));
    r->data[newlen - 1] = val;
    int32_t base_flags = arr ? arr->flags : 0;
    r->flags = base_flags | 8;  // bit 3: tagged elements present
    r->elem_tags = (int8_t*)malloc(newlen);
    if (arr && arr->elem_tags)
        memcpy(r->elem_tags, arr->elem_tags, arr->length);
    else
        memset(r->elem_tags, 0, arr ? arr->length : 0);
    r->elem_tags[newlen - 1] = (int8_t)tag;
    return r;
}

// Read element + its per-cell JdTag. If no tags array, falls back to the
// existing classifier so callers always get a sensible tag.
double jdb_array_get_tagged(JdbArray* arr, int64_t idx, int32_t* out_tag) {
    if (!arr || idx < 0 || idx >= arr->length) {
        if (out_tag) *out_tag = 1;  // F64
        return 0.0;
    }
    double v = arr->data[idx];
    if (out_tag) {
        if (arr->elem_tags && (arr->flags & 8)) {
            *out_tag = (int32_t)arr->elem_tags[idx];
        } else {
            // Fall back to flag-based / heuristic classifier.
            extern int32_t jdb_array_classify_elem(JdbArray*, double);
            *out_tag = jdb_array_classify_elem(arr, v);
        }
    }
    return v;
}

JdbArray* jdb_array_append_arr(JdbArray* a, JdbArray* b) {
    int64_t alen = a ? a->length : 0;
    int64_t blen = b ? b->length : 0;
    auto* r = jdb_array_new(alen + blen);
    if (a) memcpy(r->data,        a->data, alen * sizeof(double));
    if (b) memcpy(r->data + alen, b->data, blen * sizeof(double));
    // Propagate the per-element-type flags. Either input being a string-
    // array (or nested-array) marks the merged result the same way, so
    // PRINT / INDEX paths that consult the runtime flag still see strings.
    int32_t fa = a ? a->flags : 0;
    int32_t fb = b ? b->flags : 0;
    r->flags |= (fa | fb);
    return r;
}

// FILLV: bulk in-place fill of a numeric array. memset-speed via std::fill_n
// over the underlying double buffer. For nested arrays (flag bit 0 set),
// recursively fills each inner array. Returns the same array for chaining.
JdbArray* jdb_array_fillv(JdbArray* arr, double val) {
    if (!arr) return nullptr;
    if (arr->flags & 1) {
        for (int64_t i = 0; i < arr->length; i++)
            jdb_array_fillv(decode_inner(arr->data[i]), val);
    } else if (arr->length > 0) {
        std::fill_n(arr->data, arr->length, val);
    }
    return arr;
}
// (no fprintf - keep runtime quiet)

// COPYV helper: flatten src leaves into a flat double vector.
static void copyv_flatten(JdbArray* arr, std::vector<double>& out) {
    if (!arr) return;
    if (arr->flags & 1) {
        for (int64_t i = 0; i < arr->length; i++)
            copyv_flatten(decode_inner(arr->data[i]), out);
    } else {
        out.insert(out.end(), arr->data, arr->data + arr->length);
    }
}

// COPYV helper: walk dst leaves and assign cyclically from flat src vector.
static void copyv_assign(JdbArray* arr, const std::vector<double>& src, size_t& si) {
    if (!arr) return;
    if (arr->flags & 1) {
        for (int64_t i = 0; i < arr->length; i++)
            copyv_assign(decode_inner(arr->data[i]), src, si);
    } else {
        for (int64_t i = 0; i < arr->length; i++) {
            arr->data[i] = src[si % src.size()];
            si++;
        }
    }
}

// COPYV: bulk in-place copy from src into dst with cyclic broadcast if
// shapes differ. Memcpy fast-path when both are flat with matching lengths;
// otherwise recursive descent into nested structures. Never throws on size
// mismatch - broadcasts/truncates/cycles instead.
JdbArray* jdb_array_copyv(JdbArray* dst, JdbArray* src) {
    if (!dst) return nullptr;
    if (!src) return dst;
    // Fast path: both flat with matching lengths → single memcpy.
    if (!(dst->flags & 1) && !(src->flags & 1) &&
        dst->length == src->length && dst->length > 0) {
        std::memcpy(dst->data, src->data, dst->length * sizeof(double));
        return dst;
    }
    // General path: flatten src into a leaf-vector, then cyclic-assign.
    std::vector<double> src_flat;
    copyv_flatten(src, src_flat);
    if (src_flat.empty()) return dst;
    size_t si = 0;
    copyv_assign(dst, src_flat, si);
    return dst;
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

// Forward declaration - actual definition follows below.
extern "C" JdbArray* jdb_array_unique_str(JdbArray* arr);

JdbArray* jdb_array_unique(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    // Runtime fallback: if codegen didn't statically route to _str (e.g.
    // UNIQUE(APPEND(REVERSE(arr), ...)) - args[0] is a CALL chain, not a
    // VARIABLE in string_array_vars), the array's per-cell-type flag still
    // tells us this is a string array. Delegate so dedupe uses strcmp.
    if (arr->flags & 2) return jdb_array_unique_str(arr);
    auto* r = jdb_array_new(arr->length);
    int64_t n = 0;
    for (int64_t i = 0; i < arr->length; i++) {
        bool found = false;
        for (int64_t j = 0; j < n; j++) if (r->data[j] == arr->data[i]) { found = true; break; }
        if (!found) r->data[n++] = arr->data[i];
    }
    r->length = n;
    r->flags = arr->flags;
    return r;
}

// String-array UNIQUE: each cell carries an i8* in punned-f64 bits.
// Numeric UNIQUE only catches pointer-identity duplicates; this one
// dedupes by string content (strcmp). Codegen routes here when the
// argument is a known string-array (string_array_vars membership).
JdbArray* jdb_array_unique_str(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    int64_t n = 0;
    for (int64_t i = 0; i < arr->length; i++) {
        union { double d; intptr_t p; } u; u.d = arr->data[i];
        const char* si = (const char*)u.p;
        if (!si) si = "";
        bool found = false;
        for (int64_t j = 0; j < n; j++) {
            union { double d; intptr_t p; } v; v.d = r->data[j];
            const char* sj = (const char*)v.p;
            if (!sj) sj = "";
            if (std::strcmp(si, sj) == 0) { found = true; break; }
        }
        if (!found) r->data[n++] = arr->data[i];
    }
    r->length = n;
    r->flags |= 3;  // string elements - preserve dispatch info
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
    if (!arr || n == 0) return jdb_array_new(0);
    if (n < 0) {
        int64_t k = -n;
        if (k > arr->length) k = arr->length;
        auto* r = jdb_array_new(k);
        memcpy(r->data, arr->data + (arr->length - k), k * sizeof(double));
        r->flags = arr->flags;
        return r;
    }
    if (n > arr->length) n = arr->length;
    auto* r = jdb_array_new(n);
    memcpy(r->data, arr->data, n * sizeof(double));
    r->flags = arr->flags;  // preserve nested/string flags
    return r;
}

// (n, arr) parameter order to match the interpreter's TAKE(n, arr).
// Native codegen's TAKE entry points here so user-order args don't get
// swapped silently when the optimizer skips the VM bridge.
JdbArray* jdb_take_n(int64_t n, JdbArray* arr) {
    return jdb_array_take(arr, n);
}

JdbArray* jdb_array_drop(JdbArray* arr, int64_t n) {
    if (!arr) return jdb_array_new(0);
    if (n < 0) {
        int64_t k = -n;
        if (k >= arr->length) return jdb_array_new(0);
        int64_t newlen = arr->length - k;
        auto* r = jdb_array_new(newlen);
        memcpy(r->data, arr->data, newlen * sizeof(double));
        r->flags = arr->flags;
        return r;
    }
    if (n >= arr->length) return jdb_array_new(0);
    int64_t newlen = arr->length - n;
    auto* r = jdb_array_new(newlen);
    memcpy(r->data, arr->data + n, newlen * sizeof(double));
    r->flags = arr->flags;
    return r;
}

// (n, arr) ordering for the codegen entry-point - see jdb_take_n.
JdbArray* jdb_drop_n(int64_t n, JdbArray* arr) {
    return jdb_array_drop(arr, n);
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
    r->flags = a->flags;  // preserve element-type marker
    return r;
}

// Forward-declared; defined later alongside the arithmetic helpers.
static inline JdbArray* decode_inner(double val);

// Recursive helper: walk any-depth nested array, push all leaf values into out.
static void flatten_into(JdbArray* arr, std::vector<double>& out) {
    if (!arr) return;
    // Per-element tags win when present: a ragged array ([3, [4,5]]) mixes
    // plain cells with inner-array pointers, so the array-wide nested bit
    // cannot describe it and decoding every cell as a pointer would fault.
    if (arr->elem_tags && (arr->flags & 8)) {
        for (int64_t i = 0; i < arr->length; i++) {
            if (arr->elem_tags[i] == JD_TAG_ARR)
                flatten_into(decode_inner(arr->data[i]), out);
            else
                out.push_back(arr->data[i]);
        }
        return;
    }
    if (arr->flags & 1) {
        // Nested: each element is a pointer to an inner JdbArray
        for (int64_t i = 0; i < arr->length; i++)
            flatten_into(decode_inner(arr->data[i]), out);
    } else {
        for (int64_t i = 0; i < arr->length; i++)
            out.push_back(arr->data[i]);
    }
}

JdbArray* jdb_array_flatten(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    // Fast path: 1D is identity. A tagged array may still carry inner-array
    // cells without the array-wide nested bit, so it takes the walking path.
    bool tagged = arr->elem_tags && (arr->flags & 8);
    if (!(arr->flags & 1) && !tagged) {
        auto* r = jdb_array_new(arr->length);
        memcpy(r->data, arr->data, arr->length * sizeof(double));
        r->flags = arr->flags;
        return r;
    }
    // 2D+: walk leaves, rebuild as flat 1D.
    std::vector<double> flat;
    flatten_into(arr, flat);
    auto* r = jdb_array_new((int64_t)flat.size());
    if (!flat.empty()) memcpy(r->data, flat.data(), flat.size() * sizeof(double));
    // Carry over the string-elements bit; the nested-array bit is gone
    // by definition because we just flattened it out. Bit 3 goes with it -
    // the result carries no elem_tags, and a set bit with a NULL array
    // would send every tag-aware reader through a null dereference.
    r->flags = arr->flags & ~(1 | 8);
    return r;
}

// INSERT$ on an array: insert one cell at pos, clamped to [0, length].
// The tag travels with the value so a ragged result stays readable.
JdbArray* jdb_array_insert(JdbArray* arr, double val, int64_t pos, int32_t tag) {
    int64_t oldlen = arr ? arr->length : 0;
    if (pos < 0) pos = 0;
    if (pos > oldlen) pos = oldlen;
    auto* r = jdb_array_new(oldlen + 1);
    if (arr && pos > 0) memcpy(r->data, arr->data, pos * sizeof(double));
    r->data[pos] = val;
    if (arr && pos < oldlen)
        memcpy(r->data + pos + 1, arr->data + pos, (oldlen - pos) * sizeof(double));

    bool src_tagged = arr && arr->elem_tags && (arr->flags & 8);
    bool need_tags = src_tagged || tag == JD_TAG_ARR || tag == JD_TAG_STR ||
                     tag == JD_TAG_NATIVE_MAP || tag == JD_TAG_VM_HANDLE;
    if (need_tags) {
        r->flags = (arr ? arr->flags : 0) | 8;
        r->elem_tags = (int8_t*)malloc((size_t)oldlen + 1);
        for (int64_t i = 0; i < pos; i++)
            r->elem_tags[i] = src_tagged ? arr->elem_tags[i] : (int8_t)JD_TAG_F64;
        r->elem_tags[pos] = (int8_t)tag;
        for (int64_t i = pos; i < oldlen; i++)
            r->elem_tags[i + 1] = src_tagged ? arr->elem_tags[i] : (int8_t)JD_TAG_F64;
    } else {
        r->flags = arr ? arr->flags : 0;
    }
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
    r->flags = arr->flags;
    return r;
}

double jdb_array_median(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    std::vector<double> flat;
    flatten_into(arr, flat);
    if (flat.empty()) return 0.0;
    std::sort(flat.begin(), flat.end());
    size_t n = flat.size();
    return (n % 2) ? flat[n/2] : (flat[n/2-1] + flat[n/2]) / 2.0;
}

double jdb_array_variance(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    std::vector<double> flat;
    flatten_into(arr, flat);
    if (flat.size() < 2) return 0.0;
    double avg = 0.0;
    for (double d : flat) avg += d;
    avg /= (double)flat.size();
    double ss = 0.0;
    for (double d : flat) { double x = d - avg; ss += x * x; }
    return ss / (double)flat.size();
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
// String cells in a string array are char* pointers punned into the double.
static inline const char* decode_str(double val) {
    union { double d; int64_t i; } u; u.d = val;
    return (const char*)(intptr_t)u.i;
}
static inline double encode_str(const char* s) {
    union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)s;
    return u.d;
}
static inline char* str_concat2(const char* a, const char* b) {
    if (!a) a = ""; if (!b) b = "";
    size_t la = strlen(a), lb = strlen(b);
    char* r = (char*)malloc(la + lb + 1);
    memcpy(r, a, la); memcpy(r + la, b, lb); r[la + lb] = '\0';
    return r;
}
// Binary-safe 3-way string compare (defined below, next to jdb_str_eq).
int64_t jdb_str_cmp(const char* a, const char* b);

static inline double scalar_op(double a, double b, int op) {
    switch (op) {
        case 0: return a + b;
        case 1: return a - b;
        case 2: return a * b;
        case 3: return b != 0 ? a / b : 0;
        // Bitwise / shift - coerce both sides through int64. Native arrays
        // store doubles, so for elementwise BAND/SHL we round-trip via
        // i64. Safe up to 2^53 magnitudes; that covers Subset-Sum-style
        // bitmask use (N up to ~50) without precision loss.
        case 4: return (double)((int64_t)a & (int64_t)b);   // BAND
        case 5: return (double)((int64_t)a | (int64_t)b);   // BOR
        case 6: return (double)((int64_t)a ^ (int64_t)b);   // BXOR
        case 7: return (double)((int64_t)a << (int64_t)b);  // SHL
        case 8: return (double)((int64_t)a >> (int64_t)b);  // SHR (arith)
        case 9: { // MOD - integer modulo (matches interp's MOD on doubles)
            int64_t bi = (int64_t)b;
            if (bi == 0) return 0;
            return (double)((int64_t)a % bi);
        }
        case 10: return std::pow(a, b);
        default: return 0;
    }
}
static inline double scalar_cmp(double a, double b, int op) {
    switch (op) {
        case 0: return a == b ? 1.0 : 0.0;
        case 1: return a != b ? 1.0 : 0.0;
        case 2: return (a && b) ? 1.0 : 0.0;
        case 3: return (a || b) ? 1.0 : 0.0;
        case 4: return a <  b ? 1.0 : 0.0;
        case 5: return a <= b ? 1.0 : 0.0;
        case 6: return a >  b ? 1.0 : 0.0;
        case 7: return a >= b ? 1.0 : 0.0;
        default: return 0;
    }
}

// arr OP arr (recursive for nested arrays)
static JdbArray* arr_binop(JdbArray* a, JdbArray* b, int op) {
    if (!a || !b) return jdb_array_new(0);
    int64_t n = a->length < b->length ? a->length : b->length;
    auto* r = jdb_array_new(n);
    // String arrays carry bit0(ptr)+bit1(str). Element-wise '+' (op 0)
    // concatenates; treating a string cell as a JdbArray* (the nested path)
    // would dereference a char* and segfault.
    if ((a->flags & 2) && (b->flags & 2)) {
        for (int64_t i = 0; i < n; i++)
            r->data[i] = (op == 0)
                ? encode_str(str_concat2(decode_str(a->data[i]), decode_str(b->data[i])))
                : encode_str(_strdup(""));
        r->flags |= 3;
        return r;
    }
    // Genuine nested array (array of arrays) - bit0 set WITHOUT the string bit.
    bool nested = ((a->flags & 1) && !(a->flags & 2)) ||
                  ((b->flags & 1) && !(b->flags & 2));
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
    if ((a->flags & 1) && !(a->flags & 2)) {
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
    if ((a->flags & 1) && !(a->flags & 2)) {
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
    // String arrays: element-wise content comparison (binary-safe), not a
    // numeric compare of the punned pointer bits.
    // op: 0=eq,1=ne,4=lt,5=le,6=gt,7=ge.
    if ((a->flags & 2) && (b->flags & 2)) {
        for (int64_t i = 0; i < n; i++) {
            int64_t c = jdb_str_cmp(decode_str(a->data[i]), decode_str(b->data[i]));
            double res;
            switch (op) {
                case 0:  res = (c == 0); break;
                case 1:  res = (c != 0); break;
                case 4:  res = (c < 0);  break;
                case 5:  res = (c <= 0); break;
                case 6:  res = (c > 0);  break;
                case 7:  res = (c >= 0); break;
                default: res = 0.0;      break;
            }
            r->data[i] = res;
        }
        return r;
    }
    bool nested = ((a->flags & 1) && !(a->flags & 2)) ||
                  ((b->flags & 1) && !(b->flags & 2));
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

// Forward declaration - actual definition is jdb_array_set_bool_elems
// further down. Used by the string-cmp helper below.
extern "C" void jdb_array_set_bool_elems(JdbArray* arr);

// String-array vs string-scalar comparison. Each cell of `a` carries an
// i8* string pointer in punned-f64 bits (the codegen side already tracks
// these arrays via string_array_vars and dispatches here when the rhs is
// a JD_TAG_STR). op: 0 = ==, 1 = <>, 4 = <, 5 = >, 6 = <=, 7 = >=
// (mirrors scalar_cmp's op enum). Returns a fresh boolean-tagged array.
JdbArray* jdb_array_cmp_scalar_str(JdbArray* a, const char* s, int32_t op) {
    if (!a) return jdb_array_new(0);
    auto* r = jdb_array_new(a->length);
    jdb_array_set_bool_elems(r);
    const char* sval = s ? s : "";
    for (int64_t i = 0; i < a->length; i++) {
        union { double d; intptr_t p; } u; u.d = a->data[i];
        const char* cell = (const char*)u.p;
        if (!cell) cell = "";
        int c = std::strcmp(cell, sval);
        bool result;
        switch (op) {
            case 0:  result = (c == 0); break;  // ==
            case 1:  result = (c != 0); break;  // <>
            case 4:  result = (c <  0); break;  // <
            case 5:  result = (c >  0); break;  // >
            case 6:  result = (c <= 0); break;  // <=
            case 7:  result = (c >= 0); break;  // >=
            default: result = false;
        }
        r->data[i] = result ? 1.0 : 0.0;
    }
    return r;
}

// Mark array as containing nested array pointers (2D+)
void jdb_array_set_nested(JdbArray* arr) {
    if (arr) arr->flags |= 1;
}

// Mark array as containing string elements (ptr + bit 1 so print/cmp know)
void jdb_array_set_string_elems(JdbArray* arr) {
    if (arr) arr->flags |= 3;  // bit 0 (ptr) + bit 1 (string)
}

// Mark array as containing boolean elements so print emits TRUE/FALSE
// instead of 1/0. The data slots stay plain f64 0/1 - only printing
// changes.
void jdb_array_set_bool_elems(JdbArray* arr) {
    if (arr) arr->flags |= 4;  // bit 2 (bool)
}

// Classify a single cell of a mixed-type array. Used by the codegen for
// INDEX access on arrays that contain *both* numeric and pointer (string
// or nested-array) elements: the array-wide flags say "has pointers" but
// individual cells may still be raw doubles.
//
// Heuristic: a real f64 number's raw bits are at least 2^52 (the lowest
// non-zero exponent), well above the userspace pointer range (< 2^47 on
// x86_64 Linux/Win). Cells whose bits look numeric stay as F64; cells
// whose bits look like a pointer dispatch to STR or ARR depending on the
// array-wide has_string flag.
//
// Returns a JdTag value:
//   1 = JD_TAG_F64
//   2 = JD_TAG_STR
//   3 = JD_TAG_ARR
int32_t jdb_array_classify_elem(JdbArray* arr, double d) {
    if (!arr) return 1;  // F64
    // Per-cell tags from the tagged ARRAY_LITERAL path. The classifier
    // is called via mixed_array_vars dispatch and trusts the explicit
    // per-cell tag when present, rather than the looks_ptr heuristic.
    if ((arr->flags & 8) != 0 && arr->elem_tags != nullptr) {
        // Find this cell's index by pointer arithmetic - d here is a copy
        // of arr->data[idx], but the caller doesn't pass idx. Fall through
        // to the heuristic if we can't map it.
        for (int64_t i = 0; i < arr->length; i++) {
            if (arr->data[i] == d) return (int32_t)arr->elem_tags[i];
        }
    }
    union { double d; uint64_t u; } u; u.d = d;
    bool looks_ptr = (u.u != 0 && u.u < (1ULL << 47));
    if (!looks_ptr) return 1;  // F64
    bool has_string = (arr->flags & 2) != 0;
    return has_string ? 2 : 3;  // STR or ARR
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

// Unary `-"abc"` → ["a","b","c"], UTF-8 aware. Mirrors the interpreter's
// OpCode::NEG string case. Returns a fresh array of one strdup'd string per
// code point, flagged as a nested string array (bit0 ptr + bit1 string) with
// explicit per-cell STR tags (bit3) so both the tagged INDEX getter and the
// VM-bridge arg marshaller treat the cells as strings, not punned f64s.
JdbArray* jdb_str_to_chars(const char* s) {
    if (!s) return jdb_array_new(0);
    size_t slen = strlen(s);
    int64_t n = 0;
    for (size_t i = 0; i < slen; ) {
        unsigned char c = (unsigned char)s[i];
        int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        if (i + (size_t)len > slen) len = (int)(slen - i);
        i += len; n++;
    }
    JdbArray* arr = jdb_array_new(n);
    int64_t k = 0;
    for (size_t i = 0; i < slen; ) {
        unsigned char c = (unsigned char)s[i];
        int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        if (i + (size_t)len > slen) len = (int)(slen - i);
        char* cp = (char*)malloc((size_t)len + 1);
        memcpy(cp, s + i, len);
        cp[len] = '\0';
        union { int64_t iv; double d; } u; u.iv = (int64_t)(intptr_t)cp;
        arr->data[k++] = u.d;
        i += len;
    }
    arr->flags |= 3;  // nested ptr + string
    if (n > 0) {
        arr->elem_tags = (int8_t*)malloc((size_t)n);
        for (int64_t i = 0; i < n; i++) arr->elem_tags[i] = (int8_t)2;  // STR
        arr->flags |= 8;  // per-cell tags present
    }
    return arr;
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

// Forward decls - definitions are further down the file.
JdbMap* jdb_map_new();
void jdb_map_set_f64(JdbMap* m, const char* key, double val);
void jdb_map_set_str(JdbMap* m, const char* key, const char* val);

// ── SDL event-handler trampoline (native mode) ──────────────────
//
// In native mode the bridge in jdbrt.dll holds the event_handlers map
// (set up via __EVENT_ON), but the handler bodies live as LLVM-IR in
// the .exe. The bridge can't reach them with vm.call_function.
//
// Instead, the .exe registers user handlers in this trampoline and
// installs jdrt_dispatch_event as the bridge's user_event_dispatch.
// When an SDL event fires, the bridge marshals the info-Map fields
// into (args, tags, nargs) and the trampoline rebuilds the
// JdbArray + JdbMap that the LLVM-compiled handler expects.
//
// Schema mirrors VM::event_poll (vm.cpp):
//   KEYDOWN/KEYUP: scancode (i64), keycode (i64), key (str), repeat (i64)
//   MOUSEDOWN/UP : button (i64), x (i64), y (i64)
//   MOUSEMOVE    : x (i64), y (i64)
//   QUIT         : (no payload)

typedef void (*JdbEventFn)(JdbArray*);

static JdbEventFn g_h_keydown   = nullptr;
static JdbEventFn g_h_keyup     = nullptr;
static JdbEventFn g_h_quit      = nullptr;
static JdbEventFn g_h_mousedown = nullptr;
static JdbEventFn g_h_mouseup   = nullptr;
static JdbEventFn g_h_mousemove = nullptr;

// Handlers for every other event name (forms controls, custom events).
// These arrive on a keyed wire format - see the generic branch in
// jdrt_dispatch_event.
static std::unordered_map<std::string, JdbEventFn> g_h_custom;

void jdrt_register_event_handler(const char* name, void* fn) {
    if (!name || !fn) return;
    if (strcmp(name, "KEYDOWN") == 0)        g_h_keydown   = (JdbEventFn)fn;
    else if (strcmp(name, "KEYUP") == 0)     g_h_keyup     = (JdbEventFn)fn;
    else if (strcmp(name, "QUIT") == 0)      g_h_quit      = (JdbEventFn)fn;
    else if (strcmp(name, "MOUSEDOWN") == 0) g_h_mousedown = (JdbEventFn)fn;
    else if (strcmp(name, "MOUSEUP") == 0)   g_h_mouseup   = (JdbEventFn)fn;
    else if (strcmp(name, "MOUSEMOVE") == 0) g_h_mousemove = (JdbEventFn)fn;
    else                                     g_h_custom[name] = (JdbEventFn)fn;
}

static const char* event_arg_str(const int64_t* args, int i, int nargs) {
    if (i < 0 || i >= nargs) return "";
    const char* s = (const char*)(intptr_t)args[i];
    return s ? s : "";
}

// Wrap a JdbMap* in a length-1 JdbArray, encoding the pointer the same
// way the LLVM-codegen ARRAY_LITERAL with a Map element does.
static JdbArray* wrap_map_in_array(JdbMap* m) {
    auto* a = jdb_array_new(1);
    union { double d; intptr_t p; } u;
    u.p = (intptr_t)m;
    a->data[0] = u.d;
    a->flags |= 1;  // bit 0 = ptr/nested element
    return a;
}

void jdrt_dispatch_event(const char* event_name,
                         const int64_t* args, const int32_t* tags, int nargs) {
    // The six SDL events have fixed positional schemas and ignore tags;
    // the generic branch at the bottom reads the keyed format via tags.
    if (!event_name) return;

    if (strcmp(event_name, "QUIT") == 0) {
        if (!g_h_quit) return;
        auto* a = jdb_array_new(0);
        g_h_quit(a);
        return;
    }

    if (strcmp(event_name, "KEYDOWN") == 0 || strcmp(event_name, "KEYUP") == 0) {
        JdbEventFn fn = (event_name[3] == 'D') ? g_h_keydown : g_h_keyup;
        if (!fn) return;
        JdbMap* m = jdb_map_new();
        if (nargs >= 1) jdb_map_set_f64(m, "scancode", (double)args[0]);
        if (nargs >= 2) jdb_map_set_f64(m, "keycode",  (double)args[1]);
        if (nargs >= 3) jdb_map_set_str(m, "key",      event_arg_str(args, 2, nargs));
        if (nargs >= 4) jdb_map_set_f64(m, "repeat",   (double)args[3]);
        fn(wrap_map_in_array(m));
        return;
    }

    if (strcmp(event_name, "MOUSEDOWN") == 0 || strcmp(event_name, "MOUSEUP") == 0) {
        JdbEventFn fn = (event_name[5] == 'D') ? g_h_mousedown : g_h_mouseup;
        if (!fn) return;
        JdbMap* m = jdb_map_new();
        if (nargs >= 1) jdb_map_set_f64(m, "button", (double)args[0]);
        if (nargs >= 2) jdb_map_set_f64(m, "x",      (double)args[1]);
        if (nargs >= 3) jdb_map_set_f64(m, "y",      (double)args[2]);
        fn(wrap_map_in_array(m));
        return;
    }

    if (strcmp(event_name, "MOUSEMOVE") == 0) {
        if (!g_h_mousemove) return;
        JdbMap* m = jdb_map_new();
        if (nargs >= 1) jdb_map_set_f64(m, "x", (double)args[0]);
        if (nargs >= 2) jdb_map_set_f64(m, "y", (double)args[1]);
        g_h_mousemove(wrap_map_in_array(m));
        return;
    }

    // Every other event arrives keyed: (key STR, value) pairs, built by
    // the bridge for any name outside the six SDL schemas. Rebuild the
    // info map generically and hand it to the registered handler; no
    // handler means the event was bound speculatively - drop it.
    auto cit = g_h_custom.find(event_name);
    if (cit == g_h_custom.end() || !cit->second) return;
    JdbMap* m = jdb_map_new();
    for (int i = 0; i + 1 < nargs; i += 2) {
        const char* key = event_arg_str(args, i, nargs);
        if (!key || !key[0]) continue;
        int32_t vtag = tags ? tags[i + 1] : JD_TAG_I64;
        if (vtag == JD_TAG_STR) {
            jdb_map_set_str(m, key, event_arg_str(args, i + 1, nargs));
        } else if (vtag == JD_TAG_F64) {
            double d;
            memcpy(&d, &args[i + 1], sizeof(double));
            jdb_map_set_f64(m, key, d);
        } else {  // I64 / BOOL wire as integer
            jdb_map_set_f64(m, key, (double)args[i + 1]);
        }
    }
    cit->second(wrap_map_in_array(m));
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
// setter knows the value is actually a map/array ptr punned as f64 - the
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
    // A truth value reads back as TRUE/FALSE, matching how it prints.
    if (m->tags[idx] == JD_TAG_BOOL)
        return _strdup(m->values[idx] != 0.0 ? "TRUE" : "FALSE");
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", m->values[idx]);
    return _strdup(buf);
}

int64_t jdb_map_has(JdbMap* m, const char* key) {
    return map_find(m, key) >= 0 ? 1 : 0;
}

// Used by the codegen for nested-map / array-typed fields. The caller is
// responsible for knowing the real type - the map itself doesn't expose
// per-field tags through this entry point.
void* jdb_map_get_obj(JdbMap* m, const char* key) {
    int64_t idx = map_find(m, key);
    if (idx < 0) return nullptr;
    union { double d; int64_t i; } u; u.d = m->values[idx];
    return (void*)(intptr_t)u.i;
}

// Forward decl - the unified dispatcher that picks between this and
// jdrt_obj_get_tagged based on val_tag.
int32_t jdb_tagged_get(int64_t val_bits, int32_t val_tag, const char* key, int64_t* out_val);

// Tag-aware counterpart of jdb_map_get_* - returns (tag, bits) so the
// caller can handle numbers, strings, and pointer-punned sub-maps/arrays
// without knowing the field's type in advance.
int32_t jdb_map_get_tagged(JdbMap* m, const char* key, int64_t* out_val) {
    *out_val = 0;
    int64_t idx = map_find(m, key);
    // A key that is not there is absent, and absent is not the integer
    // zero. Reporting I64 here is what made TYPEOF answer INT64 on a
    // missing key while the interpreter answered NONE.
    if (idx < 0) return JD_TAG_NONE;
    union { double d; int64_t i; } u;
    u.d = m->values[idx];
    int32_t t = m->tags[idx];
    if (t == JD_TAG_STR) {
        const char* s = (const char*)(intptr_t)u.i;
        *out_val = (int64_t)(intptr_t)_strdup(s ? s : "");
        return JD_TAG_STR;
    }
    // Convention: out_val carries the natural i64 representation for the
    // tag. STR/ARR/MAP/VMH = ptr-or-handle bits. F64 = f64-bit-pun. I64/
    // BOOL = real int (FPToSI from the stored f64). Without the I64 leg,
    // a `vstate{"id"} = INT` round-trip lost the int because storage is
    // f64 - codegen-side now expects val to be a real int when tag=I64.
    if (t == JD_TAG_I64 || t == JD_TAG_BOOL) {
        *out_val = (int64_t)u.d;  // stored as f64; convert back to int
        return t;
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

// BASIC string slicing via `/`. `from_left` picks the side:
//   n / "str"  → first n chars  (from_left = 1)
//   "str" / n  → last  n chars  (from_left = 0)
// Byte-based, mirroring the interpreter's substr semantics exactly.
// jdb_str_slice is defined below, next to jdb_str_blen, so it can be
// binary-safe (honour embedded NULs and register the result length).

// ── Generic native vectorization ──────────────────────────────
//
// Native array_apply: apply a scalar fn to each element of an array.
// Used for SIN(arr), ABS(arr), UCASE$(arr), LEFT$(arr, n), etc. instead
// of dispatching via VM bridge. The fn pointer type is encoded by suffix:
//   _ff:  double(double)                 - SIN, COS, ABS, SQR, etc.
//   _ii:  int64(int64)                   - (rare, but e.g. BITNOT)
//   _ss:  char*(const char*)             - UCASE$, LCASE$, TRIM$, REVERSE$
//   _sfi: char*(const char*, int64)      - LEFT$, RIGHT$
//   _sfii: char*(const char*, int64, int64) - MID$
//   _ifs: int64(const char*)             - LEN$, ASC
//   _ffi: double(double, int64)          - ROUND(x, decimals)

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
            // Element is numeric, caller mistake - return empty string
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
// Bit 2: element is a boolean (TRUE/FALSE rendering).
// Bit 3: per-element tags array present - dispatch on the cell's own JdTag.
void jdb_print_array_elem(JdbArray* arr, int64_t idx) {
    if (!arr || idx < 0 || idx >= arr->length) return;
    double val = arr->data[idx];
    bool has_tagged = (arr->flags & 8) != 0 && arr->elem_tags != nullptr;
    bool has_ptr = (arr->flags & 1) != 0;
    bool has_string = (arr->flags & 2) != 0;
    bool has_bool = (arr->flags & 4) != 0;
    if (has_tagged) {
        // Per-cell tag - dispatch on the stored JdTag.
        int8_t t = arr->elem_tags[idx];
        if (t == 2) {  // STR
            union { double d; int64_t i; } u; u.d = val;
            const char* s = (const char*)(intptr_t)u.i;
            if (s) printf("%s", s);
            return;
        }
        if (t == 3) {  // ARR
            union { double d; int64_t i; } u; u.d = val;
            JdbArray* inner = (JdbArray*)(intptr_t)u.i;
            if (!inner) return;
            printf("[");
            for (int64_t i = 0; i < inner->length; i++) {
                if (i > 0) printf(", ");
                jdb_print_array_elem(inner, i);
            }
            printf("]");
            return;
        }
        if (t == 8) {  // BOOL
            printf("%s", val != 0.0 ? "TRUE" : "FALSE");
            return;
        }
        // 0 (unknown), 1 (F64), other → numeric
        char num[64]; jdb_format_double(num, sizeof(num), val); fputs(num, stdout);
        return;
    }
    if (has_string) {
        // String-flag alone is enough - element is a ptr-encoded char*.
        union { double d; int64_t i; } u; u.d = val;
        const char* s = (const char*)(intptr_t)u.i;
        if (s) printf("%s", s);
    } else if (has_ptr) {
        // Nested array - print as [e0, e1, ...]
        union { double d; int64_t i; } u; u.d = val;
        JdbArray* inner = (JdbArray*)(intptr_t)u.i;
        if (!inner) return;
        printf("[");
        for (int64_t i = 0; i < inner->length; i++) {
            if (i > 0) printf(", ");
            jdb_print_array_elem(inner, i);
        }
        printf("]");
    } else if (has_bool) {
        printf("%s", val != 0.0 ? "TRUE" : "FALSE");
    } else {
        char num[64];
        jdb_format_double(num, sizeof(num), val);
        fputs(num, stdout);
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

// OS.ARGS() returns a "string array" - each element stores the argv pointer
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
    arr->flags |= 3;  // string elements
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

// Apply a Python-style format spec `[fill][align][width][.prec][#][type]`
// to a single value, mirroring the interpreter's FORMAT$ implementation.
// `spec` is the content between `{` and `}` (leading `:` is skipped here).
// `is_str=true` formats `str_val`; otherwise `val` is formatted per the
// type letter (d/f/x/X) or the default (`%g` or `.precf`). Returns chars
// written (excluding NUL); caller must pass `cap >= 2`.
static int jdb_format_one_arg(char* out, int cap,
                               const char* spec,
                               double val,
                               const char* str_val,
                               bool is_str) {
    if (cap < 2) { if (cap > 0) out[0] = 0; return 0; }
    if (spec && spec[0] == ':') spec++;
    int slen = spec ? (int)strlen(spec) : 0;
    int sp = 0;

    char fill = ' ', align = '\0';
    if (slen >= 2 && (spec[1] == '<' || spec[1] == '>' || spec[1] == '^')) {
        fill = spec[0]; align = spec[1]; sp = 2;
    } else if (slen >= 1 && (spec[0] == '<' || spec[0] == '>' || spec[0] == '^')) {
        align = spec[0]; sp = 1;
    }
    int width = 0;
    while (sp < slen && spec[sp] >= '0' && spec[sp] <= '9')
        width = width * 10 + (spec[sp++] - '0');
    int prec = -1;
    if (sp < slen && spec[sp] == '.') {
        sp++; prec = 0;
        while (sp < slen && spec[sp] >= '0' && spec[sp] <= '9')
            prec = prec * 10 + (spec[sp++] - '0');
    }
    bool hash_flag = false;
    if (sp < slen && spec[sp] == '#') { hash_flag = true; sp++; }
    char type = (sp < slen) ? spec[sp] : 0;

    char raw[256];
    int raw_len;
    if (is_str) {
        const char* s = str_val ? str_val : "";
        raw_len = (int)strlen(s);
        if (raw_len >= (int)sizeof(raw)) raw_len = (int)sizeof(raw) - 1;
        memcpy(raw, s, raw_len);
        raw[raw_len] = 0;
    } else if (type == 'd') {
        raw_len = snprintf(raw, sizeof(raw), "%lld", (long long)(int64_t)val);
    } else if (type == 'f') {
        if (prec >= 0) raw_len = snprintf(raw, sizeof(raw), "%.*f", prec, val);
        else           raw_len = snprintf(raw, sizeof(raw), "%f", val);
    } else if (type == 'x') {
        raw_len = snprintf(raw, sizeof(raw),
            hash_flag ? "0x%llx" : "%llx", (long long)(int64_t)val);
    } else if (type == 'X') {
        raw_len = snprintf(raw, sizeof(raw),
            hash_flag ? "0x%llX" : "%llX", (long long)(int64_t)val);
    } else if (prec >= 0) {
        raw_len = snprintf(raw, sizeof(raw), "%.*f", prec, val);
    } else {
        raw_len = snprintf(raw, sizeof(raw), "%g", val);
    }
    if (raw_len < 0) raw_len = 0;
    if (raw_len >= (int)sizeof(raw)) raw_len = (int)sizeof(raw) - 1;

    int written = 0;
    if (width > 0 && raw_len < width) {
        if (align == '\0') align = '>';
        int pad = width - raw_len;
        if (align == '<') {
            if (raw_len < cap - 1) { memcpy(out, raw, raw_len); written = raw_len; }
            for (int k = 0; k < pad && written < cap - 1; k++) out[written++] = fill;
        } else if (align == '^') {
            int left = pad / 2;
            for (int k = 0; k < left && written < cap - 1; k++) out[written++] = fill;
            if (written + raw_len < cap - 1) {
                memcpy(out + written, raw, raw_len); written += raw_len;
            }
            for (int k = 0; k < pad - left && written < cap - 1; k++) out[written++] = fill;
        } else { // '>'
            for (int k = 0; k < pad && written < cap - 1; k++) out[written++] = fill;
            if (written + raw_len < cap - 1) {
                memcpy(out + written, raw, raw_len); written += raw_len;
            }
        }
    } else {
        int n = raw_len < cap - 1 ? raw_len : cap - 1;
        memcpy(out, raw, n); written = n;
    }
    out[written] = 0;
    return written;
}

char* jdb_format(const char* fmt, JdbArray* args) {
    char result[4096];
    int rp = 0;
    int arg_idx = 0;
    const char* p = fmt;

    while (*p && rp < 4090) {
        if (*p == '{') {
            if (*(p+1) == '{') { result[rp++] = '{'; p += 2; continue; }
            const char* end = strchr(p, '}');
            if (!end) { result[rp++] = *p++; continue; }

            char spec[64] = {0};
            int slen = (int)(end - p - 1);
            if (slen > 0 && slen < 63) memcpy(spec, p+1, slen);

            double val = (args && arg_idx < args->length) ? args->data[arg_idx++] : 0.0;

            char tmp[256];
            int tlen = jdb_format_one_arg(tmp, sizeof(tmp), spec, val, nullptr, false);
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

// VM-bridge handle plumbing. The LLVM-generated main() calls jdrt_init()
// to spin up the VM bridge, stores the handle in a module-internal global
// __jdrt_handle, and now also pings it back here via jdb_runtime_set_handle
// so VM_HANDLE args inside FORMAT$ can be materialised at runtime.
extern "C" {
    // Exported by jdbrt.dll - declared here so jdb_runtime.obj can call them
    // when the format spec demands either numeric or string materialisation
    // of a VM_HANDLE.
    double jdrt_val_to_f64(void* rt, int64_t handle);
    const char* jdrt_val_to_str(void* rt, int64_t handle);
}
static void* g_jdrt_handle = nullptr;
extern "C" void jdb_runtime_set_handle(void* h) { g_jdrt_handle = h; }

// types[i] is 'd' for an f64-bit-pun, 's' for a const char*, or 'h' for a
// VM-handle (i64 key into the VM's value_store). 'h' lets us defer the
// "is this number-or-string?" question to runtime, which is the only stage
// that knows both the format spec and the underlying Value's type - useful
// for MAP indexing where p{"name"} could be a string and p{"price"} a double
// in the same FORMAT$ call.
static char* jdb_format_tagged_impl(const char* fmt, const char* types,
                                     int n, const int64_t* raw) {
    char result[4096];
    int rp = 0;
    int arg_idx = 0;
    const char* p = fmt;
    while (*p && rp < 4090) {
        if (*p == '{') {
            if (*(p+1) == '{') { result[rp++] = '{'; p += 2; continue; }
            const char* end = strchr(p, '}');
            if (!end) { result[rp++] = *p++; continue; }

            char spec[64] = {0};
            int slen = (int)(end - p - 1);
            if (slen > 0 && slen < 63) memcpy(spec, p+1, slen);

            char tmp[256] = {0};
            char tag = (arg_idx < n && types) ? types[arg_idx] : 'd';
            int64_t r64 = (arg_idx < n) ? raw[arg_idx] : 0;
            arg_idx++;

            if (tag == 's') {
                const char* s = (const char*)(intptr_t)r64;
                jdb_format_one_arg(tmp, sizeof(tmp), spec, 0.0, s, true);
            } else if (tag == 'h') {
                // VM handle. Pick number-or-string materialisation by the
                // type letter in the spec - d/f/x/X/e/g all want a number,
                // anything else (including bare `{}`) renders as string.
                size_t splen = strlen(spec);
                char ty = (splen > 0) ? spec[splen-1] : 0;
                bool is_numeric = (ty == 'f' || ty == 'd' || ty == 'x' ||
                                   ty == 'X' || ty == 'e' || ty == 'g');
                if (is_numeric && g_jdrt_handle) {
                    double val = jdrt_val_to_f64(g_jdrt_handle, r64);
                    jdb_format_one_arg(tmp, sizeof(tmp), spec, val, nullptr, false);
                } else if (g_jdrt_handle) {
                    const char* s = jdrt_val_to_str(g_jdrt_handle, r64);
                    jdb_format_one_arg(tmp, sizeof(tmp), spec, 0.0, s, true);
                } else {
                    snprintf(tmp, sizeof(tmp), "<no-runtime>");
                }
            } else {
                union { double d; int64_t i; } u; u.i = r64;
                jdb_format_one_arg(tmp, sizeof(tmp), spec, u.d, nullptr, false);
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

char* jdb_format1_t(const char* fmt, const char* types, int64_t a1) {
    int64_t raw[1] = { a1 };
    return jdb_format_tagged_impl(fmt, types, 1, raw);
}
char* jdb_format2_t(const char* fmt, const char* types, int64_t a1, int64_t a2) {
    int64_t raw[2] = { a1, a2 };
    return jdb_format_tagged_impl(fmt, types, 2, raw);
}
char* jdb_format3_t(const char* fmt, const char* types, int64_t a1, int64_t a2, int64_t a3) {
    int64_t raw[3] = { a1, a2, a3 };
    return jdb_format_tagged_impl(fmt, types, 3, raw);
}
char* jdb_format4_t(const char* fmt, const char* types, int64_t a1, int64_t a2, int64_t a3, int64_t a4) {
    int64_t raw[4] = { a1, a2, a3, a4 };
    return jdb_format_tagged_impl(fmt, types, 4, raw);
}

// ── String Builtins ─────────────────────────────────────────

// String slicing via `/`:  "str" / n → last n chars; n / "str" → first n.
// Binary-safe: uses the byte length registry (embedded NULs survive) and
// registers the result so downstream slices/compares see its true length.
char* jdb_str_slice(const char* s, int64_t n, int32_t from_left) {
    if (!s) return _strdup("");
    int64_t slen = jdb_str_blen(s);
    if (n < 0) n = 0;
    if (n > slen) n = slen;
    char* out = (char*)malloc((size_t)n + 1);
    const char* src = from_left ? s : (s + (slen - n));
    memcpy(out, src, (size_t)n);
    out[n] = '\0';
    jdrt_register_binary(out, n);
    return out;
}

int64_t jdb_len_str(const char* s) {
    if (!s) return 0;
    int64_t blen = jdrt_strlen(s);
    if (blen >= 0) return blen;
    return (int64_t)strlen(s);
}

// Strict MID - matches VM's substr-based register_native("MID", ...).
// start past the end is an error so TRY/CATCH can observe it (the
// crash_test relies on this).
char* jdb_mid(const char* s, int64_t start, int64_t length) {
    if (!s) return _strdup("");
    int64_t slen = jdb_str_blen(s);
    if (start < 0 || start > slen) {
        jdb_err_set("MID: index out of range", 1);
        return _strdup("");
    }
    if (length < 0 || start + length > slen) length = slen - start;
    char* r = (char*)malloc(length + 1);
    memcpy(r, s + start, length);
    r[length] = '\0';
    jdrt_register_binary(r, length);
    return r;
}

// Lenient MID$ - matches VM's register_native("MID$", ...). Out-of-range
// start returns an empty string instead of erroring; lots of jdBasic
// programs (dialog wrappers, parsers) rely on this to scan past the
// end of a string without bounds-checking.
char* jdb_mid_lax(const char* s, int64_t start, int64_t length) {
    if (!s) return _strdup("");
    int64_t slen = jdb_str_blen(s);
    if (start < 0) start = 0;
    if (start > slen) return _strdup("");
    if (length < 0 || start + length > slen) length = slen - start;
    char* r = (char*)malloc(length + 1);
    memcpy(r, s + start, length);
    r[length] = '\0';
    jdrt_register_binary(r, length);
    return r;
}

char* jdb_left(const char* s, int64_t n) {
    return jdb_mid(s, 0, n);
}

char* jdb_right(const char* s, int64_t n) {
    if (!s) return _strdup("");
    int64_t slen = jdb_str_blen(s);
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
    char* r = _strdup(buf);
    // CHR$(0) is a 1-byte string whose only byte is 0x00. strlen would
    // report 0; register the real length so INSTR/MID$/etc. treat it as
    // a real character. Other CHR$(N) values are fine - strlen=1.
    if (code == 0) jdrt_register_binary(r, 1);
    return r;
}

int64_t jdb_asc(const char* s) {
    return (s && *s) ? (unsigned char)s[0] : 0;
}

// Binary-safe substring search: walks the registered length of the
// haystack and uses memcmp so embedded 0x00 bytes don't terminate the
// search. The old strstr-based version stopped at the first NUL, which
// broke INSTR(buf$, CHR$(0)) for any BINREADER$ buffer.
int64_t jdb_instr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return -1;
    int64_t hlen = jdb_str_blen(haystack);
    int64_t nlen = jdb_str_blen(needle);
    if (nlen == 0) return 0;
    if (nlen > hlen) return -1;
    for (int64_t i = 0; i + nlen <= hlen; i++) {
        if (memcmp(haystack + i, needle, (size_t)nlen) == 0) return i;
    }
    return -1;
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
    jdb_format_double(buf, sizeof(buf), val);
    return _strdup(buf);
}

// Element-wise stringify: returns an ARRAY of string pointers, one per
// leaf scalar. Mirrors the interpreter's STR$(array).
JdbArray* jdb_array_str(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    if (arr->flags & 1) {
        r->flags |= 1;
        for (int64_t i = 0; i < arr->length; i++) {
            JdbArray* inner = jdb_array_str(decode_inner(arr->data[i]));
            r->data[i] = encode_inner(inner);
        }
    } else {
        r->flags |= 3;  // bit 0 = ptr-encoded, bit 1 = string elems
        for (int64_t i = 0; i < arr->length; i++) {
            char* s = jdb_str(arr->data[i]);
            union { int64_t i; double d; } u;
            u.i = (int64_t)(intptr_t)s;
            r->data[i] = u.d;
        }
    }
    return r;
}

// Format a JdbMap* the way the interpreter does:
//   {"key1": value1, "key2": value2}
// Strings are quoted; numbers use jdb_format_double; booleans render as
// TRUE/FALSE; nested maps and arrays recurse.
char* jdb_frmv(JdbArray* arr);  // forward decl - definition below
static int jdb_map_str_into(JdbMap* m, char* buf, int cap, int pos);
static int jdb_value_str_tag(char* buf, int cap, int pos, double val, int32_t tag) {
    union { double d; int64_t i; } u; u.d = val;
    switch (tag) {
        case JD_TAG_I64:
            return pos + snprintf(buf + pos, cap - pos, "%lld", (long long)u.i);
        case JD_TAG_F64: {
            char num[64];
            jdb_format_double(num, sizeof(num), val);
            return pos + snprintf(buf + pos, cap - pos, "%s", num);
        }
        case JD_TAG_STR: {
            const char* s = (const char*)(intptr_t)u.i;
            return pos + snprintf(buf + pos, cap - pos, "\"%s\"", s ? s : "");
        }
        case JD_TAG_ARR: {
            JdbArray* arr = (JdbArray*)(intptr_t)u.i;
            char* sub = jdb_frmv(arr);
            int r = pos + snprintf(buf + pos, cap - pos, "%s", sub ? sub : "[]");
            free(sub);
            return r;
        }
        case JD_TAG_NATIVE_MAP: {
            JdbMap* inner = (JdbMap*)(intptr_t)u.i;
            return jdb_map_str_into(inner, buf, cap, pos);
        }
        case JD_TAG_BOOL:
            return pos + snprintf(buf + pos, cap - pos, "%s",
                val != 0.0 ? "TRUE" : "FALSE");
        default: {
            char num[64];
            jdb_format_double(num, sizeof(num), val);
            return pos + snprintf(buf + pos, cap - pos, "%s", num);
        }
    }
}

static int jdb_map_str_into(JdbMap* m, char* buf, int cap, int pos) {
    if (!m || m->count == 0)
        return pos + snprintf(buf + pos, cap - pos, "{}");
    pos += snprintf(buf + pos, cap - pos, "{");
    for (int64_t i = 0; i < m->count && pos < cap - 8; i++) {
        if (i > 0) pos += snprintf(buf + pos, cap - pos, ", ");
        pos += snprintf(buf + pos, cap - pos, "\"%s\": ", m->keys[i] ? m->keys[i] : "");
        pos = jdb_value_str_tag(buf, cap, pos, m->values[i], m->tags[i]);
    }
    pos += snprintf(buf + pos, cap - pos, "}");
    return pos;
}

char* jdb_map_str(JdbMap* m) {
    char buf[8192];
    int n = jdb_map_str_into(m, buf, (int)sizeof(buf), 0);
    if (n < 0 || n >= (int)sizeof(buf)) buf[sizeof(buf) - 1] = '\0';
    return _strdup(buf);
}

char* jdb_str_bool(int64_t val) {
    return _strdup(val ? "TRUE" : "FALSE");
}

char* jdb_str_str(const char* s) {
    // STR$(string) - pass-through. Returns a fresh copy because callers
    // own (and may free) the result.
    return _strdup(s ? s : "");
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
    // Use the binary-length registry so buffers with embedded NULs
    // (BINREADER$, PACK$, etc.) compare correctly past the first 0x00.
    int64_t la = jdrt_strlen(a); if (la < 0) la = (int64_t)strlen(a);
    int64_t lb = jdrt_strlen(b); if (lb < 0) lb = (int64_t)strlen(b);
    if (la != lb) return 0;
    return memcmp(a, b, (size_t)la) == 0 ? 1 : 0;
}

int64_t jdb_str_ne(const char* a, const char* b) {
    return !jdb_str_eq(a, b);
}

// Binary-safe 3-way compare: <0 if a<b, 0 if equal, >0 if a>b. Powers the
// native string ordering operators (<,>,<=,>=) and string-array compares.
int64_t jdb_str_cmp(const char* a, const char* b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    int64_t la = jdrt_strlen(a); if (la < 0) la = (int64_t)strlen(a);
    int64_t lb = jdrt_strlen(b); if (lb < 0) lb = (int64_t)strlen(b);
    int64_t n = la < lb ? la : lb;
    int c = n > 0 ? memcmp(a, b, (size_t)n) : 0;
    if (c != 0) return c < 0 ? -1 : 1;
    if (la == lb) return 0;
    return la < lb ? -1 : 1;
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
    bool is_bool = !is_str && (arr->flags & 4) != 0;
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
        } else if (is_bool) {
            const char* s = (arr->data[i] != 0.0) ? "TRUE" : "FALSE";
            size_t sl = strlen(s);
            memcpy(out + pos, s, sl);
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
    arr->flags |= 3;  // bit 0 (ptr) + bit 1 (string elems): PRINT + INDEX dispatch
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
    // Stream to EOF rather than sizing via ftell: /proc and /sys pseudo-files
    // report length 0, so a size-based read would return an empty string.
    std::string raw;
    char buf[65536];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) raw.append(buf, got);
    fclose(f);
    // Auto-detect UTF-16 (BOM or NUL pattern) and decode to UTF-8 so the
    // resulting C-string isn't truncated at the first interior NUL byte.
    try {
        std::string out = jdb_enc::decode_to_utf8(raw, "");
        return _strdup(out.c_str());
    } catch (const std::exception&) {
        return _strdup(raw.c_str());
    }
}

void jdb_txtwriter(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (f) { fputs(content, f); fclose(f); }
}

void jdb_txtwriter_append(const char* path, const char* content) {
    FILE* f = fopen(path, "ab");
    if (f) { fputs(content, f); fclose(f); }
}

// 3-arg form used by TXTWRITER with optional append flag - bridges
// the codegen-default of padding the 3rd i64 arg with 0 to no-append.
void jdb_txtwriter3(const char* path, const char* content, int64_t append) {
    if (append) jdb_txtwriter_append(path, content);
    else        jdb_txtwriter(path, content);
}

// 2-arg TXTREADER$ with codepage - decodes file bytes (in `encoding`) to UTF-8
// for the jdBasic string. encoding=NULL or "" means byte-pass-through (same as
// the 1-arg jdb_txtreader). On Windows the conversion goes via UTF-16; other
// platforms only support pass-through.
char* jdb_txtreader_enc(const char* path, const char* encoding) {
    FILE* f = fopen(path, "rb");
    if (!f) return _strdup("");
    // Stream to EOF (see jdb_txtreader) so /proc and /sys files read fully.
    std::string raw;
    char buf[65536];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) raw.append(buf, got);
    fclose(f);
    std::string enc = encoding ? encoding : "";
    try {
        std::string out = jdb_enc::decode_to_utf8(raw, enc);
        return _strdup(out.c_str());
    } catch (const std::exception&) {
        // Fall back to pass-through on conversion error so the script can
        // recover; the (corrupt-on-bad-bytes) data is still readable.
        return _strdup(raw.c_str());
    }
}

// 4-arg TXTWRITER with codepage - encodes UTF-8 jdBasic string to `encoding`
// bytes before writing. encoding=NULL or "" means byte-pass-through.
void jdb_txtwriter_enc(const char* path, const char* content, int64_t append,
                       const char* encoding) {
    std::string in = content ? content : "";
    std::string enc = encoding ? encoding : "";
    std::string out;
    try {
        out = jdb_enc::encode_from_utf8(in, enc);
    } catch (const std::exception&) {
        out = in;
    }
    FILE* f = fopen(path, append ? "ab" : "wb");
    if (!f) return;
    if (!out.empty()) fwrite(out.data(), 1, out.size(), f);
    fclose(f);
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

// Civil calendar fallback (Howard Hinnant's algorithm): localtime returns
// nullptr for epochs the CRT cannot represent (pre-1970 on Windows), so
// the date accessors fall back to UTC civil components computed here.
static void rt_civil_from_days(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = yy + (m <= 2);
}

static void rt_epoch_to_civil_utc(double epoch, int64_t& y, int64_t& mo, int64_t& d,
                                  int64_t& h, int64_t& mi, int64_t& se, int64_t& wd) {
    int64_t t = (int64_t)floor(epoch);
    int64_t days = t / 86400;
    int64_t rem = t % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    h = rem / 3600; mi = (rem % 3600) / 60; se = rem % 60;
    rt_civil_from_days(days, y, mo, d);
    wd = (days + 4) % 7;           // epoch day 0 = Thursday
    if (wd < 0) wd += 7;
}

char* jdb_date_str(double epoch) {
    time_t t = (time_t)epoch;
    char buf[32];
    if (struct tm* tm = localtime(&t)) {
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
    } else {
        int64_t y, mo, d, h, mi, se, wd;
        rt_epoch_to_civil_utc(epoch, y, mo, d, h, mi, se, wd);
        snprintf(buf, sizeof(buf), "%04lld-%02lld-%02lld",
                 (long long)y, (long long)mo, (long long)d);
    }
    return _strdup(buf);
}

char* jdb_time_str(double epoch) {
    time_t t = (time_t)epoch;
    char buf[32];
    if (struct tm* tm = localtime(&t)) {
        strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    } else {
        int64_t y, mo, d, h, mi, se, wd;
        rt_epoch_to_civil_utc(epoch, y, mo, d, h, mi, se, wd);
        snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld",
                 (long long)h, (long long)mi, (long long)se);
    }
    return _strdup(buf);
}

// Date accessors: accept either epoch (f64) or ISO string.
// Since we can't overload in C, we use heuristic: very small values (< 10000)
// are treated as invalid; otherwise treated as epoch. For strings, use
// jdb_year_str etc. (called via string-tagged CVDATE result).
int64_t jdb_year(double epoch) {
    time_t t = (time_t)epoch;
    if (struct tm* p = localtime(&t)) return p->tm_year + 1900;
    int64_t y, mo, d, h, mi, se, wd;
    rt_epoch_to_civil_utc(epoch, y, mo, d, h, mi, se, wd);
    return y;
}
int64_t jdb_month(double epoch) {
    time_t t = (time_t)epoch;
    if (struct tm* p = localtime(&t)) return p->tm_mon + 1;
    int64_t y, mo, d, h, mi, se, wd;
    rt_epoch_to_civil_utc(epoch, y, mo, d, h, mi, se, wd);
    return mo;
}
int64_t jdb_day(double epoch) {
    time_t t = (time_t)epoch;
    if (struct tm* p = localtime(&t)) return p->tm_mday;
    int64_t y, mo, d, h, mi, se, wd;
    rt_epoch_to_civil_utc(epoch, y, mo, d, h, mi, se, wd);
    return d;
}
int64_t jdb_hour(double epoch) {
    time_t t = (time_t)epoch;
    if (struct tm* p = localtime(&t)) return p->tm_hour;
    int64_t y, mo, d, h, mi, se, wd;
    rt_epoch_to_civil_utc(epoch, y, mo, d, h, mi, se, wd);
    return h;
}
int64_t jdb_minute(double epoch) {
    time_t t = (time_t)epoch;
    if (struct tm* p = localtime(&t)) return p->tm_min;
    int64_t y, mo, d, h, mi, se, wd;
    rt_epoch_to_civil_utc(epoch, y, mo, d, h, mi, se, wd);
    return mi;
}
int64_t jdb_second(double epoch) {
    time_t t = (time_t)epoch;
    if (struct tm* p = localtime(&t)) return p->tm_sec;
    int64_t y, mo, d, h, mi, se, wd;
    rt_epoch_to_civil_utc(epoch, y, mo, d, h, mi, se, wd);
    return se;
}
int64_t jdb_weekday(double epoch) {
    time_t t = (time_t)epoch;
    if (struct tm* p = localtime(&t)) return p->tm_wday;
    int64_t y, mo, d, h, mi, se, wd;
    rt_epoch_to_civil_utc(epoch, y, mo, d, h, mi, se, wd);
    return wd;
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

// Native dates are ISO strings in LOCAL time (matching NOW(), CVDATE(), etc.).
// Parse the string as local wall-clock → convert to UTC epoch via mktime.
// If tz_hours is NaN (2-arg form), use localtime for backward compatibility.
// Otherwise shift the epoch by tz_hours and format via gmtime so the wall
// clock reflects the chosen zone (tz_hours == 0 → UTC wall clock).
char* jdb_format_date(const char* date_str, const char* fmt, double tz_hours) {
    if (!date_str) return _strdup("");
    // Mirror the interpreter: a missing/empty format means the standard
    // "Y-m-d H:M:S" representation. Codegen passes a null fmt pointer
    // when the caller writes FORMAT_DATE(t) with no second argument.
    if (!fmt || !*fmt) fmt = "%Y-%m-%d %H:%M:%S";
    int y, m, d, hr = 0, mn = 0, sc = 0;
    int n = sscanf(date_str, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hr, &mn, &sc);
    if (n < 3) return _strdup("");
    struct tm local_tm = {0};
    local_tm.tm_year = y - 1900;
    local_tm.tm_mon  = m - 1;
    local_tm.tm_mday = d;
    local_tm.tm_hour = hr;
    local_tm.tm_min  = mn;
    local_tm.tm_sec  = sc;
    local_tm.tm_isdst = -1;
    time_t epoch = mktime(&local_tm);
    struct tm out_tm;
    if (tz_hours != tz_hours) {
#ifdef _WIN32
        localtime_s(&out_tm, &epoch);
#else
        struct tm* g = localtime(&epoch);
        if (g) out_tm = *g; else memset(&out_tm, 0, sizeof(out_tm));
#endif
    } else {
        epoch += (time_t)(tz_hours * 3600.0);
#ifdef _WIN32
        gmtime_s(&out_tm, &epoch);
#else
        struct tm* g = gmtime(&epoch);
        if (g) out_tm = *g; else memset(&out_tm, 0, sizeof(out_tm));
#endif
    }
    char buf[256];
    strftime(buf, sizeof(buf), fmt, &out_tm);
    return _strdup(buf);
}

// ── System ──────────────────────────────────────────────────

char* jdb_getenv(const char* name) {
    const char* val = getenv(name);
    return _strdup(val ? val : "");
}

void jdb_setenv(const char* name, const char* val) {
    if (!name) return;
#ifdef _WIN32
    _putenv_s(name, val ? val : "");
    SetEnvironmentVariableA(name, val);
#else
    if (val) setenv(name, val, 1);
    else unsetenv(name);
#endif
}

char* jdb_mktemp(const char* prefix) {
    const char* pfx = (prefix && *prefix) ? prefix : "jdb";
#ifdef _WIN32
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tmp);
    if (n == 0) tmp[0] = '\0';
    char name[MAX_PATH];
    if (GetTempFileNameA(tmp, pfx, 0, name)) {
        DeleteFileA(name);
        return _strdup(name);
    }
    return _strdup("");
#else
    const char* tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    size_t cap = strlen(tmp) + 1 + strlen(pfx) + 7;
    char* tmpl = (char*)malloc(cap);
    snprintf(tmpl, cap, "%s/%sXXXXXX", tmp, pfx);
    int fd = mkstemp(tmpl);
    if (fd < 0) { free(tmpl); return _strdup(""); }
    close(fd); unlink(tmpl);
    return tmpl;
#endif
}

void jdb_rmdir(const char* path) {
    if (!path) return;
#ifdef _WIN32
    RemoveDirectoryA(path);
#else
    rmdir(path);
#endif
}

// ── File metadata ───────────────────────────────────────────
int64_t jdb_file_exists(const char* path) {
    if (!path) return 0;
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
#else
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
#endif
}

int64_t jdb_file_size(const char* path) {
    if (!path) return -1;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return -1;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
    return ((int64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
#else
    struct stat st;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) return -1;
    return (int64_t)st.st_size;
#endif
}

int64_t jdb_file_isdir(const char* path) {
    if (!path) return 0;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

// ── Path helpers ────────────────────────────────────────────
char* jdb_path_dirname(const char* p) {
    if (!p) return _strdup("");
    std::string s(p);
    size_t pos = s.find_last_of("/\\");
    if (pos == std::string::npos) return _strdup("");
    if (pos == 0) return _strdup(s.substr(0, 1).c_str());
    return _strdup(s.substr(0, pos).c_str());
}

char* jdb_path_normalize(const char* p) {
    if (!p) return _strdup("");
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    std::string s; s.reserve(strlen(p));
    for (const char* c = p; *c; c++)
        s.push_back((*c == '/' || *c == '\\') ? sep : *c);
    std::string prefix;
    std::string body = s;
#ifdef _WIN32
    if (body.size() >= 2 && isalpha((unsigned char)body[0]) && body[1] == ':') {
        prefix = body.substr(0, 2);
        body = body.substr(2);
    }
#endif
    bool rooted = !body.empty() && body[0] == sep;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && body[i] == sep) i++;
        size_t j = i;
        while (j < body.size() && body[j] != sep) j++;
        if (i < j) parts.push_back(body.substr(i, j - i));
        i = j;
    }
    std::vector<std::string> out;
    for (auto& part : parts) {
        if (part == ".") continue;
        if (part == "..") {
            if (!out.empty() && out.back() != "..") out.pop_back();
            else if (!rooted) out.push_back("..");
        } else {
            out.push_back(part);
        }
    }
    std::string res = prefix;
    if (rooted) res.push_back(sep);
    for (size_t k = 0; k < out.size(); k++) {
        if (k) res.push_back(sep);
        res += out[k];
    }
    if (res.empty()) res = ".";
    return _strdup(res.c_str());
}

// ── Bit rotation ────────────────────────────────────────────
int64_t jdb_rotl(int64_t x, int64_t n, int64_t bits) {
    int w = (bits == 8 || bits == 16 || bits == 32 || bits == 64) ? (int)bits : 64;
    uint64_t mask = (w == 64) ? ~(uint64_t)0 : ((uint64_t)1 << w) - 1;
    uint64_t u = ((uint64_t)x) & mask;
    int64_t s = ((n % w) + w) % w;
    return (int64_t)(((u << s) | (u >> (w - s))) & mask);
}
int64_t jdb_rotr(int64_t x, int64_t n, int64_t bits) {
    int w = (bits == 8 || bits == 16 || bits == 32 || bits == 64) ? (int)bits : 64;
    uint64_t mask = (w == 64) ? ~(uint64_t)0 : ((uint64_t)1 << w) - 1;
    uint64_t u = ((uint64_t)x) & mask;
    int64_t s = ((n % w) + w) % w;
    return (int64_t)(((u >> s) | (u << (w - s))) & mask);
}
// 2-arg forms used by native codegen (implicit 64-bit width).
int64_t jdb_rotl2(int64_t x, int64_t n) { return jdb_rotl(x, n, 64); }
int64_t jdb_rotr2(int64_t x, int64_t n) { return jdb_rotr(x, n, 64); }

// ── Math (GCD/LCM) - binary; variadic is expanded by codegen ─
int64_t jdb_gcd(int64_t a, int64_t b) {
    if (a < 0) a = -a; if (b < 0) b = -b;
    while (b) { int64_t t = a % b; a = b; b = t; }
    return a;
}
int64_t jdb_lcm(int64_t a, int64_t b) {
    if (a == 0 || b == 0) return 0;
    int64_t g = jdb_gcd(a, b);
    if (a < 0) a = -a; if (b < 0) b = -b;
    return (a / g) * b;
}

// ── String padding ──────────────────────────────────────────
char* jdb_lpad(const char* s, int64_t n, const char* pad) {
    const char* p = (pad && *pad) ? pad : " ";
    size_t slen = s ? strlen(s) : 0;
    if ((int64_t)slen >= n) return _strdup(s ? s : "");
    size_t need = (size_t)n - slen;
    size_t plen = strlen(p);
    char* out = (char*)malloc(need + slen + 1);
    for (size_t i = 0; i < need; i++) out[i] = p[i % plen];
    memcpy(out + need, s ? s : "", slen);
    out[need + slen] = '\0';
    return out;
}
char* jdb_rpad(const char* s, int64_t n, const char* pad) {
    const char* p = (pad && *pad) ? pad : " ";
    size_t slen = s ? strlen(s) : 0;
    if ((int64_t)slen >= n) return _strdup(s ? s : "");
    size_t need = (size_t)n - slen;
    size_t plen = strlen(p);
    char* out = (char*)malloc(slen + need + 1);
    memcpy(out, s ? s : "", slen);
    for (size_t i = 0; i < need; i++) out[slen + i] = p[i % plen];
    out[slen + need] = '\0';
    return out;
}
// 2-arg forms used by native codegen (default pad = " ").
char* jdb_lpad2(const char* s, int64_t n) { return jdb_lpad(s, n, " "); }
char* jdb_rpad2(const char* s, int64_t n) { return jdb_rpad(s, n, " "); }

// IIF (inline if): returns a or b based on condition
double jdb_iif(int64_t cond, double a, double b) {
    return cond ? a : b;
}

// ISNUM, ISSTR, ISARR - type checking (simplified for native)
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

// CVDATE for numeric input - interpret as Unix epoch seconds and format
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

// CVDATE on an array - element-wise. Numeric elements use jdb_cvdate_num,
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

} // end extern "C" - regex functions need C++ linkage internally

// ── Regex (C++ internally, extern "C" interface) ────────────

// VM convention is REGEX.MATCH(pattern, text) / REPLACE(pattern, text, repl)
// - see vm.cpp's register_native impls and doc/languages.md. Native must
// match that order or the same script picks one branch in interp and the
// opposite in native (e.g. native_test.jdb's REGEX section flipping
// PASS/FAIL between modes).
static int64_t regex_match_impl(const char* pattern, const char* text) {
    try {
        return std::regex_search(std::string(text ? text : ""),
                                 std::regex(pattern ? pattern : "")) ? 1 : 0;
    } catch (...) { return 0; }
}

static char* regex_replace_impl(const char* pattern, const char* text, const char* replacement) {
    try {
        std::string result = std::regex_replace(std::string(text ? text : ""),
                                                std::regex(pattern ? pattern : ""),
                                                std::string(replacement ? replacement : ""));
        return _strdup(result.c_str());
    } catch (...) { return _strdup(text ? text : ""); }
}

static JdbArray* regex_findall_impl(const char* pattern, const char* text) {
    std::vector<double> positions;
    try {
        std::string s(text ? text : "");
        std::regex re(pattern ? pattern : "");
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
int64_t jdb_regex_match(const char* pattern, const char* text) { return regex_match_impl(pattern, text); }
char* jdb_regex_replace(const char* pattern, const char* text, const char* replacement) { return regex_replace_impl(pattern, text, replacement); }
JdbArray* jdb_regex_findall(const char* pattern, const char* text) { return regex_findall_impl(pattern, text); }

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
    // A compiled FUNC returns f64, and one that fell out of EXITFUNC
    // without a value has nothing else to say so with: NaN is how the
    // native side spells "returned nothing", and the gate checks it.
    // The cost is that a genuine NaN answers NONE here while the
    // interpreter calls SQR(-1) a FLOAT64. Telling the two apart needs
    // a runtime-tagged return, not a wider tag on the value.
    if (v != v) return _strdup("NONE");
    return _strdup("FLOAT64");
}

// Both NATIVE_MAP and VM_HANDLE surface as "OBJECT" - user-facing type
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
        case JdTag::BOOL:       return _strdup("BOOLEAN");
        case JdTag::NONE:       return _strdup("NONE");
        case JdTag::RUNTIME:
        default:                return _strdup("UNKNOWN");
    }
}

// ── FRMV$ (format array as string) ──────────────────────────

char* jdb_frmv(JdbArray* arr) {
    if (!arr || arr->length == 0) return _strdup("[]");
    bool has_tagged = (arr->flags & 8) != 0 && arr->elem_tags != nullptr;
    bool is_str = (arr->flags & 2) != 0;
    bool is_nested = (arr->flags & 1) != 0 && !is_str;
    bool is_bool = (arr->flags & 4) != 0;
    char buf[8192] = "[";
    int pos = 1;
    // snprintf returns the INTENDED length, which can exceed the space left;
    // emit() clamps pos so neither the in-loop writes nor the trailing ']'
    // ever run past buf[8191].
    const int CAP = 8190;
    auto emit = [&](const char* s) {
        if (pos >= CAP || !s) return;
        int w = snprintf(buf + pos, (size_t)(CAP - pos), "%s", s);
        if (w < 0) return;
        pos += w;
        if (pos > CAP) pos = CAP;
    };
    for (int64_t i = 0; i < arr->length && pos < CAP; i++) {
        if (i > 0) { buf[pos++] = ','; buf[pos++] = ' '; }
        if (has_tagged) {
            // Per-cell JdTag (mixed arrays, e.g. AGG/TALLY rows [key, n]).
            int8_t t = arr->elem_tags[i];
            union { double d; int64_t i; } u; u.d = arr->data[i];
            if (t == 2) {  // STR
                emit((const char*)(intptr_t)u.i ? (const char*)(intptr_t)u.i : "");
            } else if (t == 3) {  // ARR
                char* sub = jdb_frmv((JdbArray*)(intptr_t)u.i);
                emit(sub ? sub : "[]");
                free(sub);
            } else if (t == 8) {  // BOOL
                emit(arr->data[i] != 0.0 ? "TRUE" : "FALSE");
            } else {       // F64 / I64 / unknown
                char num[64];
                jdb_format_double(num, sizeof(num), arr->data[i]);
                emit(num);
            }
        } else if (is_str || is_nested) {
            // Flags mark the array as carrying pointers, but individual
            // cells may still be plain numbers (mixed rows like ["bob", 30]).
            // Classify per cell instead of dereferencing every slot.
            int32_t ct = jdb_array_classify_elem(arr, arr->data[i]);
            union { double d; int64_t i; } u; u.d = arr->data[i];
            if (ct == 2) {
                const char* s = (const char*)(intptr_t)u.i;
                emit(s ? s : "");
            } else if (ct == 3) {
                char* sub = jdb_frmv((JdbArray*)(intptr_t)u.i);
                emit(sub ? sub : "[]");
                free(sub);
            } else {
                char num[64];
                jdb_format_double(num, sizeof(num), arr->data[i]);
                emit(num);
            }
        } else if (is_bool) {
            emit(arr->data[i] != 0.0 ? "TRUE" : "FALSE");
        } else {
            char num[64];
            jdb_format_double(num, sizeof(num), arr->data[i]);
            emit(num);
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
int64_t jdb_cint(double x) { return (int64_t)(int32_t)x; }
int64_t jdb_clng(double x) { return (int64_t)x; }
double jdb_csng(double x) { return (double)(float)x; }
int64_t jdb_cbool(double x) { return x != 0.0 ? 1 : 0; }
char* jdb_tostr(double x) { return jdb_str(x); }
char* jdb_cstr(double x) { return jdb_str(x); }
double jdb_tonum(const char* s) { return s ? atof(s) : 0.0; }

int64_t jdb_byteat(const char* s, int64_t idx) {
    if (!s || idx < 0) return 0;
    // Honour binary strings (BINREADER$, PACK$) by consulting the
    // jdrt_strlen registry first - strlen alone truncates at the
    // first NUL, which makes binary file contents look truncated to
    // BYTEAT after the first zero byte.
    int64_t blen = jdrt_strlen(s);
    if (blen < 0) blen = (int64_t)strlen(s);
    if (idx >= blen) return 0;
    return (uint8_t)s[idx];
}

// OS info
// Match the VM's getos_fn in vm.cpp - uppercase platform tags so
// `IF OS.GETOS$() = "WINDOWS"` works the same way in interp + native.
char* jdb_os_getos() {
#ifdef _WIN32
    return _strdup("WINDOWS");
#elif __APPLE__
    return _strdup("MACOS");
#elif __linux__
    return _strdup("LINUX");
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

// AGG(keys, values, reducer) - group `values` by the matching `keys` entry
// (first-seen order), call reducer(group) per group, return [[key, reduced],
// ...]. `reducer` is the funcref wrapper (double(double)); it receives the
// group as a punned array ptr and returns a numeric result. Key cells keep
// the key array's type (string vs numeric); each result row carries per-cell
// tags so nested reads (row[0]=key, row[1]=reduced) decode correctly.
JdbArray* jdb_agg_fn(JdbMapFn reducer, JdbArray* keys, JdbArray* vals, int32_t reduced_tag) {
    if (!keys || !vals) return jdb_array_new(0);
    int64_t n = keys->length < vals->length ? keys->length : vals->length;
    bool key_is_str = (keys->flags & 2) != 0;
    int32_t grp_flags = vals->flags & ~(int32_t)8;  // group element-type flags

    std::vector<double> key_cell;               // raw key bits per group
    std::vector<std::vector<double>> gdata;     // value bits per group
    auto same_key = [&](double a, double b) -> bool {
        if (!key_is_str) return a == b;
        union { double d; int64_t i; } ua, ub; ua.d = a; ub.d = b;
        const char* sa = (const char*)(intptr_t)ua.i;
        const char* sb = (const char*)(intptr_t)ub.i;
        if (!sa || !sb) return sa == sb;
        return strcmp(sa, sb) == 0;
    };
    for (int64_t i = 0; i < n; i++) {
        double kc = keys->data[i];
        int64_t g = -1;
        for (size_t s = 0; s < key_cell.size(); s++)
            if (same_key(key_cell[s], kc)) { g = (int64_t)s; break; }
        if (g < 0) { g = (int64_t)key_cell.size(); key_cell.push_back(kc); gdata.push_back({}); }
        gdata[(size_t)g].push_back(vals->data[i]);
    }

    int64_t ng = (int64_t)key_cell.size();
    JdbArray* r = jdb_array_new(ng);
    for (int64_t g = 0; g < ng; g++) {
        JdbArray* grp = jdb_array_new((int64_t)gdata[(size_t)g].size());
        for (size_t j = 0; j < gdata[(size_t)g].size(); j++) grp->data[j] = gdata[(size_t)g][j];
        grp->flags = grp_flags;
        union { double d; int64_t i; } ug; ug.i = (int64_t)(intptr_t)grp;
        double reduced = reducer(ug.d);

        JdbArray* row = jdb_array_new(2);
        row->data[0] = key_cell[(size_t)g];
        row->data[1] = reduced;
        row->elem_tags = (int8_t*)malloc(2);
        row->elem_tags[0] = key_is_str ? (int8_t)2 : (int8_t)1;  // STR or F64
        // The reducer's real return type (F64 for SUM/MEAN/..., STR/ARR for a
        // string/array-producing reducer like JOIN). reduced holds the punned
        // pointer for STR/ARR, so the per-cell tag drives the typed read.
        row->elem_tags[1] = (int8_t)reduced_tag;
        // A mixed [key, number] row is tagged per-cell ONLY (flag 8). Do NOT
        // set the array-wide nested(bit0)/string(bit1) flags: bit0 would make
        // array walkers recurse into the numeric cell as a JdbArray* (crash),
        // and the per-cell tags already drive every typed read.
        row->flags |= 8;

        union { double d; int64_t i; } ur; ur.i = (int64_t)(intptr_t)row;
        r->data[g] = ur.d;
    }
    r->flags |= 1;  // rows are nested arrays
    if (ng > 0) {
        r->elem_tags = (int8_t*)malloc((size_t)ng);
        for (int64_t g = 0; g < ng; g++) r->elem_tags[g] = (int8_t)3;  // ARR
        r->flags |= 8;
    }
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
    // Preserve element-type flags (string / nested-ptr / bool) - FILTER
    // returns source elements verbatim, so they keep their type. Exclude the
    // per-cell-tags bit (8): the source's elem_tags is indexed by the ORIGINAL
    // positions and would be wrong post-filter; the flag-based classifier
    // recovers strings/arrays without it.
    r->flags = arr->flags & ~(int32_t)8;
    return r;
}

double jdb_reduce_fn(JdbReduceFn fn, JdbArray* arr, double init) {
    if (!arr) return init;
    double acc = init;
    for (int64_t i = 0; i < arr->length; i++)
        acc = fn(acc, arr->data[i]);
    return acc;
}

// TAKE_WHILE(pred@, arr): longest prefix where pred(elem) is true.
JdbArray* jdb_take_while_fn(JdbMapFn pred, JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    int64_t n = 0;
    while (n < arr->length && pred(arr->data[n]) != 0.0) n++;
    auto* r = jdb_array_new(n);
    for (int64_t i = 0; i < n; i++) r->data[i] = arr->data[i];
    r->flags = arr->flags & ~(int32_t)8;  // keep type flags; drop per-cell tags
    return r;
}

// DROP_WHILE(pred@, arr): drop the longest true-prefix, return the remainder.
JdbArray* jdb_drop_while_fn(JdbMapFn pred, JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    int64_t start = 0;
    while (start < arr->length && pred(arr->data[start]) != 0.0) start++;
    int64_t n = arr->length - start;
    auto* r = jdb_array_new(n);
    for (int64_t i = 0; i < n; i++) r->data[i] = arr->data[start + i];
    r->flags = arr->flags & ~(int32_t)8;
    return r;
}

// OUTER(a, b, op) with a user funcref operator: a 2D table whose cell [i][j]
// is op(a[i], b[j]). The string-operator form ("+", "*", ...) goes through the
// VM bridge; this native path exists so a FUNC used as an operator (op@) can be
// called directly instead of by-name through the bridge (which can't resolve a
// compiled user function).
JdbArray* jdb_outer_fn(JdbArray* a, JdbArray* b, JdbReduceFn op) {
    if (!a || !b) return jdb_array_new(0);
    auto* r = jdb_array_new(a->length);
    for (int64_t i = 0; i < a->length; i++) {
        auto* row = jdb_array_new(b->length);
        for (int64_t j = 0; j < b->length; j++)
            row->data[j] = op(a->data[i], b->data[j]);
        r->data[i] = encode_inner(row);
    }
    if (a->length > 0) r->flags |= 1;  // rows are nested arrays
    return r;
}

} // extern "C"

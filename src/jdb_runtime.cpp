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
#include "jdb_shape.h"
#include "jdb_lower_keys.h"
#include <Eigen/Dense>
#include "jdb_calendar.h"
#include "jdb_format.h"
#include "jdb_number.h"
#include "jdb_text.h"
#include "jdb_path.h"
#include "jdb_regex.h"
#include <clocale>
#include <algorithm>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <string_view>
#include <unordered_set>
#include <new>
#include <stdexcept>

#include "jdb_tags.h"
#include "jdb_encoding.h"
#include "jdb_crypto.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
// POSIX has strdup; MSVC provides the underscore-prefixed _strdup. Map one
// to the other so the call sites don't need a forest of #ifdefs.
#define _strdup strdup
#endif

// Keeping a cold helper out of its caller is spelled differently by the
// two compilers.
#ifdef _MSC_VER
#define JDB_NOINLINE __declspec(noinline)
#else
#define JDB_NOINLINE __attribute__((noinline))
#endif

static auto g_start_time = std::chrono::steady_clock::now();

// Forward declare JdbArray for use in C++ functions
struct JdbArray;
extern "C" JdbArray* jdb_array_new(int64_t size);

// The VM bridge of a compiled program (jdb_runtime_set_handle). A VM handle
// stored into an array or a map cell is made persistent there, so the sweep
// at the end of a loop pass does not take the value from under the cell.
static void* g_jdrt_handle = nullptr;
extern "C" int64_t jdrt_promote_handle(void* rt, int64_t h);
static inline double keep_vm_handle(double val, int32_t tag) {
    if (tag != JD_TAG_VM_HANDLE || !g_jdrt_handle) return val;
    union { double d; int64_t i; } u; u.d = val;
    u.i = jdrt_promote_handle(g_jdrt_handle, u.i);
    return u.d;
}

extern "C" void jdb_err_set(const char* msg, int64_t code);

// Memory the runtime's own bookkeeping cannot do without - the header of an
// array, the table of a map or of the counts - is gone: nothing can go on.
static void jdb_fatal_oom() {
    fputs("Out of memory", stderr);
    fputc(10, stderr);
    exit(7);
}

// Compiled code has no unwind tables, so no exception may leave an entry it
// calls. jdb_guard runs body and turns whatever it throws into the
// program's runtime error, answering fail() instead.
static void jdb_report_exception() {
    try {
        throw;
    } catch (const std::bad_alloc&) {
        jdb_err_set("Out of memory", 99);
    } catch (const std::length_error&) {
        jdb_err_set("Out of memory", 99);
    } catch (const std::exception& e) {
        jdb_err_set(e.what(), 1);
    } catch (...) {
        jdb_err_set("Internal error in the runtime", 1);
    }
}

// How many guards are open on this thread: inside one an allocation that
// fails may throw, outside it must not.
static thread_local int g_guard_depth = 0;
struct GuardDepth {
    GuardDepth() { g_guard_depth++; }
    ~GuardDepth() { g_guard_depth--; }
};
// A call from the runtime into compiled code: nothing in there may throw,
// guard or no guard around the call, so the depth is 0 while it runs.
struct CompiledCall {
    int saved;
    CompiledCall() : saved(g_guard_depth) { g_guard_depth = 0; }
    ~CompiledCall() { g_guard_depth = saved; }
};

template <class Fn, class... A>
static double call_compiled(Fn fn, A... args) {
    CompiledCall open;
    return fn(args...);
}

template <class F, class G>
static auto jdb_guard(F&& body, G&& fail) -> decltype(body()) {
    try {
        GuardDepth open;
        return body();
    } catch (...) {
        jdb_report_exception();
    }
    return fail();
}

template <class F>
static void jdb_guard_void(F&& body) {
    try {
        GuardDepth open;
        body();
    } catch (...) {
        jdb_report_exception();
    }
}

extern "C" {

static inline JdbArray* decode_inner(double val);
static inline double encode_inner(JdbArray* arr);
static void flatten_into(JdbArray* arr, std::vector<double>& out, std::vector<int8_t>* tags = nullptr);

// ── I/O ─────────────────────────────────────────────────────

// The punctuation SETLOCALE chose for numbers, the C locale's at first.
static jdb_num::Punct g_punct;

void jdb_print_int(int64_t val) {
    char buf[64];
    jdb_num::format_int(buf, sizeof(buf), val, g_punct);
    fputs(buf, stdout);
}

// A number as PRINT and STR$ write it (jdb_num::format_double).
static int jdb_format_double(char* out, int cap, double val) {
    return jdb_num::format_double(out, cap, val, g_punct);
}

void jdb_print_double(double val) {
    char buf[512];
    jdb_format_double(buf, sizeof(buf), val);
    fputs(buf, stdout);
}

// The byte length of a string registered as binary, -1 for one that is not;
// resolved against jdbrt.dll, declared once more below.
#ifdef _WIN32
__declspec(dllimport)
#endif
int64_t jdrt_strlen(const char* s);

// Every byte of the string, a NUL inside it included.
void jdb_print_str(const char* val) {
    if (!val) return;
    int64_t n = jdrt_strlen(val);
    if (n < 0) n = (int64_t)strlen(val);
    if (n > 0) fwrite(val, 1, (size_t)n, stdout);
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

// ── Output capture ──────────────────────────────────────────
// OUTPUT.CAPTURE_BEGIN routes stdout into a temporary file until the
// matching OUTPUT.CAPTURE_END$ hands the text back. Captures nest.
void jdb_err_set(const char* msg, int64_t code);

struct JdbCapture { int saved_fd; FILE* file; std::string path; };
static std::vector<JdbCapture> g_captures;

static char* capture_text(FILE* f) {
    fflush(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) n = 0;
    fseek(f, 0, SEEK_SET);
    char* out = (char*)malloc((size_t)n + 1);
    size_t got = n ? fread(out, 1, (size_t)n, f) : 0;
    out[got] = '\0';
    fseek(f, 0, SEEK_END);
    return out;
}

void jdb_output_capture_begin() {
    jdb_guard_void([&] {
        fflush(stdout);
        JdbCapture c;
    #ifdef _WIN32
        char* tmp = _tempnam(nullptr, "jdbcap");
        c.path = tmp ? tmp : "jdbcap.tmp";
        free(tmp);
        g_captures.reserve(g_captures.size() + 1);
        c.file = fopen(c.path.c_str(), "w+b");
        if (!c.file) return;
        c.saved_fd = _dup(_fileno(stdout));
        _dup2(_fileno(c.file), _fileno(stdout));
    #else
        g_captures.reserve(g_captures.size() + 1);
        c.file = tmpfile();
        if (!c.file) return;
        c.saved_fd = dup(fileno(stdout));
        dup2(fileno(c.file), fileno(stdout));
    #endif
        g_captures.push_back(std::move(c));
    });
}

char* jdb_output_capture_end() {
    return jdb_guard([&]() -> char* {
        fflush(stdout);
        if (g_captures.empty()) {
            jdb_err_set("OUTPUT.CAPTURE_END$: no capture is active", 1);
            return _strdup("");
        }
        JdbCapture c = g_captures.back();
        g_captures.pop_back();
    #ifdef _WIN32
        _dup2(c.saved_fd, _fileno(stdout));
        _close(c.saved_fd);
    #else
        dup2(c.saved_fd, fileno(stdout));
        close(c.saved_fd);
    #endif
        char* text = capture_text(c.file);
        fclose(c.file);
        if (!c.path.empty()) remove(c.path.c_str());
        return text;
    }, [] { return _strdup(""); });
}

char* jdb_output_capture_peek() {
    return jdb_guard([&]() -> char* {
        fflush(stdout);
        if (g_captures.empty()) {
            jdb_err_set("OUTPUT.CAPTURE_PEEK$: no capture is active", 1);
            return _strdup("");
        }
        return capture_text(g_captures.back().file);
    }, [] { return _strdup(""); });
}

// ── Exception state (THROW/TRY/CATCH) ───────────────────────
// Generated code branches to its catch-block on throw; we only
// store the message/code here so ERRMSG$/ERR can read them in
// the CATCH body.

// Per thread: an ASYNC worker's THROW and CATCH never reach the error
// checks of another thread.
static thread_local char    g_err_msg[512] = "";
static thread_local int64_t g_err_code     = 0;
// Shadow copies that persist through a catch body. The per-statement err
// check needs g_err_code to go to zero on catch entry or it would keep
// re-tripping; user code inside the catch still expects ERR / ERRMSG$
// to return the caught values. Reads fall back to these shadows.
static thread_local char    g_last_msg[512] = "";
static thread_local int64_t g_last_code     = 0;
// The line the error happened on, which is the first check to see it. The
// checks of the callers above it see the same error as it travels out, and
// they must not overwrite the line with their own.
static thread_local int64_t g_err_line      = 0;
static thread_local int64_t g_last_line     = 0;

void jdb_err_set_line(int64_t line) {
    if (g_err_line == 0 && line > 0) {
        g_err_line = line;
        g_last_line = line;
    }
}

int64_t jdb_err_line() {
    return g_err_line ? g_err_line : g_last_line;
}

// The call stack as the interpreter reports it, which is the line and
// nothing else - even from inside nested calls it answers "line N".
char* jdb_err_stack() {
    char buf[32];
    int64_t line = jdb_err_line();
    if (line <= 0) return _strdup("");
    snprintf(buf, sizeof buf, "line %lld", (long long)line);
    return _strdup(buf);
}

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
    // A new error gets a fresh line: the next check to see it records one.
    g_err_line = 0;
}

void jdb_err_clear() {
    g_err_msg[0] = '\0';
    g_err_code = 0;
    g_last_msg[0] = '\0';
    g_last_code = 0;
    g_err_line = 0;
    g_last_line = 0;
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
// `END <code>`: the program asked for this exit status, from wherever it
// stands. Matches the interpreter, which halts the whole run rather than
// returning from the enclosing FUNC.
// The window and the audio device close before exit() runs the atexit
// handlers, which would reach a graphics driver that is already gone.
#ifdef _WIN32
__declspec(dllimport)
#endif
void jdrt_close_devices(void);

static void close_devices() {
    jdrt_close_devices();
}

// A program that ends inside OUTPUT.CAPTURE_BEGIN gets its stdout back and
// leaves no capture file behind.
static void abandon_captures() {
    fflush(stdout);
    while (!g_captures.empty()) {
        JdbCapture c = g_captures.back();
        g_captures.pop_back();
#ifdef _WIN32
        _dup2(c.saved_fd, _fileno(stdout));
        _close(c.saved_fd);
#else
        dup2(c.saved_fd, fileno(stdout));
        close(c.saved_fd);
#endif
        fclose(c.file);
        if (!c.path.empty()) remove(c.path.c_str());
    }
}

void jdb_end_program(int64_t code) {
    abandon_captures();
    fflush(stdout);
    fflush(stderr);
    close_devices();
    exit((int)code);
}

void jdb_throw_uncaught() {
    abandon_captures();
    fprintf(stderr, "Unhandled exception: %s\n", g_err_msg);
    fflush(stderr);
    close_devices();
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
#ifdef NET
    if (strcmp(up, "NET")         == 0) return 1;
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
#ifdef FORMS
    if (strcmp(up, "FORMS")       == 0) return 1;
#endif
#ifdef OPENGL
    if (strcmp(up, "OPENGL")      == 0) return 1;
#endif
#ifdef MINIAUDIO
    if (strcmp(up, "MINIAUDIO")   == 0) return 1;
#endif
#if defined(GFX) || defined(SOUND_DSP)
    if (strcmp(up, "SOUND")       == 0) return 1;
#endif
#ifdef FX
    if (strcmp(up, "FX")          == 0) return 1;
#endif
#ifdef MIDI
    if (strcmp(up, "MIDI")        == 0) return 1;
#endif
#ifdef MCPSERVER
    if (strcmp(up, "MCPSERVER")   == 0) return 1;
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

#ifdef _WIN32
__declspec(dllimport)
#endif
void jdrt_forget_binary(const char* s);

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

// _strdup for values that may carry an interior NUL: copies the registered
// byte length and re-registers the copy, so the duplicate reports the same
// LEN as the original instead of the strlen prefix.
static char* jdb_str_dup_binary(const char* s) {
    if (!s) return _strdup("");
    int64_t n = jdb_str_blen(s);
    char* r = (char*)malloc((size_t)n + 1);
    if (n) memcpy(r, s, (size_t)n);
    r[n] = '\0';
    if (n != (int64_t)strlen(r)) jdrt_register_binary(r, n);
    return r;
}

// A string on its way into jdb_text, with the registered byte length of a
// buffer that holds a NUL.
static inline jdb_text::View text_in(const char* s) {
    return jdb_text::View(s, s ? (size_t)jdb_str_blen(s) : 0);
}

// A string jdb_text answered, copied into a buffer of this runtime's own,
// registered with its length when it holds a NUL.
static char* text_out(const std::string& s) {
    char* r = (char*)malloc(s.size() + 1);
    if (!s.empty()) memcpy(r, s.data(), s.size());
    r[s.size()] = '\0';
    if (s.size() != strlen(r)) jdrt_register_binary(r, (int64_t)s.size());
    return r;
}

// ── String ownership ────────────────────────────────────────
//
// Codegen owns a runtime string in exactly two places: a variable slot it
// has proved is written only by plain assignment, and an expression
// temporary it has just produced and is about to consume. Both hand the
// buffer here when they are done with it. Everything else keeps the older
// behaviour of never freeing, so a pointer this file did not allocate can
// never reach jdb_str_drop.

char* jdb_str_own(const char* s) {
    return jdb_str_dup_binary(s);
}

void jdb_str_drop(char* s) {
    if (!s) return;
    jdrt_forget_binary(s);
    free(s);
}

// SETLOCALE: the process locale except for number parsing, as in the
// interpreter, and the punctuation PRINT and STR$ use. A bare name is tried
// with the .UTF-8 and .utf8 suffixes glibc wants.
void jdb_setlocale(const char* name) {
    jdb_guard_void([&] {
        std::string loc = name ? name : "C";
        const char* set = setlocale(LC_ALL, loc.c_str());
        if (!set && loc.find('.') == std::string::npos) {
            if (setlocale(LC_ALL, (loc + ".UTF-8").c_str())) loc += ".UTF-8";
            else if (setlocale(LC_ALL, (loc + ".utf8").c_str())) loc += ".utf8";
        }
        // Numbers keep reading with a point: VAL, CSV and JSON parse through
        // strtod, which follows LC_NUMERIC; the output takes the locale's
        // punctuation from g_punct.
        setlocale(LC_NUMERIC, "C");
        try { g_punct = jdb_num::punct_of(std::locale(loc.c_str())); } catch (...) {}
    });
}

char* jdb_int_to_str(int64_t val) {
    char buf[64];
    jdb_num::format_int(buf, sizeof(buf), val, g_punct);
    return _strdup(buf);
}

char* jdb_double_to_str(double val) {
    char buf[512];
    jdb_format_double(buf, sizeof(buf), val);
    return _strdup(buf);
}

// ── Math builtins ───────────────────────────────────────────

double jdb_abs(double x)       { return fabs(x); }
double jdb_sqr(double x)       { return sqrt(x); }
double jdb_sin(double x)       { return sin(x); }
double jdb_radians(double x)   { return x * (3.14159265358979323846 / 180.0); }
double jdb_degrees(double x)   { return x * (180.0 / 3.14159265358979323846); }
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
double jdb_val(const char* s)   { return jdb_num::parse_prefix(s); }
double jdb_rnd(double ignored)  { (void)ignored; return (double)rand() / ((double)RAND_MAX + 1.0); }
double jdb_random2(double lo, double hi) { return lo + (double)rand() / ((double)RAND_MAX + 1.0) * (hi - lo); }

// Extended math
double jdb_sinh(double x)      { return sinh(x); }
double jdb_cosh(double x)      { return cosh(x); }
double jdb_tanh(double x)      { return tanh(x); }
double jdb_atan2(double y, double x) { return atan2(y, x); }
double jdb_round(double x)     { return round(x); }
double jdb_round_p(double x, double places) { return jdb_num::round_places(x, places); }
double jdb_trunc(double x)     { return trunc(x); }
double jdb_sign(double x)      { return (double)jdb_num::sign(x); }
int64_t jdb_sign_i(double x)   { return jdb_num::sign(x); }
double jdb_clamp(double x, double lo, double hi) { return jdb_num::clamp(x, lo, hi); }
double jdb_fac(double n)       { return jdb_num::fac((int64_t)n); }
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

#ifdef _WIN32
__declspec(dllimport)
#endif
int jdrt_exec_yield_begin(void);
#ifdef _WIN32
__declspec(dllimport)
#endif
void jdrt_exec_yield_end(int depth);

// SLEEP: an HTTP handler may run while the program sleeps (exec_lock.h).
void jdb_sleep(int64_t ms) {
    if (ms <= 0) return;
    int held = jdrt_exec_yield_begin();
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
    jdrt_exec_yield_end(held);
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
                         // bit 4: elements are Unix-epoch dates (formatted)
    int8_t* elem_tags;   // optional per-element JdTag (NULL when not used).
                         // Allocated only by jdb_array_append_tagged so the
                         // common no-tags case stays cheap.
    int64_t capacity;    // slots allocated in data (and elem_tags when
                         // present); jdb_array_push grows in place up to it.
    JdbArray* parent;    // the matrix a row belongs to while no count knows
                         // the row (JDB_ARR_IMPLICIT_ROW); the array itself
                         // for a matrix holding such rows; null otherwise.
};

// Set on an array once a container written into one of its cells belongs to
// it (jdb_rc_adopt); a cell written over then lets go of what it held.
static const int32_t JDB_ARR_OWNS_CHILDREN = 1 << 12;
// The rows of a fresh matrix are its alone and stay out of the counts: such
// a row carries JDB_ARR_IMPLICIT_ROW and its matrix in `parent`, the matrix
// points `parent` at itself, and it frees them with itself. A row that is
// handed out (a cell read, a POP, a higher-order call's element, another
// array taking it) is entered into the counts first, owned by its matrix.
// The matrix's mark lives in `parent` and not in its flags, since many
// builders copy flags from a source array whose rows they share.
static const int32_t JDB_ARR_IMPLICIT_ROW = 1 << 13;
static inline bool rc_holds_rows(const JdbArray* a) { return a->parent == a; }
// Marks a row met once already during one rc_register_children pass.
static const int32_t JDB_ARR_ROW_SEEN = 1 << 15;
static void rc_cell_out(JdbArray* arr, int64_t idx);
static void rc_implicit_overwrite(JdbArray* arr, int64_t idx);
static void rc_disown_cell(const void* holder, double old);

// ── Cell kinds ──
// What one cell holds. An array with per-cell tags (bit 3) says so per cell;
// otherwise the array-wide bits describe every cell: bit 1 strings, bit 0
// rows, and only a value that looks like a pointer can be a row.
static inline bool arr_tagged(const JdbArray* a) { return (a->flags & 8) && a->elem_tags; }
static inline bool bits_look_ptr(double d) {
    union { double d; uint64_t u; } u; u.d = d;
    return u.u != 0 && u.u < (1ULL << 47);
}
static inline int cell_kind(const JdbArray* a, int64_t i) {
    if (arr_tagged(a)) return a->elem_tags[i];
    if (a->flags & 2) return JD_TAG_STR;
    if ((a->flags & 1) && bits_look_ptr(a->data[i])) return JD_TAG_ARR;
    return JD_TAG_F64;
}
static inline bool kind_is_num(int k) {
    return k == JD_TAG_F64 || k == JD_TAG_I64 || k == JD_TAG_BOOL || k == JD_TAG_DATE;
}
static inline bool cell_is_arr(const JdbArray* a, int64_t i) { return cell_kind(a, i) == JD_TAG_ARR; }
static inline bool cell_is_text(const JdbArray* a, int64_t i) { return cell_kind(a, i) == JD_TAG_STR; }
static inline bool cell_is_num(const JdbArray* a, int64_t i) { return kind_is_num(cell_kind(a, i)); }
// Every cell is a number: no rows, no strings, no per-cell tags.
static inline bool arr_all_num(const JdbArray* a) { return !(a->flags & 3) && !arr_tagged(a); }
// Every cell is a row: an untagged matrix.
static inline bool arr_all_rows(const JdbArray* a) { return (a->flags & 3) == 1 && !arr_tagged(a); }
static JdbArray* settle_kinds(JdbArray* r);

// Gives an array per-cell tags, as many as it has room for, the cells it has
// taking the kinds its flags gave them. False when there is no memory.
static bool ensure_tags(JdbArray* r) {
    if (arr_tagged(r)) return true;
    int64_t cap = r->capacity > r->length ? r->capacity : r->length;
    if (cap < 1) cap = 1;
    int8_t* tags = (int8_t*)malloc((size_t)cap);
    if (!tags) return false;
    memset(tags, JD_TAG_F64, (size_t)cap);
    for (int64_t j = 0; j < r->length; j++) tags[j] = (int8_t)cell_kind(r, j);
    free(r->elem_tags);
    r->elem_tags = tags;
    r->flags |= 8;
    return true;
}

// Writes cell i together with its kind, so cells of different kinds read
// back one by one.
static void put_kind(JdbArray* r, int64_t i, double v, int tag) {
    if (!ensure_tags(r)) { r->data[i] = 0.0; return; }
    if (tag == JD_TAG_STR || tag == JD_TAG_ARR || tag == JD_TAG_NATIVE_MAP) r->flags |= 1;
    if (tag == JD_TAG_STR) r->flags |= 2;
    r->elem_tags[i] = (int8_t)tag;
    r->data[i] = v;
}
// A string POP copied out of an array that owns its cells: the original,
// now past the end, is not reached by the array's own release.
static void rc_drop_popped_text(const void* arr, const char* s);
// A string cell about to be written over, in an array that owns its texts.
static void rc_drop_cell_text(JdbArray* arr, int64_t idx);

int32_t jdb_array_classify_elem(JdbArray* arr, double d);
struct JdbMap;
char* jdb_map_str(JdbMap* m);
char* jdb_cvdate_num(double epoch_secs);

JdbArray* jdb_array_new(int64_t size) {
    if (size < 0) size = 0;
    auto* arr = (JdbArray*)malloc(sizeof(JdbArray));
    if (!arr) jdb_fatal_oom();
    // Cells that do not fit: inside a builtin's guard this throws, so the
    // builtin raises an error TRY can catch and never writes the cells it
    // asked for through a null buffer. Called straight from compiled code
    // (DIM a(N)) it raises the error and answers an empty array.
    arr->data = (double*)calloc(size > 0 ? (size_t)size : 1, sizeof(double));
    if (!arr->data) {
        if (g_guard_depth > 0) {
            free(arr);
            throw std::bad_alloc();
        }
        jdb_err_set("Out of memory", 99);
        size = 0;
        arr->data = (double*)calloc(1, sizeof(double));
        if (!arr->data) jdb_fatal_oom();
    }
    arr->length = size;
    arr->flags = 0;
    arr->elem_tags = nullptr;
    arr->capacity = size;
    arr->parent = nullptr;
    return arr;
}

static void array_free_shallow(JdbArray* a);

// rows x cols, every cell fill, built in one piece.
JdbArray* jdb_array_new_2d(int64_t rows, int64_t cols, double fill) {
    return jdb_guard([&]() -> JdbArray* {
        if (rows < 0) rows = 0;
        if (cols < 0) cols = 0;
        JdbArray* outer = jdb_array_new(rows);
        outer->flags |= 1;
        int64_t made = 0;
        try {
            for (; made < rows; made++) {
                JdbArray* row = jdb_array_new(cols);
                if (fill != 0.0)
                    for (int64_t c = 0; c < cols; c++) row->data[c] = fill;
                outer->data[made] = encode_inner(row);
            }
        } catch (...) {
            // The rows made so far and the outer array belong to nobody yet.
            for (int64_t r = 0; r < made; r++) array_free_shallow(decode_inner(outer->data[r]));
            array_free_shallow(outer);
            throw;
        }
        return outer;
    }, [] { return jdb_array_new(0); });
}

// Appends in place, so every holder of the array sees the new element;
// this is what PUSH means. The value carries its JdTag.
JdbArray* jdb_array_push(JdbArray* arr, double val, int32_t tag) {
    if (!arr) arr = jdb_array_new(0);
    int64_t need = arr->length + 1;
    if (need > arr->capacity) {
        int64_t grown = arr->capacity * 2;
        if (grown < 8) grown = 8;
        if (grown < need) grown = need;
        double* data = (double*)realloc(arr->data, (size_t)grown * sizeof(double));
        if (!data) return arr;
        arr->data = data;
        if (arr->elem_tags) {
            int8_t* tags = (int8_t*)realloc(arr->elem_tags, (size_t)grown);
            if (!tags) return arr;
            arr->elem_tags = tags;
        }
        arr->capacity = grown;
    }
    if (!arr->elem_tags) {
        // The cells already there keep the kind the array-wide flags gave
        // them, spelled out per cell now that the array carries tags.
        arr->elem_tags = (int8_t*)malloc((size_t)arr->capacity);
        memset(arr->elem_tags, 0, (size_t)arr->capacity);
        for (int64_t i = 0; i < arr->length; i++)
            arr->elem_tags[i] = (int8_t)jdb_array_classify_elem(arr, arr->data[i]);
        arr->flags |= 8;
    }
    arr->data[arr->length] = keep_vm_handle(val, tag);
    arr->elem_tags[arr->length] = (int8_t)tag;
    // The array-wide kind bits (string, nested, bool) stay set while every
    // cell is of that kind and are dropped by the first cell that is not,
    // so the readers that only look at the flags stay right for the
    // homogeneous arrays they were written for.
    auto keep_kind = [&](int32_t bit, bool cell_is) {
        if (cell_is) { if (arr->length == 0 || (arr->flags & bit)) arr->flags |= bit; }
        else arr->flags &= ~bit;
    };
    keep_kind(2, tag == JD_TAG_STR);
    keep_kind(1, tag == JD_TAG_ARR || tag == JD_TAG_STR);
    keep_kind(4, tag == JD_TAG_BOOL);
    arr->length = need;
    return arr;
}

void jdb_array_set(JdbArray* arr, int64_t idx, double val) {
    if (arr && idx >= 0 && idx < arr->length) {
        rc_drop_cell_text(arr, idx);
        if (rc_holds_rows(arr)) rc_implicit_overwrite(arr, idx);
        if (arr->flags & JDB_ARR_OWNS_CHILDREN) rc_disown_cell(arr, arr->data[idx]);
        arr->data[idx] = val;
        return;
    }
    // As in the interpreter, a write outside the array is an error.
    std::string msg = "Array index out of bounds: " + std::to_string(idx);
    jdb_err_set(msg.c_str(), 13);
}

// One line from standard input for INPUT, without its line ending.
char* jdb_input_line(void) {
    fflush(stdout);
    char buf[4096];
    if (!fgets(buf, sizeof buf, stdin)) return _strdup("");
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return _strdup(buf);
}

// The cold half of every bounds check, kept out of the getters so their
// hot path stays a compare and a load.
static JDB_NOINLINE void jdb_index_out_of_bounds(int64_t idx) {
    char msg[64];
    snprintf(msg, sizeof msg, "Array index out of bounds: %lld", (long long)idx);
    jdb_err_set(msg, 13);
}

double jdb_array_get(JdbArray* arr, int64_t idx) {
    if (idx >= 0 && idx < arr->length) {
        if (arr->flags & (1 | 8)) rc_cell_out(arr, idx);
        return arr->data[idx];
    }
    jdb_index_out_of_bounds(idx);
    return 0.0;
}

// Fancy/vector indexing: arr[indices] returns a new array with the
// elements of arr at the positions given by indices (each position
// is interpreted as an int64). Preserves arr's flag bits so nested-
// pointer arrays stay flagged on the gathered result.
JdbArray* jdb_array_gather(JdbArray* arr, JdbArray* idx) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr || !idx) return jdb_array_new(0);
        auto* r = jdb_array_new(idx->length);
        bool tagged = arr_tagged(arr);
        r->flags = arr->flags & (1 | 2 | 4 | 16);
        for (int64_t i = 0; i < idx->length; i++) {
            // Indices arrive as plain f64s (IOTA, INT(...), arithmetic
            // results); double to int64 conversion is the right read here.
            int64_t k = (int64_t)idx->data[i];
            bool in = k >= 0 && k < arr->length;
            double v = in ? arr->data[k] : 0.0;
            if (tagged) put_kind(r, i, v, in ? arr->elem_tags[k] : JD_TAG_F64);
            else r->data[i] = v;
        }
        return r;
    }, [] { return jdb_array_new(0); });
}

int64_t jdb_array_len(JdbArray* arr) {
    return arr ? arr->length : 0;
}

JdbArray* jdb_iota(int64_t n) {
    return jdb_guard([&]() -> JdbArray* {
        auto* arr = jdb_array_new(n);
        for (int64_t i = 0; i < n; i++)
            arr->data[i] = (double)(i + 1);  // IOTA starts at 1
        return arr;
    }, [] { return jdb_array_new(0); });
}

// IOTA(n, start, step) - n elements starting at `start` stepping by `step`.
JdbArray* jdb_iota3(double n, double start, double step) {
    return jdb_guard([&]() -> JdbArray* {
        int64_t cnt = (int64_t)n;
        if (cnt < 0) cnt = 0;
        auto* arr = jdb_array_new(cnt);
        for (int64_t i = 0; i < cnt; i++)
            arr->data[i] = start + step * (double)i;
        return arr;
    }, [] { return jdb_array_new(0); });
}

// POP: remove last element, return its value (numeric as double, string as ptr
// via union). Caller knows the type via array flags. Returns 0 on empty.
double jdb_array_pop(JdbArray* arr) {
    if (!arr || arr->length == 0) return 0.0;
    int k = cell_kind(arr, arr->length - 1);
    if (arr->flags & (1 | 8)) rc_cell_out(arr, arr->length - 1);
    double v = arr->data[arr->length - 1];
    arr->length--;
    // The array no longer holds a container it gave away; the taker counts it.
    if (k == JD_TAG_ARR || k == JD_TAG_NATIVE_MAP) rc_disown_cell(arr, v);
    return v;
}

char* jdb_str(double val);
int64_t jdb_str_cmp(const char* a, const char* b);

// POP returning a string-typed element (array flags indicate string storage).
char* jdb_array_pop_str(JdbArray* arr) {
    if (!arr || arr->length == 0) return _strdup("");
    int k = cell_kind(arr, arr->length - 1);
    if (k != JD_TAG_STR) {
        // A row or a map taken this way is no longer held by the array.
        if (!kind_is_num(k)) rc_cell_out(arr, arr->length - 1);
        double v = arr->data[arr->length - 1];
        arr->length--;
        if (!kind_is_num(k)) rc_disown_cell(arr, v);
        return kind_is_num(k) ? jdb_str(v) : _strdup("");
    }
    union { double d; int64_t i; } u;
    u.d = arr->data[arr->length - 1];
    arr->length--;
    const char* s = (const char*)(intptr_t)u.i;
    char* copy = jdb_str_dup_binary(s);
    rc_drop_popped_text(arr, s);
    return copy;
}

// POP that says what it took. The last cell may be a number, a string, a
// nested array or a date, and only the cell knows: a number handed back as a
// string is dereferenced as a pointer by the first reader.
int64_t jdb_array_pop_tagged(JdbArray* arr, int32_t* out_tag) {
    if (out_tag) *out_tag = JD_TAG_NONE;
    if (!arr || arr->length == 0) return 0;
    if (arr->flags & (1 | 8)) rc_cell_out(arr, arr->length - 1);
    int64_t idx = arr->length - 1;
    int32_t tag = JD_TAG_F64;
    if (arr->elem_tags && (arr->flags & 8)) tag = (int32_t)arr->elem_tags[idx];
    else tag = jdb_array_classify_elem(arr, arr->data[idx]);
    union { double d; int64_t i; } u;
    u.d = arr->data[idx];
    int64_t bits = u.i;
    if (tag == JD_TAG_STR) {
        char* copy = jdb_str_dup_binary((const char*)(intptr_t)u.i);
        rc_drop_popped_text(arr, (const char*)(intptr_t)u.i);
        bits = (int64_t)(intptr_t)copy;
    } else if (tag == JD_TAG_I64 || tag == JD_TAG_BOOL) {
        bits = (int64_t)arr->data[idx];
    }
    arr->length--;
    if (tag == JD_TAG_ARR || tag == JD_TAG_NATIVE_MAP) rc_disown_cell(arr, arr->data[idx]);
    if (out_tag) *out_tag = tag;
    return bits;
}

// Statistics walk leaves, not cells: on a matrix the top-level cells are
// inner-array pointers, and summing those bits as numbers is meaningless.
double jdb_mean(JdbArray* arr) {
    return jdb_guard([&]() -> double {
        if (!arr || arr->length == 0) return 0.0;
        std::vector<double> flat;
        flatten_into(arr, flat);
        if (flat.empty()) return 0.0;
        double sum = 0.0;
        for (double d : flat) sum += d;
        return sum / (double)flat.size();
    }, [] { return 0.0; });
}

double jdb_stdev(JdbArray* arr) {
    return jdb_guard([&]() -> double {
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
    }, [] { return 0.0; });
}

// ── Array operations ────────────────────────────────────────

static inline const char* decode_str(double val);

// A fold over the leaves of an array, depth first. op: 0 sum, 1 product,
// 2 min, 3 max, 4 any, 5 all. A string or another non-number leaf counts as
// 0, except that ANY and ALL take a non-empty string as true.
struct LeafFold { int op; double acc; bool seen; };

static inline bool fold_one(LeafFold& f, double x) {
    switch (f.op) {
        case 0: f.acc += x; break;
        case 1: f.acc *= x; break;
        case 2: if (!f.seen || x < f.acc) f.acc = x; break;
        case 3: if (!f.seen || x > f.acc) f.acc = x; break;
        case 4: if (x != 0.0) { f.acc = 1.0; f.seen = true; return false; } break;
        case 5: if (x == 0.0) { f.acc = 0.0; f.seen = true; return false; } break;
    }
    f.seen = true;
    return true;
}

static bool fold_leaves(JdbArray* a, LeafFold& f) {
    if (!a) return true;
    if (arr_all_num(a)) {
        for (int64_t i = 0; i < a->length; i++)
            if (!fold_one(f, a->data[i])) return false;
        return true;
    }
    for (int64_t i = 0; i < a->length; i++) {
        int k = cell_kind(a, i);
        double x = 0.0;
        if (k == JD_TAG_ARR) {
            if (!fold_leaves(decode_inner(a->data[i]), f)) return false;
            continue;
        }
        if (kind_is_num(k)) x = a->data[i];
        else if (k == JD_TAG_STR && f.op >= 4) {
            const char* s = decode_str(a->data[i]);
            x = (s && *s) ? 1.0 : 0.0;
        }
        if (!fold_one(f, x)) return false;
    }
    return true;
}

static double fold_array(JdbArray* a, int op, double init) {
    LeafFold f{ op, init, false };
    fold_leaves(a, f);
    return f.acc;
}

double jdb_array_sum(JdbArray* arr) { return fold_array(arr, 0, 0.0); }

// Reduce a matrix along one axis. dim 0 walks down the rows and yields one
// value per column; dim 1 walks across each row. Mirrors reduce_along_axis
// in the interpreter; the op codes match JdbReduceOp there.
//   0 sum  1 min  2 max  3 product  4 mean
//   5 median  6 variance  7 stdev  8 any  9 all
static void shape_error(int64_t n, int64_t m);

// The native array model for the shape algorithms of jdb_shape.h: a cell is
// a double, a row an array pointer punned into one.
struct RtShape {
    using Arr = JdbArray*;
    using Val = double;
    static int64_t len(JdbArray* a) { return a ? a->length : 0; }
    static bool is_row(JdbArray* a, int64_t i) { return a && ::cell_is_arr(a, i); }
    static JdbArray* row(JdbArray* a, int64_t i) { return decode_inner(a->data[i]); }
    static double num(JdbArray* a, int64_t i) { return ::cell_is_num(a, i) ? a->data[i] : 0.0; }
    static double cell(JdbArray* a, int64_t i) { return a->data[i]; }
    static JdbArray* make(int64_t n) { return jdb_array_new(n); }
    static void put_num(JdbArray* a, int64_t i, double d) { a->data[i] = d; }
    static void put_bool(JdbArray* a, int64_t i, bool b) { a->data[i] = b ? 1.0 : 0.0; a->flags |= 4; }
    static void put_row(JdbArray* a, int64_t i, JdbArray* r) { a->data[i] = encode_inner(r); a->flags |= 1; }
    static void put(JdbArray* a, int64_t i, double d) { a->data[i] = d; }
    // A number written where a cell moved away: in an array of strings the
    // cells become tagged one by one so the number reads as a number.
    static void put_fill(JdbArray* a, int64_t i, double d) {
        if ((a->flags & 2) && !arr_tagged(a)) {
            if (!ensure_tags(a)) { a->data[i] = 0.0; return; }
            a->flags &= ~2;
        }
        if (a->elem_tags) a->elem_tags[i] = JD_TAG_F64;
        a->data[i] = d;
    }
    // Whether cell i holds a row, by the per-cell tags or the array's flags.
    static bool cell_is_row(JdbArray* a, int64_t i) { return ::cell_is_arr(a, i); }
    // A copy of the array down to its leaves, sharing no row with the source;
    // string cells are shared, since an array handed to a builtin never frees
    // its own strings.
    static bool same(JdbArray* a, JdbArray* b) { return a == b; }
    static JdbArray* clone(JdbArray* a) {
        return jdb_shape::take<RtShape>(a, jdb_shape::span_indices(len(a), 0, len(a)));
    }
    // A cell with the tag it carries, when the source keeps tags per cell; a
    // row cell is a clone of the row, so the answer outlives its source.
    static void copy_cell(JdbArray* out, int64_t k, JdbArray* a, int64_t i) {
        out->data[k] = cell_is_row(a, i) ? encode_inner(clone(decode_inner(a->data[i]))) : a->data[i];
        if (out->elem_tags && a->elem_tags) out->elem_tags[k] = a->elem_tags[i];
    }
    // What the source says about all its cells: strings, rows, truth values,
    // dates, and room for per-cell tags when it keeps them.
    static void carry_flags(JdbArray* out, JdbArray* a) {
        out->flags = a->flags;
        if ((a->flags & 8) && a->elem_tags) {
            out->elem_tags = (int8_t*)malloc((size_t)(out->length > 0 ? out->length : 1));
        } else {
            out->flags &= ~8;
        }
    }
    static void drop(JdbArray* a) { if (a) { free(a->data); free(a->elem_tags); free(a); } }
    static void shape_error(int64_t n, int64_t m) { ::shape_error(n, m); }
};

// A reducer along one axis of an n-dimensional array; the axis must exist.
JdbArray* jdb_array_reduce_axis(JdbArray* arr, int64_t dim, int32_t op) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr || arr->length == 0 || !(arr->flags & 1)) return jdb_array_new(0);
        int d = jdb_shape::depth<RtShape>(arr);
        if (dim < 0 || dim >= d) {
            char msg[96];
            snprintf(msg, sizeof msg, "axis %lld is outside a %d-D array", (long long)dim, d);
            jdb_err_set(msg, 11);
            return jdb_array_new(0);
        }
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
        return jdb_shape::reduce_axis<RtShape>(arr, (int)dim, reduce_lane, op == 8 || op == 9).a;
    }, [] { return jdb_array_new(0); });
}

double jdb_array_product(JdbArray* arr) { return fold_array(arr, 1, 1.0); }
double jdb_array_min(JdbArray* arr) { return fold_array(arr, 2, 0.0); }
double jdb_array_max(JdbArray* arr) { return fold_array(arr, 3, 0.0); }
int64_t jdb_array_any(JdbArray* arr) { return fold_array(arr, 4, 0.0) != 0.0; }

// Dot product on flat double buffers, past the bridge's
// jdbarray_to_value conversion, which allocates 2*N Values before the
// loop starts. At 4M elements that is 7 ms here against 91 ms through
// the conversion.
double jdb_array_dot(JdbArray* a, JdbArray* b) {
    if (!a || !b) return 0.0;
    int64_t n = a->length < b->length ? a->length : b->length;
    double s = 0.0;
    if (arr_all_num(a) && arr_all_num(b)) {
        for (int64_t i = 0; i < n; i++) s += a->data[i] * b->data[i];
        return s;
    }
    for (int64_t i = 0; i < n; i++)
        if (cell_is_num(a, i) && cell_is_num(b, i)) s += a->data[i] * b->data[i];
    return s;
}

int64_t jdb_array_all(JdbArray* arr) { return fold_array(arr, 5, 1.0) != 0.0; }

struct RtShape;
JdbArray* jdb_array_reverse(JdbArray* arr);

// Whether cell i holds a string, by the per-cell tags or the array's flag.
static bool rt_cell_is_str(JdbArray* arr, int64_t i) {
    if ((arr->flags & 8) && arr->elem_tags) return arr->elem_tags[i] == JD_TAG_STR;
    return (arr->flags & 2) != 0;
}

static const char* rt_cell_str(JdbArray* arr, int64_t i) {
    union { double d; int64_t b; } u; u.d = arr->data[i];
    const char* p = (const char*)(intptr_t)u.b;
    return p ? p : "";
}

// Orders indices by the cells they name: strings by content, numbers by
// value, a string after every number. Used by SORT and GRADE.
static void jdb_order_indices(JdbArray* arr, std::vector<int64_t>& idx) {
    idx = jdb_shape::order(arr->length, [&](int64_t a, int64_t b) {
        bool sa = rt_cell_is_str(arr, a), sb = rt_cell_is_str(arr, b);
        if (sa && sb) return strcmp(rt_cell_str(arr, a), rt_cell_str(arr, b)) < 0;
        if (sa != sb) return !sa;
        return arr->data[a] < arr->data[b];
    });
}

// The tag a cell carries when the array keeps none per cell: the
// array-wide flags say whether a pointer-looking value is a string or
// a nested array.
static int8_t jdb_tag_from_flags(int32_t flags, double d) {
    union { double d; uint64_t u; } u; u.d = d;
    bool looks_ptr = (u.u != 0 && u.u < (1ULL << 47));
    if (looks_ptr && (flags & 1)) return (flags & 2) ? JD_TAG_STR : JD_TAG_ARR;
    if (flags & 4) return JD_TAG_BOOL;
    return JD_TAG_F64;
}

JdbArray* jdb_array_append(JdbArray* arr, double val) {
    return jdb_guard([&]() -> JdbArray* {
        int64_t newlen = arr ? arr->length + 1 : 1;
        auto* r = jdb_array_new(newlen);
        if (arr) memcpy(r->data, arr->data, arr->length * sizeof(double));
        r->data[newlen - 1] = val;
        if (arr) {
            r->flags = arr->flags;  // preserve element-type marker
            if (arr->elem_tags) {
                r->elem_tags = (int8_t*)malloc(newlen);
                memcpy(r->elem_tags, arr->elem_tags, arr->length);
                r->elem_tags[newlen - 1] = jdb_tag_from_flags(arr->flags, val);
            }
        }
        return r;
    }, [] { return jdb_array_new(0); });
}

// Append a value carrying its own JdTag. Allocates the per-element tags
// array on first tagged append and sets the JD_ARR_FLAG_TAGGED bit.
// Used by ARRAY_LITERAL codegen when an element is RUNTIME-tagged
// (e.g. `[m{"name"}, m{"age"}, m{"email"}]` from map-of-mixed-types) -
// without per-cell tags the consumer can't tell strings from numbers.
// Writes a cell together with the kind it holds, so an array whose
// cells are of different kinds can be read back one cell at a time.
void jdb_array_set_tagged(JdbArray* arr, int64_t idx, double val, int32_t tag) {
    if (!arr || idx < 0 || idx >= arr->length) {
        jdb_index_out_of_bounds(idx);
        return;
    }
    if (!ensure_tags(arr)) return;
    if (tag == JD_TAG_STR || tag == JD_TAG_ARR || tag == JD_TAG_NATIVE_MAP)
        arr->flags |= 1;
    if (rc_holds_rows(arr)) rc_implicit_overwrite(arr, idx);
    rc_drop_cell_text(arr, idx);
    if (tag == JD_TAG_STR) arr->flags |= 2;
    if (arr->flags & JDB_ARR_OWNS_CHILDREN) rc_disown_cell(arr, arr->data[idx]);
    arr->data[idx] = keep_vm_handle(val, tag);
    arr->elem_tags[idx] = (int8_t)tag;
}

void jdrt_tagged_arr_set(void* rt, int64_t val_bits, int32_t val_tag,
                         int64_t idx, int64_t bits, int32_t tag);

static int rc_kind_of(uintptr_t k);
static bool rc_owns_texts(uintptr_t k);

void jdb_map_set_tagged(JdbMap* m, const char* key, double val, int32_t tag);

// A key written on a value whose kind only the running program knows, other
// than a VM value: a native map takes it, and so does a map an untyped
// parameter carries as a number; anything else has no keys, as in the
// interpreter.
void jdb_tagged_key_set(int64_t base, int32_t base_tag, const char* key, double val, int32_t tag) {
    // Not every map is in the counts, so pointer bits are taken for a map
    // unless the counts know them as an array.
    if (base_tag == JD_TAG_F64 &&
        (uint64_t)base >= 4096 && (uint64_t)base < (1ULL << 47) &&
        rc_kind_of((uintptr_t)base) != JD_TAG_ARR)
        base_tag = JD_TAG_NATIVE_MAP;
    if (base_tag != JD_TAG_NATIVE_MAP || !base) {
        jdb_err_set("Cannot index-assign into this type", 99);
        return;
    }
    jdb_map_set_tagged((JdbMap*)(intptr_t)base, key, val, tag);
}

int64_t jdb_array_len(JdbArray* arr);
int64_t jdb_map_count(JdbMap* m);

// The container a value read without its kind names: pointer bits are one,
// a number is not, and indexing it raises the interpreter's error.
void* jdb_index_base(int64_t bits, double shown) {
    if ((uint64_t)bits >= 4096 && (uint64_t)bits < (1ULL << 47))
        return (void*)(intptr_t)bits;
    char text[64];
    jdb_num::format_double(text, sizeof(text), shown, g_punct);
    std::string msg = std::string("Cannot index into ") + text;
    jdb_err_set(msg.c_str(), 99);
    return nullptr;
}

// LEN of an f64 slot whose kind only the running program knows: pointer
// bits name a container (a map when the counts say so, otherwise an
// array); a number has length 0, as in the interpreter.
int64_t jdb_len_bits(double slot) {
    union { double d; uint64_t u; } c; c.d = slot;
    if (c.u < 4096 || c.u >= (1ULL << 47)) return 0;
    if (rc_kind_of((uintptr_t)c.u) == JD_TAG_NATIVE_MAP)
        return jdb_map_count((JdbMap*)(intptr_t)c.u);
    return jdb_array_len((JdbArray*)(intptr_t)c.u);
}

// The map an f64 slot carries as pointer bits, as an untyped parameter
// does. A number in the slot, or an array, has no keys: that raises the
// interpreter's error and answers null.
void* jdb_slot_map_for_write(double slot) {
    union { double d; uint64_t u; } c; c.d = slot;
    if (c.u >= 4096 && c.u < (1ULL << 47) && rc_kind_of((uintptr_t)c.u) != JD_TAG_ARR)
        return (void*)(intptr_t)c.u;
    jdb_err_set("Cannot index-assign into this type", 99);
    return nullptr;
}

// A position written on a value whose kind only the running program knows.
// A native array takes the cell with its kind; a map or a VM value goes to
// the bridge; a value without cells is an error, as in the interpreter.
void jdb_tagged_arr_set(int64_t base, int32_t base_tag, int64_t idx, int64_t bits, int32_t tag) {
    // An untyped parameter passes a container as a number: the counts say
    // which kind it is, and one they do not know is taken for an array.
    if (base_tag == JD_TAG_F64 &&
        (uint64_t)base >= 4096 && (uint64_t)base < (1ULL << 47))
        base_tag = rc_kind_of((uintptr_t)base) == JD_TAG_NATIVE_MAP ? JD_TAG_NATIVE_MAP : JD_TAG_ARR;
    if (base_tag == JD_TAG_ARR) {
        JdbArray* arr = (JdbArray*)(intptr_t)base;
        if (!arr) return;
        if (idx < 0 || idx >= arr->length) { jdb_index_out_of_bounds(idx); return; }
        double d;
        if (tag == JD_TAG_I64 || tag == JD_TAG_BOOL) d = (double)bits;
        else memcpy(&d, &bits, sizeof d);
        // A value written back into the cell it was read from stays as it is.
        if (tag != JD_TAG_STR && memcmp(&arr->data[idx], &d, sizeof d) == 0) return;
        // An array that owns its texts takes a string as its own copy, the way
        // a plain cell write does; any other keeps the pointer it is handed.
        if (tag == JD_TAG_STR && rc_owns_texts((uintptr_t)arr)) {
            char* copy = jdb_str_dup_binary((const char*)(intptr_t)bits);
            memcpy(&d, &copy, sizeof d);
        }
        // The text the cell held goes, a row of the array is let out; the
        // counts of a container in the cell are the caller's, as they were
        // when this write went through the bridge.
        rc_drop_cell_text(arr, idx);
        if (rc_holds_rows(arr)) rc_implicit_overwrite(arr, idx);
        arr->data[idx] = keep_vm_handle(d, tag);
        // An untyped value the compiler passes as a number may be a
        // container's bits: an array without per-cell tags keeps reading such
        // a cell by its bits, as it reads every other one.
        if (tag == JD_TAG_F64 && bits_look_ptr(d) && !arr_tagged(arr)) return;
        if (!ensure_tags(arr)) return;
        int32_t kind = tag == JD_TAG_I64 ? JD_TAG_F64 : tag;
        if (kind == JD_TAG_STR || kind == JD_TAG_ARR || kind == JD_TAG_NATIVE_MAP) arr->flags |= 1;
        if (kind == JD_TAG_STR) arr->flags |= 2;
        arr->elem_tags[idx] = (int8_t)kind;
        return;
    }
    if (base_tag == JD_TAG_NATIVE_MAP || base_tag == JD_TAG_VM_HANDLE) {
        if (g_jdrt_handle) jdrt_tagged_arr_set(g_jdrt_handle, base, base_tag, idx, bits, tag);
        return;
    }
    if (base_tag == JD_TAG_NONE) return;
    jdb_err_set("Cannot index-assign into this type", 11);
}

JdbArray* jdb_array_append_tagged(JdbArray* arr, double val, int32_t tag) {
    return jdb_guard([&]() -> JdbArray* {
        int64_t newlen = arr ? arr->length + 1 : 1;
        auto* r = jdb_array_new(newlen);
        if (arr) memcpy(r->data, arr->data, arr->length * sizeof(double));
        r->data[newlen - 1] = keep_vm_handle(val, tag);
        int32_t base_flags = arr ? arr->flags : 0;
        r->flags = base_flags | 8;  // bit 3: tagged elements present
        r->elem_tags = (int8_t*)malloc(newlen);
        if (arr && arr->elem_tags)
            memcpy(r->elem_tags, arr->elem_tags, arr->length);
        else if (arr)
            for (int64_t i = 0; i < arr->length; i++)
                r->elem_tags[i] = jdb_tag_from_flags(arr->flags, arr->data[i]);
        r->elem_tags[newlen - 1] = (int8_t)tag;
        auto keep_kind = [&](int32_t bit, bool cell_is) {
            if (cell_is) { if (newlen == 1 || (base_flags & bit)) r->flags |= bit; else r->flags &= ~bit; }
            else r->flags &= ~bit;
        };
        keep_kind(2, tag == JD_TAG_STR);
        keep_kind(1, tag == JD_TAG_ARR || tag == JD_TAG_STR);
        keep_kind(4, tag == JD_TAG_BOOL);
        return r;
    }, [] { return jdb_array_new(0); });
}

// An array literal is built one cell at a time; each step's array belongs
// to nobody but the next step, which frees it.
static void array_free_shallow(JdbArray* a) {
    if (!a) return;
    free(a->data);
    free(a->elem_tags);
    free(a);
}

// Room for one more cell in a literal's array, doubled as it grows, so a
// literal of n cells copies O(n) cells rather than O(n*n).
static void lit_room(JdbArray* a) {
    if (a->length < a->capacity) return;
    int64_t grown = a->capacity < 8 ? 8 : a->capacity * 2;
    double* data = (double*)realloc(a->data, (size_t)grown * sizeof(double));
    if (!data) throw std::bad_alloc();
    a->data = data;
    if (a->elem_tags) {
        int8_t* tags = (int8_t*)realloc(a->elem_tags, (size_t)grown);
        if (!tags) throw std::bad_alloc();
        a->elem_tags = tags;
    }
    a->capacity = grown;
}

// The array is the literal's own until it is done, so it grows in place; the
// cells and flags come out as jdb_array_append and _append_tagged make them.
JdbArray* jdb_array_lit_append(JdbArray* arr, double val) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_append(nullptr, val);
        lit_room(arr);
        arr->data[arr->length] = val;
        if (arr->elem_tags) arr->elem_tags[arr->length] = jdb_tag_from_flags(arr->flags, val);
        arr->length++;
        return arr;
    }, [] { return jdb_array_new(0); });
}

JdbArray* jdb_array_lit_append_tagged(JdbArray* arr, double val, int32_t tag) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_append_tagged(nullptr, val, tag);
        int32_t base_flags = arr->flags;
        if (!arr->elem_tags) {
            int64_t cap = arr->capacity > arr->length ? arr->capacity : arr->length;
            int8_t* tags = (int8_t*)malloc((size_t)(cap > 0 ? cap : 1));
            if (!tags) throw std::bad_alloc();
            for (int64_t i = 0; i < arr->length; i++)
                tags[i] = jdb_tag_from_flags(arr->flags, arr->data[i]);
            arr->elem_tags = tags;
        }
        lit_room(arr);
        arr->data[arr->length] = keep_vm_handle(val, tag);
        arr->elem_tags[arr->length] = (int8_t)tag;
        arr->flags = base_flags | 8;
        bool first = arr->length == 0;
        auto keep_kind = [&](int32_t bit, bool cell_is) {
            if (cell_is) { if (first || (base_flags & bit)) arr->flags |= bit; else arr->flags &= ~bit; }
            else arr->flags &= ~bit;
        };
        keep_kind(2, tag == JD_TAG_STR);
        keep_kind(1, tag == JD_TAG_ARR || tag == JD_TAG_STR);
        keep_kind(4, tag == JD_TAG_BOOL);
        arr->length++;
        return arr;
    }, [] { return jdb_array_new(0); });
}

// Read element + its per-cell JdTag. If no tags array, falls back to the
// existing classifier so callers always get a sensible tag.
double jdb_array_get_tagged(JdbArray* arr, int64_t idx, int32_t* out_tag) {
    if (!arr || idx < 0 || idx >= arr->length) {
        if (out_tag) *out_tag = JD_TAG_NONE;
        jdb_index_out_of_bounds(idx);
        return 0.0;
    }
    if (arr->flags & (1 | 8)) rc_cell_out(arr, idx);
    double v = arr->data[idx];
    if (out_tag) {
        if (arr->elem_tags && (arr->flags & 8)) {
            *out_tag = (int32_t)arr->elem_tags[idx];
        } else {
            // Fall back to flag-based / heuristic classifier.
            *out_tag = jdb_array_classify_elem(arr, v);
        }
        // An integer or bool cell holds a real double; the caller reads the
        // returned bits as the integer its tag names.
        if (*out_tag == JD_TAG_I64 || *out_tag == JD_TAG_BOOL) {
            int64_t iv = (int64_t)v;
            memcpy(&v, &iv, sizeof v);
        }
    }
    return v;
}

JdbArray* jdb_array_append_arr(JdbArray* a, JdbArray* b) {
    return jdb_guard([&]() -> JdbArray* {
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
        r->flags |= (fa | fb) & 7;
        // When either side carries per-cell tags, or the two sides hold
        // different kinds, every cell of the result gets its own tag.
        bool tagged = ((fa & 8) && a && a->elem_tags) || ((fb & 8) && b && b->elem_tags) ||
                      (alen > 0 && blen > 0 && (fa & 7) != (fb & 7));
        if (tagged) {
            r->elem_tags = (int8_t*)malloc((size_t)(alen + blen > 0 ? alen + blen : 1));
            for (int64_t i = 0; i < alen; i++)
                r->elem_tags[i] = (a->elem_tags && (fa & 8)) ? a->elem_tags[i]
                                                            : (int8_t)jdb_array_classify_elem(a, a->data[i]);
            for (int64_t i = 0; i < blen; i++)
                r->elem_tags[alen + i] = (b->elem_tags && (fb & 8)) ? b->elem_tags[i]
                                                                   : (int8_t)jdb_array_classify_elem(b, b->data[i]);
            r->flags |= 8;
        }
        return r;
    }, [] { return jdb_array_new(0); });
}

// FILLV: bulk in-place fill of an array, rows recursed; a string cell
// becomes the number too, a map or a handle stays as it is. Returns the
// same array for chaining.
JdbArray* jdb_array_fillv(JdbArray* arr, double val) {
    if (!arr) return nullptr;
    if (arr_all_num(arr)) {
        if (arr->length > 0) std::fill_n(arr->data, arr->length, val);
        return arr;
    }
    for (int64_t i = 0; i < arr->length; i++) {
        int k = cell_kind(arr, i);
        if (k == JD_TAG_ARR) {
            jdb_array_fillv(decode_inner(arr->data[i]), val);
        } else if (kind_is_num(k)) {
            arr->data[i] = val;
        } else if (k == JD_TAG_STR) {
            rc_drop_cell_text(arr, i);
            put_kind(arr, i, val, JD_TAG_F64);
        }
    }
    settle_kinds(arr);
    return arr;
}

// COPYV helper: walk the number cells of dst and assign cyclically from
// the flat src vector.
static void copyv_assign(JdbArray* arr, const std::vector<double>& src, size_t& si) {
    if (!arr) return;
    bool plain = arr_all_num(arr);
    for (int64_t i = 0; i < arr->length; i++) {
        int k = plain ? JD_TAG_F64 : cell_kind(arr, i);
        if (k == JD_TAG_ARR) {
            copyv_assign(decode_inner(arr->data[i]), src, si);
        } else if (kind_is_num(k)) {
            arr->data[i] = src[si % src.size()];
            si++;
        }
    }
}

// COPYV: bulk in-place copy from src into dst with cyclic broadcast if
// shapes differ. Memcpy fast-path when both are flat numbers with matching
// lengths; otherwise the number leaves of src fill the number cells of dst.
// Never throws on size mismatch - broadcasts/truncates/cycles instead.
JdbArray* jdb_array_copyv(JdbArray* dst, JdbArray* src) {
    if (!dst) return nullptr;
    if (!src) return dst;
    if (arr_all_num(dst) && arr_all_num(src) &&
        dst->length == src->length && dst->length > 0) {
        std::memcpy(dst->data, src->data, dst->length * sizeof(double));
        return dst;
    }
    try {
        std::vector<double> src_flat;
        flatten_into(src, src_flat);
        if (src_flat.empty()) return dst;
        size_t si = 0;
        copyv_assign(dst, src_flat, si);
    } catch (...) {
        jdb_err_set("COPYV: out of memory", 1);
    }
    return dst;
}

int64_t jdb_array_count(JdbArray* arr, double val) {
    if (!arr) return 0;
    int64_t c = 0;
    bool plain = arr_all_num(arr);
    for (int64_t i = 0; i < arr->length; i++)
        if (arr->data[i] == val && (plain || cell_is_num(arr, i))) c++;
    return c;
}

int64_t jdb_array_indexof(JdbArray* arr, double val) {
    if (!arr) return -1;
    bool plain = arr_all_num(arr);
    for (int64_t i = 0; i < arr->length; i++)
        if (arr->data[i] == val && (plain || cell_is_num(arr, i))) return i;
    return -1;
}

// INDEXOF over a string array: the cells are pointers, so the match is by
// content. Comparing the punned bits only ever found a needle that happened
// to be the same interned literal.
int64_t jdb_array_indexof_str(JdbArray* arr, const char* needle) {
    if (!arr || !needle) return -1;
    for (int64_t i = 0; i < arr->length; i++) {
        union { double d; int64_t i; } u; u.d = arr->data[i];
        if (!cell_is_text(arr, i)) continue;
        const char* s = (const char*)(intptr_t)u.i;
        if (s && jdb_str_cmp(s, needle) == 0) return i;
    }
    return -1;
}

// IN operator on arrays: returns 1 if needle is a member, else 0.
int64_t jdb_array_has_str(JdbArray* arr, const char* needle) {
    if (!arr || !needle) return 0;
    for (int64_t i = 0; i < arr->length; i++) {
        union { double d; int64_t i; } u; u.d = arr->data[i];
        if (!cell_is_text(arr, i)) continue;
        const char* s = (const char*)(intptr_t)u.i;
        if (s && jdb_str_cmp(s, needle) == 0) return 1;
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

static bool cell_equality_key(JdbArray* a, int64_t i, std::string& key);

// UNIQUE: the first of every group of equal cells, strings by content and
// numbers by value, whether the array says string per cell or as a whole.
JdbArray* jdb_array_unique(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        // The first cell of every group of equal ones, in order.
        std::unordered_set<std::string> seen;
        std::vector<int64_t> kept;
        std::string key;
        for (int64_t i = 0; i < arr->length; i++)
            if (!cell_equality_key(arr, i, key) || seen.insert(key).second) kept.push_back(i);
        return jdb_shape::take<RtShape>(arr, kept);
    }, [] { return jdb_array_new(0); });
}

JdbArray* jdb_array_unique_str(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        return jdb_array_unique(arr);
    }, [] { return jdb_array_new(0); });
}

// CUMSUM and CUMPROD run along the cells, into the rows of a nested array.
JdbArray* jdb_array_cumsum(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        return jdb_shape::cumulate<RtShape>(arr, 0.0, [](const double& acc, const double& x) { return acc + x; });
    }, [] { return jdb_array_new(0); });
}

JdbArray* jdb_array_cumprod(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        return jdb_shape::cumulate<RtShape>(arr, 1.0, [](const double& acc, const double& x) { return acc * x; });
    }, [] { return jdb_array_new(0); });
}

// TAKE and DROP: the first or last n cells, or all but them, by
// jdb_shape::take over take_count and drop_count.
JdbArray* jdb_array_take(JdbArray* arr, int64_t n) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        return jdb_shape::take<RtShape>(arr, jdb_shape::take_count(arr->length, n));
    }, [] { return jdb_array_new(0); });
}

// (n, arr) parameter order to match the interpreter's TAKE(n, arr).
JdbArray* jdb_take_n(int64_t n, JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        return jdb_array_take(arr, n);
    }, [] { return jdb_array_new(0); });
}

JdbArray* jdb_array_drop(JdbArray* arr, int64_t n) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        return jdb_shape::take<RtShape>(arr, jdb_shape::drop_count(arr->length, n));
    }, [] { return jdb_array_new(0); });
}

JdbArray* jdb_drop_n(int64_t n, JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        return jdb_array_drop(arr, n);
    }, [] { return jdb_array_new(0); });
}

// The entries of the shift vector; more of them than the array has axes is
// an error.
static bool rt_shift_vector(const char* fn, JdbArray* arr, JdbArray* shifts, std::vector<int64_t>& out) {
    out.clear();
    if (shifts) for (int64_t i = 0; i < shifts->length; i++) out.push_back((int64_t)shifts->data[i]);
    int rank = jdb_shape::depth<RtShape>(arr);
    if ((int)out.size() > rank) {
        char msg[96];
        snprintf(msg, sizeof msg, "%s got %d shifts for an array of rank %d", fn, (int)out.size(), rank);
        jdb_err_set(msg, 11);
        return false;
    }
    return true;
}

// ROTATE and SHIFT move the cells along one axis per entry of the shift
// vector, by jdb_shape::move_axes; the answer is always a fresh array.
JdbArray* jdb_array_rotate(JdbArray* arr, JdbArray* shifts) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        std::vector<int64_t> k;
        if (!rt_shift_vector("ROTATE", arr, shifts, k)) return jdb_array_new(0);
        JdbArray* r = jdb_shape::move_axes<RtShape>(arr, k, 0, true, 0.0);
        return r == arr ? RtShape::clone(arr) : r;
    }, [] { return jdb_array_new(0); });
}

JdbArray* jdb_array_shift(JdbArray* arr, JdbArray* shifts, double fill) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        std::vector<int64_t> k;
        if (!rt_shift_vector("SHIFT", arr, shifts, k)) return jdb_array_new(0);
        JdbArray* r = jdb_shape::move_axes<RtShape>(arr, k, 0, false, fill);
        return r == arr ? RtShape::clone(arr) : r;
    }, [] { return jdb_array_new(0); });
}

// SLICE(matrix, axis, index): a row or a column of a matrix; a flat array,
// an axis other than 0 or 1 and an index outside the matrix are errors.
JdbArray* jdb_array_slice(JdbArray* arr, int64_t dim, int64_t idx) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        int64_t rows = arr->length;
        int64_t cols = (rows > 0 && RtShape::is_row(arr, 0)) ? RtShape::len(RtShape::row(arr, 0)) : 0;
        char msg[128];
        if (cols == 0) {
            jdb_err_set("SLICE: argument 1 is a flat array, a matrix is expected", 11);
            return jdb_array_new(0);
        }
        if (dim != 0 && dim != 1) {
            snprintf(msg, sizeof msg, "SLICE: the axis is 0 for a row or 1 for a column, got %lld", (long long)dim);
            jdb_err_set(msg, 11);
            return jdb_array_new(0);
        }
        int64_t extent = dim == 0 ? rows : cols;
        if (idx < 0 || idx >= extent) {
            snprintf(msg, sizeof msg, "SLICE: index %lld is outside 0 to %lld", (long long)idx, (long long)(extent - 1));
            jdb_err_set(msg, 13);
            return jdb_array_new(0);
        }
        if (dim == 0) return RtShape::clone(RtShape::row(arr, idx));
        for (int64_t ro = 0; ro < rows; ro++) {
            JdbArray* row = RtShape::is_row(arr, ro) ? RtShape::row(arr, ro) : nullptr;
            if (!row || idx >= row->length) {
                snprintf(msg, sizeof msg, "SLICE: row %lld has no column %lld", (long long)ro, (long long)idx);
                jdb_err_set(msg, 13);
                return jdb_array_new(0);
            }
        }
        return jdb_shape::column<RtShape>(arr, idx);
    }, [] { return jdb_array_new(0); });
}

// SLICE(matrix, axis, start, count): a band of rows, or of columns out of
// every row.
JdbArray* jdb_array_slice_band(JdbArray* arr, int64_t dim, int64_t start, int64_t count) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        if (dim == 0) return jdb_shape::take<RtShape>(arr, jdb_shape::span_indices(arr->length, start, count));
        if (dim != 1) {
            char msg[96];
            snprintf(msg, sizeof msg, "SLICE: the axis is 0 for rows or 1 for columns, got %lld", (long long)dim);
            jdb_err_set(msg, 11);
            return jdb_array_new(0);
        }
        for (int64_t i = 0; i < arr->length; i++) {
            if (!RtShape::is_row(arr, i)) {
                jdb_err_set("SLICE: argument 1 is a flat array, a matrix is expected", 11);
                return jdb_array_new(0);
            }
        }
        JdbArray* out = jdb_array_new(arr->length);
        for (int64_t i = 0; i < arr->length; i++) {
            JdbArray* row = RtShape::row(arr, i);
            RtShape::put_row(out, i, jdb_shape::take<RtShape>(row, jdb_shape::span_indices(row->length, start, count)));
        }
        return out;
    }, [] { return jdb_array_new(0); });
}

static bool rt_cell_is_str(JdbArray* arr, int64_t i);
static const char* rt_cell_str(JdbArray* arr, int64_t i);

// What two cells are equal by, as UNIQUE and DIFF compare them: a string by
// its text, anything else by its value, -0 as 0. A NaN equals nothing, so it
// has no key (false).
static bool cell_equality_key(JdbArray* a, int64_t i, std::string& key) {
    key.clear();
    if (rt_cell_is_str(a, i)) {
        key += 's';
        key += rt_cell_str(a, i);
        return true;
    }
    double d = a->data[i];
    if (d != d) return false;
    if (d == 0.0) d = 0.0;
    key += 'n';
    key.append((const char*)&d, sizeof d);
    return true;
}

// DIFF(a, b): the cells of a that b has no equal of, strings by content.
JdbArray* jdb_array_diff(JdbArray* a, JdbArray* b) {
    return jdb_guard([&]() -> JdbArray* {
        if (!a) return jdb_array_new(0);
        std::unordered_set<std::string> in_b;
        std::string key;
        if (b) for (int64_t j = 0; j < b->length; j++)
            if (cell_equality_key(b, j, key)) in_b.insert(key);
        std::vector<int64_t> kept = jdb_shape::select_indices(a->length, [&](int64_t i) {
            return !cell_equality_key(a, i, key) || !in_b.count(key);
        });
        return jdb_shape::take<RtShape>(a, kept);
    }, [] { return jdb_array_new(0); });
}

// Forward-declared; defined later alongside the arithmetic helpers.
static inline JdbArray* decode_inner(double val);

// Every leaf of an array, depth first. With `tags`, a leaf keeps its cell
// and its kind; without, a leaf that is no number counts as 0.
static void flatten_into(JdbArray* arr, std::vector<double>& out, std::vector<int8_t>* tags) {
    if (!arr) return;
    if (arr_all_num(arr)) {
        out.insert(out.end(), arr->data, arr->data + arr->length);
        if (tags) tags->insert(tags->end(), (size_t)arr->length, (int8_t)JD_TAG_F64);
        return;
    }
    for (int64_t i = 0; i < arr->length; i++) {
        int k = cell_kind(arr, i);
        if (k == JD_TAG_ARR) {
            flatten_into(decode_inner(arr->data[i]), out, tags);
        } else if (tags) {
            out.push_back(arr->data[i]);
            tags->push_back((int8_t)k);
        } else {
            out.push_back(kind_is_num(k) ? arr->data[i] : 0.0);
        }
    }
}

JdbArray* jdb_array_flatten(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        // No row anywhere: the cells are the leaves already.
        if (!arr_tagged(arr) && !arr_all_rows(arr)) {
            auto* r = jdb_array_new(arr->length);
            memcpy(r->data, arr->data, arr->length * sizeof(double));
            r->flags = arr->flags & (2 | 4 | 16);
            if (r->flags & 2) r->flags |= 1;
            return r;
        }
        try {
            std::vector<double> flat;
            std::vector<int8_t> kinds;
            flatten_into(arr, flat, &kinds);
            auto* r = jdb_array_new((int64_t)flat.size());
            if (!flat.empty()) memcpy(r->data, flat.data(), flat.size() * sizeof(double));
            bool all_num = true, all_str = true;
            for (int8_t k : kinds) {
                all_num = all_num && kind_is_num(k);
                all_str = all_str && k == JD_TAG_STR;
            }
            if (all_num) {
                r->flags = arr->flags & (4 | 16);
            } else if (all_str) {
                r->flags = 3;
            } else {
                for (size_t i = 0; i < kinds.size(); i++)
                    put_kind(r, (int64_t)i, flat[i], kinds[i]);
            }
            return r;
        } catch (...) {
            jdb_err_set("FLATTEN: out of memory", 1);
            return jdb_array_new(0);
        }
    }, [] { return jdb_array_new(0); });
}

// INSERT$ on an array: insert one cell at pos, clamped to [0, length].
// The tag travels with the value so a ragged result stays readable.
JdbArray* jdb_array_insert(JdbArray* arr, double val, int64_t pos, int32_t tag) {
    return jdb_guard([&]() -> JdbArray* {
        int64_t oldlen = arr ? arr->length : 0;
        if (pos < 0) pos = 0;
        if (pos > oldlen) pos = oldlen;
        auto* r = jdb_array_new(oldlen + 1);
        if (arr && pos > 0) memcpy(r->data, arr->data, pos * sizeof(double));
        r->data[pos] = keep_vm_handle(val, tag);
        if (arr && pos < oldlen)
            memcpy(r->data + pos + 1, arr->data + pos, (oldlen - pos) * sizeof(double));

        // The new cell fits the array-wide kind of an untagged source, or every
        // cell keeps its own tag.
        int src_kind = !arr || oldlen == 0 ? -1
                     : arr_tagged(arr) ? -2
                     : arr_all_num(arr) ? JD_TAG_F64
                     : (arr->flags & 2) ? JD_TAG_STR
                     : arr_all_rows(arr) ? JD_TAG_ARR : -2;
        int new_kind = kind_is_num(tag) ? JD_TAG_F64 : tag;
        if (src_kind == -1 && (new_kind == JD_TAG_F64 || new_kind == JD_TAG_STR || new_kind == JD_TAG_ARR)) {
            r->flags = new_kind == JD_TAG_STR ? 3 : new_kind == JD_TAG_ARR ? 1 : 0;
        } else if (src_kind == new_kind) {
            r->flags = arr->flags & (1 | 2 | 4 | 16);
        } else {
            r->flags = arr ? arr->flags & (4 | 16) : 0;
            for (int64_t i = 0; i < pos; i++) put_kind(r, i, r->data[i], cell_kind(arr, i));
            put_kind(r, pos, r->data[pos], tag);
            for (int64_t i = pos; i < oldlen; i++) put_kind(r, i + 1, r->data[i + 1], cell_kind(arr, i));
        }
        return r;
    }, [] { return jdb_array_new(0); });
}

JdbArray* jdb_array_shuffle(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        auto* r = jdb_array_new(arr->length);
        memcpy(r->data, arr->data, arr->length * sizeof(double));
        bool tagged = arr_tagged(arr);
        if (tagged) {
            r->elem_tags = (int8_t*)malloc((size_t)(arr->length > 0 ? arr->length : 1));
            if (r->elem_tags) memcpy(r->elem_tags, arr->elem_tags, (size_t)arr->length);
            else tagged = false;
        }
        for (int64_t i = r->length - 1; i > 0; i--) {
            int64_t j = jdb_shape::random_below(i + 1);
            double tmp = r->data[i]; r->data[i] = r->data[j]; r->data[j] = tmp;
            if (tagged) { int8_t t = r->elem_tags[i]; r->elem_tags[i] = r->elem_tags[j]; r->elem_tags[j] = t; }
        }
        r->flags = arr->flags & (1 | 2 | 4 | 16 | (tagged ? 8 : 0));
        return r;
    }, [] { return jdb_array_new(0); });
}

double jdb_array_median(JdbArray* arr) {
    return jdb_guard([&]() -> double {
        if (!arr || arr->length == 0) return 0.0;
        std::vector<double> flat;
        flatten_into(arr, flat);
        if (flat.empty()) return 0.0;
        std::sort(flat.begin(), flat.end());
        size_t n = flat.size();
        return (n % 2) ? flat[n/2] : (flat[n/2-1] + flat[n/2]) / 2.0;
    }, [] { return 0.0; });
}

double jdb_array_variance(JdbArray* arr) {
    return jdb_guard([&]() -> double {
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
    }, [] { return 0.0; });
}

JdbArray* jdb_linspace(double start, double stop, int64_t n) {
    return jdb_guard([&]() -> JdbArray* {
        if (n <= 0) return jdb_array_new(0);
        auto* r = jdb_array_new(n);
        if (n == 1) { r->data[0] = start; return r; }
        double step = (stop - start) / (n - 1);
        for (int64_t i = 0; i < n; i++) r->data[i] = start + i * step;
        return r;
    }, [] { return jdb_array_new(0); });
}

JdbArray* jdb_zeros(int64_t n) { return jdb_array_new(n); }

JdbArray* jdb_ones(int64_t n) {
    return jdb_guard([&]() -> JdbArray* {
        auto* r = jdb_array_new(n);
        for (int64_t i = 0; i < n; i++) r->data[i] = 1.0;
        return r;
    }, [] { return jdb_array_new(0); });
}

// start up to but not including stop; a step of 0 means 1 or -1 by the
// direction, as the interpreter's default does.
JdbArray* jdb_range(int64_t start, int64_t stop, int64_t step) {
    return jdb_guard([&]() -> JdbArray* {
        if (step == 0) step = start <= stop ? 1 : -1;
        int64_t span = step > 0 ? stop - start : start - stop;
        int64_t mag = step > 0 ? step : -step;
        if (span <= 0) return jdb_array_new(0);
        int64_t n = (span + mag - 1) / mag;
        auto* r = jdb_array_new(n);
        for (int64_t i = 0; i < n; i++) r->data[i] = (double)(start + i * step);
        return r;
    }, [] { return jdb_array_new(0); });
}

JdbArray* jdb_grade(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        std::vector<int64_t> idx;
        jdb_order_indices(arr, idx);
        auto* r = jdb_array_new(arr->length);
        for (int64_t i = 0; i < arr->length; i++) r->data[i] = (double)idx[(size_t)i];
        return r;
    }, [] { return jdb_array_new(0); });
}

// SORT: the cells in the order jdb_order_indices puts them.
JdbArray* jdb_array_sort(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        std::vector<int64_t> idx;
        jdb_order_indices(arr, idx);
        return jdb_shape::take<RtShape>(arr, idx);
    }, [] { return jdb_array_new(0); });
}

// SORT(array, descending): the ascending order, reversed when descending.
JdbArray* jdb_array_sort_dir(JdbArray* arr, double descending) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        std::vector<int64_t> idx;
        jdb_order_indices(arr, idx);
        if (descending != 0.0) std::reverse(idx.begin(), idx.end());
        return jdb_shape::take<RtShape>(arr, idx);
    }, [] { return jdb_array_new(0); });
}

// REVERSE: the cells from the last to the first.
JdbArray* jdb_array_reverse(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        std::vector<int64_t> idx((size_t)arr->length);
        for (int64_t i = 0; i < arr->length; i++) idx[(size_t)i] = arr->length - 1 - i;
        return jdb_shape::take<RtShape>(arr, idx);
    }, [] { return jdb_array_new(0); });
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

// A double as an int64 for the integer operators: NaN is 0 and a value
// outside the range is the nearest end of it, where a plain cast is undefined.
static inline int64_t to_i64(double d) {
    if (d != d) return 0;
    if (d >= 9223372036854775807.0) return INT64_MAX;
    if (d <= -9223372036854775808.0) return INT64_MIN;
    return (int64_t)d;
}

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
        case 4: return (double)(to_i64(a) & to_i64(b));   // BAND
        case 5: return (double)(to_i64(a) | to_i64(b));   // BOR
        case 6: return (double)(to_i64(a) ^ to_i64(b));   // BXOR
        case 7: {  // SHL; a count outside 0..63 shifts everything out
            return (double)jdb_num::shift_left(to_i64(a), to_i64(b));
        }
        case 8: {  // SHR (arith); past 63 only the sign is left
            return (double)jdb_num::shift_right(to_i64(a), to_i64(b));
        }
        case 9: { // MOD - integer modulo (matches interp's MOD on doubles)
            int64_t bi = to_i64(b);
            if (bi == 0) return 0;
            return (double)jdb_num::int_mod(to_i64(a), bi);
        }
        case 10: return std::pow(a, b);
        case 11: { // integer division, truncating toward zero
            int64_t bi = to_i64(b);
            if (bi == 0) return 0;
            return (double)jdb_num::int_div(to_i64(a), bi);
        }
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

// Nested: bit 0 set without the string bit, so the cells are rows.
static inline bool arr_nested(const JdbArray* a) { return a && (a->flags & 1) && !(a->flags & 2); }

static void shape_error(int64_t n, int64_t m) {
    char msg[96];
    snprintf(msg, sizeof msg, "shapes differ: %lld and %lld", (long long)n, (long long)m);
    jdb_err_set(msg, 11);
}

}  // extern "C": the band helpers and element-wise fast paths below are C++

static unsigned mat_workers(double work) {
    static const unsigned cores = (std::max)(1u, std::thread::hardware_concurrency());
    if (work < 2.0e6) return 1;
    return (unsigned)std::min<double>(cores, (std::max)(1.0, work / 1.0e6));
}

// Runs body(lo, hi) over [0, n) in up to `workers` bands at once.
template <class F>
static void mat_bands(int64_t n, unsigned workers, F body) {
    if (workers <= 1 || n < 2) { body(0, n); return; }
    if ((int64_t)workers > n) workers = (unsigned)n;
    // A band that throws, or a thread that cannot start, is reported once
    // every started thread is back.
    std::atomic<bool> failed{ false };
    std::vector<std::thread> pool;
    int64_t step = (n + workers - 1) / workers;
    int64_t done_to = step;
    for (unsigned w = 1; w < workers; w++) {
        int64_t lo = w * step, hi = (std::min)(n, lo + step);
        if (lo >= hi) break;
        try {
            pool.emplace_back([=, &failed] {
                // A band's allocation that fails throws, as on the thread
                // that started the bands.
                GuardDepth open;
                try { body(lo, hi); } catch (...) { failed = true; }
            });
            done_to = hi;
        } catch (...) {
            break;
        }
    }
    try {
        GuardDepth open;
        body(0, (std::min)(n, step));
        if (done_to < n) body(done_to, n);
    } catch (...) {
        failed = true;
    }
    for (auto& t : pool) t.join();
    if (failed) throw std::bad_alloc();
}

// ── Element-wise work on arrays of numbers ──────────────────
// A matrix here is rows that each are a flat array of numbers, all of one
// width. Such operands skip the generic shape walk: each answer row is
// computed in one loop, and a big answer is cut into row bands, one per core.
static unsigned ew_workers(int64_t cells) {
    static const unsigned cores = (std::max)(1u, std::thread::hardware_concurrency());
    if (cells < 100000) return 1;
    return (unsigned)(std::min<int64_t>)(cores, cells / 50000);
}

static inline bool ew_flat_numbers(const JdbArray* v) {
    return v && !(v->flags & (1 | 2)) && !((v->flags & 8) && v->elem_tags);
}

// The width of a matrix of numbers, or -1 for anything else.
static int64_t ew_matrix_width(const JdbArray* a) {
    if (!a || a->length == 0 || !arr_nested(a) || ((a->flags & 8) && a->elem_tags)) return -1;
    int64_t w = -1;
    for (int64_t i = 0; i < a->length; i++) {
        const JdbArray* r = decode_inner(a->data[i]);
        if (!ew_flat_numbers(r)) return -1;
        if (w < 0) w = r->length;
        else if (r->length != w) return -1;
    }
    return w;
}

static inline const double* ew_row(const JdbArray* a, int64_t i) {
    return decode_inner(a->data[i])->data;
}

// out[i] = x[i*xs] OP y[i*ys]; a stride of 0 repeats one value.
static void ew_kernel(const double* x, int64_t xs, const double* y, int64_t ys,
                      double* out, int64_t n, int op, bool cmp) {
    if (cmp) {
        for (int64_t i = 0; i < n; i++) out[i] = scalar_cmp(x[i * xs], y[i * ys], op);
        return;
    }
    switch (op) {
        case 0: for (int64_t i = 0; i < n; i++) out[i] = x[i * xs] + y[i * ys]; return;
        case 1: for (int64_t i = 0; i < n; i++) out[i] = x[i * xs] - y[i * ys]; return;
        case 2: for (int64_t i = 0; i < n; i++) out[i] = x[i * xs] * y[i * ys]; return;
        default: for (int64_t i = 0; i < n; i++) out[i] = scalar_op(x[i * xs], y[i * ys], op); return;
    }
}

// n answer rows of width w, row i filled by fill(i, cells). nullptr when a
// band failed: the generic path then answers, and no exception reaches the
// compiled program.
template <class F>
static JdbArray* ew_rows(int64_t n, int64_t w, bool as_bool, F fill) {
    JdbArray* out = jdb_array_new(n);
    out->flags |= 1;
    try {
        mat_bands(n, ew_workers(n * w), [&](int64_t lo, int64_t hi) {
            for (int64_t i = lo; i < hi; i++) {
                JdbArray* row = jdb_array_new(w);
                if (as_bool) row->flags |= 4;
                fill(i, row->data);
                out->data[i] = encode_inner(row);
            }
        });
    } catch (...) {
        for (int64_t i = 0; i < n; i++) {
            if (JdbArray* row = decode_inner(out->data[i])) {
                free(row->data);
                free(row);
            }
        }
        free(out->data);
        free(out);
        return nullptr;
    }
    return out;
}

// A flat answer of n cells, filled band by band by fill(lo, hi, cells);
// nullptr when a band failed.
template <class F>
static JdbArray* ew_flat(int64_t n, bool as_bool, F fill) {
    JdbArray* out = jdb_array_new(n);
    if (as_bool) out->flags |= 4;
    try {
        mat_bands(n, ew_workers(n), [&](int64_t lo, int64_t hi) { fill(lo, hi, out->data); });
    } catch (...) {
        free(out->data);
        free(out);
        return nullptr;
    }
    return out;
}

// Two arrays of numbers by the shape rule of jdb_shape::pair: two matrices
// of one shape, a matrix and a flat array as wide as its rows, or two flat
// arrays of one length. nullptr for anything else.
static JdbArray* ew_pair(JdbArray* a, JdbArray* b, int op, bool cmp) {
    int64_t wa = ew_matrix_width(a);
    if (wa >= 0) {
        int64_t n = a->length;
        if (arr_nested(b)) {
            if (b->length != n || ew_matrix_width(b) != wa) return nullptr;
            return ew_rows(n, wa, cmp, [&](int64_t i, double* o) {
                ew_kernel(ew_row(a, i), 1, ew_row(b, i), 1, o, wa, op, cmp);
            });
        }
        if (!ew_flat_numbers(b) || b->length != wa) return nullptr;
        return ew_rows(n, wa, cmp, [&](int64_t i, double* o) {
            ew_kernel(ew_row(a, i), 1, b->data, 1, o, wa, op, cmp);
        });
    }
    if (!ew_flat_numbers(a)) return nullptr;
    if (arr_nested(b)) {
        int64_t wb = ew_matrix_width(b);
        if (wb < 0 || a->length != wb) return nullptr;
        return ew_rows(b->length, wb, cmp, [&](int64_t i, double* o) {
            ew_kernel(a->data, 1, ew_row(b, i), 1, o, wb, op, cmp);
        });
    }
    if (!ew_flat_numbers(b) || a->length != b->length) return nullptr;
    return ew_flat(a->length, cmp, [&](int64_t lo, int64_t hi, double* o) {
        ew_kernel(a->data + lo, 1, b->data + lo, 1, o + lo, hi - lo, op, cmp);
    });
}

// An array of numbers and one number; nullptr for any other array.
static JdbArray* ew_spread(JdbArray* a, double s, int op, bool scalar_left, bool cmp) {
    auto run = [&](const double* x, double* o, int64_t n) {
        if (scalar_left) ew_kernel(&s, 0, x, 1, o, n, op, cmp);
        else ew_kernel(x, 1, &s, 0, o, n, op, cmp);
    };
    int64_t w = ew_matrix_width(a);
    if (w >= 0)
        return ew_rows(a->length, w, cmp, [&](int64_t i, double* o) { run(ew_row(a, i), o, w); });
    if (!ew_flat_numbers(a)) return nullptr;
    return ew_flat(a->length, cmp, [&](int64_t lo, int64_t hi, double* o) {
        run(a->data + lo, o + lo, hi - lo);
    });
}

// A pure function of one number over an array of numbers; nullptr for any
// other array.
template <class F>
static JdbArray* ew_apply(JdbArray* a, F fn) {
    int64_t w = ew_matrix_width(a);
    if (w >= 0) {
        return ew_rows(a->length, w, false, [&](int64_t i, double* o) {
            const double* x = ew_row(a, i);
            for (int64_t j = 0; j < w; j++) o[j] = fn(x[j]);
        });
    }
    if (!ew_flat_numbers(a)) return nullptr;
    return ew_flat(a->length, false, [&](int64_t lo, int64_t hi, double* o) {
        for (int64_t j = lo; j < hi; j++) o[j] = fn(a->data[j]);
    });
}

// RESHAPE of an array of numbers into rows: the cells are read straight from
// the source, in order and again from the start. nullptr for anything else.
static JdbArray* ew_reshape(JdbArray* src, const std::vector<int64_t>& dims) {
    if (dims.size() != 2) return nullptr;
    const double* cells = nullptr;
    int64_t n = 0;
    std::vector<double> joined;
    if (ew_flat_numbers(src)) {
        cells = src->data;
        n = src->length;
    } else {
        int64_t w = ew_matrix_width(src);
        if (w < 0) return nullptr;
        n = src->length * w;
        try { joined.resize((size_t)n); } catch (...) { return nullptr; }
        for (int64_t i = 0; i < src->length && w > 0; i++)
            memcpy(joined.data() + i * w, ew_row(src, i), (size_t)w * sizeof(double));
        cells = joined.data();
    }
    if (n == 0) return nullptr;
    int64_t rows = (std::max)((int64_t)0, dims[0]), cols = (std::max)((int64_t)0, dims[1]);
    return ew_rows(rows, cols, false, [&](int64_t i, double* o) {
        int64_t k = (i % n) * (cols % n) % n;
        for (int64_t j = 0; j < cols; j++) {
            o[j] = cells[k];
            if (++k == n) k = 0;
        }
    });
}

extern "C" {

// ── Cells of mixed kinds ──
// The fallbacks below meet arrays whose cells are not all numbers: rows
// recurse, numbers compute, and a string cell is answered as the
// interpreter answers it: + joins the texts, * by a number repeats the
// string, a comparison orders the texts. Anything else on a string, a map
// or a handle gives 0.
struct KindedCell { double v; int k; };

static int mirror_cmp(int op) {
    switch (op) {
        case 4: return 6;
        case 5: return 7;
        case 6: return 4;
        case 7: return 5;
        default: return op;
    }
}

static std::string cell_text(double v, int k) {
    if (k == JD_TAG_STR) {
        jdb_text::View s = text_in(decode_str(v));
        return std::string(s.p ? s.p : "", s.n);
    }
    char buf[512];
    jdb_format_double(buf, (int)sizeof buf, v);
    return buf;
}

static double cmp_result(int64_t c, int op) {
    switch (op) {
        case 0: return c == 0;
        case 1: return c != 0;
        case 4: return c < 0;
        case 5: return c <= 0;
        case 6: return c > 0;
        case 7: return c >= 0;
        default: return 0.0;
    }
}

static KindedCell cell_op(double va, int ka, double vb, int kb, int op, bool cmp) {
    bool na = kind_is_num(ka), nb = kind_is_num(kb);
    if (na && nb) {
        if (cmp) return { scalar_cmp(va, vb, op), JD_TAG_BOOL };
        return { scalar_op(va, vb, op), JD_TAG_F64 };
    }
    bool sa = ka == JD_TAG_STR, sb = kb == JD_TAG_STR;
    if ((sa || na) && (sb || nb)) {
        try {
            if (cmp) {
                std::string ta = cell_text(va, ka), tb = cell_text(vb, kb);
                return { cmp_result(ta.compare(tb), op), JD_TAG_BOOL };
            }
            if (op == 0)
                return { encode_str(text_out(cell_text(va, ka) + cell_text(vb, kb))), JD_TAG_STR };
            if (op == 2 && sa != sb) {
                double n = sa ? vb : va;
                jdb_text::View s = text_in(decode_str(sa ? va : vb));
                return { encode_str(text_out(jdb_text::repeat(s, (int64_t)n))), JD_TAG_STR };
            }
        } catch (...) {
            jdb_err_set("out of memory for a string this size", 1);
        }
    }
    return { 0.0, cmp ? JD_TAG_BOOL : JD_TAG_F64 };
}

// Once every cell of an answer is built: one kind throughout goes back to
// the array-wide bits, so the answer reads like any other array of it.
static JdbArray* settle_kinds(JdbArray* r) {
    if (!arr_tagged(r) || r->length == 0) return r;
    int k0 = r->elem_tags[0];
    for (int64_t i = 1; i < r->length; i++)
        if (r->elem_tags[i] != k0) return r;
    int32_t bits = k0 == JD_TAG_F64 ? 0 : k0 == JD_TAG_BOOL ? 4
                 : k0 == JD_TAG_ARR ? 1 : k0 == JD_TAG_STR ? 3 : -1;
    if (bits < 0) return r;
    free(r->elem_tags);
    r->elem_tags = nullptr;
    r->flags = (r->flags & ~(1 | 2 | 4 | 8)) | bits;
    return r;
}

// Some cell holds neither a number nor a row: a string, a map, a handle.
static bool arr_has_text(const JdbArray* a) {
    if (!arr_tagged(a)) return (a->flags & 2) != 0;
    for (int64_t i = 0; i < a->length; i++)
        if (a->elem_tags[i] != JD_TAG_ARR && !kind_is_num(a->elem_tags[i])) return true;
    return false;
}

static JdbArray* arr_scalar_op(JdbArray* a, double s, int op, bool scalar_left);
static JdbArray* arr_cmp_scalar(JdbArray* a, double s, int op);
static JdbArray* arr_pair(JdbArray* a, JdbArray* b, int op, bool cmp);

// A number against every cell of an array that is not all numbers.
static JdbArray* mixed_scalar(JdbArray* a, double s, int op, bool cmp, bool scalar_left) {
    auto* r = jdb_array_new(a->length);
    for (int64_t i = 0; i < a->length; i++) {
        int k = cell_kind(a, i);
        if (k == JD_TAG_ARR) {
            JdbArray* row = decode_inner(a->data[i]);
            JdbArray* sub = cmp ? arr_cmp_scalar(row, s, scalar_left ? mirror_cmp(op) : op)
                                : arr_scalar_op(row, s, op, scalar_left);
            put_kind(r, i, encode_inner(sub), JD_TAG_ARR);
            continue;
        }
        KindedCell c = scalar_left ? cell_op(s, JD_TAG_F64, a->data[i], k, op, cmp)
                                   : cell_op(a->data[i], k, s, JD_TAG_F64, op, cmp);
        put_kind(r, i, c.v, c.k);
    }
    return settle_kinds(r);
}

// Two arrays of one length, cell by cell, when either is not all numbers.
static JdbArray* mixed_pair(JdbArray* a, JdbArray* b, int op, bool cmp) {
    if (a->length != b->length) { shape_error(a->length, b->length); return jdb_array_new(0); }
    auto* r = jdb_array_new(a->length);
    for (int64_t i = 0; i < a->length; i++) {
        int ka = cell_kind(a, i), kb = cell_kind(b, i);
        double va = a->data[i], vb = b->data[i];
        JdbArray* sub = nullptr;
        if (ka == JD_TAG_ARR && kb == JD_TAG_ARR)
            sub = arr_pair(decode_inner(va), decode_inner(vb), op, cmp);
        else if (ka == JD_TAG_ARR && kind_is_num(kb))
            sub = cmp ? arr_cmp_scalar(decode_inner(va), vb, op)
                      : arr_scalar_op(decode_inner(va), vb, op, false);
        else if (kb == JD_TAG_ARR && kind_is_num(ka))
            sub = cmp ? arr_cmp_scalar(decode_inner(vb), va, mirror_cmp(op))
                      : arr_scalar_op(decode_inner(vb), va, op, true);
        if (sub) { put_kind(r, i, encode_inner(sub), JD_TAG_ARR); continue; }
        KindedCell c = (ka == JD_TAG_ARR || kb == JD_TAG_ARR)
            ? KindedCell{ 0.0, cmp ? JD_TAG_BOOL : JD_TAG_F64 }
            : cell_op(va, ka, vb, kb, op, cmp);
        put_kind(r, i, c.v, c.k);
    }
    return settle_kinds(r);
}

// Unary math on an array that is not all numbers: rows recurse, a cell that
// is no number gives 0.
static JdbArray* mixed_unary(JdbArray* a, double (*fn)(double), JdbArray* (*self)(JdbArray*)) {
    auto* r = jdb_array_new(a->length);
    for (int64_t i = 0; i < a->length; i++) {
        int k = cell_kind(a, i);
        if (k == JD_TAG_ARR) put_kind(r, i, encode_inner(self(decode_inner(a->data[i]))), JD_TAG_ARR);
        else put_kind(r, i, kind_is_num(k) ? fn(a->data[i]) : 0.0, JD_TAG_F64);
    }
    return settle_kinds(r);
}

// Unary math with the scalar function baked in, so the compiler inlines it
// into the loop. Arrays of numbers go through ew_apply; a matrix recurses
// its rows, and anything else goes cell by cell.
#define JDB_ARRAY_FF(NAME, SCALAR_FN)                                    \
    static double jdb_ff_##NAME(double x) { return SCALAR_FN(x); }      \
    JdbArray* jdb_array_##NAME(JdbArray* arr) {                          \
        return jdb_guard([&]() -> JdbArray* {                            \
            if (!arr) return jdb_array_new(0);                           \
            if (JdbArray* fast = ew_apply(arr, [](double x) { return SCALAR_FN(x); })) \
                return fast;                                             \
            if (!arr_all_num(arr))                                       \
                return mixed_unary(arr, jdb_ff_##NAME, jdb_array_##NAME); \
            auto* r = jdb_array_new(arr->length);                        \
            for (int64_t i = 0; i < arr->length; i++)                    \
                r->data[i] = SCALAR_FN(arr->data[i]);                    \
            return r;                                                    \
        }, [] { return jdb_array_new(0); });                             \
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

// arr OP scalar (recursive for nested arrays, broadcasts scalar to all leaves)
static JdbArray* arr_scalar_op(JdbArray* a, double s, int op, bool scalar_left) {
    if (!a) return jdb_array_new(0);
    if (JdbArray* fast = ew_spread(a, s, op, scalar_left, false)) return fast;
    if (!arr_all_num(a)) return mixed_scalar(a, s, op, false, scalar_left);
    auto* r = jdb_array_new(a->length);
    for (int64_t i = 0; i < a->length; i++) {
        double av = a->data[i];
        r->data[i] = scalar_left ? scalar_op(s, av, op) : scalar_op(av, s, op);
    }
    return r;
}

// The result of a comparison is a truth value per element; the flag is what
// makes PRINT say TRUE/FALSE and TYPEOF say BOOLEAN while the stored 0/1
// still adds and multiplies.
extern "C" void jdb_array_set_bool_elems(JdbArray* arr);
// arr CMP scalar (recursive)
static JdbArray* arr_cmp_scalar(JdbArray* a, double s, int op) {
    if (!a) return jdb_array_new(0);
    if (JdbArray* fast = ew_spread(a, s, op, false, true)) return fast;
    if (!arr_all_num(a)) return mixed_scalar(a, s, op, true, false);
    auto* r = jdb_array_new(a->length);
    for (int64_t i = 0; i < a->length; i++)
        r->data[i] = scalar_cmp(a->data[i], s, op);
    jdb_array_set_bool_elems(r);
    return r;
}

// Every array of numbers below a renders as TRUE/FALSE.
static void mark_bool_leaves(JdbArray* a) {
    if (!a) return;
    if (arr_all_num(a)) { jdb_array_set_bool_elems(a); return; }
    for (int64_t i = 0; i < a->length; i++)
        if (cell_is_arr(a, i)) mark_bool_leaves(decode_inner(a->data[i]));
}

// Two arrays under an operator, by the shape rule of jdb_shape::pair, which
// the VM shares. Arithmetic or comparison, by cmp; an array with strings or
// cells of several kinds goes cell by cell.
static JdbArray* arr_pair(JdbArray* a, JdbArray* b, int op, bool cmp) {
    if (!a || !b) return jdb_array_new(0);
    if (arr_has_text(a) || arr_has_text(b)) return mixed_pair(a, b, op, cmp);
    if (JdbArray* fast = ew_pair(a, b, op, cmp)) return fast;
    jdb_shape::Op<RtShape> fn = cmp
        ? jdb_shape::Op<RtShape>([op](const double& x, const double& y) { return scalar_cmp(x, y, op); })
        : jdb_shape::Op<RtShape>([op](const double& x, const double& y) { return scalar_op(x, y, op); });
    JdbArray* r = jdb_shape::pair<RtShape>(a, b, fn);
    if (cmp) mark_bool_leaves(r);
    return r;
}

// op: 0=add,1=sub,2=mul,3=div
JdbArray* jdb_array_binop(JdbArray* a, JdbArray* b, int32_t op) {
    return jdb_guard([&]() -> JdbArray* {
        return arr_pair(a, b, op, false);
    }, [] { return jdb_array_new(0); });
}
JdbArray* jdb_array_scalar_op(JdbArray* a, double s, int32_t op, int32_t scalar_left) {
    return jdb_guard([&]() -> JdbArray* {
        return arr_scalar_op(a, s, op, scalar_left != 0);
    }, [] { return jdb_array_new(0); });
}
JdbArray* jdb_array_cmp_scalar(JdbArray* a, double s, int32_t op) {
    return jdb_guard([&]() -> JdbArray* {
        return arr_cmp_scalar(a, s, op);
    }, [] { return jdb_array_new(0); });
}
JdbArray* jdb_array_cmp_arr(JdbArray* a, JdbArray* b, int32_t op) {
    return jdb_guard([&]() -> JdbArray* {
        return arr_pair(a, b, op, true);
    }, [] { return jdb_array_new(0); });
}

// Forward declaration - actual definition is jdb_array_set_bool_elems
// further down. Used by the string-cmp helper below.
extern "C" void jdb_array_set_bool_elems(JdbArray* arr);

// String-array vs string-scalar comparison. Each cell of `a` carries an
// i8* string pointer in punned-f64 bits (the codegen side already tracks
// these arrays via string_array_vars and dispatches here when the rhs is
// a JD_TAG_STR). op: 0 = ==, 1 = <>, 4 = <, 5 = <=, 6 = >, 7 = >=
// (scalar_cmp's op enum). Returns a fresh boolean-tagged array.
JdbArray* jdb_array_cmp_scalar_str(JdbArray* a, const char* s, int32_t op) {
    return jdb_guard([&]() -> JdbArray* {
        if (!a) return jdb_array_new(0);
        auto* r = jdb_array_new(a->length);
        jdb_array_set_bool_elems(r);
        const char* sval = s ? s : "";
        for (int64_t i = 0; i < a->length; i++) {
            union { double d; intptr_t p; } u; u.d = a->data[i];
            const char* cell = (const char*)u.p;
            if (!cell) cell = "";
            int64_t c = jdb_str_cmp(cell, sval);
            bool result;
            switch (op) {
                case 0:  result = (c == 0); break;
                case 1:  result = (c != 0); break;
                case 4:  result = (c <  0); break;
                case 5:  result = (c <= 0); break;
                case 6:  result = (c >  0); break;
                case 7:  result = (c >= 0); break;
                default: result = false;
            }
            r->data[i] = result ? 1.0 : 0.0;
        }
        return r;
    }, [] { return jdb_array_new(0); });
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

// Bit 4: every cell is epoch seconds that are a date. The bridge sets the
// same bit for an array of dates coming out of the VM.
void jdb_array_set_date_elems(JdbArray* arr) {
    if (arr) arr->flags |= 16;
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
    // Every cell is a date, so a plain number out of one is still a date.
    if ((arr->flags & 16) != 0) return JD_TAG_DATE;
    // Every cell is a truth value, so a plain 0/1 out of one is boolean.
    if ((arr->flags & 4) != 0) return JD_TAG_BOOL;
    union { double d; uint64_t u; } u; u.d = d;
    bool looks_ptr = (u.u != 0 && u.u < (1ULL << 47));
    if (!looks_ptr) return 1;  // F64
    bool has_string = (arr->flags & 2) != 0;
    return has_string ? 2 : 3;  // STR or ARR
}

// String * int → repeat: "-" * 5 → "-----"
char* jdb_str_repeat(const char* s, int64_t n) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::repeat(text_in(s), n));
    }, [] { return _strdup(""); });
}

// Unary `-"abc"` answers ["a", "b", "c"], one string per UTF-8 character,
// as an array flagged nested and string with per-cell STR tags, so the
// tagged INDEX getter and the bridge marshaller read the cells as strings.
JdbArray* jdb_str_to_chars(const char* s) {
    return jdb_guard([&]() -> JdbArray* {
        if (!s) return jdb_array_new(0);
        std::vector<std::string> parts;
        jdb_text::chars(text_in(s), [&](std::string c) { parts.push_back(std::move(c)); });
        int64_t n = (int64_t)parts.size();
        JdbArray* arr = jdb_array_new(n);
        for (int64_t i = 0; i < n; i++) {
            union { int64_t iv; double d; } u; u.iv = (int64_t)(intptr_t)text_out(parts[(size_t)i]);
            arr->data[i] = u.d;
        }
        arr->flags |= 3;
        if (n > 0) {
            arr->elem_tags = (int8_t*)malloc((size_t)n);
            for (int64_t i = 0; i < n; i++) arr->elem_tags[i] = (int8_t)JD_TAG_STR;
            arr->flags |= 8;
        }
        return arr;
    }, [] { return jdb_array_new(0); });
}

// ── Maps / Objects (string-keyed) ──────────────────────────────
// Keys in insertion order; a map of 16 keys or more finds them through a
// hash index built on the first lookup that needs it.
// Layout: { count, capacity, char** keys, double* values, int32* tags,
// index } - vm_bridge.cpp mirrors it as JdbMapFwd.
// Tag values follow JdTag enum (jdb_tags.h): F64, STR, ARR, NATIVE_MAP.

struct JdbMap {
    int64_t count;
    int64_t capacity;
    char** keys;
    double* values;
    int32_t* tags;
    void* index;     // MapIndex*, or null while none is built
    int32_t lower_keys;  // made by a builtin; see jdb_lower_keys.h
};

JdbMap* jdb_map_new() {
    auto* m = (JdbMap*)malloc(sizeof(JdbMap));
    if (!m) jdb_fatal_oom();
    m->count = 0;
    m->capacity = 0;
    m->keys = nullptr;
    m->values = nullptr;
    m->tags = nullptr;
    m->index = nullptr;
    m->lower_keys = 0;
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

// What an event hands its handler goes to the counts, so the next drain
// frees it unless the handler kept it.
void* jdb_rc_new(void* p, int32_t kind);
void jdb_rc_own_cells(int64_t bits);
void jdb_rc_adopt(int64_t parent_bits, int64_t child_bits);

void jdb_event_on(const char* name, void* handler) {
    jdb_guard_void([&] {
        if (!name || !handler) return;
        g_event_handlers[name] = (JdbEventHandler)handler;
    });
}

void jdb_event_raise_str(const char* name, const char* arg) {
    jdb_guard_void([&] {
        if (!name) return;
        auto it = g_event_handlers.find(name);
        if (it == g_event_handlers.end() || !it->second) return;
        auto* arr = jdb_array_new(1);
        arr->flags |= 2;  // string-flag
        union { double d; int64_t i; } u;
        u.i = (int64_t)(intptr_t)_strdup(arg ? arg : "");
        arr->data[0] = u.d;
        jdb_rc_new(arr, JD_TAG_ARR);
        jdb_rc_own_cells((int64_t)(intptr_t)arr);
        { CompiledCall open; it->second(arr); }
    });
}

// Forward decls - definitions are further down the file.
JdbMap* jdb_map_new();
void jdb_map_set_f64(JdbMap* m, const char* key, double val);
void jdb_map_set_str(JdbMap* m, const char* key, const char* val);
void jdb_map_set_tagged(JdbMap* m, const char* key, double val, int32_t tag);
JdbArray* jdb_split(const char* src, const char* delim);

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
    // The cell is a map, and the pointer bit alone does not say so: a reader
    // takes it for a nested array, which is what TYPEOF answered where the
    // interpreter says OBJECT.
    a->elem_tags = (int8_t*)malloc(1);
    a->elem_tags[0] = (int8_t)JD_TAG_NATIVE_MAP;
    a->flags |= 8;
    return a;
}

// A handler may answer by writing into the info map it was handed. On this
// side of the bridge that map is a copy, so after the handler returns the
// fields travel back to the object the raiser still holds.
void jdrt_event_writeback(void* map);

static void call_event_handler(JdbEventFn fn, JdbMap* m) {
    jdb_rc_new(m, JD_TAG_NATIVE_MAP);
    // A container among the fields (a list the bridge sent) belongs to the
    // map, so it goes when the map does.
    for (int64_t i = 0; i < m->count; i++) {
        int32_t t = m->tags[i];
        if (t != JD_TAG_ARR && t != JD_TAG_NATIVE_MAP) continue;
        union { double d; int64_t i; } u; u.d = m->values[i];
        jdb_rc_new((void*)(intptr_t)u.i, t);
        jdb_rc_adopt((int64_t)(intptr_t)m, u.i);
    }
    auto* a = (JdbArray*)jdb_rc_new(wrap_map_in_array(m), JD_TAG_ARR);
    jdb_rc_adopt((int64_t)(intptr_t)a, (int64_t)(intptr_t)m);
    { CompiledCall open; fn(a); }
    jdrt_event_writeback(m);
}

void jdrt_dispatch_event(const char* event_name,
                         const int64_t* args, const int32_t* tags, int nargs) {
    // The six SDL events have fixed positional schemas and ignore tags;
    // the generic branch at the bottom reads the keyed format via tags.
    if (!event_name) return;

    if (strcmp(event_name, "QUIT") == 0) {
        if (!g_h_quit) return;
        auto* a = jdb_array_new(0);
        jdb_rc_new(a, JD_TAG_ARR);
        CompiledCall open;
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
        call_event_handler(fn, m);
        return;
    }

    if (strcmp(event_name, "MOUSEDOWN") == 0 || strcmp(event_name, "MOUSEUP") == 0) {
        JdbEventFn fn = (event_name[5] == 'D') ? g_h_mousedown : g_h_mouseup;
        if (!fn) return;
        JdbMap* m = jdb_map_new();
        if (nargs >= 1) jdb_map_set_f64(m, "button", (double)args[0]);
        if (nargs >= 2) jdb_map_set_f64(m, "x",      (double)args[1]);
        if (nargs >= 3) jdb_map_set_f64(m, "y",      (double)args[2]);
        call_event_handler(fn, m);
        return;
    }

    if (strcmp(event_name, "MOUSEMOVE") == 0) {
        if (!g_h_mousemove) return;
        JdbMap* m = jdb_map_new();
        if (nargs >= 1) jdb_map_set_f64(m, "x", (double)args[0]);
        if (nargs >= 2) jdb_map_set_f64(m, "y", (double)args[1]);
        call_event_handler(g_h_mousemove, m);
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
        } else if (vtag == JD_TAG_ARR) {
            // a string array, joined by the unit separator on the bridge side
            const char* joined = event_arg_str(args, i + 1, nargs);
            JdbArray* arr = joined[0] ? jdb_split(joined, "\x1f") : jdb_array_new(0);
            union { double d; intptr_t p; } u;
            u.p = (intptr_t)arr;
            jdb_map_set_tagged(m, key, u.d, JD_TAG_ARR);
        } else if (vtag == JD_TAG_F64) {
            double d;
            memcpy(&d, &args[i + 1], sizeof(double));
            jdb_map_set_f64(m, key, d);
        } else {  // I64 / BOOL wire as integer
            jdb_map_set_f64(m, key, (double)args[i + 1]);
        }
    }
    call_event_handler(cit->second, m);
}

static void map_grow(JdbMap* m) {
    int64_t newcap = m->capacity ? m->capacity * 2 : 8;
    char** keys = (char**)realloc(m->keys, newcap * sizeof(char*));
    if (keys) m->keys = keys;
    double* values = keys ? (double*)realloc(m->values, newcap * sizeof(double)) : nullptr;
    if (values) m->values = values;
    int32_t* tags = values ? (int32_t*)realloc(m->tags, newcap * sizeof(int32_t)) : nullptr;
    if (!tags) jdb_fatal_oom();
    m->tags = tags;
    m->capacity = newcap;
}

static bool rc_on_owner();

// The positions of a big map's keys. It covers the first `covered` keys, so
// keys appended since (the bridge appends without it) join on the next
// lookup; a removed key drops it, since the keys after it move.
struct MapIndex {
    std::unordered_map<std::string_view, int64_t> at;
    int64_t covered = 0;
};

static void map_index_drop(JdbMap* m) {
    delete (MapIndex*)m->index;
    m->index = nullptr;
}

static int64_t map_find_linear(JdbMap* m, const char* key) {
    for (int64_t i = 0; i < m->count; i++) {
        if (m->keys[i] && strcmp(m->keys[i], key) == 0) return i;
    }
    return -1;
}

static int64_t map_find_exact(JdbMap* m, const char* key);

static int64_t map_find(JdbMap* m, const char* key) {
    int64_t at = map_find_exact(m, key);
    if (at >= 0 || !m || !key || !m->lower_keys) return at;
    std::string lower = jdb_lowered_key(key);
    if (lower.empty()) return -1;
    at = map_find_exact(m, lower.c_str());
    if (at >= 0) jdb_warn_upper_key(key, lower);
    return at;
}

static int64_t map_find_exact(JdbMap* m, const char* key) {
    if (!m || !key) return -1;
    // The index is built and read on the thread the counts belong to only;
    // another thread (a compiled HTTP handler) scans.
    if (m->count < 16 || !rc_on_owner()) return map_find_linear(m, key);
    try {
        auto* ix = (MapIndex*)m->index;
        if (!ix) m->index = ix = new MapIndex;
        if (ix->covered > m->count) { ix->at.clear(); ix->covered = 0; }
        for (; ix->covered < m->count; ix->covered++)
            if (const char* k = m->keys[ix->covered]) ix->at.emplace(k, ix->covered);
        auto it = ix->at.find(std::string_view(key));
        return it == ix->at.end() ? -1 : it->second;
    } catch (...) {
        map_index_drop(m);
        return map_find_linear(m, key);
    }
}

// A map owns the copy of every string it stores (the getters hand out
// copies too), so the string a key held is released once the key is
// written over.
static void map_drop_cell_str(int32_t tag, double bits) {
    if (tag != JD_TAG_STR) return;
    union { double d; int64_t i; } u; u.d = bits;
    jdb_str_drop((char*)(intptr_t)u.i);
}

// A key written over or removed: the string it held goes, and a container
// it held loses this map as a holder.
static void map_let_go(JdbMap* m, int32_t tag, double bits) {
    if (tag == JD_TAG_ARR || tag == JD_TAG_NATIVE_MAP) rc_disown_cell(m, bits);
    else map_drop_cell_str(tag, bits);
}

void jdb_map_set_f64(JdbMap* m, const char* key, double val) {
    if (!m || !key) return;
    int64_t idx = map_find(m, key);
    int32_t old_tag = JD_TAG_NONE;
    double old_bits = 0;
    if (idx < 0) {
        if (m->count == m->capacity) map_grow(m);
        idx = m->count++;
        m->keys[idx] = _strdup(key);
    } else {
        old_tag = m->tags[idx];
        old_bits = m->values[idx];
    }
    m->values[idx] = val;
    m->tags[idx] = JD_TAG_F64;
    map_let_go(m, old_tag, old_bits);
}

void jdb_map_set_str(JdbMap* m, const char* key, const char* val) {
    if (!m || !key) return;
    int64_t idx = map_find(m, key);
    int32_t old_tag = JD_TAG_NONE;
    double old_bits = 0;
    if (idx < 0) {
        if (m->count == m->capacity) map_grow(m);
        idx = m->count++;
        m->keys[idx] = _strdup(key);
    } else {
        old_tag = m->tags[idx];
        old_bits = m->values[idx];
    }
    union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)jdb_str_dup_binary(val);
    m->values[idx] = u.d;
    m->tags[idx] = JD_TAG_STR;
    map_let_go(m, old_tag, old_bits);
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
    int32_t old_tag = JD_TAG_NONE;
    double old_bits = 0;
    if (idx < 0) {
        if (m->count == m->capacity) map_grow(m);
        idx = m->count++;
        m->keys[idx] = _strdup(key);
    } else {
        old_tag = m->tags[idx];
        old_bits = m->values[idx];
    }
    if (tag == JD_TAG_STR) {
        union { double d; int64_t i; } u; u.d = val;
        const char* s = (const char*)(intptr_t)u.i;
        union { int64_t i; double d; } out; out.i = (int64_t)(intptr_t)jdb_str_dup_binary(s);
        m->values[idx] = out.d;
    } else {
        m->values[idx] = keep_vm_handle(val, tag);
    }
    m->tags[idx] = tag;
    map_let_go(m, old_tag, old_bits);
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
        return jdb_str_dup_binary(s);
    }
    // A truth value reads back as TRUE/FALSE, matching how it prints.
    if (m->tags[idx] == JD_TAG_BOOL)
        return _strdup(m->values[idx] != 0.0 ? "TRUE" : "FALSE");
    char buf[400];
    jdb_format_double(buf, (int)sizeof(buf), m->values[idx]);
    return _strdup(buf);
}

char* jdb_bool_to_str(int64_t v) {
    return _strdup(v ? "TRUE" : "FALSE");
}

int64_t jdb_map_has(JdbMap* m, const char* key) {
    return map_find(m, key) >= 0 ? 1 : 0;
}

// Removes one key; answers whether it was there.
int64_t jdb_map_delete(JdbMap* m, const char* key) {
    if (!m || !key) return 0;
    int64_t idx = map_find(m, key);
    if (idx < 0) return 0;
    map_index_drop(m);
    free(m->keys[idx]);
    map_let_go(m, m->tags[idx], m->values[idx]);
    for (int64_t i = idx + 1; i < m->count; i++) {
        m->keys[i - 1] = m->keys[i];
        m->values[i - 1] = m->values[i];
        m->tags[i - 1] = m->tags[i];
    }
    m->count--;
    return 1;
}

// Frees a map the program owns outright, with its keys, its strings and the
// maps nested in it. Arrays and VM handles in its cells are not followed:
// the codegen only hands over maps that hold none.
static void rc_forget(const void* p);
void jdb_map_free_tree(JdbMap* m) {
    if (!m) return;
    rc_forget(m);
    for (int64_t i = 0; i < m->count; i++) {
        free(m->keys[i]);
        map_drop_cell_str(m->tags[i], m->values[i]);
        if (m->tags[i] == JD_TAG_NATIVE_MAP) {
            union { double d; int64_t i; } u; u.d = m->values[i];
            jdb_map_free_tree((JdbMap*)(intptr_t)u.i);
        }
    }
    map_index_drop(m);
    free(m->keys);
    free(m->values);
    free(m->tags);
    free(m);
}

// ── Reference counts for arrays and maps ─────────────────────
//
// A container the compiled code made (a literal, an array the bridge built)
// is registered here with a count of zero and put in the pool. A variable
// that stores it counts one; overwriting the variable, or the end of the
// FUNC that owns it, takes one away. At the end of a loop pass the pool
// frees what was made inside the pass and nobody counts. A container that
// went into a cell, a closure or a builtin that keeps its arguments is
// pinned and never freed, since those holders do not count.
//
// Only pointers in the registry are ever freed, so a slot whose bits are a
// number, a VM handle or a container this file did not register is passed
// over. A count that reaches zero from a global slot waits for the end of a
// pass of a loop in the main program, where no expression is half done; one
// from a local slot is freed at the end of the current pass.
//
// Everything here runs on the thread that first used it. A callback the VM
// runs on another thread counts nothing and frees nothing.

namespace {

struct RcEntry {
    uintptr_t key;       // 0 = empty, 1 = deleted
    uint64_t gen;
    int32_t rc;
    uint8_t kind;        // JD_TAG_ARR or JD_TAG_NATIVE_MAP
    uint8_t pinned;
    uint8_t owns_cells;  // an array whose string cells nobody else points at
    uintptr_t owner;     // the array whose cell holds it and counts for it
    uint64_t owner_gen;
};

struct RcPoolItem { uintptr_t key; uint64_t gen; };

struct RcState {
    RcEntry* slots = nullptr;
    size_t cap = 0;      // power of two
    size_t used = 0;     // live + deleted
    size_t live = 0;
    uint64_t next_gen = 1;
    std::vector<RcPoolItem> pool;
    std::vector<RcPoolItem> zombies;
    // Holders beyond the owner: a container in cells of several holders, or
    // in several cells of one, counts once per cell. Keyed by the container,
    // each item a holder and its generation.
    std::unordered_map<uintptr_t, std::vector<RcPoolItem>> extra;
    std::vector<char*> str_later;  // strings global slots let go of inside a FUNC or SUB
    std::vector<void*> udt_later;  // UDT instances a frame let go of, freed like str_later
    int func_depth = 0;  // FUNC frames on the stack; a SUB chain from the main program is depth 0
    bool init = false;
    bool off = false;
    std::thread::id owner{};
};

}

static RcState g_rc;

static inline std::thread::id rc_this_thread() { return std::this_thread::get_id(); }

static inline size_t rc_hash(uintptr_t k) {
    k ^= k >> 33; k *= 0xff51afd7ed558ccdULL; k ^= k >> 33;
    return (size_t)k;
}

static bool rc_ready() {
    if (!g_rc.init) {
        g_rc.init = true;
        g_rc.off = std::getenv("JDB_RC_OFF") != nullptr;
        g_rc.owner = rc_this_thread();
    }
    return !g_rc.off && g_rc.owner == rc_this_thread();
}

// The thread the counts belong to, whether they are kept or not (JDB_RC_OFF).
// Bookkeeping another thread would do - a compiled HTTP handler, a callback -
// is left out: what it lets go of stays, which can leak but never frees
// under the main thread.
static bool rc_on_owner() {
    rc_ready();
    return g_rc.owner == rc_this_thread();
}

static RcEntry* rc_find(uintptr_t k) {
    if (!g_rc.cap || k < 2) return nullptr;
    size_t mask = g_rc.cap - 1;
    for (size_t i = rc_hash(k) & mask;; i = (i + 1) & mask) {
        RcEntry& e = g_rc.slots[i];
        if (e.key == k) return &e;
        if (e.key == 0) return nullptr;
    }
}

// The kind the counts know a container by (JD_TAG_ARR or JD_TAG_NATIVE_MAP),
// or -1 when they do not know it.
static int rc_kind_of(uintptr_t k) {
    RcEntry* e = rc_find(k);
    return e ? (int)e->kind : -1;
}

// Whether an array the counts know owns the texts in its cells.
static bool rc_owns_texts(uintptr_t k) {
    RcEntry* e = rc_find(k);
    return e && e->kind == JD_TAG_ARR && e->owns_cells;
}

static void rc_rehash(size_t cap) {
    RcEntry* old = g_rc.slots;
    size_t old_cap = g_rc.cap;
    g_rc.slots = (RcEntry*)calloc(cap, sizeof(RcEntry));
    if (!g_rc.slots) jdb_fatal_oom();
    g_rc.cap = cap;
    g_rc.used = g_rc.live;
    size_t mask = cap - 1;
    for (size_t j = 0; j < old_cap; j++) {
        if (old[j].key < 2) continue;
        size_t i = rc_hash(old[j].key) & mask;
        while (g_rc.slots[i].key != 0) i = (i + 1) & mask;
        g_rc.slots[i] = old[j];
    }
    free(old);
}

static RcEntry* rc_insert(uintptr_t k) {
    if ((g_rc.used + 1) * 2 > g_rc.cap)
        rc_rehash(g_rc.cap ? (g_rc.live * 4 > g_rc.cap ? g_rc.cap * 2 : g_rc.cap) : 1024);
    size_t mask = g_rc.cap - 1;
    RcEntry* tomb = nullptr;
    for (size_t i = rc_hash(k) & mask;; i = (i + 1) & mask) {
        RcEntry& e = g_rc.slots[i];
        if (e.key == 1 && !tomb) tomb = &e;
        if (e.key == 0) {
            RcEntry* at = tomb ? tomb : &e;
            if (!tomb) g_rc.used++;
            at->key = k;
            g_rc.live++;
            return at;
        }
    }
}

// JDB_RC_POISON=1 overwrites what is freed, so a holder the counts missed
// reads garbage at once instead of the old values that freed memory keeps.
static bool rc_poison() {
    static int on = -1;
    if (on < 0) on = std::getenv("JDB_RC_POISON") ? 1 : 0;
    return on == 1;
}

static void rc_remove(RcEntry* e);
static void rc_free_container(uintptr_t k, uint8_t kind, bool owns_cells, uint64_t gen);
static JdbArray* rc_cell_row(const JdbArray* arr, int64_t idx);
static void rc_free_implicit_row(JdbArray* row);

// One cell of holder lets go of c: the owner's, else one of the holders
// beyond it. FALSE when holder does not count for c.
static bool rc_let_go(RcEntry* c, uintptr_t holder, uint64_t holder_gen) {
    if (c->owner == holder && c->owner_gen == holder_gen) {
        c->owner = 0;
        return true;
    }
    auto it = g_rc.extra.find(c->key);
    if (it == g_rc.extra.end()) return false;
    auto& v = it->second;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].key != holder || v[i].gen != holder_gen) continue;
        v[i] = v.back();
        v.pop_back();
        if (v.empty()) g_rc.extra.erase(it);
        return true;
    }
    return false;
}

// A cell of a holder that is going: the container in it loses that holder
// and goes with it unless something else counts it.
static void rc_release_child(uintptr_t holder, uint64_t holder_gen, double bits) {
    union { double d; int64_t i; } u; u.d = bits;
    RcEntry* c = rc_find((uintptr_t)u.i);
    if (!c || !rc_let_go(c, holder, holder_gen)) return;
    if (--c->rc > 0 || c->pinned) return;
    uintptr_t ck = c->key;
    uint8_t ckind = c->kind;
    bool cowns = c->owns_cells != 0;
    uint64_t cgen = c->gen;
    rc_remove(c);
    rc_free_container(ck, ckind, cowns, cgen);
}

static void rc_free_container(uintptr_t k, uint8_t kind, bool owns_cells, uint64_t gen) {
    if (kind == JD_TAG_ARR) {
        JdbArray* a = (JdbArray*)k;
        // Its rows outside the counts are nobody else's.
        if (rc_holds_rows(a) && a->data) {
            for (int64_t i = 0; i < a->length; i++) {
                JdbArray* row = rc_cell_row(a, i);
                if (row && (row->flags & JDB_ARR_IMPLICIT_ROW) && row->parent == a)
                    rc_free_implicit_row(row);
            }
        }
        // The containers its cells own go with it unless something else
        // counts them too.
        if ((a->flags & JDB_ARR_OWNS_CHILDREN) && a->data)
            for (int64_t i = 0; i < a->length; i++) rc_release_child(k, gen, a->data[i]);
        if (owns_cells && a->data) {
            for (int64_t i = 0; i < a->length; i++) {
                bool str = a->elem_tags ? a->elem_tags[i] == JD_TAG_STR : (a->flags & 2) != 0;
                if (!str) continue;
                union { double d; int64_t i; } u; u.d = a->data[i];
                char* s = (char*)(intptr_t)u.i;
                if (!s) continue;
                if (rc_poison()) { if (*s) s[0] = '\x7f'; }
                else jdb_str_drop(s);
            }
        }
        if (rc_poison()) {
            if (a->data) memset(a->data, 0xDD, (size_t)a->capacity * sizeof(double));
            a->length = 0x7DDDDDDD;
            a->data = nullptr;
            return;
        }
        free(a->data);
        free(a->elem_tags);
        free(a);
    } else {
        JdbMap* m = (JdbMap*)k;
        for (int64_t i = 0; i < m->count; i++)
            if (m->tags[i] == JD_TAG_ARR || m->tags[i] == JD_TAG_NATIVE_MAP)
                rc_release_child(k, gen, m->values[i]);
        if (rc_poison()) {
            for (int64_t i = 0; i < m->count; i++) {
                m->values[i] = 0;
                if (m->keys[i]) m->keys[i][0] = '\x7f';
            }
            map_index_drop(m);
            m->count = 0;
            return;
        }
        for (int64_t i = 0; i < m->count; i++) {
            free(m->keys[i]);
            map_drop_cell_str(m->tags[i], m->values[i]);
        }
        map_index_drop(m);
        free(m->keys);
        free(m->values);
        free(m->tags);
        free(m);
    }
}

static void rc_remove(RcEntry* e) {
    if (!g_rc.extra.empty()) g_rc.extra.erase(e->key);
    e->key = 1;
    g_rc.live--;
}

// Forgets a container that is freed by other means.
static void rc_forget(const void* p) {
    if (!g_rc.cap) return;
    if (RcEntry* e = rc_find((uintptr_t)p)) rc_remove(e);
}

// The rows of a matrix the runtime built (arithmetic, ZEROS of a shape, a
// result the bridge marshalled) are its alone: each one that no entry knows
// yet is registered with the matrix as its owner, so it goes when the
// matrix goes. A row that is registered already is somebody else's and is
// left as it is. Only a caller that knows the cells are rows asks for this:
// the pointer bit alone also marks an array of UDT objects.
// A row its matrix still holds outside the counts, entered now: the matrix
// owns it, as if it had been registered with the matrix. A row whose matrix
// the counts do not know (freed by other means) stays as it is.
static RcEntry* rc_materialize(JdbArray* row) {
    if (!(row->flags & JDB_ARR_IMPLICIT_ROW) || !row->parent) return rc_find((uintptr_t)row);
    JdbArray* p = row->parent;
    RcEntry* pe = rc_find((uintptr_t)p);
    if (!pe) return nullptr;
    uint64_t pgen = pe->gen;
    row->flags &= ~JDB_ARR_IMPLICIT_ROW;
    row->parent = nullptr;
    RcEntry* c = rc_insert((uintptr_t)row);
    c->gen = g_rc.next_gen++;
    c->rc = 1;
    c->kind = JD_TAG_ARR;
    c->pinned = 0;
    c->owns_cells = 0;
    c->owner = (uintptr_t)p;
    c->owner_gen = pgen;
    p->flags |= JDB_ARR_OWNS_CHILDREN;
    return c;
}

// The array cell at idx as a row pointer, or null when the cell holds no
// array. A cell the flags call a pointer may hold a UDT object, whose memory
// is read only where a JdbArray keeps flags and parent.
static JdbArray* rc_cell_row(const JdbArray* arr, int64_t idx) {
    if (!arr || idx < 0 || idx >= arr->length || !arr->data) return nullptr;
    if (!cell_is_arr(arr, idx)) return nullptr;
    union { double d; int64_t i; } u; u.d = arr->data[idx];
    return (uintptr_t)u.i < 2 ? nullptr : (JdbArray*)(intptr_t)u.i;
}

// An implicit row that is about to leave its matrix as a value. Its parent
// must be an array the counts know that holds implicit rows, which the bits
// of a UDT object read as a row would not name.
static void rc_row_out(JdbArray* row) {
    if (!row || !(row->flags & JDB_ARR_IMPLICIT_ROW) || !row->parent) return;
    if (!g_rc.cap || !rc_ready()) return;
    RcEntry* pe = rc_find((uintptr_t)row->parent);
    if (!pe || pe->kind != JD_TAG_ARR || !rc_holds_rows(row->parent)) return;
    rc_materialize(row);
}

static void rc_cell_out(JdbArray* arr, int64_t idx) {
    if (!g_rc.cap) return;
    rc_row_out(rc_cell_row(arr, idx));
}

// The program read a cell through the bridge, and the bridge called it an
// array: the bits are a native array (or a UDT object) it now holds.
extern "C" void jdb_rc_cell_out(int64_t row_bits) {
    if (!g_rc.cap || (uint64_t)row_bits < 2) return;
    rc_row_out((JdbArray*)(intptr_t)row_bits);
}

static void rc_free_implicit_row(JdbArray* row) {
    row->flags &= ~JDB_ARR_IMPLICIT_ROW;
    row->parent = nullptr;
    if (rc_poison()) {
        if (row->data) memset(row->data, 0xDD, (size_t)row->capacity * sizeof(double));
        row->length = 0x7DDDDDDD;
        row->data = nullptr;
        return;
    }
    free(row->data);
    free(row->elem_tags);
    free(row);
}

// The cell idx of arr is written over: an implicit row it held is entered
// into the counts as arr's, so letting go of the cell sends it to the pool
// like any counted row, and a value still in flight keeps it until the drain.
static void rc_implicit_overwrite(JdbArray* arr, int64_t idx) {
    if (!rc_holds_rows(arr) || !rc_on_owner()) return;
    JdbArray* row = rc_cell_row(arr, idx);
    if (!row || !(row->flags & JDB_ARR_IMPLICIT_ROW) || row->parent != arr) return;
    if (!rc_materialize(row)) rc_free_implicit_row(row);
}

// One more holder for c, an array: the owner when c has none, else one of
// the holders beyond it.
static void rc_hold(RcEntry* c, uintptr_t holder, uint64_t holder_gen) {
    if (c->pinned || c->key == holder) return;
    c->rc++;
    if (c->owner) {
        g_rc.extra[c->key].push_back({ holder, holder_gen });
    } else {
        c->owner = holder;
        c->owner_gen = holder_gen;
    }
    ((JdbArray*)holder)->flags |= JDB_ARR_OWNS_CHILDREN;
}

static void rc_register_children(uintptr_t parent_key, uint64_t parent_gen) {
    JdbArray* a = (JdbArray*)parent_key;
    if (!a->data || (!arr_tagged(a) && !arr_all_rows(a))) return;
    bool any = false;
    bool seen_any = false;
    for (int64_t i = 0; i < a->length; i++) {
        if (!cell_is_arr(a, i)) continue;
        union { double d; int64_t i; } u; u.d = a->data[i];
        uintptr_t ck = (uintptr_t)u.i;
        if (ck < 2) continue;
        if (RcEntry* held = rc_find(ck)) {
            rc_hold(held, parent_key, parent_gen);
            continue;
        }
        JdbArray* row = (JdbArray*)ck;
        if (row->flags & JDB_ARR_ROW_SEEN) {
            // One row in two cells: it is counted, as it was before rows
            // stayed outside the counts, so the matrix frees it once.
            rc_materialize(row);
            row->flags &= ~JDB_ARR_ROW_SEEN;
            continue;
        }
        if (row->flags & JDB_ARR_IMPLICIT_ROW) {
            // A row of another matrix stays that matrix's and is held by
            // this one too; one of this matrix is already counted as such.
            if (row->parent != a) {
                rc_row_out(row);
                if (RcEntry* held = rc_find(ck)) rc_hold(held, parent_key, parent_gen);
            }
            if (row->flags & JDB_ARR_IMPLICIT_ROW) {
                row->parent = a;
                row->flags |= JDB_ARR_ROW_SEEN;
                a->parent = a;
                seen_any = true;
            }
            continue;
        }
        if (!(row->flags & (1 | 8))) {
            // A row of numbers belongs to this matrix without an entry.
            row->flags |= JDB_ARR_IMPLICIT_ROW | JDB_ARR_ROW_SEEN;
            row->parent = a;
            a->parent = a;
            seen_any = true;
            continue;
        }
        RcEntry* c = rc_insert(ck);
        c->gen = g_rc.next_gen++;
        c->rc = 1;
        c->kind = JD_TAG_ARR;
        c->pinned = 0;
        c->owns_cells = 0;
        c->owner = parent_key;
        c->owner_gen = parent_gen;
        any = true;
        rc_register_children(ck, c->gen);
    }
    if (any) a->flags |= JDB_ARR_OWNS_CHILDREN;
    if (seen_any) {
        for (int64_t i = 0; i < a->length; i++)
            if (JdbArray* row = rc_cell_row(a, i))
                if (row->flags & JDB_ARR_ROW_SEEN) row->flags &= ~JDB_ARR_ROW_SEEN;
    }
}

extern "C" void* jdb_rc_new(void* p, int32_t kind) {
    if (!p || !rc_ready()) return p;
    uintptr_t k = (uintptr_t)p;
    // A row its matrix holds outside the counts is no fresh container: it
    // is entered as its matrix's.
    if (kind == JD_TAG_ARR && (((JdbArray*)p)->flags & JDB_ARR_IMPLICIT_ROW)) rc_row_out((JdbArray*)p);
    if (rc_find(k)) return p;
    RcEntry* e = rc_insert(k);
    uint64_t gen = g_rc.next_gen++;
    e->gen = gen;
    e->rc = 0;
    e->kind = (uint8_t)kind;
    e->pinned = 0;
    e->owns_cells = 0;
    e->owner = 0;
    e->owner_gen = 0;
    g_rc.pool.push_back({ k, gen });
    return p;
}

// A fresh matrix: the array and the rows it owns.
extern "C" void* jdb_rc_new_rows(void* p) {
    if (!p || !rc_ready()) return p;
    // An array the counts know already holds its rows already.
    bool fresh = !rc_find((uintptr_t)p) && !(((JdbArray*)p)->flags & JDB_ARR_IMPLICIT_ROW);
    jdb_rc_new(p, JD_TAG_ARR);
    if (!fresh) return p;
    if (RcEntry* e = rc_find((uintptr_t)p)) rc_register_children(e->key, e->gen);
    return p;
}

// A container written into a cell of an array or a map: the cell counts
// for it next to the variables that hold it, and lets go of it when it is
// written over or the holder goes. The first such cell makes its holder
// the owner; every further one, of the same holder or another, is kept with
// the holders beyond it. A cell of a holder the registry does not know, or
// of the container itself, keeps it for good.
extern "C" void jdb_rc_adopt(int64_t parent_bits, int64_t child_bits) {
    if (!g_rc.cap || !rc_ready()) return;
    RcEntry* c = rc_find((uintptr_t)child_bits);
    if (!c) return;
    RcEntry* p = rc_find((uintptr_t)parent_bits);
    if (!p || p == c || c->pinned) {
        c->pinned = 1;
        return;
    }
    c->rc++;
    if (c->owner) {
        g_rc.extra[c->key].push_back({ p->key, p->gen });
    } else {
        c->owner = p->key;
        c->owner_gen = p->gen;
    }
    if (p->kind == JD_TAG_ARR) ((JdbArray*)p->key)->flags |= JDB_ARR_OWNS_CHILDREN;
}

static void rc_drop_cell_text(JdbArray* arr, int64_t idx) {
    bool text = (arr->flags & 8) && arr->elem_tags ? arr->elem_tags[idx] == JD_TAG_STR
                                                   : (arr->flags & 2) != 0;
    if (!text) return;
    union { double d; int64_t i; } u; u.d = arr->data[idx];
    rc_drop_popped_text(arr, (const char*)(intptr_t)u.i);
}

static void rc_drop_popped_text(const void* arr, const char* s) {
    if (!s || !g_rc.cap || !rc_ready()) return;
    RcEntry* e = rc_find((uintptr_t)arr);
    if (!e || e->kind != JD_TAG_ARR || !e->owns_cells) return;
    if (rc_poison()) { if (*s) ((char*)s)[0] = '\x7f'; }
    else jdb_str_drop((char*)s);
}

static void rc_disown_cell(const void* holder, double old) {
    if (!g_rc.cap || !rc_ready()) return;
    union { double d; int64_t i; } u; u.d = old;
    RcEntry* c = rc_find((uintptr_t)u.i);
    if (!c) return;
    RcEntry* p = rc_find((uintptr_t)holder);
    if (!p || !rc_let_go(c, p->key, p->gen)) return;
    if (--c->rc == 0 && !c->pinned) g_rc.pool.push_back({ c->key, c->gen });
}

// The strings in an array the bridge built are copies made for it alone; a
// variable whose cells are only ever read hands them to the array.
extern "C" void jdb_rc_own_cells(int64_t bits) {
    if (!g_rc.cap || !rc_ready()) return;
    if (RcEntry* e = rc_find((uintptr_t)bits)) if (e->kind == JD_TAG_ARR) e->owns_cells = 1;
}

extern "C" void jdb_rc_pin(int64_t bits) {
    if (!g_rc.cap || !rc_ready()) return;
    if (RcEntry* e = rc_find((uintptr_t)bits)) e->pinned = 1;
}

extern "C" void jdb_rc_retain(int64_t bits) {
    if (!g_rc.cap || !rc_ready()) return;
    if (RcEntry* e = rc_find((uintptr_t)bits)) e->rc++;
}

extern "C" void jdb_rc_release(int64_t bits, int32_t global) {
    if (!g_rc.cap || !rc_ready()) return;
    RcEntry* e = rc_find((uintptr_t)bits);
    if (!e || e->rc <= 0) return;
    if (--e->rc == 0 && !e->pinned)
        (global ? g_rc.zombies : g_rc.pool).push_back({ e->key, e->gen });
}

// The store of a slot: the new value counts before the old one stops
// counting, so `a = a` keeps what a holds.
extern "C" void jdb_rc_assign(int64_t old_bits, int64_t new_bits, int32_t global) {
    if (old_bits == new_bits) return;
    jdb_rc_retain(new_bits);
    jdb_rc_release(old_bits, global);
}

extern "C" int64_t jdb_rc_mark(void) {
    return (int64_t)g_rc.pool.size();
}

static void rc_sweep(std::vector<RcPoolItem>& list, size_t from) {
    for (size_t i = from; i < list.size(); i++) {
        RcEntry* e = rc_find(list[i].key);
        if (!e || e->gen != list[i].gen || e->rc != 0 || e->pinned) continue;
        uint8_t kind = e->kind;
        bool owns = e->owns_cells != 0;
        rc_remove(e);
        rc_free_container(list[i].key, kind, owns, list[i].gen);
    }
    list.resize(from);
}

// Zombies whose container is gone already, freed by a pool sweep after a
// slot let go of it, leave the list so it stays as short as what is live.
static void rc_compact_zombies() {
    auto& z = g_rc.zombies;
    size_t keep = 0;
    for (size_t i = 0; i < z.size(); i++) {
        RcEntry* e = rc_find(z[i].key);
        if (e && e->gen == z[i].gen) z[keep++] = z[i];
    }
    z.resize(keep);
}

extern "C" void jdb_udt_free(struct JdbObject* obj);

static void rc_drop_later_strings() {
    for (char* s : g_rc.str_later) {
        if (rc_poison()) { if (*s) s[0] = (char)0x7f; }
        else jdb_str_drop(s);
    }
    g_rc.str_later.clear();
    for (void* o : g_rc.udt_later) jdb_udt_free((struct JdbObject*)o);
    g_rc.udt_later.clear();
}

// FUNC frames on the stack. A SUB is called at a statement boundary, so
// while no FUNC is open no caller holds a container half way through an
// expression, and what a global or a parameter let go of may be freed.
extern "C" void jdb_rc_func_enter(void) { if (rc_on_owner()) g_rc.func_depth++; }
extern "C" void jdb_rc_func_leave(void) { if (rc_on_owner() && g_rc.func_depth > 0) g_rc.func_depth--; }

// The end of a loop pass: frees what the pass made or let go of and nobody
// counts. The containers global slots and parameters let go of go too once
// no FUNC frame is open; under one they wait, and only their dead entries
// are dropped.
extern "C" void jdb_rc_drain(int64_t mark, int32_t main_level) {
    if (!rc_on_owner()) return;
    // A raise that left a FUNC skipped its leave; the main program is depth 0.
    if (main_level) g_rc.func_depth = 0;
    if (g_rc.func_depth == 0 && (!g_rc.str_later.empty() || !g_rc.udt_later.empty()))
        rc_drop_later_strings();
    size_t from = mark < 0 ? 0 : (size_t)mark;
    if (from >= g_rc.pool.size() && g_rc.zombies.empty()) return;
    if (!g_rc.cap || !rc_ready()) return;
    if (from < g_rc.pool.size()) rc_sweep(g_rc.pool, from);
    if (g_rc.zombies.empty()) return;
    if (g_rc.func_depth == 0) rc_sweep(g_rc.zombies, 0);
    else rc_compact_zombies();
}

// A string a global slot let go of inside a FUNC or SUB: a caller may be
// half way through an expression over it, so it waits for a drain with no
// FUNC frame open.
extern "C" void jdb_str_drop_later(char* s) {
    if (!s || !rc_on_owner()) return;
    g_rc.str_later.push_back(s);
}

// A field read out of the instance may still be on its way to the caller,
// so the instance goes at the next drain outside every FUNC.
extern "C" void jdb_udt_free_later(void* obj) {
    if (obj && rc_on_owner()) g_rc.udt_later.push_back(obj);
}

// Number of keys in a map.
int64_t jdb_map_count(JdbMap* m) {
    return m ? m->count : 0;
}

// Used by the codegen for nested-map / array-typed fields. The caller is
// responsible for knowing the real type - the map itself doesn't expose
// per-field tags through this entry point.
// The pointer behind a key that holds a container. A key that is not
// there answers null and nothing else; a key that holds a scalar is the
// interpreter's error, since there is nothing to read into.
void* jdb_map_get_obj(JdbMap* m, const char* key) {
    int64_t idx = map_find(m, key);
    if (idx < 0) return nullptr;
    union { double d; int64_t i; } u; u.d = m->values[idx];
    int32_t t = m->tags[idx];
    if (t == JD_TAG_ARR || t == JD_TAG_NATIVE_MAP) return (void*)(intptr_t)u.i;
    char msg[128];
    if (t == JD_TAG_STR) {
        const char* s = (const char*)(intptr_t)u.i;
        snprintf(msg, sizeof msg, "Cannot index into %s", s ? s : "");
    } else if (t == JD_TAG_BOOL) {
        snprintf(msg, sizeof msg, "Cannot index into %s", u.d != 0.0 ? "TRUE" : "FALSE");
    } else {
        char num[512];
        jdb_format_double(num, sizeof num, u.d);
        snprintf(msg, sizeof msg, "Cannot index into %s", num);
    }
    jdb_err_set(msg, 99);
    return nullptr;
}

// A message for the statement that finds nothing pending yet; an error
// already raised further in keeps its own words.
void jdb_err_set_if_clear(const char* msg, int64_t code) {
    if (g_err_code) return;
    jdb_err_set(msg, code);
}

// The same read for a step of an assignment chain. A missing entry is not a
// NONE to carry on with here: there is nothing to write into, which is what
// the interpreter says too.
void* jdb_map_get_obj_for_write(JdbMap* m, const char* key) {
    if (m && map_find(m, key) < 0) {
        jdb_err_set("Multi-index assignment: leaf is not array or object", 99);
        return nullptr;
    }
    return jdb_map_get_obj(m, key);
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
        *out_val = (int64_t)(intptr_t)jdb_str_dup_binary(s);
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
    // A stored absent value is absent when it is read back, the way a
    // missing key is.
    if (t == JD_TAG_NONE) return JD_TAG_NONE;
    *out_val = u.i;
    // A date is epoch seconds like any f64, but it has to keep saying so or
    // the read renders the number.
    return (t == JD_TAG_ARR || t == JD_TAG_NATIVE_MAP || t == JD_TAG_VM_HANDLE ||
            t == JD_TAG_DATE) ? t : JD_TAG_F64;
}

// String - String → remove all occurrences: "abcabc" - "bc" → "aa"
char* jdb_str_sub(const char* a, const char* b) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::remove_all(text_in(a), text_in(b)));
    }, [] { return _strdup(""); });
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

// Numeric → numeric element-wise, rows recursed. The function is a pure math
// builtin, so a big array of numbers is mapped band by band on every core.
JdbArray* jdb_array_apply_ff(JdbArray* arr, void* fnp) {
    if (!arr) return jdb_array_new(0);
    fn_ff fn = (fn_ff)fnp;
    if (JdbArray* fast = ew_apply(arr, fn)) return fast;
    auto* r = jdb_array_new(arr->length);
    if (!arr_all_num(arr)) {
        for (int64_t i = 0; i < arr->length; i++) {
            int k = cell_kind(arr, i);
            if (k == JD_TAG_ARR)
                put_kind(r, i, encode_inner(jdb_array_apply_ff(decode_inner(arr->data[i]), fnp)), JD_TAG_ARR);
            else
                put_kind(r, i, kind_is_num(k) ? fn(arr->data[i]) : 0.0, JD_TAG_F64);
        }
        return settle_kinds(r);
    } else {
        for (int64_t i = 0; i < arr->length; i++)
            r->data[i] = fn(arr->data[i]);
    }
    return r;
}

// A function over the string cells of an array, into the rows of a nested
// one: the leaf gets the string and answers the new cell, a cell that is no
// string stays as it is. str_result marks the answers as strings. An array
// that carries per-cell tags answers one that does too.
static JdbArray* apply_string_cells(JdbArray* arr, bool str_result,
                                    const std::function<double(const char*)>& leaf) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    bool tagged = (arr->flags & 8) && arr->elem_tags;
    if (tagged) {
        r->elem_tags = (int8_t*)malloc((size_t)(arr->length > 0 ? arr->length : 1));
        r->flags |= 8;
        bool all_str = arr->length > 0;
        for (int64_t i = 0; i < arr->length; i++) {
            int8_t t = arr->elem_tags[i];
            union { double d; int64_t i; } u; u.d = arr->data[i];
            if (t == JD_TAG_STR) {
                r->data[i] = leaf(u.i ? (const char*)(intptr_t)u.i : "");
                t = str_result ? (int8_t)JD_TAG_STR : (int8_t)JD_TAG_F64;
            } else if (t == JD_TAG_ARR) {
                r->data[i] = encode_inner(apply_string_cells(decode_inner(arr->data[i]), str_result, leaf));
            } else {
                r->data[i] = arr->data[i];
            }
            r->elem_tags[i] = t;
            if (t != JD_TAG_STR) all_str = false;
        }
        if (all_str) r->flags |= 3;
        else if (str_result) r->flags |= 1;
        return r;
    }
    if ((arr->flags & 1) && !(arr->flags & 2)) {
        r->flags |= 1;
        for (int64_t i = 0; i < arr->length; i++)
            r->data[i] = cell_is_arr(arr, i)
                ? encode_inner(apply_string_cells(decode_inner(arr->data[i]), str_result, leaf))
                : arr->data[i];
        return r;
    }
    if (!(arr->flags & 2)) {
        memcpy(r->data, arr->data, (size_t)arr->length * sizeof(double));
        r->flags = arr->flags;
        return r;
    }
    for (int64_t i = 0; i < arr->length; i++) {
        union { double d; int64_t i; } u; u.d = arr->data[i];
        r->data[i] = leaf(u.i ? (const char*)(intptr_t)u.i : "");
    }
    if (str_result) r->flags |= 3;
    return r;
}

static inline double encode_str_cell(char* s) {
    union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)s;
    return u.d;
}

// String → string element-wise (UCASE$, TRIM$, REVERSE$).
JdbArray* jdb_array_apply_ss(JdbArray* arr, void* fnp) {
    return jdb_guard([&]() -> JdbArray* {
        fn_ss fn = (fn_ss)fnp;
        return apply_string_cells(arr, true, [fn](const char* s) { return encode_str_cell(fn(s)); });
    }, [] { return jdb_array_new(0); });
}

// String → int element-wise (LEN$, ASC).
JdbArray* jdb_array_apply_ifs(JdbArray* arr, void* fnp) {
    return jdb_guard([&]() -> JdbArray* {
        fn_ifs fn = (fn_ifs)fnp;
        return apply_string_cells(arr, false, [fn](const char* s) { return (double)fn(s); });
    }, [] { return jdb_array_new(0); });
}

// String + int → string element-wise (LEFT$, RIGHT$ with scalar count).
JdbArray* jdb_array_apply_sfi(JdbArray* arr, int64_t n, void* fnp) {
    return jdb_guard([&]() -> JdbArray* {
        fn_sfi fn = (fn_sfi)fnp;
        return apply_string_cells(arr, true, [fn, n](const char* s) { return encode_str_cell(fn(s, n)); });
    }, [] { return jdb_array_new(0); });
}

// String + int + int → string element-wise (MID$).
JdbArray* jdb_array_apply_sfii(JdbArray* arr, int64_t a, int64_t b, void* fnp) {
    fn_sfii fn = (fn_sfii)fnp;
    return apply_string_cells(arr, true, [fn, a, b](const char* s) { return encode_str_cell(fn(s, a, b)); });
}

char* jdb_replace(const char* src, const char* find, const char* rep);

// REPLACE$ over every string cell of an array.
JdbArray* jdb_array_replace(JdbArray* arr, const char* find, const char* repl) {
    return jdb_guard([&]() -> JdbArray* {
        return apply_string_cells(arr, true, [&](const char* s) { return encode_str_cell(jdb_replace(s, find, repl)); });
    }, [] { return jdb_array_new(0); });
}

// double + int → double (ROUND(arr, 2))
JdbArray* jdb_array_apply_ffi(JdbArray* arr, int64_t n, void* fnp) {
    if (!arr) return jdb_array_new(0);
    double (*fn)(double, int64_t) = (double(*)(double, int64_t))fnp;
    auto* r = jdb_array_new(arr->length);
    if (!arr_all_num(arr)) {
        for (int64_t i = 0; i < arr->length; i++) {
            int k = cell_kind(arr, i);
            if (k == JD_TAG_ARR)
                put_kind(r, i, encode_inner(jdb_array_apply_ffi(decode_inner(arr->data[i]), n, fnp)), JD_TAG_ARR);
            else
                put_kind(r, i, kind_is_num(k) ? fn(arr->data[i], n) : 0.0, JD_TAG_F64);
        }
        return settle_kinds(r);
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
    if (!arr || idx < 0 || idx >= arr->length) {
        jdb_index_out_of_bounds(idx);
        return;
    }
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
            if (s) jdb_print_str(s);
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
        if (t == JD_TAG_NATIVE_MAP) {
            union { double d; int64_t i; } u; u.d = val;
            JdbMap* m = (JdbMap*)(intptr_t)u.i;
            if (!m) return;
            char* s = jdb_map_str(m);
            if (s) { fputs(s, stdout); free(s); }
            return;
        }
        if (t == 8) {  // BOOL
            printf("%s", val != 0.0 ? "TRUE" : "FALSE");
            return;
        }
        if (t == JD_TAG_DATE) {
            char* iso = jdb_cvdate_num(val);
            fputs(iso ? iso : "", stdout);
            free(iso);
            return;
        }
        // 0 (unknown), 1 (F64), other → numeric
        char num[512]; jdb_format_double(num, sizeof(num), val); fputs(num, stdout);
        return;
    }
    if (has_string) {
        // String-flag alone is enough - element is a ptr-encoded char*.
        union { double d; int64_t i; } u; u.d = val;
        const char* s = (const char*)(intptr_t)u.i;
        if (s) jdb_print_str(s);
    } else if (has_ptr && bits_look_ptr(val)) {
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
    } else if ((arr->flags & 16) != 0) {
        char* iso = jdb_cvdate_num(val);
        fputs(iso ? iso : "", stdout);
        free(iso);
    } else {
        char num[512];
        jdb_format_double(num, sizeof(num), val);
        fputs(num, stdout);
    }
}

// Element-wise concat: each cell of arr + the scalar as suffix (or prefix if
// scalar_left). A string cell is joined as it is, a number cell as PRINT
// writes it; the answer is a string array.
JdbArray* jdb_array_str_concat(JdbArray* arr, const char* s, int32_t scalar_left) {
    if (!arr || !s) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    try {
        jdb_text::View sv = text_in(s);
        std::string scalar(sv.p ? sv.p : "", sv.n);
        for (int64_t i = 0; i < arr->length; i++) {
            int k = cell_kind(arr, i);
            if (k == JD_TAG_ARR) {
                JdbArray* sub = jdb_array_str_concat(decode_inner(arr->data[i]), s, scalar_left);
                put_kind(r, i, encode_inner(sub), JD_TAG_ARR);
                continue;
            }
            std::string elem = (k == JD_TAG_STR || kind_is_num(k)) ? cell_text(arr->data[i], k) : std::string();
            put_kind(r, i, encode_str(text_out(scalar_left ? scalar + elem : elem + scalar)), JD_TAG_STR);
        }
    } catch (...) {
        jdb_err_set("out of memory for a string this size", 1);
    }
    return settle_kinds(r);
}

// ROUND of every cell to a number of places, into the rows of a matrix.
JdbArray* jdb_array_round_p(JdbArray* arr, double places) {
    if (!arr) return jdb_array_new(0);
    auto* r = jdb_array_new(arr->length);
    if (arr_all_num(arr)) {
        for (int64_t i = 0; i < arr->length; i++) r->data[i] = jdb_round_p(arr->data[i], places);
        return r;
    }
    for (int64_t i = 0; i < arr->length; i++) {
        int k = cell_kind(arr, i);
        if (k == JD_TAG_ARR)
            put_kind(r, i, encode_inner(jdb_array_round_p(decode_inner(arr->data[i]), places)), JD_TAG_ARR);
        else
            put_kind(r, i, kind_is_num(k) ? jdb_round_p(arr->data[i], places) : 0.0, JD_TAG_F64);
    }
    return settle_kinds(r);
}

// The leaves of a fresh RESHAPE answer take the kinds of the cells they
// were filled from, cyclically; a string leaf is a copy of its own.
static void kind_leaves(JdbArray* a, const std::vector<int8_t>& kinds, size_t& idx) {
    if (!a) return;
    if (a->flags & 1) {
        for (int64_t i = 0; i < a->length; i++) kind_leaves(decode_inner(a->data[i]), kinds, idx);
        return;
    }
    for (int64_t i = 0; i < a->length; i++) {
        int k = kinds[idx++ % kinds.size()];
        if (k == JD_TAG_STR)
            put_kind(a, i, encode_str(jdb_str_dup_binary(decode_str(a->data[i]))), JD_TAG_STR);
        else
            put_kind(a, i, a->data[i], k);
    }
    settle_kinds(a);
}

// RESHAPE(array, shape): the cells of the array, in order and again from the
// start when they run out, in the dimensions of the shape.
JdbArray* jdb_reshape(JdbArray* src, JdbArray* shape) {
    if (!src || !shape) return jdb_array_new(0);
    try {
        std::vector<int64_t> dims;
        // Every dimension a count, and all of them together a number of
        // cells memory can hold.
        double cells = 1.0;
        for (int64_t i = 0; i < shape->length; i++) {
            double d = shape->data[i];
            if (!cell_is_num(shape, i) || d != d || d < 0 || d > 9.0e15) {
                jdb_err_set("RESHAPE: every dimension of the shape is a count", 11);
                return jdb_array_new(0);
            }
            cells *= d;
            dims.push_back((int64_t)d);
        }
        if (cells > 4.0e9) {
            jdb_err_set("Out of memory", 99);
            return jdb_array_new(0);
        }
        if (JdbArray* fast = ew_reshape(src, dims)) return fast;
        std::vector<double> flat;
        std::vector<int8_t> kinds;
        flatten_into(src, flat, &kinds);
        if (flat.empty()) { jdb_err_set("RESHAPE: the array has no cells", 11); return jdb_array_new(0); }
        if (dims.empty()) return jdb_array_new(0);
        size_t idx = 0;
        JdbArray* out = jdb_shape::build<RtShape>(dims, 0, flat, idx);
        bool all_num = true;
        for (int8_t k : kinds) all_num = all_num && kind_is_num(k);
        if (!all_num) {
            size_t kidx = 0;
            kind_leaves(out, kinds, kidx);
        }
        return out;
    } catch (...) {
        jdb_err_set("RESHAPE: out of memory", 1);
        return jdb_array_new(0);
    }
}

// Get array element as a pointer (decodes f64-encoded ptr back).
// Used when array has flags bit 0 set (contains pointers/strings).
void* jdb_array_get_ptr(JdbArray* arr, int64_t idx) {
    if (!arr || idx < 0 || idx >= arr->length) return nullptr;
    union { double d; int64_t i; } u; u.d = arr->data[idx];
    return (void*)(intptr_t)u.i;
}

// One step of a nested write, a[i][j] = v or a[i]{"k"} = v: the row or the
// map in cell i, which the next step writes into. An index outside the
// array, or a cell that holds no container, raises the interpreter's error
// and answers null.
void* jdb_array_step_write(JdbArray* arr, int64_t idx) {
    if (arr && (idx < 0 || idx >= arr->length)) {
        std::string msg = "Multi-index: out of bounds " + std::to_string(idx);
        jdb_err_set(msg.c_str(), 13);
        return nullptr;
    }
    bool container = false;
    union { double d; uint64_t u; } c; c.u = 0;
    if (arr) {
        c.d = arr->data[idx];
        if ((arr->flags & 8) && arr->elem_tags) {
            int t = arr->elem_tags[idx];
            container = (t == JD_TAG_ARR || t == JD_TAG_NATIVE_MAP);
        } else {
            container = (arr->flags & 1) && !(arr->flags & 2) &&
                        c.u >= 4096 && c.u < (1ULL << 47);
        }
    }
    if (!container) {
        jdb_err_set("Multi-index assignment: leaf is not array or object", 99);
        return nullptr;
    }
    return (void*)(intptr_t)c.u;
}

// LEN that returns shape array for nested (2D+) arrays, otherwise scalar length.
// Mirrors interpreter behavior: LEN(2d_arr) returns [rows, cols, ...].
// For 1D arrays returns a scalar-wrapped value (caller decodes).
// Returns: ptr to JdbArray holding the shape, OR ptr encoding a single i64.
// Caller uses flags to tell the difference: nested=shape array, non-nested=1D scalar wrap.
// Bit 0 says the cells hold pointers, which a string array sets as well, so
// it alone does not mean nested. Walking a string array as one dereferences
// a char* and reads the text as the next dimension.
static bool shape_is_nested(const JdbArray* a) {
    if (!a || !(a->flags & 1) || (a->flags & 2)) return false;
    if ((a->flags & 8) && a->elem_tags && a->length > 0)
        return a->elem_tags[0] == JD_TAG_ARR;
    return true;
}

}  // extern "C": the matrix product below is C++
// ── MATMUL / MATMUL_T on the program's own arrays ───────────
// The same rules as the VM's (numerics.cpp): a flat array on the right is a
// column, one on the left a row, the answer of a vector side is flat, and two
// vectors are no product. Rows are copied into one row-major block, Eigen
// multiplies, and a product big enough is cut into row bands, one per core.
using JdbMat = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

static JdbArray* mat_row(const JdbArray* a, int64_t r) {
    union { double d; int64_t i; } u; u.d = a->data[r];
    return (JdbArray*)(intptr_t)u.i;
}

// rows x cols of a matrix argument; a flat one is rows x 0.
static bool mat_shape(const JdbArray* a, int64_t& rows, int64_t& cols, bool& flat) {
    rows = a ? a->length : 0;
    cols = 0;
    flat = true;
    if (rows > 0 && shape_is_nested(a)) {
        flat = false;
        const JdbArray* r0 = mat_row(a, 0);
        cols = r0 ? r0->length : 0;
    }
    return a != nullptr;
}

// Every cell a number: a flat array of them, or rows that each are one.
static bool mat_numbers(const JdbArray* a, bool flat) {
    auto flat_numbers = [](const JdbArray* v) {
        if (!v || (v->flags & 2)) return false;
        if ((v->flags & 8) && v->elem_tags) {
            for (int64_t i = 0; i < v->length; i++) {
                int t = v->elem_tags[i];
                if (t != JD_TAG_F64 && t != JD_TAG_I64 && t != JD_TAG_BOOL) return false;
            }
            return true;
        }
        return !(v->flags & 1);
    };
    if (flat) return flat_numbers(a);
    for (int64_t r = 0; r < a->length; r++) {
        if ((a->flags & 8) && a->elem_tags && a->elem_tags[r] != JD_TAG_ARR) return false;
        if (!flat_numbers(mat_row(a, r))) return false;
    }
    return true;
}

static JdbMat mat_from(const JdbArray* a, bool flat, int64_t rows, int64_t cols) {
    if (flat) {
        JdbMat m(rows, 1);
        for (int64_t r = 0; r < rows; r++) m(r, 0) = a->data[r];
        return m;
    }
    JdbMat m(rows, cols);
    for (int64_t r = 0; r < rows; r++) {
        const JdbArray* row = mat_row(a, r);
        int64_t n = row ? std::min<int64_t>(row->length, cols) : 0;
        double* dst = m.data() + r * cols;
        if (n) memcpy(dst, row->data, (size_t)n * sizeof(double));
        for (int64_t c = n; c < cols; c++) dst[c] = 0.0;
    }
    return m;
}

static JdbArray* matmul_body(JdbArray* a, JdbArray* b, int32_t transpose_left, const char* fn) {
    int64_t ra, ca, rb, cb;
    bool fa, fb;
    if (!mat_shape(a, ra, ca, fa) || !mat_shape(b, rb, cb, fb)) {
        jdb_err_set((std::string(fn) + ": expected a matrix argument").c_str(), 1);
        return jdb_array_new(0);
    }
    if (fa && fb) {
        jdb_err_set((std::string(fn) + ": two vectors; the dot product is SUM(a * b)").c_str(), 1);
        return jdb_array_new(0);
    }
    if (!mat_numbers(a, fa) || !mat_numbers(b, fb)) {
        jdb_err_set((std::string(fn) + ": expected a matrix of numbers").c_str(), 1);
        return jdb_array_new(0);
    }
    JdbMat R = mat_from(b, fb, rb, cb);
    JdbMat L = mat_from(a, fa, ra, ca);
    bool lt = fa || transpose_left;
    int64_t lrows = lt ? L.cols() : L.rows(), linner = lt ? L.rows() : L.cols();
    if (linner != R.rows()) {
        auto text = [](int64_t r, int64_t c, bool f) {
            return f ? std::to_string(r) : std::to_string(r) + "x" + std::to_string(c);
        };
        std::string left = transpose_left && !fa ? std::to_string(ca) + "x" + std::to_string(ra)
                                                 : text(ra, ca, fa);
        jdb_err_set((std::string(fn) + ": incompatible dimensions, " + left + " and " +
                     text(rb, cb, fb)).c_str(), 1);
        return jdb_array_new(0);
    }
    int64_t pcols = R.cols();
    JdbMat P(lrows, pcols);
    unsigned workers = mat_workers((double)lrows * (double)linner * (double)pcols);
    if (!lt) {
        mat_bands(lrows, workers, [&](int64_t lo, int64_t hi) {
            P.middleRows(lo, hi - lo).noalias() = L.middleRows(lo, hi - lo) * R;
        });
    } else if (workers <= 1) {
        P.noalias() = L.transpose() * R;
    } else {
        // L' * R sums over L's rows: each band adds its share, then the shares meet.
        int64_t n = L.rows();
        unsigned w = (unsigned)std::min<int64_t>(workers, n);
        std::vector<JdbMat> part(w, JdbMat::Zero(lrows, pcols));
        int64_t step = (n + w - 1) / w;
        mat_bands(w, w, [&](int64_t lo, int64_t hi) {
            for (int64_t k = lo; k < hi; k++) {
                int64_t s = k * step, e = (std::min)(n, s + step);
                if (s < e) part[k].noalias() = L.middleRows(s, e - s).transpose() * R.middleRows(s, e - s);
            }
        });
        P.setZero();
        for (auto& p : part) P += p;
    }
    if (fa || fb) {
        int64_t n = fa ? pcols : lrows;
        JdbArray* out = jdb_array_new(n);
        for (int64_t i = 0; i < n; i++) out->data[i] = fa ? P(0, i) : P(i, 0);
        return out;
    }
    JdbArray* out = jdb_array_new(lrows);
    out->flags |= 1;
    mat_bands(lrows, mat_workers((double)lrows * (double)pcols * 4.0), [&](int64_t lo, int64_t hi) {
        for (int64_t r = lo; r < hi; r++) {
            JdbArray* row = jdb_array_new(pcols);
            if (pcols) memcpy(row->data, P.data() + r * pcols, (size_t)pcols * sizeof(double));
            union { double d; int64_t i; } u; u.i = (int64_t)(intptr_t)row;
            out->data[r] = u.d;
        }
    });
    return out;
}

// The C entry the compiled program calls: nothing of C++ crosses it.
extern "C" JdbArray* jdb_matmul(JdbArray* a, JdbArray* b, int32_t transpose_left) {
    const char* fn = transpose_left ? "MATMUL_T" : "MATMUL";
    try {
        return matmul_body(a, b, transpose_left, fn);
    } catch (...) {
        // Fixed texts: a message built here could need the memory that ran out.
        jdb_err_set(transpose_left ? "MATMUL_T: out of memory for a product this size"
                                   : "MATMUL: out of memory for a product this size", 1);
        return jdb_array_new(0);
    }
}

// TRANSPOSE: the columns of a matrix become its rows. A matrix of numbers is
// copied row by row of the answer, banded across the cores; other cells keep
// their kind, and a row shorter than the first answers 0 where it has no
// cell. A flat array answers a copy of itself, as in the interpreter.
extern "C" JdbArray* jdb_transpose(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        int64_t rows = arr->length;
        if (rows == 0 || !cell_is_arr(arr, 0)) return RtShape::clone(arr);
        int64_t w = ew_matrix_width(arr);
        if (w >= 0) {
            bool as_bool = (decode_inner(arr->data[0])->flags & 4) != 0;
            JdbArray* out = ew_rows(w, rows, as_bool, [&](int64_t j, double* o) {
                for (int64_t i = 0; i < rows; i++) o[i] = decode_inner(arr->data[i])->data[j];
            });
            if (out) return out;
        }
        JdbArray* first = decode_inner(arr->data[0]);
        int64_t cols = first ? first->length : 0;
        JdbArray* out = jdb_array_new(cols);
        out->flags |= 1;
        for (int64_t j = 0; j < cols; j++) {
            JdbArray* col = jdb_array_new(rows);
            for (int64_t i = 0; i < rows; i++) {
                JdbArray* src = cell_is_arr(arr, i) ? decode_inner(arr->data[i]) : nullptr;
                int k = src && j < src->length ? cell_kind(src, j) : -1;
                if (k < 0) put_kind(col, i, 0.0, JD_TAG_F64);
                // A row in a cell is copied, so the answer shares none with
                // its source.
                else if (k == JD_TAG_ARR) put_kind(col, i, encode_inner(RtShape::clone(decode_inner(src->data[j]))), k);
                else put_kind(col, i, src->data[j], k);
            }
            out->data[j] = encode_inner(settle_kinds(col));
        }
        return out;
    }, [] { return jdb_array_new(0); });
}
extern "C" {

JdbArray* jdb_array_len_shape(JdbArray* arr) {
    if (!arr) return jdb_array_new(0);
    // Non-nested: return a single-element array with the length
    if (!shape_is_nested(arr)) {
        auto* r = jdb_array_new(1);
        r->data[0] = (double)arr->length;
        return r;
    }
    // Nested: walk structure to build shape
    auto* r = jdb_array_new(0);
    JdbArray* cur = arr;
    // An array that holds itself would be walked for ever; no real one is
    // that deep.
    while (cur && cur->length > 0 && r->length < 256) {
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
        if (shape_is_nested(cur) && cur->length > 0) {
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
    if (!arr || idx < 0 || idx >= arr->length) {
        jdb_index_out_of_bounds(idx);
        return "";
    }
    intptr_t p;
    memcpy(&p, &arr->data[idx], sizeof(p));
    return (const char*)p;
}

// VAL of a cell the compiler holds as a double: the bits of a string
// pointer (an OS.ARGS cell) are read as that string, a number is itself.
double jdb_val_ptr(double encoded_ptr) {
    if (!bits_look_ptr(encoded_ptr)) return encoded_ptr;
    intptr_t p;
    memcpy(&p, &encoded_ptr, sizeof(p));
    return atof((const char*)p);
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
// One placeholder of FORMAT$ rendered into out: a number, or a string that
// is read as a number when the spec asks for one. Raises FORMAT$'s error
// for a spec the language does not have.
static void jdb_format_one_arg(std::string& out, const char* spec, double val,
                               const char* str_val, bool is_str) {
    jdb_fmt::Spec s;
    if (!jdb_fmt::parse_spec(spec, s)) {
        const char* shown = spec ? (spec[0] == ':' ? spec + 1 : spec) : "";
        jdb_err_set(jdb_fmt::spec_error(shown).c_str(), 1);
        return;
    }
    std::string text;
    if (is_str && !jdb_fmt::wants_number(s)) {
        jdb_text::View sv = text_in(str_val);
        text.assign(sv.p ? sv.p : "", sv.n);
    } else {
        if (is_str) val = atof(str_val ? str_val : "");
        char plain[512];
        jdb_format_double(plain, (int)sizeof(plain), val);
        text = jdb_fmt::number_text(s, val, (int64_t)val, plain);
    }
    out += jdb_fmt::lay_out(s, text, !is_str);
}

// FORMAT$ over an array of numbers.
char* jdb_format(const char* fmt, JdbArray* args) {
    std::string result;
    try {
        jdb_fmt::expand(fmt ? fmt : "", result,
            [&](int ai, const std::string& spec, std::string& out) {
                // A placeholder past the arguments renders as "?".
                if (!args || ai < 0 || ai >= args->length) { out += "?"; return true; }
                jdb_format_one_arg(out, spec.c_str(), args->data[ai], nullptr, false);
                return true;
            });
    } catch (...) {
        jdb_err_set("FORMAT$: out of memory for a text this size", 1);
        return _strdup("");
    }
    return text_out(result);
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
    void jdrt_str_free(char* s);
}
// The text of a VM value is a copy the bridge made for the caller.
static void bridge_text_free(const char* s) { if (s) jdrt_str_free((char*)s); }
extern "C" void jdb_runtime_set_handle(void* h) { g_jdrt_handle = h; }

// types[i] is 'd' for an f64 bit pun, 's' for a const char*, or 'h' for a
// VM handle (an i64 key into the VM's value store). A handle is read as a
// number when the spec asks for one and as text otherwise, which is the
// only stage that knows both the spec and the value's type: p{"name"} may
// be a string and p{"price"} a number in the same FORMAT$ call.
static char* jdb_format_tagged_impl(const char* fmt, const char* types,
                                     int n, const int64_t* raw) {
    std::string result;
    try {
        jdb_fmt::expand(fmt ? fmt : "", result,
            [&](int ai, const std::string& spec, std::string& out) {
                if (ai < 0 || ai >= n) { out += "?"; return true; }
                char tag = types ? types[ai] : 'd';
                int64_t r64 = raw[ai];
                if (tag == 's') {
                    jdb_format_one_arg(out, spec.c_str(), 0.0,
                                       (const char*)(intptr_t)r64, true);
                } else if (tag == 'h') {
                    jdb_fmt::Spec s;
                    bool numeric = jdb_fmt::parse_spec(spec.c_str(), s) && jdb_fmt::wants_number(s);
                    if (numeric && g_jdrt_handle) {
                        jdb_format_one_arg(out, spec.c_str(),
                                           jdrt_val_to_f64(g_jdrt_handle, r64), nullptr, false);
                    } else if (g_jdrt_handle) {
                        const char* vt = jdrt_val_to_str(g_jdrt_handle, r64);
                        jdb_format_one_arg(out, spec.c_str(), 0.0, vt, true);
                        bridge_text_free(vt);
                    } else {
                        out += "<no-runtime>";
                    }
                } else {
                    union { double d; int64_t i; } u; u.i = r64;
                    jdb_format_one_arg(out, spec.c_str(), u.d, nullptr, false);
                }
                return true;
            });
    } catch (...) {
        jdb_err_set("FORMAT$: out of memory for a text this size", 1);
        return _strdup("");
    }
    return text_out(result);
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
// BASIC string slicing via `/`: n / "str" answers the first n bytes,
// "str" / n the last n.
char* jdb_str_slice(const char* s, int64_t n, int32_t from_left) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::slice(text_in(s), n, from_left != 0));
    }, [] { return _strdup(""); });
}

int64_t jdb_len_str(const char* s) {
    return s ? jdb_str_blen(s) : 0;
}

// Strict MID: a start past the end is an error TRY/CATCH can observe.
char* jdb_mid(const char* s, int64_t start, int64_t length) {
    return jdb_guard([&]() -> char* {
        jdb_text::View v = text_in(s);
        if (!jdb_text::in_range(v, start)) {
            jdb_err_set("MID: index out of range", 1);
            return _strdup("");
        }
        return text_out(jdb_text::mid(v, start, length));
    }, [] { return _strdup(""); });
}

// Lenient MID$: a start past the end answers an empty string, so a parser
// may scan past the end of its text without checking.
char* jdb_mid_lax(const char* s, int64_t start, int64_t length) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::mid(text_in(s), start, length));
    }, [] { return _strdup(""); });
}

char* jdb_left(const char* s, int64_t n) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::left(text_in(s), n));
    }, [] { return _strdup(""); });
}

char* jdb_right(const char* s, int64_t n) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::right(text_in(s), n));
    }, [] { return _strdup(""); });
}

char* jdb_upper(const char* s) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::upper(text_in(s)));
    }, [] { return _strdup(""); });
}

char* jdb_lower(const char* s) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::lower(text_in(s)));
    }, [] { return _strdup(""); });
}

char* jdb_trim(const char* s) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::trim(text_in(s)));
    }, [] { return _strdup(""); });
}

char* jdb_chr(int64_t code) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::chr(code));
    }, [] { return _strdup(""); });
}

int64_t jdb_asc(const char* s) {
    return jdb_text::asc(text_in(s));
}

int64_t jdb_instr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return -1;
    return jdb_text::find(text_in(haystack), text_in(needle));
}

char* jdb_replace(const char* src, const char* find, const char* rep) {
    return jdb_guard([&]() -> char* {
        if (!src || !find || !rep) return _strdup(src ? src : "");
        return text_out(jdb_text::replace_all(text_in(src), text_in(find), text_in(rep)));
    }, [] { return _strdup(""); });
}

char* jdb_str(double val) {
    char buf[512];
    jdb_format_double(buf, sizeof(buf), val);
    return _strdup(buf);
}

// Element-wise stringify: returns an ARRAY of string pointers, one per
// leaf scalar. Mirrors the interpreter's STR$(array).
JdbArray* jdb_array_str(JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        auto* r = jdb_array_new(arr->length);
        for (int64_t i = 0; i < arr->length; i++) {
            int k = cell_kind(arr, i);
            if (k == JD_TAG_ARR) {
                put_kind(r, i, encode_inner(jdb_array_str(decode_inner(arr->data[i]))), JD_TAG_ARR);
                continue;
            }
            char* s = k == JD_TAG_STR ? jdb_str_dup_binary(decode_str(arr->data[i]))
                    : kind_is_num(k) ? jdb_str(arr->data[i]) : _strdup("");
            put_kind(r, i, encode_str(s), JD_TAG_STR);
        }
        return settle_kinds(r);
    }, [] { return jdb_array_new(0); });
}

// Format a JdbMap* the way the interpreter does:
//   {"key1": value1, "key2": value2}
// Strings are quoted; numbers use jdb_format_double; booleans render as
// TRUE/FALSE; nested maps and arrays recurse.
char* jdb_frmv(JdbArray* arr);  // forward decl - definition below
static void jdb_map_text(JdbMap* m, std::string& out);
static void jdb_value_text(std::string& out, double val, int32_t tag) {
    union { double d; int64_t i; } u; u.d = val;
    char num[512];
    switch (tag) {
        case JD_TAG_I64:
            // Map cells hold every number as f64; the tag says how it reads.
            snprintf(num, sizeof num, "%lld", (long long)val);
            out += num;
            return;
        case JD_TAG_STR: {
            jdb_text::View s = text_in((const char*)(intptr_t)u.i);
            out += '"';
            out.append(s.p ? s.p : "", s.n);
            out += '"';
            return;
        }
        case JD_TAG_ARR: {
            char* sub = jdb_frmv((JdbArray*)(intptr_t)u.i);
            jdb_text::View s = text_in(sub);
            if (sub) out.append(s.p, s.n); else out += "[]";
            free(sub);
            return;
        }
        case JD_TAG_NATIVE_MAP:
            jdb_map_text((JdbMap*)(intptr_t)u.i, out);
            return;
        case JD_TAG_BOOL:
            out += val != 0.0 ? "TRUE" : "FALSE";
            return;
        default:
            jdb_format_double(num, sizeof num, val);
            out += num;
            return;
    }
}

static void jdb_map_text(JdbMap* m, std::string& out) {
    if (!m || m->count == 0) { out += "{}"; return; }
    out += '{';
    for (int64_t i = 0; i < m->count; i++) {
        if (i > 0) out += ", ";
        out += '"';
        out += m->keys[i] ? m->keys[i] : "";
        out += "\": ";
        jdb_value_text(out, m->values[i], m->tags[i]);
    }
    out += '}';
}

char* jdb_map_str(JdbMap* m) {
    try {
        std::string out;
        jdb_map_text(m, out);
        return text_out(out);
    } catch (...) {
        jdb_err_set("out of memory for the text of a map this size", 1);
        return _strdup("{}");
    }
}

char* jdb_str_bool(int64_t val) {
    return _strdup(val ? "TRUE" : "FALSE");
}

char* jdb_str_str(const char* s) {
    // STR$(string) - pass-through. Returns a fresh copy because callers
    // own (and may free) the result.
    return jdb_str_dup_binary(s);
}

char* jdb_space(int64_t n) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::spaces(n));
    }, [] { return _strdup(""); });
}

int64_t jdb_str_eq(const char* a, const char* b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    return jdb_text::equal(text_in(a), text_in(b)) ? 1 : 0;
}

int64_t jdb_str_ne(const char* a, const char* b) {
    return !jdb_str_eq(a, b);
}

// Binary-safe 3-way compare: <0 if a<b, 0 if equal, >0 if a>b. Powers the
// native string ordering operators (<,>,<=,>=) and string-array compares.
// Three-way compare of two tagged values: two strings compare by content,
// anything else as numbers (a string operand counts by its numeric value).

// The same comparison with the operands the other way round. Codes are
// 0=eq, 1=ne, 4=lt, 5=le, 6=gt, 7=ge.
static inline int32_t jdb_cmp_op_flipped(int32_t op) {
    switch (op) {
        case 4: return 6;
        case 6: return 4;
        case 5: return 7;
        case 7: return 5;
        default: return op;
    }
}
int64_t jdb_dyn_cmp(int64_t a, int32_t ta, int64_t b, int32_t tb) {
    // A VM value behind a handle is read through the bridge: as text
    // against a string, as a number otherwise.
    if (g_jdrt_handle && (ta == JD_TAG_VM_HANDLE || tb == JD_TAG_VM_HANDLE)) {
        bool textual = ta == JD_TAG_STR || tb == JD_TAG_STR ||
                       (ta == JD_TAG_VM_HANDLE && tb == JD_TAG_VM_HANDLE);
        if (textual) {
            const char* sa = ta == JD_TAG_VM_HANDLE ? jdrt_val_to_str(g_jdrt_handle, a)
                                                    : (const char*)(intptr_t)a;
            const char* sb = tb == JD_TAG_VM_HANDLE ? jdrt_val_to_str(g_jdrt_handle, b)
                                                    : (const char*)(intptr_t)b;
            int64_t order = jdb_str_cmp(sa, sb);
            if (ta == JD_TAG_VM_HANDLE) bridge_text_free(sa);
            if (tb == JD_TAG_VM_HANDLE) bridge_text_free(sb);
            return order;
        }
        if (ta == JD_TAG_VM_HANDLE) {
            double d = jdrt_val_to_f64(g_jdrt_handle, a);
            memcpy(&a, &d, sizeof d); ta = JD_TAG_F64;
        }
        if (tb == JD_TAG_VM_HANDLE) {
            double d = jdrt_val_to_f64(g_jdrt_handle, b);
            memcpy(&b, &d, sizeof d); tb = JD_TAG_F64;
        }
    }
    if (ta == JD_TAG_STR && tb == JD_TAG_STR)
        return jdb_str_cmp((const char*)(intptr_t)a, (const char*)(intptr_t)b);
    auto as_num = [](int64_t v, int32_t t) -> double {
        if (t == JD_TAG_I64 || t == JD_TAG_BOOL) return (double)v;
        if (t == JD_TAG_STR) return 0.0;
        double d; memcpy(&d, &v, sizeof d); return d;
    };
    // A string against a number is no number and counts as 0.
    double x = as_num(a, ta), y = as_num(b, tb);
    return x < y ? -1 : (x > y ? 1 : 0);
}

// A runtime-tagged scalar as a number: a string, a map or a value without a
// runtime to ask is no number and counts as 0.
static double dyn_num(int64_t bits, int32_t t) {
    if (t == JD_TAG_I64 || t == JD_TAG_BOOL) return (double)bits;
    if (t == JD_TAG_VM_HANDLE) return g_jdrt_handle ? jdrt_val_to_f64(g_jdrt_handle, bits) : 0.0;
    if (!jd_is_f64_tag(t)) return 0.0;
    union { double d; int64_t i; } u; u.i = bits; return u.d;
}

// A comparison where an array hides behind one of the tags: it answers a
// truth value per element, so the caller needs the kind of the answer too.
// Anything else falls through to the three-way compare above, which the
// caller turns into a single truth value.
int64_t jdb_dyn_cmp_arr(int64_t a, int32_t ta, int64_t b, int32_t tb,
                        int32_t op, int32_t* out_tag) {
    bool a_arr = (ta == JD_TAG_ARR), b_arr = (tb == JD_TAG_ARR);
    if (a_arr || b_arr) {
        if (out_tag) *out_tag = JD_TAG_ARR;
        JdbArray* out = nullptr;
        if (a_arr && b_arr)
            out = jdb_array_cmp_arr((JdbArray*)(intptr_t)a, (JdbArray*)(intptr_t)b, op);
        else if (a_arr && tb == JD_TAG_STR)
            out = jdb_array_cmp_scalar_str((JdbArray*)(intptr_t)a, (const char*)(intptr_t)b, op);
        else if (b_arr && ta == JD_TAG_STR)
            out = jdb_array_cmp_scalar_str((JdbArray*)(intptr_t)b, (const char*)(intptr_t)a,
                                           jdb_cmp_op_flipped(op));
        else if (a_arr)
            out = jdb_array_cmp_scalar((JdbArray*)(intptr_t)a, dyn_num(b, tb), op);
        else
            out = jdb_array_cmp_scalar((JdbArray*)(intptr_t)b, dyn_num(a, ta),
                                       jdb_cmp_op_flipped(op));
        return (int64_t)(intptr_t)out;
    }
    if (out_tag) *out_tag = JD_TAG_BOOL;
    return jdb_dyn_cmp(a, ta, b, tb);
}

// Arithmetic where both operands only name their kind when they run - two
// map values, two cells of a mixed array. An array on either side decides
// element-wise or array-with-a-scalar; a string answers what the
// interpreter's string operators answer (join, removal, repetition,
// slicing); anything else is a number. The caller reads the kind of the
// answer from out_tag.
int64_t jdb_dyn_arith(int64_t a, int32_t ta, int64_t b, int32_t tb,
                      int32_t op, int32_t* out_tag) {
    bool a_arr = (ta == JD_TAG_ARR), b_arr = (tb == JD_TAG_ARR);
    if (a_arr || b_arr) {
        if (out_tag) *out_tag = JD_TAG_ARR;
        JdbArray* out = nullptr;
        if (a_arr && b_arr)
            out = jdb_array_binop((JdbArray*)(intptr_t)a, (JdbArray*)(intptr_t)b, op);
        else if (a_arr && tb == JD_TAG_STR && op == 0)
            out = jdb_array_str_concat((JdbArray*)(intptr_t)a, (const char*)(intptr_t)b, 0);
        else if (b_arr && ta == JD_TAG_STR && op == 0)
            out = jdb_array_str_concat((JdbArray*)(intptr_t)b, (const char*)(intptr_t)a, 1);
        else if (a_arr)
            out = jdb_array_scalar_op((JdbArray*)(intptr_t)a, dyn_num(b, tb), op, 0);
        else
            out = jdb_array_scalar_op((JdbArray*)(intptr_t)b, dyn_num(a, ta), op, 1);
        return (int64_t)(intptr_t)out;
    }
    if (ta == JD_TAG_STR || tb == JD_TAG_STR) {
        bool sa = ta == JD_TAG_STR, sb = tb == JD_TAG_STR;
        auto text_of = [](int64_t bits, int32_t t) -> std::string {
            if (t == JD_TAG_STR) {
                jdb_text::View v = text_in((const char*)(intptr_t)bits);
                return std::string(v.p ? v.p : "", v.n);
            }
            return cell_text(dyn_num(bits, t), JD_TAG_F64);
        };
        std::string out;
        try {
            if (op == 1 && sa && sb)
                out = jdb_text::remove_all(text_in((const char*)(intptr_t)a),
                                           text_in((const char*)(intptr_t)b));
            else if (op == 2)
                out = sa ? jdb_text::repeat(text_in((const char*)(intptr_t)a),
                                            sb ? 0 : (int64_t)dyn_num(b, tb))
                         : jdb_text::repeat(text_in((const char*)(intptr_t)b), (int64_t)dyn_num(a, ta));
            else if (op == 3 && sa != sb)
                out = sa ? jdb_text::slice(text_in((const char*)(intptr_t)a), (int64_t)dyn_num(b, tb), false)
                         : jdb_text::slice(text_in((const char*)(intptr_t)b), (int64_t)dyn_num(a, ta), true);
            else if (op == 3)
                out = text_of(a, ta);
            else
                out = text_of(a, ta) + text_of(b, tb);
        } catch (...) {
            jdb_err_set("out of memory for a string this size", 1);
            out.clear();
        }
        if (out_tag) *out_tag = JD_TAG_STR;
        return (int64_t)(intptr_t)text_out(out);
    }
    // Two integers stay an integer under + - *, as in the interpreter, until
    // the result leaves the INT64 range.
    if (ta == JD_TAG_I64 && tb == JD_TAG_I64 && op >= 0 && op <= 2) {
        uint64_t ua = (uint64_t)a, ub = (uint64_t)b;
        int64_t r = (int64_t)(op == 0 ? ua + ub : op == 1 ? ua - ub : ua * ub);
        bool ovf;
        if (op == 0)      ovf = ((a ^ r) & (b ^ r)) < 0;
        else if (op == 1) ovf = ((a ^ b) & (a ^ r)) < 0;
        else              ovf = (a == -1 && b == INT64_MIN) || (a != 0 && r / a != b);
        if (!ovf) {
            if (out_tag) *out_tag = JD_TAG_I64;
            return r;
        }
    }
    if (out_tag) *out_tag = JD_TAG_F64;
    double x = dyn_num(a, ta), y = dyn_num(b, tb), r = 0.0;
    switch (op) {
        case 0: r = x + y; break;
        case 1: r = x - y; break;
        case 2: r = x * y; break;
        case 3:
            if (y == 0.0) { jdb_err_set("Division by zero", 12); r = 0.0; }
            else r = x / y;
            break;
        default: r = 0.0; break;
    }
    union { double d; int64_t i; } u; u.d = r; return u.i;
}

// The truth of a value whose kind is known only now, as the interpreter
// reads it: a string is true when it is not empty, an array when every cell
// is, a map never, a number when it is not 0.
int64_t jdb_dyn_truth(int64_t bits, int32_t tag) {
    if (tag == JD_TAG_STR) {
        const char* s = (const char*)(intptr_t)bits;
        return s && text_in(s).n > 0;
    }
    if (tag == JD_TAG_ARR) {
        JdbArray* a = (JdbArray*)(intptr_t)bits;
        return a && a->length > 0 && jdb_array_all(a);
    }
    if (tag == JD_TAG_NATIVE_MAP || tag == JD_TAG_NONE) return 0;
    return dyn_num(bits, tag) != 0.0;
}

int64_t jdb_str_cmp(const char* a, const char* b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return jdb_text::compare(text_in(a), text_in(b));
}

char* jdb_ltrim(const char* s) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::ltrim(text_in(s)));
    }, [] { return _strdup(""); });
}

char* jdb_rtrim(const char* s) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::rtrim(text_in(s)));
    }, [] { return _strdup(""); });
}

int64_t jdb_startswith(const char* s, const char* prefix) {
    if (!s || !prefix) return 0;
    return jdb_text::starts_with(text_in(s), text_in(prefix)) ? 1 : 0;
}

int64_t jdb_endswith(const char* s, const char* suffix) {
    if (!s || !suffix) return 0;
    return jdb_text::ends_with(text_in(s), text_in(suffix)) ? 1 : 0;
}

char* jdb_hex(int64_t val) {
    char buf[32];
    jdb_num::format_hex(buf, sizeof(buf), val);
    return _strdup(buf);
}

char* jdb_bin(int64_t val) {
    char buf[72];
    jdb_num::format_bin(buf, sizeof(buf), val);
    return _strdup(buf);
}

char* jdb_oct(int64_t val) {
    char buf[32];
    jdb_num::format_oct(buf, sizeof(buf), val);
    return _strdup(buf);
}

char* jdb_join_arr(JdbArray* arr, const char* delim) {
    return jdb_guard([&]() -> char* {
        if (!arr || arr->length == 0) return _strdup("");
        try {
            jdb_text::View d = text_in(delim);
            std::string out;
            char num[512];
            for (int64_t i = 0; i < arr->length; i++) {
                if (i > 0 && d.n) out.append(d.p, d.n);
                int k = cell_kind(arr, i);
                if (k == JD_TAG_STR) {
                    jdb_text::View s = text_in(decode_str(arr->data[i]));
                    if (s.n) out.append(s.p, s.n);
                } else if (k == JD_TAG_BOOL || (k == JD_TAG_F64 && (arr->flags & 4) && !arr_tagged(arr))) {
                    out += arr->data[i] != 0.0 ? "TRUE" : "FALSE";
                } else if (k == JD_TAG_ARR) {
                    char* sub = jdb_frmv(decode_inner(arr->data[i]));
                    jdb_text::View s = text_in(sub);
                    if (s.n) out.append(s.p, s.n);
                    free(sub);
                } else {
                    jdb_format_double(num, (int)sizeof num, arr->data[i]);
                    out += num;
                }
            }
            return text_out(out);
        } catch (...) {
            jdb_err_set("JOIN: out of memory for a text this size", 1);
            return _strdup("");
        }
    }, [] { return _strdup(""); });
}

// SPLIT answers an array of strings, one per part between the delimiters.
JdbArray* jdb_split(const char* src, const char* delim) {
    return jdb_guard([&]() -> JdbArray* {
        if (!src || !delim) return jdb_array_new(0);
        std::vector<std::string> parts;
        jdb_text::split(text_in(src), text_in(delim), [&](std::string p) { parts.push_back(std::move(p)); });
        auto* arr = jdb_array_new((int64_t)parts.size());
        for (size_t i = 0; i < parts.size(); i++) {
            intptr_t ptr = (intptr_t)text_out(parts[i]);
            memcpy(&arr->data[i], &ptr, sizeof(double));
        }
        arr->flags |= 3;  // bit 0 (ptr) + bit 1 (string elems): PRINT + INDEX dispatch
        return arr;
    }, [] { return jdb_array_new(0); });
}

char* jdb_insert_str(const char* target, const char* insert, int64_t pos) {
    return jdb_guard([&]() -> char* {
        return jdb_guard([&]() -> char* {
            if (!target) return _strdup(insert ? insert : "");
            if (!insert) return _strdup(target);
            return text_out(jdb_text::insert(text_in(target), text_in(insert), pos));
        }, [] { return _strdup(""); });
    }, [] { return _strdup(""); });
}

// ── File I/O ────────────────────────────────────────────────

// A directory opens and reads as nothing where fopen accepts one, so the
// readers below ask instead of answering an empty file.
static bool jdb_path_is_directory(const char* path) { return path && jdb_path::is_dir(path); }

char* jdb_txtreader(const char* path) {
    FILE* f = path ? fopen(path, "rb") : nullptr;
    if (!f || jdb_path_is_directory(path)) {
        if (f) fclose(f);
        // A path that cannot be opened - a directory, a missing file - is an
        // error, the way the interpreter has it. Answering an empty string
        // made a typo look like an empty file.
        std::string msg = "TXTREADER$: Cannot open file: ";
        msg += path ? path : "";
        jdb_err_set(msg.c_str(), 99);
        return _strdup("");
    }
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
        return text_out(out);
    } catch (...) {
        return text_out(raw);
    }
}

// Writes the text with its full byte length; a file that cannot be opened
// is an error, as in the interpreter.
static void txt_write(const char* path, const char* content, const char* mode) {
    FILE* f = path ? fopen(path, mode) : nullptr;
    if (!f) {
        std::string msg = "Cannot write file: ";
        msg += path ? path : "";
        jdb_err_set(msg.c_str(), 1);
        return;
    }
    jdb_text::View s = text_in(content);
    if (s.n) fwrite(s.p, 1, s.n, f);
    fclose(f);
}

void jdb_txtwriter(const char* path, const char* content) {
    jdb_guard_void([&] { txt_write(path, content, "wb"); });
}

void jdb_txtwriter_append(const char* path, const char* content) {
    jdb_guard_void([&] { txt_write(path, content, "ab"); });
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
    FILE* f = path ? fopen(path, "rb") : nullptr;
    if (!f || jdb_path_is_directory(path)) {
        if (f) fclose(f);
        // A path that cannot be opened - a directory, a missing file - is an
        // error, the way the interpreter has it. Answering an empty string
        // made a typo look like an empty file.
        std::string msg = "TXTREADER$: Cannot open file: ";
        msg += path ? path : "";
        jdb_err_set(msg.c_str(), 99);
        return _strdup("");
    }
    // Stream to EOF (see jdb_txtreader) so /proc and /sys files read fully.
    std::string raw;
    char buf[65536];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) raw.append(buf, got);
    fclose(f);
    std::string enc = encoding ? encoding : "";
    try {
        std::string out = jdb_enc::decode_to_utf8(raw, enc);
        return text_out(out);
    } catch (...) {
        // Fall back to pass-through on conversion error so the script can
        // recover; the (corrupt-on-bad-bytes) data is still readable.
        return text_out(raw);
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
    DWORD n = GetCurrentDirectoryA(sizeof(buf), buf);
    if (n == 0 || n >= sizeof(buf)) buf[0] = 0;
#else
    if (!getcwd(buf, sizeof(buf))) buf[0] = 0;
#endif
    return _strdup(buf);
}

// Return the resulting current directory as a fresh string. The parser
// wraps `CD "..."` in a PRINT to show the path; matching that contract.
char* jdb_cd(const char* path) {
    // No argument answers the current directory; an argument that is empty is
    // a path the caller worked out wrongly, and answering the current
    // directory for it hides that. The interpreter raises, so this does too.
    if (path && !*path) {
        jdb_err_set("CD: empty path", 1);
        return _strdup("");
    }
#ifdef _WIN32
    if (path && *path) {
        if (!SetCurrentDirectoryA(path)) {
            jdb_err_set("CD: cannot change directory", 1);
        }
    }
    char buf[MAX_PATH];
    DWORD n = GetCurrentDirectoryA(MAX_PATH, buf);
    return _strdup(n > 0 && n < MAX_PATH ? buf : "");
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

// Creates every missing parent too, like mkdir -p.
void jdb_mkdir_native(const char* path) {
    if (!path) path = "";
    std::string full(path), part;
    for (size_t i = 0; i <= full.size(); i++) {
        bool sep = i == full.size() || full[i] == '/' || full[i] == '\\';
        if (sep && !part.empty() && !(part.size() == 2 && part[1] == ':')) {
#ifdef _WIN32
            CreateDirectoryA(part.c_str(), NULL);
#else
            mkdir(part.c_str(), 0755);
#endif
        }
        if (i < full.size()) part += full[i];
    }
    struct stat st;
    if (stat(full.c_str(), &st) != 0 || !(st.st_mode & S_IFDIR)) {
        std::string msg = "MKDIR: Cannot create directory: " + full;
        jdb_err_set(msg.c_str(), 1);
    }
}

void jdb_kill(const char* path) {
    remove(path);
}

// ── Date/Time ───────────────────────────────────────────────
// A native date is an ISO timestamp in local time, or epoch seconds where
// it came through the VM bridge; the calendar itself is jdb_cal.

static char* iso_dup(const jdb_cal::Civil& c, bool with_time) {
    char buf[32];
    jdb_cal::format_iso(buf, sizeof(buf), c, with_time);
    return _strdup(buf);
}

double jdb_now_epoch() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// NOW as the ISO timestamp CVDATE answers.
char* jdb_now() {
    return iso_dup(jdb_cal::civil_local(jdb_now_epoch()), true);
}

char* jdb_date_str(double epoch) {
    return iso_dup(jdb_cal::civil_local(epoch), false);
}

char* jdb_time_str(double epoch) {
    char buf[32];
    jdb_cal::format_time(buf, sizeof(buf), jdb_cal::civil_local(epoch));
    return _strdup(buf);
}

// The parts of an instant's local wall clock.
int64_t jdb_year(double epoch)    { return jdb_cal::civil_local(epoch).y; }
int64_t jdb_month(double epoch)   { return jdb_cal::civil_local(epoch).mo; }
int64_t jdb_day(double epoch)     { return jdb_cal::civil_local(epoch).d; }
int64_t jdb_hour(double epoch)    { return jdb_cal::civil_local(epoch).h; }
int64_t jdb_minute(double epoch)  { return jdb_cal::civil_local(epoch).mi; }
int64_t jdb_second(double epoch)  { return jdb_cal::civil_local(epoch).se; }
int64_t jdb_weekday(double epoch) { return jdb_cal::civil_local(epoch).wd; }

// The parts of an ISO timestamp as written; all 0 for text that is none.
static jdb_cal::Civil iso_parts(const char* s) {
    jdb_cal::Civil c;
    if (!jdb_cal::parse_iso(s, c)) c = jdb_cal::Civil{0, 0, 0, 0, 0, 0, 0};
    return c;
}
int64_t jdb_year_str(const char* s)   { return iso_parts(s).y; }
int64_t jdb_month_str(const char* s)  { return iso_parts(s).mo; }
int64_t jdb_day_str(const char* s)    { return iso_parts(s).d; }
int64_t jdb_hour_str(const char* s)   { return iso_parts(s).h; }
int64_t jdb_minute_str(const char* s) { return iso_parts(s).mi; }
int64_t jdb_second_str(const char* s) { return iso_parts(s).se; }

// FORMAT_DATE of an ISO timestamp read as a local wall clock, shown in the
// machine's zone, or in the zone tz_hours east of UTC when tz_hours is a
// number. A null or empty format is the standard timestamp.
char* jdb_format_date(const char* date_str, const char* fmt, double tz_hours) {
    jdb_cal::Civil c;
    if (!jdb_cal::parse_iso(date_str, c)) return _strdup("");
    char buf[256];
    if (jdb_cal::format_at(buf, sizeof(buf), fmt, jdb_cal::local_to_epoch(c), tz_hours == tz_hours, tz_hours) < 0)
        jdb_err_set("FORMAT_DATE: the format has an unknown conversion, or the year cannot be shown", 24);
    return _strdup(buf);
}

// FORMAT_DATE of an epoch rather than a timestamp: a date that came
// through the bridge inside an array is a number on this side.
char* jdb_format_date_num(double epoch, const char* fmt, double tz_hours) {
    // A date that was declared and never set has no text.
    if (epoch != epoch) return _strdup("");
    char buf[256];
    if (jdb_cal::format_at(buf, sizeof(buf), fmt, epoch, tz_hours == tz_hours, tz_hours) < 0)
        jdb_err_set("FORMAT_DATE: the format has an unknown conversion, or the year cannot be shown", 24);
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
int64_t jdb_file_exists(const char* path) { return path && jdb_path::exists(path) ? 1 : 0; }
int64_t jdb_file_size(const char* path) { return path ? jdb_path::size(path) : -1; }
int64_t jdb_file_isdir(const char* path) { return path && jdb_path::is_dir(path) ? 1 : 0; }

// ── Path helpers ────────────────────────────────────────────
char* jdb_path_dirname(const char* p) { return p ? text_out(jdb_path::dirname(p)) : _strdup(""); }
char* jdb_path_normalize(const char* p) { return p ? text_out(jdb_path::normalize(p)) : _strdup(""); }

// ── Bit rotation ────────────────────────────────────────────
// A width other than 8, 16, 32 or 64 rotates within 64 bits.
int64_t jdb_rotl(int64_t x, int64_t n, int64_t bits) {
    int w = jdb_num::rotation_width_ok((int)bits) ? (int)bits : 64;
    return (int64_t)jdb_num::rotl((uint64_t)x, n, w);
}
int64_t jdb_rotr(int64_t x, int64_t n, int64_t bits) {
    int w = jdb_num::rotation_width_ok((int)bits) ? (int)bits : 64;
    return (int64_t)jdb_num::rotr((uint64_t)x, n, w);
}
// 2-arg forms used by native codegen (implicit 64-bit width).
int64_t jdb_rotl2(int64_t x, int64_t n) { return jdb_rotl(x, n, 64); }
int64_t jdb_rotr2(int64_t x, int64_t n) { return jdb_rotr(x, n, 64); }

// ── Math (GCD/LCM) - binary; variadic is expanded by codegen ─
int64_t jdb_gcd(int64_t a, int64_t b) { return jdb_num::gcd(a, b); }
int64_t jdb_lcm(int64_t a, int64_t b) { return jdb_num::lcm(a, b); }

// ── String padding ──────────────────────────────────────────
char* jdb_lpad(const char* s, int64_t n, const char* pad) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::lpad(text_in(s), n, text_in(pad)));
    }, [] { return _strdup(""); });
}
char* jdb_rpad(const char* s, int64_t n, const char* pad) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_text::rpad(text_in(s), n, text_in(pad)));
    }, [] { return _strdup(""); });
}
// 2-arg forms used by native codegen (default pad = " ").
char* jdb_lpad2(const char* s, int64_t n) { return jdb_lpad(s, n, " "); }
char* jdb_rpad2(const char* s, int64_t n) { return jdb_rpad(s, n, " "); }

// IIF (inline if): returns a or b based on condition
double jdb_iif(int64_t cond, double a, double b) {
    return cond ? a : b;
}

// IIF over an array condition picks cell by cell, into nested rows as well;
// a branch that is an array gives its own cell (its rows for a nested
// condition), a number is the same in every cell. The branches arrive as
// bits with their kind, since an array is a pointer punned into the double.
JdbArray* jdb_array_iif(JdbArray* cond, double a, int32_t a_tag, double b, int32_t b_tag) {
    if (!cond) return jdb_array_new(0);
    auto branch = [](double v, int32_t tag) -> jdb_shape::Branch<RtShape> {
        if (tag == JD_TAG_ARR) return { true, decode_inner(v), 0.0 };
        return { false, nullptr, v };
    };
    return jdb_shape::pick<RtShape>(cond, branch(a, a_tag), branch(b, b_tag),
        [](const double& c) { return c != 0.0; });
}

// ISNUM, ISSTR, ISARR - type checking (simplified for native)
int64_t jdb_isnum(double val) { (void)val; return 1; }
int64_t jdb_isstr(const char* val) { (void)val; return 1; }

// ── Codec ───────────────────────────────────────────────────

char* jdb_base64_encode(const char* input) {
    jdb_text::View s = text_in(input);
    try {
        return text_out(jdb_crypto::base64_encode(s.p, s.n));
    } catch (...) {
        jdb_err_set("CODEC.BASE64_ENCODE$: out of memory", 1);
        return _strdup("");
    }
}

char* jdb_base64_decode(const char* input) {
    jdb_text::View s = text_in(input);
    try {
        return text_out(jdb_crypto::base64_decode(s.p, s.n));
    } catch (...) {
        jdb_err_set("CODEC.BASE64_DECODE$: out of memory", 1);
        return _strdup("");
    }
}

char* jdb_uuid() {
    uint8_t bytes[16];
    if (!jdb_crypto::random_bytes(bytes, sizeof(bytes))) {
        jdb_err_set("CODEC.UUID$: the system random source failed", 5);
        return _strdup("");
    }
    char buf[37];
    jdb_crypto::uuid4_format(bytes, buf);
    return _strdup(buf);
}

char* jdb_randombytes(int64_t n) {
    if (n < 0 || n > jdb_crypto::RANDOM_BYTES_MAX) {
        jdb_err_set("CODEC.RANDOMBYTES$: count must be between 0 and 1048576", 5);
        return _strdup("");
    }
    char* out = (char*)malloc((size_t)n + 1);
    if (!jdb_crypto::random_bytes((uint8_t*)out, (size_t)n)) {
        free(out);
        jdb_err_set("CODEC.RANDOMBYTES$: the system random source failed", 5);
        return _strdup("");
    }
    out[n] = '\0';
    if ((size_t)n != strlen(out)) jdrt_register_binary(out, n);
    return out;
}

// ── Date Helpers ────────────────────────────────────────────
// The text CDATE accepts, YYYY-MM-DD or DD.MM.YYYY with an optional time
// after a space or a T; raises CDATE's error for anything else.
static bool rt_parse_cdate_text(const char* datestr, jdb_cal::Civil& c) {
    if (jdb_cal::parse_text(datestr ? datestr : "", c)) return true;
    std::string msg = std::string("CDATE: \"") + (datestr ? datestr : "") +
        "\" is not a date; write YYYY-MM-DD or DD.MM.YYYY, with HH:MM:SS after a space if needed";
    jdb_err_set(msg.c_str(), 99);
    return false;
}

// CVDATE: the text as a normalized ISO timestamp.
char* jdb_cvdate(const char* datestr) {
    jdb_cal::Civil c;
    if (!rt_parse_cdate_text(datestr, c)) return _strdup("");
    return iso_dup(c, true);
}

// The unit letter of DATEADD or DATEDIFF; 0 after raising an error when
// part is not one of the letters in allowed.
static char rt_date_unit(const char* part, const char* allowed, const char* fn, const char* hint) {
    char p = jdb_cal::unit(part, allowed);
    if (p) return p;
    char msg[200];
    snprintf(msg, sizeof(msg), "%s: unknown unit \"%.40s\"; use %s", fn, part ? part : "", hint);
    jdb_err_set(msg, 1);
    return 0;
}

// ── Dates as epoch seconds (JD_TAG_DATE) ────────────────────
// The same operations over the representation the VM uses: seconds since
// 1970-01-01 UTC.

// CDATE text to epoch seconds; 0 after raising the error, as the string
// form answers "" after raising it.
double jdb_cvdate_to_num(const char* datestr) {
    jdb_cal::Civil c;
    if (!rt_parse_cdate_text(datestr, c)) return 0.0;
    return jdb_cal::local_to_epoch(c);
}

double jdb_dateadd_num(const char* part, double amount, double epoch) {
    char p = rt_date_unit(part, "YMWDHNS", "DATEADD", "Y, M, W, D, H, N or S");
    return p ? jdb_cal::date_add(p, amount, epoch) : epoch;
}

double jdb_datediff_num(const char* part, double epoch1, double epoch2) {
    char p = rt_date_unit(part, "DHNS", "DATEDIFF", "D, H, N or S");
    return p ? jdb_cal::date_diff(p, epoch1, epoch2) : 0;
}

// CVDATE of a number: epoch seconds as a local ISO timestamp.
char* jdb_cvdate_num(double epoch_secs) {
    return iso_dup(jdb_cal::civil_local(epoch_secs), true);
}

// CVDATE on an array - element-wise. Numeric elements use jdb_cvdate_num,
// existing string elements pass through jdb_cvdate. Returns a string-flagged
// JdbArray so existing print / index paths Just Work.
JdbArray* jdb_cvdate_arr(JdbArray* in) {
    if (!in) return jdb_array_new(0);
    auto* out = jdb_array_new(in->length);
    out->flags |= 2;  // marks element storage as string ptrs
    for (int64_t i = 0; i < in->length; i++) {
        char* s;
        if (cell_is_text(in, i)) {
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

// DATEADD over an ISO timestamp, answering one.
char* jdb_dateadd(const char* part, double amount, const char* date_str) {
    char p = rt_date_unit(part, "YMWDHNS", "DATEADD", "Y, M, W, D, H, N or S");
    jdb_cal::Civil c;
    if (!p || !jdb_cal::parse_iso(date_str, c)) return _strdup("");
    double epoch = jdb_cal::date_add(p, amount, jdb_cal::local_to_epoch(c));
    return iso_dup(jdb_cal::civil_local(epoch), true);
}

// DATEDIFF between two ISO timestamps.
double jdb_datediff(const char* part, const char* date1, const char* date2) {
    char p = rt_date_unit(part, "DHNS", "DATEDIFF", "D, H, N or S");
    jdb_cal::Civil c1, c2;
    if (!p || !jdb_cal::parse_iso(date1, c1) || !jdb_cal::parse_iso(date2, c2)) return 0;
    return jdb_cal::date_diff(p, jdb_cal::local_to_epoch(c1), jdb_cal::local_to_epoch(c2));
}

// Vectorised FORMAT_DATE: one format over an array of dates. Cells are ISO
// strings when the array carries them, epoch seconds when it came over the
// VM bridge. Answers a string-flagged array, as jdb_cvdate_arr does.
JdbArray* jdb_format_date_vec(JdbArray* dates, const char* fmt, double tz_hours) {
    if (!dates) return jdb_array_new(0);
    bool tagged = (dates->flags & 8) && dates->elem_tags;
    bool all_str = (dates->flags & 2) != 0;
    auto* r = jdb_array_new(dates->length);
    r->flags |= 2;
    for (int64_t i = 0; i < dates->length; i++) {
        char* out;
        if (tagged ? (dates->elem_tags[i] == JD_TAG_STR) : all_str) {
            union { double d; int64_t i; } u; u.d = dates->data[i];
            out = jdb_format_date((const char*)(intptr_t)u.i, fmt, tz_hours);
        } else {
            out = jdb_format_date_num(dates->data[i], fmt, tz_hours);
        }
        union { double d; int64_t i; } u; u.i = (int64_t)(intptr_t)out;
        r->data[i] = u.d;
    }
    return r;
}

// Vectorized DATEDIFF: scalar start, array of end-dates. Cells are ISO
// strings when the array carries them, epoch seconds when it came over the
// VM bridge (DATERANGE), which stores a date as its number.
JdbArray* jdb_datediff_vec(const char* part, const char* date1, JdbArray* dates) {
    return jdb_guard([&]() -> JdbArray* {
        if (!dates) return jdb_array_new(0);
        bool tagged = (dates->flags & 8) && dates->elem_tags;
        bool all_str = (dates->flags & 2) != 0;
        auto* r = jdb_array_new(dates->length);
        for (int64_t i = 0; i < dates->length; i++) {
            bool is_str = tagged ? (dates->elem_tags[i] == JD_TAG_STR) : all_str;
            if (is_str) {
                union { double d; int64_t i; } u; u.d = dates->data[i];
                const char* date2 = (const char*)(intptr_t)u.i;
                r->data[i] = jdb_datediff(part, date1, date2);
            } else {
                char* date2 = jdb_cvdate_num(dates->data[i]);
                r->data[i] = jdb_datediff(part, date1, date2);
                free(date2);
            }
        }
        return r;
    }, [] { return jdb_array_new(0); });
}

} // end extern "C" - regex functions need C++ linkage internally

// ── Regex (C++ internally, extern "C" interface) ────────────

// VM convention is REGEX.MATCH(pattern, text) / REPLACE(pattern, text, repl)
// - see vm.cpp's register_native impls and doc/languages.md. Native must
// match that order or the same script picks one branch in interp and the
// opposite in native (e.g. native_test.jdb's REGEX section flipping
// PASS/FAIL between modes).
// Every match as a string; with capture groups, one row of group strings
// per match instead.
static JdbArray* regex_findall_impl(const char* pattern, const char* text) {
    std::vector<std::vector<std::string>> rows;
    bool has_groups = false;
    jdb_regex::find_all(pattern ? pattern : "", text ? text : "", rows, has_groups);
    auto put_str = [](JdbArray* a, int64_t i, const std::string& v) {
        char* copy = (char*)malloc(v.size() + 1);
        memcpy(copy, v.data(), v.size());
        copy[v.size()] = '\0';
        if (v.size() != strlen(copy)) jdrt_register_binary(copy, (int64_t)v.size());
        union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)copy;
        a->data[i] = u.d;
    };
    auto* arr = jdb_array_new((int64_t)rows.size());
    for (size_t i = 0; i < rows.size(); i++) {
        if (has_groups) {
            auto* inner = jdb_array_new((int64_t)rows[i].size());
            for (size_t g = 0; g < rows[i].size(); g++) put_str(inner, (int64_t)g, rows[i][g]);
            inner->flags |= 3;
            union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)inner;
            arr->data[i] = u.d;
        } else {
            put_str(arr, (int64_t)i, rows[i][0]);
        }
    }
    if (rows.size()) arr->flags |= has_groups ? 1 : 3;
    return arr;
}

extern "C" {
int64_t jdb_regex_match(const char* pattern, const char* text) {
    return jdb_guard([&]() -> int64_t {
        return jdb_regex::search(pattern ? pattern : "", text ? text : "") ? 1 : 0;
    }, [] { return 0; });
}
char* jdb_regex_replace(const char* pattern, const char* text, const char* replacement) {
    return jdb_guard([&]() -> char* {
        return text_out(jdb_regex::replace(pattern ? pattern : "", text ? text : "", replacement ? replacement : ""));
    }, [] { return _strdup(""); });
}
// The legacy REGEX_REPLACE$ takes the text first.
char* jdb_regex_replace_text_first(const char* text, const char* pattern, const char* replacement) {
    return jdb_guard([&]() -> char* {
        return jdb_regex_replace(pattern, text, replacement);
    }, [] { return _strdup(""); });
}
JdbArray* jdb_regex_findall(const char* pattern, const char* text) {
    return jdb_guard([&] { return regex_findall_impl(pattern, text); }, [] { return jdb_array_new(0); });
}

// ── SHA-256 ─────────────────────────────────────────────────
// Minimal self-contained SHA-256 implementation

static uint32_t sha_rotr(uint32_t x, int n) { return (x >> n) | (x << (32-n)); }
static uint32_t sha_ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static uint32_t sha_maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static uint32_t sha_sig0(uint32_t x) { return sha_rotr(x,2)^sha_rotr(x,13)^sha_rotr(x,22); }
static uint32_t sha_sig1(uint32_t x) { return sha_rotr(x,6)^sha_rotr(x,11)^sha_rotr(x,25); }
static uint32_t sha_gam0(uint32_t x) { return sha_rotr(x,7)^sha_rotr(x,18)^(x>>3); }
static uint32_t sha_gam1(uint32_t x) { return sha_rotr(x,17)^sha_rotr(x,19)^(x>>10); }

static void sha256_digest(const uint8_t* input, size_t len, uint8_t out[32]) {
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

    // Pad message
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    uint8_t* msg = (uint8_t*)calloc(padded_len, 1);
    if (input && len) memcpy(msg, input, len);
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

    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)(h[i]);
    }
}

static char* hex_of(const uint8_t* b, size_t n) {
    static const char* d = "0123456789abcdef";
    char* out = (char*)malloc(n * 2 + 1);
    for (size_t i = 0; i < n; i++) { out[i*2] = d[b[i] >> 4]; out[i*2+1] = d[b[i] & 0x0F]; }
    out[n*2] = 0;
    return out;
}

char* jdb_sha256(const char* input) {
    uint8_t digest[32];
    sha256_digest((const uint8_t*)input, (size_t)jdb_str_blen(input), digest);
    return hex_of(digest, 32);
}

// HMAC per RFC 2104. algo is optional at the call site, so it arrives null
// for a two-argument call; SHA256 is the only algorithm.
char* jdb_hmac_sha256(const char* key, const char* message, const char* algo) {
    if (algo && *algo) {
        std::string want(algo);
        for (auto& ch : want) ch = (char)toupper((unsigned char)ch);
        if (want != "SHA256" && want != "SHA-256") {
            jdb_err_set(("CODEC.HMAC$: unsupported algorithm '" + want + "'").c_str(), 5);
            return _strdup("");
        }
    }
    const size_t BLOCK = 64;
    size_t key_len = (size_t)jdb_str_blen(key);
    size_t msg_len = (size_t)jdb_str_blen(message);

    uint8_t k[BLOCK];
    memset(k, 0, BLOCK);
    if (key_len > BLOCK) {
        sha256_digest((const uint8_t*)key, key_len, k);
    } else if (key_len) {
        memcpy(k, key, key_len);
    }

    uint8_t* inner = (uint8_t*)malloc(BLOCK + msg_len);
    for (size_t i = 0; i < BLOCK; i++) inner[i] = k[i] ^ 0x36;
    if (msg_len) memcpy(inner + BLOCK, message, msg_len);
    uint8_t inner_digest[32];
    sha256_digest(inner, BLOCK + msg_len, inner_digest);
    free(inner);

    uint8_t outer[BLOCK + 32];
    for (size_t i = 0; i < BLOCK; i++) outer[i] = k[i] ^ 0x5c;
    memcpy(outer + BLOCK, inner_digest, 32);
    uint8_t out_digest[32];
    sha256_digest(outer, sizeof(outer), out_digest);
    return hex_of(out_digest, 32);
}

char* jdb_pbkdf2(const char* password, const char* salt, int64_t iterations, int64_t n_bytes) {
    if (iterations < 1) {
        jdb_err_set("CODEC.PBKDF2$: iterations must be at least 1", 5);
        return _strdup("");
    }
    if (n_bytes < 1 || n_bytes > jdb_crypto::PBKDF2_BYTES_MAX) {
        jdb_err_set("CODEC.PBKDF2$: bytes must be between 1 and 65536", 5);
        return _strdup("");
    }
    std::vector<uint8_t> key((size_t)n_bytes);
    jdb_crypto::pbkdf2_hmac_sha256((const uint8_t*)(password ? password : ""),
                                   (size_t)jdb_str_blen(password),
                                   (const uint8_t*)(salt ? salt : ""),
                                   (size_t)jdb_str_blen(salt),
                                   (uint64_t)iterations, key.data(), key.size());
    return hex_of(key.data(), key.size());
}

// TYPEOF answers with one of ten fixed words, so it hands out a shared buffer
// rather than a fresh copy. Nothing in the runtime frees or mutates a string it
// was given, and a TYPEOF in a hot loop otherwise allocates once per pass -
// which is most of what a compiled emulator loop loses to memory.
static char* interned_word(const char* w) {
    static std::mutex words_mx;
    static std::unordered_map<std::string, char*> words;
    std::lock_guard<std::mutex> lock(words_mx);
    auto it = words.find(w);
    if (it != words.end()) return it->second;
    char* copy = _strdup(w);
    words[w] = copy;
    return copy;
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
    if (v != v) return interned_word("NONE");
    return interned_word("FLOAT64");
}

// Both NATIVE_MAP and VM_HANDLE surface as "OBJECT" - user-facing type
// language doesn't distinguish native-runtime maps from VM Values.
char* jdb_typeof_tag(int64_t tag) {
    switch ((JdTag)tag) {
        case JdTag::I64:        return interned_word("INT64");
        case JdTag::F64:        return interned_word("FLOAT64");
        case JdTag::STR:        return interned_word("STRING");
        case JdTag::ARR:        return interned_word("ARRAY");
        case JdTag::NATIVE_MAP: return interned_word("OBJECT");
        case JdTag::FUNCREF:    return interned_word("FUNCREF");
        case JdTag::VM_HANDLE:  return interned_word("OBJECT");
        case JdTag::BOOL:       return interned_word("BOOLEAN");
        case JdTag::DATE:       return interned_word("DATE");
        case JdTag::NONE:       return interned_word("NONE");
        case JdTag::RUNTIME:
        default:                return interned_word("UNKNOWN");
    }
}

// ── FRMV$ (format array as string) ──────────────────────────

char* jdb_frmv(JdbArray* arr) {
    return jdb_guard([&]() -> char* {
        if (!arr || arr->length == 0) return _strdup("[]");
        bool has_tagged = (arr->flags & 8) != 0 && arr->elem_tags != nullptr;
        bool is_str = (arr->flags & 2) != 0;
        bool is_nested = (arr->flags & 1) != 0 && !is_str;
        bool is_bool = (arr->flags & 4) != 0;
        bool is_date = (arr->flags & 16) != 0;
        // A string cell is appended by its registered length, so a binary cell
        // (UNPACK "a", BINREADER$) keeps the bytes behind its first NUL.
        std::string out = "[";
        auto emit = [&](const char* s) {
            if (!s) return;
            int64_t n = jdrt_strlen(s);
            if (n < 0) n = (int64_t)strlen(s);
            out.append(s, (size_t)n);
        };
        for (int64_t i = 0; i < arr->length; i++) {
            if (i > 0) out += ", ";
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
                } else if (t == JD_TAG_NATIVE_MAP) {
                    char* ms = jdb_map_str((JdbMap*)(intptr_t)u.i);
                    emit(ms ? ms : "{}");
                    free(ms);
                } else if (t == JD_TAG_VM_HANDLE) {
                    const char* vs = g_jdrt_handle
                        ? jdrt_val_to_str(g_jdrt_handle, u.i) : nullptr;
                    emit(vs ? vs : "");
                    bridge_text_free(vs);
                } else if (t == 8) {  // BOOL
                    emit(arr->data[i] != 0.0 ? "TRUE" : "FALSE");
                } else {       // F64 / I64 / unknown
                    char num[512];
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
                    char num[512];
                    jdb_format_double(num, sizeof(num), arr->data[i]);
                    emit(num);
                }
            } else if (is_bool) {
                emit(arr->data[i] != 0.0 ? "TRUE" : "FALSE");
            } else if (is_date) {
                char* iso = jdb_cvdate_num(arr->data[i]);
                emit(iso ? iso : "");
                free(iso);
            } else {
                char num[512];
                jdb_format_double(num, sizeof(num), arr->data[i]);
                emit(num);
            }
        }
        out += "]";
        return text_out(out);
    }, [] { return _strdup(""); });
}

// FRMV$ as the language documents it: a 1-D array becomes one element per
// line, a 2-D array a right-aligned column matrix. jdb_frmv is the bracketed
// one-line form PRINT uses for an array, which is a different job.
char* jdb_frmv_matrix(JdbArray* arr) {
    return jdb_guard([&]() -> char* {
        if (!arr) return _strdup("");
        auto cell_text = [](JdbArray* a, int64_t i) -> std::string {
            int32_t t = (a->flags & 8) && a->elem_tags ? (int32_t)a->elem_tags[i]
                                                       : jdb_array_classify_elem(a, a->data[i]);
            union { double d; int64_t i; } u; u.d = a->data[i];
            if (t == JD_TAG_STR) {
                const char* s = (const char*)(intptr_t)u.i;
                return s ? std::string(s) : std::string();
            }
            if (t == JD_TAG_BOOL) return a->data[i] != 0.0 ? "TRUE" : "FALSE";
            if (t == JD_TAG_NATIVE_MAP) {
                char* ms = jdb_map_str((JdbMap*)(intptr_t)u.i);
                std::string out = ms ? ms : "{}";
                free(ms);
                return out;
            }
            if (t == JD_TAG_DATE) {
                char* iso = jdb_cvdate_num(a->data[i]);
                std::string out = iso ? iso : "";
                free(iso);
                return out;
            }
            char num[512];
            jdb_format_double(num, sizeof num, a->data[i]);
            return num;
        };
        bool is_2d = arr->length > 0 && cell_is_arr(arr, 0);
        std::string out;
        if (!is_2d) {
            for (int64_t i = 0; i < arr->length; i++) out += cell_text(arr, i) + "\n";
            return text_out(out);
        }
        std::vector<std::vector<std::string>> cells;
        std::vector<size_t> widths;
        for (int64_t r = 0; r < arr->length; r++) {
            union { double d; int64_t i; } u; u.d = arr->data[r];
            JdbArray* row = (JdbArray*)(intptr_t)u.i;
            std::vector<std::string> line;
            if (!cell_is_arr(arr, r)) line.push_back(cell_text(arr, r));
            else if (row)
                for (int64_t c = 0; c < row->length; c++) line.push_back(cell_text(row, c));
            if (widths.size() < line.size()) widths.resize(line.size(), 0);
            for (size_t c = 0; c < line.size(); c++)
                if (line[c].size() > widths[c]) widths[c] = line[c].size();
            cells.push_back(std::move(line));
        }
        for (auto& line : cells) {
            for (size_t c = 0; c < line.size(); c++) {
                if (widths[c] > line[c].size()) out.append(widths[c] - line[c].size(), ' ');
                out += line[c];
                if (c + 1 < line.size()) out += " ";
            }
            out += "\n";
        }
        return text_out(out);
    }, [] { return _strdup(""); });
}

// ── Misc ────────────────────────────────────────────────────

// The number CINT, CLNG, CDBL and CSNG read from a string: parsed whole,
// surrounding spaces allowed; ISO date text reads as its epoch seconds;
// other text raises a type mismatch.
double jdb_conv_str(const char* s, const char* fn) {
    const char* p = s ? s : "";
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p) {
        double d;
        if (jdb_num::parse_whole(p, d)) return d;
        jdb_cal::Civil c;
        if (jdb_cal::looks_iso(p) && jdb_cal::parse_iso(p, c)) return jdb_cal::local_to_epoch(c);
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "%s: \"%.200s\" is not a number", fn, s ? s : "");
    jdb_err_set(msg, 1);
    return 0.0;
}

double jdb_cdbl(double x) { return x; }
int64_t jdb_cint(double x) { return jdb_num::cint(x); }
int64_t jdb_clng(double x) { return jdb_num::clng(x); }
double jdb_csng(double x) { return jdb_num::csng(x); }
int64_t jdb_cbool(double x) { return x != 0.0 ? 1 : 0; }
char* jdb_tostr(double x) { return jdb_str(x); }
char* jdb_cstr(double x) { return jdb_str(x); }
double jdb_tonum(const char* s) { return jdb_num::parse_prefix(s); }

int64_t jdb_byteat(const char* s, int64_t idx) {
    if (!s || idx < 0) return 0;
    // No NUL up to idx: the byte is inside the string, whatever its length.
    // Only a NUL before it asks the binary registry (BINREADER$, PACK$),
    // whose strings carry zero bytes; scanning just this far keeps a loop
    // over a long string from reading all of it on every call.
    if (!memchr(s, 0, (size_t)idx + 1)) return (uint8_t)s[idx];
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
    // A member declared AS <user type> holds the instance itself, so two
    // objects can share one nested instance.
    std::unordered_map<std::string, JdbObject*> obj_fields;
    char* type_name;
};

extern "C" {

JdbObject* jdb_udt_new(const char* type_name) {
    return jdb_guard([&]() -> JdbObject* {
        auto* obj = new JdbObject();
        obj->type_name = _strdup(type_name ? type_name : "");
        return obj;
    }, [] { return nullptr; });
}

void jdb_udt_set_f64(JdbObject* obj, const char* field, double val) {
    jdb_guard_void([&] {
        if (obj && field) obj->num_fields[field] = val;
    });
}

double jdb_udt_get_f64(JdbObject* obj, const char* field) {
    if (obj && field) {
        auto it = obj->num_fields.find(field);
        if (it != obj->num_fields.end()) return it->second;
    }
    return 0.0;
}

static void udt_drop_text(char* s) {
    if (!s) return;
    if (rc_poison()) { if (*s) s[0] = '\x7f'; }
    else free(s);
}

// An instance the compiler proved nobody else holds: its own texts and the
// object go. A nested instance may be shared, so it stays.
void jdb_udt_free(JdbObject* obj) {
    if (!obj) return;
    for (auto& kv : obj->str_fields) udt_drop_text(kv.second);
    if (rc_poison()) {
        obj->num_fields.clear();
        return;
    }
    free(obj->type_name);
    delete obj;
}

void jdb_udt_set_str(JdbObject* obj, const char* field, const char* val) {
    jdb_guard_void([&] {
        if (!obj || !field) return;
        char* fresh = _strdup(val ? val : "");
        char*& slot = obj->str_fields[field];
        udt_drop_text(slot);
        slot = fresh;
    });
}

void jdb_udt_set_obj(JdbObject* obj, const char* field, JdbObject* val) {
    jdb_guard_void([&] {
        if (obj && field) obj->obj_fields[field] = val;
    });
}

JdbObject* jdb_udt_get_obj(JdbObject* obj, const char* field) {
    if (obj && field) {
        auto it = obj->obj_fields.find(field);
        if (it != obj->obj_fields.end()) return it->second;
    }
    return nullptr;
}

const char* jdb_udt_get_str(JdbObject* obj, const char* field) {
    if (obj && field) {
        if (strcmp(field, "__TYPE__") == 0) {
            // The type name in capitals (interpreter-compat), one copy per
            // type, borrowed like every other field read.
            std::string up = obj->type_name ? obj->type_name : "";
            for (auto& c : up) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
            return interned_word(up.c_str());
        }
        auto it = obj->str_fields.find(field);
        if (it != obj->str_fields.end()) return it->second;
    }
    return "";
}

// ── Lambda closures ─────────────────────────────────────────

// A LAMBDA USE(...) written inside a function keeps its own copy of the
// captured values, so two closures of the same lambda can hold different
// ones. The value handed around is a pointer to the closure; a call site
// resolves it to the lambda's function pointer and makes that copy readable
// for the duration of the call.
struct JdbClosure {
    uint64_t magic;
    void*    fn;
    int32_t  n;
    int64_t* bits;
    int32_t* tags;
};

static const uint64_t JDB_CLOSURE_MAGIC = 0x6a64436c6f737231ULL;

// A funcref is either a closure pointer or a plain function pointer. Code
// addresses never fall inside the heap span the closures were cut from, so
// the span plus the header word tells the two apart without dereferencing
// anything that might not be ours.
static std::atomic<uintptr_t> g_clo_lo{ (uintptr_t)-1 };
static std::atomic<uintptr_t> g_clo_hi{ 0 };
static thread_local std::vector<JdbClosure*> g_closure_stack;

void* jdb_closure_new(void* fn, int32_t n) {
    if (n < 0) n = 0;
    JdbClosure* c = (JdbClosure*)calloc(1, sizeof(JdbClosure));
    if (!c) return fn;
    c->magic = JDB_CLOSURE_MAGIC;
    c->fn = fn;
    c->n = n;
    if (n > 0) {
        c->bits = (int64_t*)calloc((size_t)n, sizeof(int64_t));
        c->tags = (int32_t*)calloc((size_t)n, sizeof(int32_t));
        if (!c->bits || !c->tags) { c->n = 0; }
    }
    uintptr_t a = (uintptr_t)c;
    uintptr_t lo = g_clo_lo.load(std::memory_order_relaxed);
    while (a < lo && !g_clo_lo.compare_exchange_weak(lo, a, std::memory_order_relaxed)) {}
    uintptr_t hi = g_clo_hi.load(std::memory_order_relaxed);
    while (a > hi && !g_clo_hi.compare_exchange_weak(hi, a, std::memory_order_relaxed)) {}
    return c;
}

void jdb_closure_set(void* h, int32_t i, int64_t bits, int32_t tag) {
    JdbClosure* c = (JdbClosure*)h;
    if (!c || c->magic != JDB_CLOSURE_MAGIC || i < 0 || i >= c->n) return;
    c->bits[i] = bits;
    c->tags[i] = tag;
}

// Answers the function pointer to call, and keeps the captured values of a
// closure reachable until the matching leave.
void* jdb_closure_enter(void* p) {
    JdbClosure* c = nullptr;
    uintptr_t a = (uintptr_t)p;
    if (p && a >= g_clo_lo.load(std::memory_order_relaxed) &&
             a <= g_clo_hi.load(std::memory_order_relaxed)) {
        JdbClosure* cand = (JdbClosure*)p;
        if (cand->magic == JDB_CLOSURE_MAGIC) c = cand;
    }
    g_closure_stack.push_back(c);
    return c ? c->fn : p;
}

void jdb_closure_leave(void) {
    if (!g_closure_stack.empty()) g_closure_stack.pop_back();
}

// A closure written straight into a call is held by nothing once the call
// returns. A plain function pointer is left alone.
void jdb_closure_free(void* p) {
    uintptr_t a = (uintptr_t)p;
    if (!p || a < g_clo_lo.load(std::memory_order_relaxed) ||
        a > g_clo_hi.load(std::memory_order_relaxed)) return;
    JdbClosure* c = (JdbClosure*)p;
    if (c->magic != JDB_CLOSURE_MAGIC) return;
    c->magic = 0;
    free(c->bits);
    free(c->tags);
    free(c);
}

// Capture i of the closure whose call is innermost. A lambda body only reads
// a slot its own closure filled; anything else answers zero rather than the
// captures of an unrelated call.
int64_t jdb_closure_get(int32_t i) {
    if (g_closure_stack.empty()) return 0;
    JdbClosure* c = g_closure_stack.back();
    if (!c || i < 0 || i >= c->n) return 0;
    return c->bits[i];
}

int32_t jdb_closure_get_tag(int32_t i) {
    if (g_closure_stack.empty()) return JD_TAG_F64;
    JdbClosure* c = g_closure_stack.back();
    if (!c || i < 0 || i >= c->n) return JD_TAG_F64;
    return c->tags[i];
}

// ── Higher-order Array Functions (with native function pointers) ─

// Function pointer type: double fn(double)
typedef double (*JdbMapFn)(double);
typedef double (*JdbReduceFn)(double, double);

// The element a higher-order loop hands to a lambda, as tag and bits, and
// the tag of the lambda's answer, valid only at the loop's own depth.
static thread_local int32_t g_hof_elem_tag = JD_TAG_F64;
static thread_local int64_t g_hof_elem_bits = 0;
static thread_local int32_t g_hof_depth = 0;
// One higher-order call open while it lives, also when a cell's work throws.
struct HofDepth {
    HofDepth() { g_hof_depth++; }
    ~HofDepth() { g_hof_depth--; }
};
static thread_local int32_t g_hof_ret_tag = -1;
static thread_local int32_t g_hof_ret_depth = -1;

// Publishes element i of arr for the lambda about to be called.
static void hof_publish(JdbArray* arr, int64_t i) {
    if (arr->flags & (1 | 8)) rc_cell_out(arr, i);
    int32_t t = (arr->flags & 8) && arr->elem_tags ? (int32_t)arr->elem_tags[i]
                                                   : (int32_t)jdb_tag_from_flags(arr->flags, arr->data[i]);
    union { double d; int64_t i; } u; u.d = arr->data[i];
    g_hof_elem_tag = t;
    g_hof_elem_bits = (t == JD_TAG_I64 || t == JD_TAG_BOOL) ? (int64_t)arr->data[i] : u.i;
    g_hof_ret_tag = -1;
}

int32_t jdb_hof_elem_tag(void) { return g_hof_elem_tag; }
int64_t jdb_hof_elem_bits(void) { return g_hof_elem_bits; }

// The kind of a value a higher-order loop passed through a FUNC's f64
// wrapper: the published element's when the bits are its bits, else a number.
int32_t jdb_hof_arg_tag(int64_t bits) {
    if (g_hof_depth <= 0 || bits != g_hof_elem_bits) return JD_TAG_F64;
    return g_hof_elem_tag;
}
void jdb_hof_set_ret(int32_t tag) { g_hof_ret_tag = tag; g_hof_ret_depth = g_hof_depth; }

// A runtime-typed lambda answer as the f64 slot of the call, with its tag reported.
double jdb_hof_ret_tagged(int64_t bits, int32_t tag) {
    if (tag == JD_TAG_I64 || tag == JD_TAG_BOOL) {
        jdb_hof_set_ret(JD_TAG_F64);
        return (double)bits;
    }
    jdb_hof_set_ret(tag);
    union { double d; int64_t i; } u; u.i = bits;
    return u.d;
}

// The tag the lambda reported for its answer, -1 when it reported none.
static int32_t hof_answer_tag(void) {
    return g_hof_ret_depth == g_hof_depth ? g_hof_ret_tag : -1;
}

// Gives r one tag per cell from tags, and the array-wide flags they imply.
static void hof_tag_cells(JdbArray* r, const std::vector<int8_t>& tags) {
    if (r->length == 0) return;
    r->elem_tags = (int8_t*)malloc((size_t)r->length);
    bool ptr = false, all_str = true;
    for (int64_t i = 0; i < r->length; i++) {
        r->elem_tags[i] = tags[(size_t)i];
        int t = tags[(size_t)i];
        if (t == JD_TAG_STR || t == JD_TAG_ARR || t == JD_TAG_NATIVE_MAP) ptr = true;
        if (t != JD_TAG_STR) all_str = false;
    }
    r->flags |= 8;
    if (ptr) r->flags |= 1;
    if (all_str) r->flags |= 2;
}

// The cells of arr chosen by keep, with their per-cell tags.
static JdbArray* hof_pick(JdbArray* arr, const std::vector<int64_t>& keep) {
    auto* r = jdb_array_new((int64_t)keep.size());
    for (size_t j = 0; j < keep.size(); j++) r->data[j] = arr->data[keep[j]];
    r->flags = arr->flags & ~((int32_t)8 | JDB_ARR_OWNS_CHILDREN);
    if ((arr->flags & 8) && arr->elem_tags && !keep.empty()) {
        r->elem_tags = (int8_t*)malloc(keep.size());
        for (size_t j = 0; j < keep.size(); j++) r->elem_tags[j] = arr->elem_tags[keep[j]];
        r->flags |= 8;
    }
    return r;
}

JdbArray* jdb_select_fn(JdbMapFn fn, JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        auto* r = jdb_array_new(arr->length);
        std::vector<int8_t> tags((size_t)arr->length, (int8_t)JD_TAG_F64);
        bool tagged = false;
        HofDepth hof_depth_scope;
        // A callback may grow or shrink the array it walks: these walks cover
        // the cells there were when they began, as long as they still exist.
        for (int64_t i = 0, n = arr->length; i < n && i < arr->length; i++) {
            hof_publish(arr, i);
            r->data[i] = call_compiled(fn, arr->data[i]);
            int32_t t = hof_answer_tag();
            if (t >= 0) {
                tags[(size_t)i] = (int8_t)t;
                if (t != JD_TAG_F64) tagged = true;
            }
        }
        if (tagged) hof_tag_cells(r, tags);
        return r;
    }, [] { return jdb_array_new(0); });
}

// AGG(keys, values, reducer) - group `values` by the matching `keys` entry
// (first-seen order), call reducer(group) per group, return [[key, reduced],
// ...]. `reducer` is the funcref wrapper (double(double)); it receives the
// group as a punned array ptr and returns a numeric result. Key cells keep
// the key array's type (string vs numeric); each result row carries per-cell
// tags so nested reads (row[0]=key, row[1]=reduced) decode correctly.
JdbArray* jdb_agg_fn(JdbMapFn reducer, JdbArray* keys, JdbArray* vals, int32_t reduced_tag) {
    return jdb_guard([&]() -> JdbArray* {
        if (!keys || !vals) return jdb_array_new(0);
        int64_t n = keys->length < vals->length ? keys->length : vals->length;
        std::vector<double> key_cell;               // raw key bits per group
        std::vector<int8_t> key_kind;               // the kind of each group's key
        std::vector<std::vector<double>> gdata;     // value bits per group
        std::vector<std::vector<int8_t>> gkind;     // value kinds per group
        try {
            // A key is found by its kind and its content: a string by its bytes,
            // anything else by its bits.
            std::unordered_map<std::string, int64_t> seen;
            for (int64_t i = 0; i < n; i++) {
                double kc = keys->data[i];
                int kk = cell_kind(keys, i);
                std::string hk(1, kind_is_num(kk) ? (char)JD_TAG_F64 : (char)kk);
                if (kk == JD_TAG_STR) {
                    jdb_text::View s = text_in(decode_str(kc));
                    hk.append(s.p ? s.p : "", s.n);
                } else {
                    hk.append((const char*)&kc, sizeof kc);
                }
                auto it = seen.find(hk);
                int64_t g;
                if (it == seen.end()) {
                    g = (int64_t)key_cell.size();
                    seen.emplace(std::move(hk), g);
                    key_cell.push_back(kc);
                    key_kind.push_back((int8_t)kk);
                    gdata.push_back({});
                    gkind.push_back({});
                } else {
                    g = it->second;
                }
                gdata[(size_t)g].push_back(vals->data[i]);
                gkind[(size_t)g].push_back((int8_t)cell_kind(vals, i));
            }
        } catch (...) {
            jdb_err_set("AGG: out of memory", 1);
            return jdb_array_new(0);
        }

        int64_t ng = (int64_t)key_cell.size();
        JdbArray* r = jdb_array_new(ng);
        for (int64_t g = 0; g < ng; g++) {
            JdbArray* grp = jdb_array_new((int64_t)gdata[(size_t)g].size());
            for (size_t j = 0; j < gdata[(size_t)g].size(); j++)
                put_kind(grp, (int64_t)j, gdata[(size_t)g][j], gkind[(size_t)g][j]);
            settle_kinds(grp);
            grp->flags |= vals->flags & (4 | 16);
            union { double d; int64_t i; } ug; ug.i = (int64_t)(intptr_t)grp;
            // The group lives as long as somebody holds it; the reducer's
            // answer may be the group itself, which the row then keeps.
            jdb_rc_new(grp, JD_TAG_ARR);
            double reduced = call_compiled(reducer, ug.d);
            union { double d; int64_t i; } ur0; ur0.d = reduced;
            if (ur0.i == ug.i) jdb_rc_pin(ug.i);

            JdbArray* row = jdb_array_new(2);
            row->data[0] = key_cell[(size_t)g];
            row->data[1] = reduced;
            row->elem_tags = (int8_t*)malloc(2);
            row->elem_tags[0] = key_kind[(size_t)g];
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
    }, [] { return jdb_array_new(0); });
}

JdbArray* jdb_filter_fn(JdbMapFn fn, JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        std::vector<int64_t> keep;
        HofDepth hof_depth_scope;
        for (int64_t i = 0, n = arr->length; i < n && i < arr->length; i++) {
            hof_publish(arr, i);
            if (call_compiled(fn, arr->data[i]) != 0.0) keep.push_back(i);
        }
        return hof_pick(arr, keep);
    }, [] { return jdb_array_new(0); });
}

double jdb_reduce_fn(JdbReduceFn fn, JdbArray* arr, double init) {
    return jdb_guard([&]() -> double {
        if (!arr) return init;
        double acc = init;
        HofDepth hof_depth_scope;
        for (int64_t i = 0, n = arr->length; i < n && i < arr->length; i++) {
            hof_publish(arr, i);
            acc = call_compiled(fn, acc, arr->data[i]);
        }
        return acc;
    }, [] { return 0.0; });
}

// TAKE_WHILE(pred@, arr): longest prefix where pred(elem) is true.
JdbArray* jdb_take_while_fn(JdbMapFn pred, JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        std::vector<int64_t> keep;
        HofDepth hof_depth_scope;
        for (int64_t i = 0, n = arr->length; i < n && i < arr->length; i++) {
            hof_publish(arr, i);
            if (call_compiled(pred, arr->data[i]) == 0.0) break;
            keep.push_back(i);
        }
        return hof_pick(arr, keep);
    }, [] { return jdb_array_new(0); });
}

// DROP_WHILE(pred@, arr): drop the longest true-prefix, return the remainder.
JdbArray* jdb_drop_while_fn(JdbMapFn pred, JdbArray* arr) {
    return jdb_guard([&]() -> JdbArray* {
        if (!arr) return jdb_array_new(0);
        int64_t start = 0;
        HofDepth hof_depth_scope;
        const int64_t n = arr->length;
        while (start < n && start < arr->length) {
            hof_publish(arr, start);
            if (call_compiled(pred, arr->data[start]) == 0.0) break;
            start++;
        }
        std::vector<int64_t> keep;
        for (int64_t i = start; i < arr->length; i++) keep.push_back(i);
        return hof_pick(arr, keep);
    }, [] { return jdb_array_new(0); });
}

// OUTER(a, b, op) with a user funcref operator: a 2D table whose cell [i][j]
// is op(a[i], b[j]). The string-operator form ("+", "*", ...) goes through the
// VM bridge; this native path exists so a FUNC used as an operator (op@) can be
// called directly instead of by-name through the bridge (which can't resolve a
// compiled user function).
JdbArray* jdb_outer_fn(JdbArray* a, JdbArray* b, JdbReduceFn op) {
    return jdb_guard([&]() -> JdbArray* {
        if (!a || !b) return jdb_array_new(0);
        auto* r = jdb_array_new(a->length);
        for (int64_t i = 0; i < a->length; i++) {
            auto* row = jdb_array_new(b->length);
            for (int64_t j = 0; j < b->length; j++)
                row->data[j] = call_compiled(op, a->data[i], b->data[j]);
            r->data[i] = encode_inner(row);
        }
        if (a->length > 0) r->flags |= 1;  // rows are nested arrays
        return r;
    }, [] { return jdb_array_new(0); });
}

} // extern "C"

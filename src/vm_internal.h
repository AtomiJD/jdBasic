#pragma once
// Includes and helpers shared by vm.cpp and the vm_builtins_*.cpp files.

#include "vm.h"
#include "builtin_sigs.h"
#include "dap.h"
#include "errors.h"
#include "channels.h"
#include "file_streams.h"
#include "jdb_crypto.h"
#include "jdb_deflate.h"
#include <clocale>
#include <locale>
#ifdef COM
#include "com.h"
#endif
#ifdef GFX
#include "graphics.h"
#include <SDL3/SDL.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cstdlib>
#endif
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <fstream>
#ifndef JDB_LEAN
#include <regex>
#endif
#ifdef JDB_MCU
extern "C" int jdb_break_poll(void);
extern "C" int jdb_stdin_getc(int timeout_us);
#endif

// How deep jdBasic calls may nest. Every frame costs about 130 bytes of
// the C stack, so a board with a small stack sets this lower.
#ifndef JDB_MAX_FRAMES
#define JDB_MAX_FRAMES 512
#endif
#include <unordered_set>
#include <random>
#include <thread>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#include <windows.h>
#include <conio.h>
#include <eh.h>  // _set_se_translator
#else
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <dirent.h>
#include <fnmatch.h>
#include <termios.h>   // raw-mode keyboard polling for console-mode KEYDOWN
#endif

// jdb_encoding.h pulls <windows.h>; must come after the winsock2 / ws2tcpip
// block above, otherwise winsock1.h leaks in via windows.h and collides.
#include "jdb_encoding.h"
#include "pdf_extract.h"

// OS.SCREENSHOT backing (src/screencap.cpp). GDI+WIC on Windows, stub
// elsewhere. rc 0 = ok, negative = error.
extern "C" int jdb_screencap(const char* path, const char* mode, const char* caption);

#ifdef PICOCALC
extern "C" int  picocalc_gfx_buffered(void);
extern "C" void picocalc_gfx_clear(int index);
#endif

// Defined in vm_builtins_datetime.cpp. jdb_date_arith answers false when
// neither operand is a date, and the caller then does ordinary numeric
// arithmetic on a result that carries no DATE subtype.
double jdb_date_wall_seconds(double epoch);
bool jdb_date_arith(const Value& a, const Value& b, OpCode op, Value& out);

#include <mutex>
#include "async_task.h"

// Walk the leaves of a possibly nested array without building a copy of
// it. A reduction only ever reads its input, and materialising the
// leaves costs as much as the array already holds: on 100000 elements
// that is another 2.4 MB, which is what makes SUM fail on a board with
// megabytes still free.
template <class F>
static void for_each_leaf(const Value& v, F&& fn) {
    if (v.type == ValueType::ARRAY) {
        for (const auto& e : v.as_array()->elements) for_each_leaf(e, fn);
    } else {
        fn(v);
    }
}

// The same walk, stopping at the first leaf the predicate accepts.
template <class F>
static bool any_leaf(const Value& v, F&& pred) {
    if (v.type == ValueType::ARRAY) {
        for (const auto& e : v.as_array()->elements)
            if (any_leaf(e, pred)) return true;
        return false;
    }
    return pred(v);
}

// The VM's array model for the shape algorithms of jdb_shape.h: nested
// Values, a cell holding whatever Value it holds.
struct VmShape {
    using Arr = Value;
    using Val = Value;
    static int64_t len(const Value& a) { return (int64_t)a.as_array()->elements.size(); }
    static bool is_row(const Value& a, int64_t i) { return a.as_array()->elements[(size_t)i].type == ValueType::ARRAY; }
    static Value row(const Value& a, int64_t i) { return a.as_array()->elements[(size_t)i]; }
    static double num(const Value& a, int64_t i) { return a.as_array()->elements[(size_t)i].to_double(); }
    static Value cell(const Value& a, int64_t i) { return a.as_array()->elements[(size_t)i]; }
    static Value make(int64_t n) {
        Value v = Value::make_array();
        v.as_array()->elements.assign((size_t)n, Value::make_f64(0.0));
        return v;
    }
    static void put_num(Value& a, int64_t i, double d) { a.as_array()->elements[(size_t)i] = Value::make_f64(d); }
    static void put_bool(Value& a, int64_t i, bool b) { a.as_array()->elements[(size_t)i] = Value::make_bool(b); }
    static void put_row(Value& a, int64_t i, const Value& r) { a.as_array()->elements[(size_t)i] = r; }
    static void put(Value& a, int64_t i, const Value& v) { a.as_array()->elements[(size_t)i] = v; }
    static void put_fill(Value& a, int64_t i, const Value& v) { a.as_array()->elements[(size_t)i] = v; }
    static void copy_cell(Value& out, int64_t k, const Value& a, int64_t i) {
        out.as_array()->elements[(size_t)k] = a.as_array()->elements[(size_t)i];
    }
    static Value clone(const Value& a) { return a; }
    static bool same(const Value& a, const Value& b) { return a.as_array() == b.as_array(); }
    static void carry_flags(Value&, const Value&) {}
    static void drop(Value&) {}
    static void shape_error(int64_t n, int64_t m) {
        throw jdError(ErrCode::TYPE_MISMATCH, "shapes differ: " + std::to_string(n) + " and " + std::to_string(m));
    }
};

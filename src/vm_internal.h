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

// Days since 1970-01-01 of a proleptic Gregorian date.
inline int64_t jdb_days_from_civil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

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

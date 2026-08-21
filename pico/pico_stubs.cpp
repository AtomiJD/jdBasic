// What newlib does not have and the board cannot mean: process spawning,
// environment writes, host names. Each answers the way the runtime treats
// an absent feature, so the core above stays untouched.

#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>

extern "C" {

FILE* popen(const char*, const char*) { errno = ENOSYS; return nullptr; }
int   pclose(FILE*) { return -1; }

int setenv(const char*, const char*, int) { return -1; }
int unsetenv(const char*) { return -1; }

int gethostname(char* name, size_t cap) {
    strncpy(name, "pico", cap);
    return 0;
}

int mkstemp(char*) { errno = ENOSYS; return -1; }

// UTC mktime for a runtime without timezones: on the board local time
// and UTC are the same clock.
time_t timegm(struct tm* t) { return mktime(t); }

}

// The pieces of the desktop build the board does not carry: the debug
// adapter, the builtin families behind feature flags, screen capture,
// and the editor knob. Each is the quiet version of itself.

#include "../src/dap.h"
#include "../src/vm.h"

void DAPHandler::send_stopped_message(const std::string&, int, const std::string&) {}
void DAPHandler::send_output_message(const std::string&) {}
void DAPHandler::send_program_ended_message() {}
void DebugInfo::pause() {}

void register_ai_builtins(VM&) {}
void register_llm_builtins(VM&) {}
void register_numerics_builtins(VM&) {}

bool g_editor_autoindent = true;

extern "C" int jdb_screencap(const char*, const char*, const char*) { return -1; }

// Filesystem syscalls newlib routes here. No filesystem yet: the current
// directory is the root and everything else declines.
extern "C" {
char* getcwd(char* buf, size_t cap) {
    if (!buf || cap < 2) return nullptr;
    buf[0] = '/';
    buf[1] = 0;
    return buf;
}
int chdir(const char*) { errno = ENOSYS; return -1; }
}

// The glob matcher DIR$ filters through: star and question mark, the
// part of fnmatch anyone means.
extern "C" int fnmatch(const char* pat, const char* str, int flags) {
    (void)flags;
    if (*pat == 0) return *str == 0 ? 0 : 1;
    if (*pat == '*') {
        for (const char* s = str; ; s++) {
            if (fnmatch(pat + 1, s, 0) == 0) return 0;
            if (*s == 0) return 1;
        }
    }
    if (*str == 0) return 1;
    if (*pat == '?' || *pat == *str) return fnmatch(pat + 1, str + 1, 0);
    return 1;
}

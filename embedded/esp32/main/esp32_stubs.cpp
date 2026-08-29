// What the desktop build carries and this one cannot: process spawning,
// the debug adapter, the builtin families behind feature flags, screen
// capture. Each answers the way the runtime treats an absent feature,
// so the core above stays untouched.

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>

extern "C" {

FILE* popen(const char*, const char*) { errno = ENOSYS; return nullptr; }
int   pclose(FILE*) { return -1; }

// The glob matcher DIR$ filters through: star and question mark, the
// part of fnmatch anyone means.
int fnmatch(const char* pat, const char* str, int flags) {
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

int jdb_screencap(const char*, const char*, const char*) { return -1; }

}

#include "../../../src/dap.h"
#include "../../../src/vm.h"

void DAPHandler::send_stopped_message(const std::string&, int, const std::string&) {}
void DAPHandler::send_output_message(const std::string&) {}
void DAPHandler::send_program_ended_message() {}
void DebugInfo::pause() {}

void register_ai_builtins(VM&) {}
void register_llm_builtins(VM&) {}
void register_numerics_builtins(VM&) {}

bool g_editor_autoindent = true;

// UTC mktime for a runtime without timezones: on the board local time
// and UTC are the same clock.
extern "C" time_t timegm(struct tm* t) { return mktime(t); }

// IDF has no host name to give. The runtime treats an absent one the
// way the board's does.
extern "C" int gethostname(char* name, size_t cap) {
    strncpy(name, "esp32s3", cap);
    return 0;
}

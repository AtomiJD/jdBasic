// Declarations IDF's newlib does not carry; the definitions live in
// esp32_stubs.cpp. Force-included ahead of every C++ source.
#pragma once
#include <stdio.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
FILE*  popen(const char*, const char*);
int    pclose(FILE*);
int    fnmatch(const char* pattern, const char* string, int flags);
time_t timegm(struct tm*);
#ifdef __cplusplus
}
#endif

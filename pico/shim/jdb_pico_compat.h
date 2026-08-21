// Declarations newlib does not carry; the definitions live in
// pico_stubs.cpp. Force-included ahead of every source, so it stays
// harmless for the SDK's C files too.
#pragma once
#include <stdio.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
FILE*  popen(const char*, const char*);
int    pclose(FILE*);
int    setenv(const char*, const char*, int);
int    unsetenv(const char*);
int    gethostname(char*, size_t);
int    mkstemp(char*);
time_t timegm(struct tm*);
#ifdef __cplusplus
}
#endif

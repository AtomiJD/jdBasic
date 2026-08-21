// Directory reading over the flash filesystem; implemented in pico_fs.cpp.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DIR DIR;
struct dirent { char d_name[256]; int d_type; };
#define DT_DIR 4
#define DT_REG 8

DIR* opendir(const char* path);
struct dirent* readdir(DIR* d);
int closedir(DIR* d);

#ifdef __cplusplus
}
#endif

#pragma once
#include <stddef.h>
typedef struct DIR DIR;
struct dirent { char d_name[256]; int d_type; };
#define DT_DIR 4
#define DT_REG 8
static inline DIR* opendir(const char* p) { (void)p; return 0; }
static inline struct dirent* readdir(DIR* d) { (void)d; return 0; }
static inline int closedir(DIR* d) { (void)d; return 0; }

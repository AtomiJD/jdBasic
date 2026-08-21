// A filesystem in the spare flash: littlefs on the last part of the
// chip, wired into newlib's syscall layer. Everything above - fopen,
// TXTREADER$, TXTWRITER, DIR$, the embed loader - works unchanged,
// because it all funnels through these weak symbols.

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "littlefs/lfs.h"
#include "shim/dirent.h"

// The image sits at the front of the 4 MB chip; the filesystem takes
// the back. 1.5 MB of programs is a lot of BASIC.
#define FS_SIZE   (1536u * 1024u)
#define FS_OFFSET (PICO_FLASH_SIZE_BYTES - FS_SIZE)
#define FS_BLOCK  4096u

static int bd_read(const struct lfs_config* c, lfs_block_t block,
                   lfs_off_t off, void* buffer, lfs_size_t size) {
    (void)c;
    memcpy(buffer, (const uint8_t*)(XIP_BASE + FS_OFFSET + block * FS_BLOCK + off), size);
    return 0;
}

static int bd_prog(const struct lfs_config* c, lfs_block_t block,
                   lfs_off_t off, const void* buffer, lfs_size_t size) {
    (void)c;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(FS_OFFSET + block * FS_BLOCK + off, (const uint8_t*)buffer, size);
    restore_interrupts(ints);
    return 0;
}

static int bd_erase(const struct lfs_config* c, lfs_block_t block) {
    (void)c;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FS_OFFSET + block * FS_BLOCK, FS_BLOCK);
    restore_interrupts(ints);
    return 0;
}

static int bd_sync(const struct lfs_config* c) { (void)c; return 0; }

static lfs_t g_lfs;
static bool g_mounted = false;

static uint8_t g_read_buf[256];
static uint8_t g_prog_buf[256];
static uint8_t g_lookahead[16];

static const struct lfs_config g_cfg = {
    .read  = bd_read,
    .prog  = bd_prog,
    .erase = bd_erase,
    .sync  = bd_sync,
    .read_size = 256,
    .prog_size = 256,
    .block_size = FS_BLOCK,
    .block_count = FS_SIZE / FS_BLOCK,
    .block_cycles = 500,
    .cache_size = 256,
    .lookahead_size = 16,
    .read_buffer = g_read_buf,
    .prog_buffer = g_prog_buf,
    .lookahead_buffer = g_lookahead,
};

extern "C" void jdb_pico_fs_init(void) {
    if (lfs_mount(&g_lfs, &g_cfg) != 0) {
        lfs_format(&g_lfs, &g_cfg);
        lfs_mount(&g_lfs, &g_cfg);
    }
    g_mounted = true;
}

// Open-file table, newlib fds 3 and up. fds 0 to 2 stay with the SDK's
// stdio, handled the way the weak defaults handle them.

#define MAX_FILES 8
static lfs_file_t g_files[MAX_FILES];
static bool g_used[MAX_FILES];

static int lfs_err_to_errno(int e) {
    switch (e) {
        case LFS_ERR_NOENT:  return ENOENT;
        case LFS_ERR_EXIST:  return EEXIST;
        case LFS_ERR_NOSPC:  return ENOSPC;
        case LFS_ERR_ISDIR:  return EISDIR;
        case LFS_ERR_NOTDIR: return ENOTDIR;
        default:             return EIO;
    }
}

extern "C" {

int _open(const char* path, int oflag, ...) {
    if (!g_mounted) { errno = ENODEV; return -1; }
    int slot = -1;
    for (int i = 0; i < MAX_FILES; i++) if (!g_used[i]) { slot = i; break; }
    if (slot < 0) { errno = EMFILE; return -1; }

    int flags = 0;
    switch (oflag & O_ACCMODE) {
        case O_RDONLY: flags = LFS_O_RDONLY; break;
        case O_WRONLY: flags = LFS_O_WRONLY; break;
        default:       flags = LFS_O_RDWR;   break;
    }
    if (oflag & O_CREAT)  flags |= LFS_O_CREAT;
    if (oflag & O_TRUNC)  flags |= LFS_O_TRUNC;
    if (oflag & O_APPEND) flags |= LFS_O_APPEND;

    int rc = lfs_file_open(&g_lfs, &g_files[slot], path, flags);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    g_used[slot] = true;
    return slot + 3;
}

int _read(int fd, char* buf, int len) {
    if (fd == 0) return stdio_get_until(buf, len, at_the_end_of_time);
    int slot = fd - 3;
    if (slot < 0 || slot >= MAX_FILES || !g_used[slot]) { errno = EBADF; return -1; }
    int rc = lfs_file_read(&g_lfs, &g_files[slot], buf, len);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    return rc;
}

int _write(int fd, char* buf, int len) {
    if (fd == 1 || fd == 2) {
        stdio_put_string(buf, len, false, true);
        return len;
    }
    int slot = fd - 3;
    if (slot < 0 || slot >= MAX_FILES || !g_used[slot]) { errno = EBADF; return -1; }
    int rc = lfs_file_write(&g_lfs, &g_files[slot], buf, len);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    return rc;
}

int _close(int fd) {
    int slot = fd - 3;
    if (slot < 0 || slot >= MAX_FILES || !g_used[slot]) { errno = EBADF; return -1; }
    lfs_file_close(&g_lfs, &g_files[slot]);
    g_used[slot] = false;
    return 0;
}

off_t _lseek(int fd, off_t pos, int whence) {
    int slot = fd - 3;
    if (slot < 0 || slot >= MAX_FILES || !g_used[slot]) { errno = EBADF; return -1; }
    int w = (whence == SEEK_SET) ? LFS_SEEK_SET
          : (whence == SEEK_CUR) ? LFS_SEEK_CUR
          : LFS_SEEK_END;
    lfs_soff_t rc = lfs_file_seek(&g_lfs, &g_files[slot], pos, w);
    if (rc < 0) { errno = lfs_err_to_errno((int)rc); return -1; }
    return (off_t)rc;
}

int _fstat(int fd, struct stat* st) {
    memset(st, 0, sizeof *st);
    if (fd < 3) { st->st_mode = S_IFCHR; return 0; }
    int slot = fd - 3;
    if (slot < 0 || slot >= MAX_FILES || !g_used[slot]) { errno = EBADF; return -1; }
    st->st_mode = S_IFREG;
    st->st_size = lfs_file_size(&g_lfs, &g_files[slot]);
    return 0;
}

int _isatty(int fd) { return fd < 3; }

int _stat(const char* path, struct stat* st) {
    if (!g_mounted) { errno = ENODEV; return -1; }
    struct lfs_info info;
    int rc = lfs_stat(&g_lfs, path, &info);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    memset(st, 0, sizeof *st);
    st->st_mode = (info.type == LFS_TYPE_DIR) ? S_IFDIR : S_IFREG;
    st->st_size = info.size;
    return 0;
}

int stat(const char* path, struct stat* st) { return _stat(path, st); }

int _unlink(const char* path) {
    int rc = lfs_remove(&g_lfs, path);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    return 0;
}

int unlink(const char* path) { return _unlink(path); }

int mkdir(const char* path, mode_t mode) {
    (void)mode;
    int rc = lfs_mkdir(&g_lfs, path);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    return 0;
}

int rmdir(const char* path) { return _unlink(path); }

// Directory listing for DIR$.

struct DIR {
    lfs_dir_t dir;
    struct dirent ent;
    bool open;
};

static DIR g_dirs[4];

DIR* opendir(const char* path) {
    if (!g_mounted) return nullptr;
    for (auto& d : g_dirs) {
        if (d.open) continue;
        if (lfs_dir_open(&g_lfs, &d.dir, path) < 0) return nullptr;
        d.open = true;
        return &d;
    }
    return nullptr;
}

struct dirent* readdir(DIR* d) {
    if (!d || !d->open) return nullptr;
    struct lfs_info info;
    for (;;) {
        int rc = lfs_dir_read(&g_lfs, &d->dir, &info);
        if (rc <= 0) return nullptr;
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;
        strncpy(d->ent.d_name, info.name, sizeof d->ent.d_name - 1);
        d->ent.d_name[sizeof d->ent.d_name - 1] = 0;
        d->ent.d_type = (info.type == LFS_TYPE_DIR) ? DT_DIR : DT_REG;
        return &d->ent;
    }
}

int closedir(DIR* d) {
    if (!d || !d->open) return -1;
    lfs_dir_close(&g_lfs, &d->dir);
    d->open = false;
    return 0;
}

}

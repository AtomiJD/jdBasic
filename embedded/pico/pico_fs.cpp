// A filesystem in the spare flash: littlefs on the last part of the
// chip, wired into newlib's syscall layer. Everything above - fopen,
// TXTREADER$, TXTWRITER, DIR$, the embed loader - works unchanged,
// because it all funnels through these weak symbols.

#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "littlefs/lfs.h"
#include "shim/dirent.h"

// The image sits at the front of the chip; the filesystem takes the
// back. On a 4 MB chip 1.5 MB of programs is a lot of BASIC; a 2 MB
// chip keeps a quarter for files and the rest for the image.
#if PICO_FLASH_SIZE_BYTES >= (4u * 1024u * 1024u)
#define FS_SIZE   (1536u * 1024u)
#else
#define FS_SIZE   (512u * 1024u)
#endif
#define FS_OFFSET (PICO_FLASH_SIZE_BYTES - FS_SIZE)
#define FS_BLOCK  4096u

static int bd_read(const struct lfs_config* c, lfs_block_t block,
                   lfs_off_t off, void* buffer, lfs_size_t size) {
    (void)c;
    // Through the uncached alias: littlefs verifies what it programs,
    // and the XIP cache would hand it yesterday's bytes.
    memcpy(buffer,
           (const uint8_t*)(XIP_NOCACHE_NOALLOC_BASE + FS_OFFSET + block * FS_BLOCK + off),
           size);
    return 0;
}


#include "pico/flash.h"
#include "pico/multicore.h"
#include "hardware/irq.h"

struct prog_args { uint32_t off; const uint8_t* buf; size_t n; };

static void do_prog(void* p) {
    struct prog_args* a = (struct prog_args*)p;
    flash_range_program(a->off, a->buf, a->n);
}

static void do_erase(void* p) {
    flash_range_erase((uint32_t)(uintptr_t)p, FS_BLOCK);
}

#ifdef FRUITJAM
// Erasing a sector takes about fifty milliseconds, and the SDK's safe
// wrapper switches every interrupt off for the duration. On this board
// one of them cannot wait that long: the scanout needs an interrupt per
// frame to aim its DMA back at the top of the command list, and three
// frames the monitor does not get are enough for it to drop the signal
// and go black for a second or two. That is what a save from the editor
// looked like.
//
// So the operation runs with that one interrupt still alive and every
// other one parked by hand. It is safe because nothing on its path
// leaves RAM: the handler, the console tick it calls, the command list,
// the line templates and the framebuffer are all there, and code
// fetched from flash while the flash is busy would simply stall until
// the erase finished, which is the whole problem.
#define KEEP_IRQ DMA_IRQ_1

static uint32_t g_irq_saved[2];

static void irqs_park(void) {
    g_irq_saved[0] = 0;
    g_irq_saved[1] = 0;
    for (uint i = 0; i < NUM_IRQS; i++) {
        if (i == (uint)KEEP_IRQ) continue;
        if (irq_is_enabled(i)) {
            g_irq_saved[i / 32] |= 1u << (i % 32);
            irq_set_enabled(i, false);
        }
    }
}

static void irqs_unpark(void) {
    for (uint i = 0; i < NUM_IRQS; i++) {
        if (g_irq_saved[i / 32] & (1u << (i % 32))) irq_set_enabled(i, true);
    }
}

// The other core is not parked either. Parking it is what the SDK's
// wrapper does, and it is only needed because a core executing from
// flash cannot execute while the flash is busy. Core 1 here runs the USB
// frame loop, and that loop and everything it reaches were put in RAM
// for this reason: a sector erase is fifty milliseconds, a USB device
// that hears nothing for three of them goes to sleep, and nothing wakes
// it again. Parking the core cost the keyboard on every save.
static int flash_op(void (*fn)(void*), void* arg) {
    irqs_park();
    fn(arg);
    irqs_unpark();
    return 0;
}
#else
static int flash_op(void (*fn)(void*), void* arg) {
    return flash_safe_execute(fn, arg, 2000) == PICO_OK ? 0 : LFS_ERR_IO;
}
#endif

static int bd_prog(const struct lfs_config* c, lfs_block_t block,
                   lfs_off_t off, const void* buffer, lfs_size_t size) {
    (void)c;
    struct prog_args a = { (uint32_t)(FS_OFFSET + block * FS_BLOCK + off),
                           (const uint8_t*)buffer, size };
    return flash_op(do_prog, &a);
}

static int bd_erase(const struct lfs_config* c, lfs_block_t block) {
    (void)c;
    uintptr_t off = FS_OFFSET + block * FS_BLOCK;
    return flash_op(do_erase, (void*)off);
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
    int rc = lfs_mount(&g_lfs, &g_cfg);
    if (rc == 0) {
        // A leftover littlefs from other firmware mounts fine and then
        // fails every write: only a store with our geometry counts.
        struct lfs_fsinfo fi;
        if (lfs_fs_stat(&g_lfs, &fi) != 0 || fi.block_count != g_cfg.block_count) {
            lfs_unmount(&g_lfs);
            rc = -1;
        }
    }
    if (rc != 0) {
        lfs_format(&g_lfs, &g_cfg);
        rc = lfs_mount(&g_lfs, &g_cfg);
    }
    g_mounted = (rc == 0);
}

// The SD card behind the /sd prefix, through the wrappers in
// picocalc_sd.c so FatFS types stay out of this file. A bare board has
// no card slot; stubs keep every /sd path an ENOENT.
#if defined(PICOCALC) || defined(FRUITJAM)
extern "C" {
int  sd_open(const char* path, int write, int create, int truncate, int append);
int  sd_read(int h, void* buf, int len);
int  sd_write(int h, const void* buf, int len);
long sd_lseek(int h, long pos, int whence);
long sd_size(int h);
int  sd_close(int h);
int  sd_stat(const char* path, long* size, int* isdir);
int  sd_unlink(const char* path);
int  sd_mkdir(const char* path);
int  sd_rename(const char* oldp, const char* newp);
int  sd_opendir(const char* path);
int  sd_readdir(int h, char* name, int cap, int* isdir);
int  sd_closedir(int h);
}
#else
static int  sd_open(const char*, int, int, int, int) { return -1; }
static int  sd_read(int, void*, int) { return -1; }
static int  sd_write(int, const void*, int) { return -1; }
static long sd_lseek(int, long, int) { return -1; }
static long sd_size(int) { return 0; }
static int  sd_close(int) { return 0; }
static int  sd_stat(const char*, long*, int*) { return -1; }
static int  sd_unlink(const char*) { return -1; }
static int  sd_mkdir(const char*) { return -1; }
static int  sd_rename(const char*, const char*) { return -1; }
static int  sd_opendir(const char*) { return -1; }
static int  sd_readdir(int, char*, int, int*) { return -1; }
static int  sd_closedir(int) { return 0; }
#endif

// A path that means the card: /sd, /sd/... - the rest of the path in
// FatFS terms, or NULL when it belongs to the flash store.
static const char* sd_part(const char* path) {
    if (strncmp(path, "/sd", 3) != 0) return nullptr;
    if (path[3] == 0) return "/";
    if (path[3] == '/') return path + 3;
    return nullptr;
}

// The current directory, applied to every path the runtime hands down
// before the store split - so relative names, dot and dotdot work the
// same on flash and card.
static char g_cwd[128] = "/";

static void jdb_resolve(const char* path, char* out, int cap) {
    char tmp[256];
    if (path[0] == '/')
        snprintf(tmp, sizeof tmp, "%s", path);
    else
        snprintf(tmp, sizeof tmp, "%s/%s", g_cwd, path);
    char* parts[24];
    int np = 0;
    for (char* p = strtok(tmp, "/"); p; p = strtok(nullptr, "/")) {
        if (strcmp(p, ".") == 0) continue;
        if (strcmp(p, "..") == 0) { if (np) np--; continue; }
        if (np < 24) parts[np++] = p;
    }
    int at = 0;
    out[0] = 0;
    for (int i = 0; i < np && at < cap - 1; i++)
        at += snprintf(out + at, cap - at, "/%s", parts[i]);
    if (!out[0]) snprintf(out, cap, "/");
}

extern "C" int _stat(const char* path, struct stat* st);

extern "C" char* getcwd(char* buf, size_t cap) {
    if (!buf || cap < 2) return nullptr;
    snprintf(buf, cap, "%s", g_cwd);
    return buf;
}

extern "C" int chdir(const char* path) {
    char rp[160];
    jdb_resolve(path, rp, sizeof rp);
    // The two roots exist by definition; FatFS cannot stat its own "/".
    if (strcmp(rp, "/") != 0 && strcmp(rp, "/sd") != 0) {
        struct stat st;
        if (_stat(rp, &st) != 0) return -1;
        if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return -1; }
    }
    snprintf(g_cwd, sizeof g_cwd, "%s", rp);
    return 0;
}

// Open-file table, newlib fds 3 and up. fds 0 to 2 stay with the SDK's
// stdio, handled the way the weak defaults handle them.

#define MAX_FILES 8
static lfs_file_t g_files[MAX_FILES];
// littlefs keeps no modification time of its own, so a written file
// gets one as a custom attribute, stamped when it closes.
#define ATTR_MTIME 0x74
static char g_wpath[MAX_FILES][160];
static bool g_used[MAX_FILES];
static bool g_is_sd[MAX_FILES];
static int  g_sdh[MAX_FILES];

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
    char rp[160]; jdb_resolve(path, rp, sizeof rp); path = rp;
    int slot = -1;
    for (int i = 0; i < MAX_FILES; i++) if (!g_used[i]) { slot = i; break; }
    if (slot < 0) { errno = EMFILE; return -1; }

    const char* sp = sd_part(path);
    if (sp) {
        int wr = (oflag & O_ACCMODE) != O_RDONLY;
        int h = sd_open(sp, wr, (oflag & O_CREAT) != 0,
                        (oflag & O_TRUNC) != 0, (oflag & O_APPEND) != 0);
        if (h < 0) { errno = EIO; return -1; }
        g_used[slot] = true;
        g_is_sd[slot] = true;
        g_sdh[slot] = h;
        return slot + 3;
    }

    if (!g_mounted) { errno = ENODEV; return -1; }
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
    g_is_sd[slot] = false;
    g_wpath[slot][0] = 0;
    if ((oflag & O_ACCMODE) != O_RDONLY)
        snprintf(g_wpath[slot], sizeof g_wpath[slot], "%s", path);
    return slot + 3;
}

int _read(int fd, char* buf, int len) {
    if (fd == 0) return stdio_get_until(buf, len, at_the_end_of_time);
    int slot = fd - 3;
    if (slot < 0 || slot >= MAX_FILES || !g_used[slot]) { errno = EBADF; return -1; }
    if (g_is_sd[slot]) {
        int rc = sd_read(g_sdh[slot], buf, len);
        if (rc < 0) { errno = EIO; return -1; }
        return rc;
    }
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
    if (g_is_sd[slot]) {
        int rc = sd_write(g_sdh[slot], buf, len);
        if (rc < 0) { errno = EIO; return -1; }
        return rc;
    }
    int rc = lfs_file_write(&g_lfs, &g_files[slot], buf, len);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    return rc;
}

int _close(int fd) {
    int slot = fd - 3;
    if (slot < 0 || slot >= MAX_FILES || !g_used[slot]) { errno = EBADF; return -1; }
    if (g_is_sd[slot]) sd_close(g_sdh[slot]);
    else {
        lfs_file_close(&g_lfs, &g_files[slot]);
        if (g_wpath[slot][0]) {
            uint32_t now = (uint32_t)time(nullptr);
            lfs_setattr(&g_lfs, g_wpath[slot], ATTR_MTIME, &now, sizeof now);
        }
    }
    g_used[slot] = false;
    g_is_sd[slot] = false;
    g_wpath[slot][0] = 0;
    return 0;
}

off_t _lseek(int fd, off_t pos, int whence) {
    int slot = fd - 3;
    if (slot < 0 || slot >= MAX_FILES || !g_used[slot]) { errno = EBADF; return -1; }
    if (g_is_sd[slot]) {
        int w = (whence == SEEK_SET) ? 0 : (whence == SEEK_CUR) ? 1 : 2;
        long rc = sd_lseek(g_sdh[slot], (long)pos, w);
        if (rc < 0) { errno = EIO; return -1; }
        return (off_t)rc;
    }
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
    st->st_size = g_is_sd[slot] ? sd_size(g_sdh[slot])
                                : lfs_file_size(&g_lfs, &g_files[slot]);
    return 0;
}

int _isatty(int fd) { return fd < 3; }

int _stat(const char* path, struct stat* st) {
    char rp[160]; jdb_resolve(path, rp, sizeof rp); path = rp;
    const char* sp = sd_part(path);
    if (sp) {
        long size; int isdir;
        if (sd_stat(sp, &size, &isdir) != 0) { errno = ENOENT; return -1; }
        memset(st, 0, sizeof *st);
        st->st_mode = isdir ? S_IFDIR : S_IFREG;
        st->st_size = size;
        return 0;
    }
    if (!g_mounted) { errno = ENODEV; return -1; }
    struct lfs_info info;
    int rc = lfs_stat(&g_lfs, path, &info);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    memset(st, 0, sizeof *st);
    st->st_mode = (info.type == LFS_TYPE_DIR) ? S_IFDIR : S_IFREG;
    st->st_size = info.size;
    uint32_t mtime = 0;
    if (lfs_getattr(&g_lfs, path, ATTR_MTIME, &mtime, sizeof mtime) == sizeof mtime)
        st->st_mtime = (time_t)mtime;
    return 0;
}

int stat(const char* path, struct stat* st) { return _stat(path, st); }

int _unlink(const char* path) {
    char rp[160]; jdb_resolve(path, rp, sizeof rp); path = rp;
    const char* sp = sd_part(path);
    if (sp) {
        if (sd_unlink(sp) != 0) { errno = EIO; return -1; }
        return 0;
    }
    int rc = lfs_remove(&g_lfs, path);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    return 0;
}

int unlink(const char* path) { return _unlink(path); }

int mkdir(const char* path, mode_t mode) {
    (void)mode;
    char rp[160]; jdb_resolve(path, rp, sizeof rp); path = rp;
    const char* sp = sd_part(path);
    if (sp) {
        if (sd_mkdir(sp) != 0) { errno = EIO; return -1; }
        return 0;
    }
    int rc = lfs_mkdir(&g_lfs, path);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    return 0;
}

int rmdir(const char* path) { return _unlink(path); }

// Rename inside one store is native; across stores the caller falls
// back to copy and delete, signalled by EXDEV.
int rename(const char* oldp, const char* newp) {
    char ra[160], rb[160];
    jdb_resolve(oldp, ra, sizeof ra);
    jdb_resolve(newp, rb, sizeof rb);
    const char* sa = sd_part(ra);
    const char* sb = sd_part(rb);
    if ((sa != nullptr) != (sb != nullptr)) { errno = EXDEV; return -1; }
    if (sa) {
        if (sd_rename(sa, sb) != 0) { errno = EIO; return -1; }
        return 0;
    }
    if (!g_mounted) { errno = ENODEV; return -1; }
    int rc = lfs_rename(&g_lfs, ra, rb);
    if (rc < 0) { errno = lfs_err_to_errno(rc); return -1; }
    return 0;
}

// Directory listing for DIR$.

struct DIR {
    lfs_dir_t dir;
    struct dirent ent;
    bool open;
    bool is_sd;
    int sdh;
};

static DIR g_dirs[4];

DIR* opendir(const char* path) {
    char rp[160]; jdb_resolve(path, rp, sizeof rp); path = rp;
    for (auto& d : g_dirs) {
        if (d.open) continue;
        const char* sp = sd_part(path);
        if (sp) {
            int h = sd_opendir(sp);
            if (h < 0) return nullptr;
            d.is_sd = true;
            d.sdh = h;
            d.open = true;
            return &d;
        }
        if (!g_mounted) return nullptr;
        if (lfs_dir_open(&g_lfs, &d.dir, path) < 0) return nullptr;
        d.is_sd = false;
        d.open = true;
        return &d;
    }
    return nullptr;
}

struct dirent* readdir(DIR* d) {
    if (!d || !d->open) return nullptr;
    if (d->is_sd) {
        int isdir = 0;
        int rc = sd_readdir(d->sdh, d->ent.d_name, sizeof d->ent.d_name, &isdir);
        if (rc <= 0) return nullptr;
        d->ent.d_type = isdir ? DT_DIR : DT_REG;
        return &d->ent;
    }
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
    if (d->is_sd) sd_closedir(d->sdh);
    else lfs_dir_close(&g_lfs, &d->dir);
    d->open = false;
    return 0;
}

}

// Layer-by-layer self test, reachable from the prompt as FS.TEST:
// raw erase, raw program, verify, then every littlefs step with its
// error code. The answer names the layer that fails.
extern "C" void jdb_pico_fs_selftest(char* out, int cap) {
    uint8_t pat[256];
    const uint8_t* rd = (const uint8_t*)(XIP_NOCACHE_NOALLOC_BASE + FS_OFFSET);
    uint8_t tx[2], rx[2];

    uint32_t ints = save_and_disable_interrupts();
    tx[0] = 0x15; tx[1] = 0;
    flash_do_cmd(tx, rx, 2);
    uint8_t sr3 = rx[1];
    // Global block unlock: with WPS the chip wakes with every block
    // locked, and this is the key that opens them all.
    tx[0] = 0x06;
    flash_do_cmd(tx, rx, 1);
    tx[0] = 0x98;
    flash_do_cmd(tx, rx, 1);
    restore_interrupts(ints);

    int erc = bd_erase(&g_cfg, 0);
    for (int i = 0; i < 256; i++) pat[i] = (uint8_t)(i * 7 + 3);
    int prc = bd_prog(&g_cfg, 0, 0, pat, 256);
    int prog_ok = 1;
    for (int i = 0; i < 256; i++) if (rd[i] != pat[i]) { prog_ok = 0; break; }

    int fmt = -99, mnt = -99;
    if (prog_ok) {
        fmt = lfs_format(&g_lfs, &g_cfg);
        mnt = lfs_mount(&g_lfs, &g_cfg);
        g_mounted = (mnt == 0);
    }
    snprintf(out, cap, "sr3=%02x erc=%d prc=%d prog=%d got=%02x %02x fmt=%d mnt=%d",
        sr3, erc, prc, prog_ok, rd[0], rd[1], fmt, mnt);
}

extern "C" void jdb_pico_alias_probe(char* out, int cap) {
    const uint8_t* cached   = (const uint8_t*)(XIP_BASE);
    const uint8_t* uncached = (const uint8_t*)(XIP_NOCACHE_NOALLOC_BASE);
    snprintf(out, cap, "cached=%02x %02x %02x %02x uncached=%02x %02x %02x %02x",
        cached[0], cached[1], cached[2], cached[3],
        uncached[0], uncached[1], uncached[2], uncached[3]);
}

// The QMI's address translation exists only on the RP2350; the RP2040
// runs flash straight, so the probe just says so.
#if PICO_RP2040
extern "C" void jdb_pico_atrans_probe(char* out, int cap) {
    snprintf(out, cap, "no atrans on RP2040");
}
#else
#include "hardware/structs/qmi.h"

extern "C" void jdb_pico_atrans_probe(char* out, int cap) {
    snprintf(out, cap, "atrans0=%08x 1=%08x 2=%08x 3=%08x",
        (unsigned)qmi_hw->atrans[0], (unsigned)qmi_hw->atrans[1],
        (unsigned)qmi_hw->atrans[2], (unsigned)qmi_hw->atrans[3]);
}
#endif

// What the flash store has left. littlefs counts the blocks in use, so
// the numbers move a block at a time rather than byte for byte.
extern "C" int jdb_pico_fs_free(unsigned* freebytes, unsigned* total) {
    lfs_ssize_t used = lfs_fs_size(&g_lfs);
    if (used < 0) return -1;
    *total = (unsigned)(g_cfg.block_count * g_cfg.block_size);
    unsigned in_use = (unsigned)used * g_cfg.block_size;
    *freebytes = in_use > *total ? 0 : *total - in_use;
    return 0;
}

#include "pico/bootrom.h"

// One-shot repair: erase the leftover partition table at the physical
// start of flash and drop to BOOTSEL. The next uf2 lands at zero and
// boots without address translation.
extern "C" void jdb_pico_nuke_pt(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(0x0, 8192);
    restore_interrupts(ints);
    reset_usb_boot(0, 0);
}

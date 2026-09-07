// The flash store. FATFS on a wear-levelled partition, mounted as the
// default filesystem: registering a VFS with an empty prefix makes it
// the fallback for every path that matches no other mount, so bare
// names work. IDF has no chdir, and without that a mount at /flash
// would leave the interpreter's own opens - IMPORT, TXTREADER$, OPEN -
// unable to find anything.

#include <stdio.h>
#include <string.h>
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ff.h"

#include "../../../src/vm.h"

static wl_handle_t s_wl = WL_INVALID_HANDLE;
static bool s_mounted = false;

// A store that will not mount is left as it is; FS.FORMAT("ERASE") is
// the way to a new one.
extern "C" bool esp32_fs_init(void) {
    esp_vfs_fat_mount_config_t cfg = {};
    cfg.max_files = 8;
    cfg.format_if_mount_failed = false;
    cfg.allocation_unit_size = CONFIG_WL_SECTOR_SIZE;

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("", "storage", &cfg, &s_wl);
    s_mounted = (err == ESP_OK);
    return s_mounted;
}

extern "C" bool esp32_fs_mounted(void) { return s_mounted; }

// FS.FORMAT("ERASE"): a new, empty store. Every file is gone.
extern "C" int esp32_fs_format(void) {
    esp_err_t err = esp_vfs_fat_spiflash_format_rw_wl("", "storage");
    if (err == ESP_OK && !s_mounted) esp32_fs_init();
    return (int)err;
}

// Blocks total and free, in bytes. FATFS counts in clusters, so the
// numbers move in cluster steps rather than byte for byte.
bool esp32_fs_space(uint64_t* total, uint64_t* freebytes) {
    FATFS* fs;
    DWORD free_clusters;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK) return false;
    uint64_t sector = CONFIG_WL_SECTOR_SIZE;
    *total = (uint64_t)(fs->n_fatent - 2) * fs->csize * sector;
    *freebytes = (uint64_t)free_clusters * fs->csize * sector;
    return true;
}

void register_esp32_fs(VM& vm) {
    // FS.FORMAT("ERASE") makes a new, empty store; anything else only
    // reports.
    vm.register_native("FS.FORMAT", 0, 1, [](const std::vector<Value>& args) -> Value {
        char buf[128];
        if (args.size() >= 1 && args[0].to_string() == "ERASE") {
            int rc = esp32_fs_format();
            snprintf(buf, sizeof buf, "formatted: rc=%d mounted=%d, the store is empty",
                     rc, s_mounted ? 1 : 0);
        } else {
            snprintf(buf, sizeof buf, "mounted=%d - FS.FORMAT(\"ERASE\") makes a new, empty store",
                     s_mounted ? 1 : 0);
        }
        return Value::make_string(buf);
    });
    vm.register_native("SYS.DF", 0, 0, [](const std::vector<Value>&) -> Value {
        uint64_t total = 0, avail = 0;
        if (!esp32_fs_space(&total, &avail)) return Value::make_string("no filesystem");
        char buf[96];
        snprintf(buf, sizeof buf, "flash %u free of %u bytes",
                 (unsigned)avail, (unsigned)total);
        return Value::make_string(buf);
    });

    vm.register_native("SYS.FREEDISK", 0, 0, [](const std::vector<Value>&) -> Value {
        uint64_t total = 0, avail = 0;
        if (!esp32_fs_space(&total, &avail)) return Value::make_i64(0);
        return Value::make_i64((int64_t)avail);
    });
}

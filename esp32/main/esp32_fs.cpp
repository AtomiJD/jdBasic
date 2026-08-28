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

#include "../../src/vm.h"

static wl_handle_t s_wl = WL_INVALID_HANDLE;

extern "C" bool esp32_fs_init(void) {
    esp_vfs_fat_mount_config_t cfg = {};
    cfg.max_files = 8;
    cfg.format_if_mount_failed = true;
    cfg.allocation_unit_size = CONFIG_WL_SECTOR_SIZE;

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("", "storage", &cfg, &s_wl);
    return err == ESP_OK;
}

// Blocks total and free, in bytes. FATFS counts in clusters, so the
// numbers move in cluster steps rather than byte for byte.
static bool fs_space(uint64_t* total, uint64_t* freebytes) {
    FATFS* fs;
    DWORD free_clusters;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK) return false;
    uint64_t sector = CONFIG_WL_SECTOR_SIZE;
    *total = (uint64_t)(fs->n_fatent - 2) * fs->csize * sector;
    *freebytes = (uint64_t)free_clusters * fs->csize * sector;
    return true;
}

void register_esp32_fs(VM& vm) {
    vm.register_native("SYS.DF", 0, 0, [](const std::vector<Value>&) -> Value {
        uint64_t total = 0, avail = 0;
        if (!fs_space(&total, &avail)) return Value::make_string("no filesystem");
        char buf[96];
        snprintf(buf, sizeof buf, "flash %u free of %u bytes",
                 (unsigned)avail, (unsigned)total);
        return Value::make_string(buf);
    });

    vm.register_native("SYS.FREEDISK", 0, 0, [](const std::vector<Value>&) -> Value {
        uint64_t total = 0, avail = 0;
        if (!fs_space(&total, &avail)) return Value::make_i64(0);
        return Value::make_i64((int64_t)avail);
    });
}

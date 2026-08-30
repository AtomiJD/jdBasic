// The TF card slot, on SDIO rather than SPI: four data lines instead of
// one, which the board wires for and the manual points out is the faster
// of the two.
//
// It mounts at /sd. The flash store owns the empty prefix and is the
// fallback for every path that matches no other mount, so a bare name
// still means flash and only a path that says /sd goes to the card.

#include <string.h>
#include <stdio.h>
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"

#define PIN_CLK  38
#define PIN_CMD  40
#define PIN_D0   39
#define PIN_D1   41
#define PIN_D2   48
#define PIN_D3   47

#define SD_ROOT  "/sd"

static sdmmc_card_t* g_card;

int es3c28p_sd_mounted(void) { return g_card != NULL; }

int es3c28p_sd_mount(void) {
    if (g_card) return 0;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = PIN_CLK;
    slot.cmd = PIN_CMD;
    slot.d0 = PIN_D0;
    slot.d1 = PIN_D1;
    slot.d2 = PIN_D2;
    slot.d3 = PIN_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t cfg = {0};
    // A card that will not mount is a card to look at, not one to erase.
    cfg.format_if_mount_failed = false;
    cfg.max_files = 4;
    cfg.allocation_unit_size = 16 * 1024;

    esp_err_t e = esp_vfs_fat_sdmmc_mount(SD_ROOT, &host, &slot, &cfg, &g_card);
    if (e != ESP_OK) {
        g_card = NULL;
        return -(int)e;
    }
    return 0;
}

int es3c28p_sd_unmount(void) {
    if (!g_card) return 0;
    esp_err_t e = esp_vfs_fat_sdmmc_unmount();
    g_card = NULL;
    return e == ESP_OK ? 0 : -(int)e;
}

// Name, megabytes, and the bus width actually negotiated - four lines is
// what this slot is wired for, but a card may end up on one.
int es3c28p_sd_info(char* name, int cap, int* mb, int* width) {
    if (name && cap > 0) name[0] = 0;
    *mb = *width = 0;
    if (!g_card) return -1;
    if (name && cap > 0) snprintf(name, cap, "%s", g_card->cid.name);
    uint64_t bytes = (uint64_t)g_card->csd.capacity * g_card->csd.sector_size;
    *mb = (int)(bytes / (1024 * 1024));
    *width = g_card->log_bus_width ? (1 << g_card->log_bus_width) : 1;
    return 0;
}

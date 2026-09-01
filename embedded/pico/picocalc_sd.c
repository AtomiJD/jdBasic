// The SD slot: a classic SPI card driver under ChaN's FatFS, wrapped
// so the syscall layer never sees FatFS types. Cards stay readable on
// any PC. Pins are the PicoCalc's: SPI0 with CS on GP17.

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"

// Same driver, two boards, different corners of the chip.
#ifdef FRUITJAM
#define SD_SPI    spi0
#define PIN_SCK   34
#define PIN_MOSI  35
#define PIN_MISO  36
#define PIN_CS    39
#define PIN_DETECT 33
#else
#define SD_SPI    spi0
#define PIN_MISO  16
#define PIN_CS    17
#define PIN_SCK   18
#define PIN_MOSI  19
#endif

#define SPI_SLOW  400000
#define SPI_FAST  12500000

static bool g_sd_hc = false;
static bool g_sd_ready = false;

static void cs_low(void)  { gpio_put(PIN_CS, 0); }
static void cs_high(void) { gpio_put(PIN_CS, 1); }

static uint8_t xfer(uint8_t out) {
    uint8_t in;
    spi_write_read_blocking(SD_SPI, &out, &in, 1);
    return in;
}

static uint8_t sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    // Eight clocks of quiet before every command: the card needs its
    // Ncr gap, and without it responses arrive shifted mid-byte.
    xfer(0xFF);
    uint8_t buf[6] = {
        (uint8_t)(0x40 | cmd),
        (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
        (uint8_t)(arg >> 8), (uint8_t)arg, crc
    };
    spi_write_blocking(SD_SPI, buf, 6);
    for (int i = 0; i < 16; i++) {
        uint8_t r = xfer(0xFF);
        if (!(r & 0x80)) return r;
    }
    return 0xFF;
}

static int sd_init_card(void) {
    spi_init(SD_SPI, SPI_SLOW);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_pull_up(PIN_MISO);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, 1);
    cs_high();

    for (int i = 0; i < 10; i++) xfer(0xFF);

    cs_low();
    uint8_t r = sd_cmd(0, 0, 0x95);
    if (r != 0x01) { cs_high(); return -1; }

    r = sd_cmd(8, 0x1AA, 0x87);
    int v2 = (r == 0x01);
    if (v2) for (int i = 0; i < 4; i++) xfer(0xFF);

    for (int i = 0; i < 2000; i++) {
        sd_cmd(55, 0, 0xFF);
        r = sd_cmd(41, v2 ? 0x40000000 : 0, 0xFF);
        if (r == 0x00) break;
        sleep_ms(1);
    }
    if (r != 0x00) { cs_high(); return -1; }

    g_sd_hc = false;
    if (v2) {
        r = sd_cmd(58, 0, 0xFF);
        if (r == 0x00) {
            uint8_t ocr[4];
            for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
            g_sd_hc = (ocr[0] & 0x40) != 0;
        }
    }
    cs_high();
    xfer(0xFF);
    spi_set_baudrate(SD_SPI, SPI_FAST);
    return 0;
}

static int sd_read_block(uint32_t sector, uint8_t* dst) {
    uint32_t addr = g_sd_hc ? sector : sector * 512;
    cs_low();
    if (sd_cmd(17, addr, 0xFF) != 0x00) { cs_high(); return -1; }
    int ok = 0;
    for (int i = 0; i < 200000; i++) {
        if (xfer(0xFF) == 0xFE) { ok = 1; break; }
    }
    if (!ok) { cs_high(); return -1; }
    for (int i = 0; i < 512; i++) dst[i] = xfer(0xFF);
    xfer(0xFF); xfer(0xFF);
    cs_high();
    xfer(0xFF);
    return 0;
}

static int sd_write_block(uint32_t sector, const uint8_t* src) {
    uint32_t addr = g_sd_hc ? sector : sector * 512;
    cs_low();
    if (sd_cmd(24, addr, 0xFF) != 0x00) { cs_high(); return -1; }
    xfer(0xFF);
    xfer(0xFE);
    spi_write_blocking(SD_SPI, src, 512);
    xfer(0xFF); xfer(0xFF);
    uint8_t r = xfer(0xFF);
    if ((r & 0x1F) != 0x05) { cs_high(); return -1; }
    for (int i = 0; i < 500000; i++) {
        if (xfer(0xFF) == 0xFF) break;
    }
    cs_high();
    xfer(0xFF);
    return 0;
}

// ── FatFS glue ────────────────────────────────────────────────────────

DSTATUS disk_status(BYTE drv) {
    (void)drv;
    return g_sd_ready ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE drv) {
    (void)drv;
    g_sd_ready = (sd_init_card() == 0);
    return g_sd_ready ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE drv, BYTE* buff, LBA_t sector, UINT count) {
    (void)drv;
    for (UINT i = 0; i < count; i++)
        if (sd_read_block((uint32_t)(sector + i), buff + i * 512) != 0)
            return RES_ERROR;
    return RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE* buff, LBA_t sector, UINT count) {
    (void)drv;
    for (UINT i = 0; i < count; i++)
        if (sd_write_block((uint32_t)(sector + i), buff + i * 512) != 0)
            return RES_ERROR;
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE cmd, void* buff) {
    (void)drv; (void)buff;
    if (cmd == CTRL_SYNC) return RES_OK;
    return RES_PARERR;
}

DWORD get_fattime(void) {
    // No clock on the board: a fixed, honest timestamp.
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

// ── The wrapper the syscall layer talks to ────────────────────────────

static FATFS g_fat;
static bool g_mounted_fat = false;

#define SD_MAX_FILES 4
static FIL g_fil[SD_MAX_FILES];
static bool g_fil_used[SD_MAX_FILES];

#define SD_MAX_DIRS 2
static DIR g_dir[SD_MAX_DIRS];
static bool g_dir_used[SD_MAX_DIRS];

int sd_mount(void) {
    if (g_mounted_fat) return 0;
    g_sd_ready = false;
    if (f_mount(&g_fat, "", 1) != FR_OK) return -1;
    g_mounted_fat = true;
    return 0;
}

int sd_open(const char* path, int write, int create, int truncate, int append) {
    if (sd_mount() != 0) return -1;
    int slot = -1;
    for (int i = 0; i < SD_MAX_FILES; i++) if (!g_fil_used[i]) { slot = i; break; }
    if (slot < 0) return -1;

    BYTE mode = write ? (FA_WRITE | FA_READ) : FA_READ;
    if (create)   mode |= truncate ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
    if (append)   mode |= FA_OPEN_APPEND;

    if (f_open(&g_fil[slot], path, mode) != FR_OK) return -1;
    g_fil_used[slot] = true;
    return slot;
}

int sd_read(int h, void* buf, int len) {
    UINT n = 0;
    if (h < 0 || h >= SD_MAX_FILES || !g_fil_used[h]) return -1;
    if (f_read(&g_fil[h], buf, (UINT)len, &n) != FR_OK) return -1;
    return (int)n;
}

int sd_write(int h, const void* buf, int len) {
    UINT n = 0;
    if (h < 0 || h >= SD_MAX_FILES || !g_fil_used[h]) return -1;
    if (f_write(&g_fil[h], buf, (UINT)len, &n) != FR_OK) return -1;
    return (int)n;
}

long sd_lseek(int h, long pos, int whence) {
    if (h < 0 || h >= SD_MAX_FILES || !g_fil_used[h]) return -1;
    FSIZE_t target;
    if (whence == 1) target = f_tell(&g_fil[h]) + pos;
    else if (whence == 2) target = f_size(&g_fil[h]) + pos;
    else target = (FSIZE_t)pos;
    if (f_lseek(&g_fil[h], target) != FR_OK) return -1;
    return (long)f_tell(&g_fil[h]);
}

long sd_size(int h) {
    if (h < 0 || h >= SD_MAX_FILES || !g_fil_used[h]) return -1;
    return (long)f_size(&g_fil[h]);
}

int sd_close(int h) {
    if (h < 0 || h >= SD_MAX_FILES || !g_fil_used[h]) return -1;
    f_close(&g_fil[h]);
    g_fil_used[h] = false;
    return 0;
}

int sd_stat(const char* path, long* size, int* isdir) {
    if (sd_mount() != 0) return -1;
    if (path[0] == 0 || (path[0] == '/' && path[1] == 0)) {
        *size = 0; *isdir = 1;
        return 0;
    }
    FILINFO fi;
    if (f_stat(path, &fi) != FR_OK) return -1;
    *size = (long)fi.fsize;
    *isdir = (fi.fattrib & AM_DIR) ? 1 : 0;
    return 0;
}

int sd_unlink(const char* path) {
    if (sd_mount() != 0) return -1;
    return f_unlink(path) == FR_OK ? 0 : -1;
}

int sd_mkdir(const char* path) {
    if (sd_mount() != 0) return -1;
    return f_mkdir(path) == FR_OK ? 0 : -1;
}

int sd_rename(const char* oldp, const char* newp) {
    if (sd_mount() != 0) return -1;
    return f_rename(oldp, newp) == FR_OK ? 0 : -1;
}

int sd_opendir(const char* path) {
    if (sd_mount() != 0) return -1;
    int slot = -1;
    for (int i = 0; i < SD_MAX_DIRS; i++) if (!g_dir_used[i]) { slot = i; break; }
    if (slot < 0) return -1;
    if (f_opendir(&g_dir[slot], path[0] ? path : "/") != FR_OK) return -1;
    g_dir_used[slot] = true;
    return slot;
}

int sd_readdir(int h, char* name, int cap, int* isdir) {
    if (h < 0 || h >= SD_MAX_DIRS || !g_dir_used[h]) return -1;
    FILINFO fi;
    if (f_readdir(&g_dir[h], &fi) != FR_OK) return -1;
    if (fi.fname[0] == 0) return 0;
    strncpy(name, fi.fname, cap - 1);
    name[cap - 1] = 0;
    *isdir = (fi.fattrib & AM_DIR) ? 1 : 0;
    return 1;
}

int sd_closedir(int h) {
    if (h < 0 || h >= SD_MAX_DIRS || !g_dir_used[h]) return -1;
    f_closedir(&g_dir[h]);
    g_dir_used[h] = false;
    return 0;
}

// Step-by-step card diagnosis for the prompt: every init response and
// the mount code, so a failure names its stage.
void sd_selftest(char* out, int cap) {
    spi_init(SD_SPI, SPI_SLOW);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_pull_up(PIN_MISO);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, 1);
    cs_high();
    for (int i = 0; i < 10; i++) xfer(0xFF);

    cs_low();
    uint8_t r0 = sd_cmd(0, 0, 0x95);
    uint8_t r8 = sd_cmd(8, 0x1AA, 0x87);
    uint8_t echo[4] = {0, 0, 0, 0};
    if (r8 == 0x01) for (int i = 0; i < 4; i++) echo[i] = xfer(0xFF);

    uint8_t ra = 0xFF;
    int loops = 0;
    for (loops = 0; loops < 2000; loops++) {
        sd_cmd(55, 0, 0xFF);
        ra = sd_cmd(41, 0x40000000, 0xFF);
        if (ra == 0x00) break;
        sleep_ms(1);
    }
    uint8_t r58 = sd_cmd(58, 0, 0xFF);
    uint8_t ocr[4] = {0, 0, 0, 0};
    if (r58 == 0x00) for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
    cs_high();
    xfer(0xFF);

    g_sd_hc = (ocr[0] & 0x40) != 0;
    g_sd_ready = (ra == 0x00);
    spi_set_baudrate(SD_SPI, SPI_FAST);

    int mnt = -1;
    if (g_sd_ready) {
        g_mounted_fat = false;
        mnt = (int)f_mount(&g_fat, "", 1);
        g_mounted_fat = (mnt == 0);
    }

    snprintf(out, cap,
        "cmd0=%02x cmd8=%02x echo=%02x%02x%02x%02x acmd41=%02x loops=%d ocr=%02x hc=%d mnt=%d",
        r0, r8, echo[0], echo[1], echo[2], echo[3], ra, loops, ocr[0], g_sd_hc ? 1 : 0, mnt);
}

// Bit-banged CMD0, no SPI peripheral involved: separates a hardware
// mystery from a driver one. Reports the sixteen bytes after the
// command, raw.
static uint8_t bb_byte(uint8_t out) {
    uint8_t in = 0;
    for (int b = 7; b >= 0; b--) {
        gpio_put(PIN_MOSI, (out >> b) & 1);
        sleep_us(3);
        gpio_put(PIN_SCK, 1);
        sleep_us(3);
        in = (uint8_t)((in << 1) | gpio_get(PIN_MISO));
        gpio_put(PIN_SCK, 0);
    }
    return in;
}

void sd_bitbang_test(char* out, int cap) {
    gpio_init(PIN_SCK);  gpio_set_dir(PIN_SCK, 1);  gpio_put(PIN_SCK, 0);
    gpio_init(PIN_MOSI); gpio_set_dir(PIN_MOSI, 1); gpio_put(PIN_MOSI, 1);
    gpio_init(PIN_CS);   gpio_set_dir(PIN_CS, 1);   gpio_put(PIN_CS, 1);
    gpio_init(PIN_MISO); gpio_set_dir(PIN_MISO, 0); gpio_pull_up(PIN_MISO);
    sleep_ms(10);

    for (int i = 0; i < 10; i++) bb_byte(0xFF);
    gpio_put(PIN_CS, 0);
    sleep_us(10);
    uint8_t cmd[6] = {0x40, 0, 0, 0, 0, 0x95};
    for (int i = 0; i < 6; i++) bb_byte(cmd[i]);
    uint8_t resp[16];
    for (int i = 0; i < 16; i++) resp[i] = bb_byte(0xFF);
    gpio_put(PIN_CS, 1);
    bb_byte(0xFF);

    int at = 0;
    for (int i = 0; i < 16 && at < cap - 4; i++)
        at += snprintf(out + at, cap - at, "%02x", resp[i]);
    out[at] = 0;
}

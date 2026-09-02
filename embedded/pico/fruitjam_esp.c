// The Fruit Jam's radio is an ESP32-C6 on SPI1 carrying Adafruit's nina
// firmware, the same one the AirLift boards use. That firmware runs the
// whole TCP/IP stack itself, so the host does not need lwIP here: it
// sends commands and gets answers.
//
// A command is E0, the command byte, a parameter count, then each
// parameter as a length and its bytes, then EE, padded to a multiple of
// four. The answer comes back in the same shape with the top bit of the
// command byte set. A busy line says when the chip will listen: it goes
// low when the chip is ready, and rises again once it has been selected.
//
// This file is the transport and the commands. The jdBasic verbs sit on
// top of it in fruitjam_wifi.cpp.

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>

#define ESP_SCK    30
#define ESP_MOSI   31
#define ESP_MISO   28
#define ESP_CS     46
#define ESP_BUSY    3
#define ESP_RESET  22

#define CMD_START  0xE0
#define CMD_END    0xEE
#define CMD_ERR    0xEF
#define REPLY_FLAG 0x80

#define ESP_TX 1600
#define ESP_RX 1600

void fruitjam_snd_codec_reinit(void);

static uint8_t g_tx[ESP_TX];
static uint8_t g_rx[ESP_RX];
static int g_up = 0;
static int g_khz = 8000;
static int g_err = 0;

static int g_reset_done = 0;

static void esp_bus_init(void) {
    if (g_up) return;
    g_up = 1;
    spi_init(spi1, g_khz * 1000);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(ESP_SCK, GPIO_FUNC_SPI);
    gpio_set_function(ESP_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(ESP_MISO, GPIO_FUNC_SPI);
    gpio_init(ESP_CS);
    gpio_set_dir(ESP_CS, true);
    gpio_put(ESP_CS, 1);
    gpio_init(ESP_BUSY);
    gpio_set_dir(ESP_BUSY, false);
}

void fruitjam_esp_reset(void) {
    esp_bus_init();
    gpio_put(ESP_CS, 1);
    gpio_init(ESP_RESET);
    gpio_set_dir(ESP_RESET, true);
    gpio_put(ESP_RESET, 0);
    sleep_ms(10);
    gpio_put(ESP_RESET, 1);
    // The codec sits on the same reset line. Its registers are all that
    // need programming again; the sound engine's PIO, DMA pair and
    // interrupt are untouched and must not be set up twice.
    sleep_ms(120);
    fruitjam_snd_codec_reinit();
    sleep_ms(700);
}

static int wait_busy(int want, int ms) {
    for (int i = 0; i < ms * 10; i++) {
        if ((gpio_get(ESP_BUSY) ? 1 : 0) == want) return 1;
        sleep_us(100);
    }
    return 0;
}

static uint8_t esp_byte(void) {
    uint8_t b = 0;
    spi_read_blocking(spi1, 0x00, &b, 1);
    return b;
}

// Errors are reported as a code rather than a message: this runs under
// a REPL where the caller decides what to say.
enum {
    ESP_OK = 0,
    ESP_E_BUSY = 1,     // the chip never became ready
    ESP_E_SELECT = 2,   // it did not take the selection
    ESP_E_START = 3,    // no start byte in the answer
    ESP_E_CMD = 4,      // the answer was for a different command
    ESP_E_ERR = 5,      // the chip answered with its error byte
    ESP_E_END = 6,      // the answer did not finish
    ESP_E_SPACE = 7     // the answer did not fit
};

int fruitjam_esp_error(void) { return g_err; }

// One command out. Parameters are pairs of pointer and length; a
// sixteen bit length is what the bulk data commands want.
static int esp_send(uint8_t cmd, const uint8_t** par, const uint16_t* len,
                    int n, int len16) {
    int p = 0;
    g_tx[p++] = CMD_START;
    g_tx[p++] = cmd & ~REPLY_FLAG;
    g_tx[p++] = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        if (p + len[i] + 3 > ESP_TX) return (g_err = ESP_E_SPACE);
        if (len16) g_tx[p++] = (uint8_t)(len[i] >> 8);
        g_tx[p++] = (uint8_t)(len[i] & 0xFF);
        memcpy(g_tx + p, par[i], len[i]);
        p += len[i];
    }
    g_tx[p++] = CMD_END;
    while (p % 4) g_tx[p++] = 0;

    if (!wait_busy(0, 10000)) return (g_err = ESP_E_BUSY);
    gpio_put(ESP_CS, 0);
    if (!wait_busy(1, 1000)) {
        gpio_put(ESP_CS, 1);
        return (g_err = ESP_E_SELECT);
    }
    spi_write_blocking(spi1, g_tx, p);
    gpio_put(ESP_CS, 1);
    return (g_err = ESP_OK);
}

// The answer, with each parameter left in the receive buffer and only
// its offset and length handed back.
static int esp_recv(uint8_t cmd, uint16_t* off, uint16_t* len, int n_max,
                    int* n_got, int len16) {
    *n_got = 0;
    if (!wait_busy(0, 10000)) return (g_err = ESP_E_BUSY);
    gpio_put(ESP_CS, 0);
    if (!wait_busy(1, 1000)) {
        gpio_put(ESP_CS, 1);
        return (g_err = ESP_E_SELECT);
    }

    int rc = ESP_OK;
    int found = 0;
    for (int i = 0; i < 24; i++) {
        uint8_t b = esp_byte();
        if (b == CMD_ERR) { rc = ESP_E_ERR; break; }
        if (b == CMD_START) { found = 1; break; }
    }
    if (rc == ESP_OK && !found) rc = ESP_E_START;

    if (rc == ESP_OK && esp_byte() != (cmd | REPLY_FLAG)) rc = ESP_E_CMD;

    if (rc == ESP_OK) {
        int n = esp_byte();
        if (n > n_max) n = n_max;
        unsigned pos = 0;
        for (int i = 0; i < n && rc == ESP_OK; i++) {
            unsigned l = esp_byte();
            if (len16) l = (l << 8) | esp_byte();
            if (pos + l > ESP_RX) { rc = ESP_E_SPACE; break; }
            if (l) spi_read_blocking(spi1, 0x00, g_rx + pos, l);
            off[i] = (uint16_t)pos;
            len[i] = (uint16_t)l;
            pos += l;
            (*n_got)++;
        }
        if (rc == ESP_OK && esp_byte() != CMD_END) rc = ESP_E_END;
    }

    gpio_put(ESP_CS, 1);
    return (g_err = rc);
}

// The chip wants its select line held high while it comes out of reset,
// and at power-on that pin is still floating. So the first command of a
// session resets it once with the bus properly driven.
static void esp_cold_start(void) {
    esp_bus_init();
    if (g_reset_done) return;
    g_reset_done = 1;
    fruitjam_esp_reset();
}

int fruitjam_esp_call(int cmd, const uint8_t** par, const uint16_t* len, int n,
                      uint16_t* off, uint16_t* rlen, int n_max, int* n_got,
                      int len16_in, int len16_out) {
    esp_cold_start();
    int rc = esp_send((uint8_t)cmd, par, len, n, len16_in);
    if (rc != ESP_OK) return rc;
    return esp_recv((uint8_t)cmd, off, rlen, n_max, n_got, len16_out);
}

const uint8_t* fruitjam_esp_data(void) { return g_rx; }

// A single byte answer, which is what most of the status commands are.
int fruitjam_esp_call_u8(int cmd, const uint8_t** par, const uint16_t* len, int n) {
    uint16_t off[1], rlen[1];
    int got = 0;
    if (fruitjam_esp_call(cmd, par, len, n, off, rlen, 1, &got, 0, 0) != ESP_OK)
        return -1;
    if (got < 1 || rlen[0] < 1) return -1;
    return g_rx[off[0]];
}

// ── diagnostics ──────────────────────────────────────────────────────

#define CMD_GET_FW_VERSION 0x37

int fruitjam_esp_fw(char* out, int cap) {
    uint16_t off[1], rlen[1];
    int got = 0;
    if (fruitjam_esp_call(CMD_GET_FW_VERSION, NULL, NULL, 0,
                          off, rlen, 1, &got, 0, 0) != ESP_OK || got < 1)
        return snprintf(out, cap, "");
    int n = rlen[0];
    if (n > cap - 1) n = cap - 1;
    memcpy(out, g_rx + off[0], n);
    out[n] = 0;
    // The version comes back with its terminator included.
    while (n > 0 && (unsigned char)out[n - 1] < 0x20) out[--n] = 0;
    return n;
}

int fruitjam_esp_pins(char* out, int cap) {
    esp_bus_init();
    return snprintf(out, cap, "busy=%d khz=%d err=%d",
                    gpio_get(ESP_BUSY) ? 1 : 0, g_khz, g_err);
}

int fruitjam_esp_probe(char* out, int cap, int do_reset, int khz) {
    if (khz > 0) {
        g_khz = khz;
        g_up = 0;
    }
    esp_cold_start();
    if (do_reset) fruitjam_esp_reset();

    char fw[40];
    fruitjam_esp_fw(fw, sizeof fw);
    if (fw[0])
        return snprintf(out, cap, "nina firmware %s at %d kHz", fw, g_khz);
    return snprintf(out, cap, "no answer (busy=%d err=%d)",
                    gpio_get(ESP_BUSY) ? 1 : 0, g_err);
}

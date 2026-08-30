// Sound: an ES8311 codec at 0x18 fed over I2S, into an amplifier whose
// enable line is active low. The three functions at the bottom are the
// ones the PicoCalc's score engine calls, so PLAY, BEEP and TONE mean
// the same on both boards - only what makes the air move differs.
//
// The codec's own driver does the register work. Its clock setup is a
// lookup over a coefficient table, which is the last thing worth
// transcribing by hand.

#include <math.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "es8311.h"

#define PIN_EN    1        // low enables the amplifier
#define PIN_MCLK  4
#define PIN_BCLK  5
// The board names these I2S_DO and I2S_DI without saying from whose
// point of view. Measured, not argued: the codec hears IO8 and speaks on
// IO6, so the board names them from the codec end.
#define PIN_DOUT  8
#define PIN_LRCK  7
#define PIN_DIN   6

#define SAMPLE_HZ   16000
#define MCLK_MULT   256
#define CHUNK       256    // frames per write, 16 ms at this rate

extern int es3c28p_i2c_up(void);

static i2s_chan_handle_t g_tx;
static es8311_handle_t g_codec;
static int g_ready = 0;

static volatile int g_freq = 0;      // 0 is silence
static volatile int g_vol = 70;
static volatile int64_t g_until = 0; // 0 means until told otherwise
static TaskHandle_t g_task;

static int64_t now_ms(void) {
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

// A square wave, because that is what a beeper sounds like and what the
// score engine was written against. The phase carries across chunks so
// a held note does not click every sixteen milliseconds.
static void feed(void* arg) {
    (void)arg;
    static int16_t buf[CHUNK * 2];
    double phase = 0.0;

    for (;;) {
        int f = g_freq;
        int64_t until = g_until;
        if (f > 0 && until != 0 && now_ms() > until) { g_freq = 0; f = 0; }

        if (f <= 0) {
            memset(buf, 0, sizeof buf);
            phase = 0.0;
        } else {
            double step = (double)f / SAMPLE_HZ;
            int16_t hi = (int16_t)(8000 * g_vol / 100);
            for (int i = 0; i < CHUNK; i++) {
                int16_t s = (phase < 0.5) ? hi : (int16_t)(-hi);
                buf[i * 2] = s;
                buf[i * 2 + 1] = s;
                phase += step;
                if (phase >= 1.0) phase -= 1.0;
            }
        }
        size_t wrote = 0;
        i2s_channel_write(g_tx, buf, sizeof buf, &wrote, portMAX_DELAY);
    }
}

static int snd_init(void) {
    if (g_ready) return 0;

    gpio_config_t io = {0};
    io.pin_bit_mask = (1ULL << PIN_EN);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)PIN_EN, 1);      // amplifier off

    if (es3c28p_i2c_up() != 0) return -1;

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 4;
    cc.dma_frame_num = CHUNK;
    if (i2s_new_channel(&cc, &g_tx, NULL) != ESP_OK) return -2;

    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_MCLK,
            .bclk = PIN_BCLK,
            .ws   = PIN_LRCK,
            .dout = PIN_DOUT,
            .din  = PIN_DIN,
            .invert_flags = { false, false, false },
        },
    };
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (i2s_channel_init_std_mode(g_tx, &sc) != ESP_OK) return -3;
    if (i2s_channel_enable(g_tx) != ESP_OK) return -4;

    g_codec = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
    if (!g_codec) return -5;

    es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = SAMPLE_HZ * MCLK_MULT,
        .sample_frequency = SAMPLE_HZ,
    };
    if (es8311_init(g_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK)
        return -6;
    es8311_voice_mute(g_codec, false);
    es8311_voice_volume_set(g_codec, g_vol, NULL);

    // The amplifier stays enabled once the codec is up. Toggling it per
    // note saves current but costs the attack of every short beep, and a
    // beep is mostly attack.
    gpio_set_level((gpio_num_t)PIN_EN, 0);

    xTaskCreate(feed, "snd", 3072, NULL, 5, &g_task);
    g_ready = 1;
    return 0;
}

// The three the score engine calls. Names and meaning are the RP2350's.

void picocalc_snd_volume(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    g_vol = pct;
    if (g_ready) es8311_voice_volume_set(g_codec, pct, NULL);
}

// The enable line is asserted on every note, not once at startup. It is
// an ordinary GPIO, so anything else that writes it - a program, a probe
// - would otherwise leave the board silently muted until the next reset.
void picocalc_snd_tone(int freq) {
    if (snd_init() != 0) return;
    gpio_set_level((gpio_num_t)PIN_EN, 0);
    g_until = 0;
    g_freq = freq > 0 ? freq : 0;
}

void picocalc_snd_beep(int freq, int ms) {
    if (snd_init() != 0) return;
    gpio_set_level((gpio_num_t)PIN_EN, 0);
    if (ms <= 0) ms = 1;
    g_freq = freq > 0 ? freq : 0;
    g_until = now_ms() + ms;
}

int es3c28p_snd_ready(void) { return g_ready; }

int es3c28p_snd_start(void) { return snd_init(); }

// The melody engine's timer and lock. A one-shot esp_timer ends the note
// and a spinlock guards the hand-over, which is the same shape as the
// RP2350's alarm and interrupt mask - only the words differ.

extern void jdb_snd_note_due(void);

static esp_timer_handle_t g_note_timer;
static portMUX_TYPE g_note_lock = portMUX_INITIALIZER_UNLOCKED;

static void note_timer_cb(void* arg) {
    (void)arg;
    jdb_snd_note_due();
}

void jdb_snd_timer_start(int ms) {
    if (!g_note_timer) {
        esp_timer_create_args_t a = {0};
        a.callback = note_timer_cb;
        a.name = "note";
        if (esp_timer_create(&a, &g_note_timer) != ESP_OK) return;
    }
    esp_timer_stop(g_note_timer);
    esp_timer_start_once(g_note_timer, (uint64_t)(ms > 0 ? ms : 1) * 1000);
}

void jdb_snd_timer_cancel(void) {
    if (g_note_timer) esp_timer_stop(g_note_timer);
}

uint32_t jdb_snd_lock(void) {
    portENTER_CRITICAL(&g_note_lock);
    return 0;
}

void jdb_snd_unlock(uint32_t saved) {
    (void)saved;
    portEXIT_CRITICAL(&g_note_lock);
}

// The speaker: a square wave out of the PWM slice behind GP26 and
// GP27. jdb_snd_out_tone holds a pitch until it is changed or
// silenced; BEEP is that plus a wait. Volume rides on the duty cycle,
// full swing at 50 percent.

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#define SND_L 26
#define SND_R 27

static int g_volume = 60;
static unsigned g_slice;
static uint16_t g_wrap;
static int g_tone_on = 0;

static void snd_apply_level(void) {
    uint32_t lvl = (uint32_t)g_wrap * g_volume / 200;
    pwm_set_gpio_level(SND_L, (uint16_t)lvl);
    pwm_set_gpio_level(SND_R, (uint16_t)lvl);
}

void jdb_snd_out_volume(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    g_volume = pct;
    if (g_tone_on) snd_apply_level();
}

void jdb_snd_out_tone(int freq) {
    if (freq < 20 || freq > 20000) {
        if (g_tone_on) {
            pwm_set_gpio_level(SND_L, 0);
            pwm_set_gpio_level(SND_R, 0);
            pwm_set_enabled(g_slice, false);
            g_tone_on = 0;
        }
        return;
    }
    gpio_set_function(SND_L, GPIO_FUNC_PWM);
    gpio_set_function(SND_R, GPIO_FUNC_PWM);
    g_slice = pwm_gpio_to_slice_num(SND_L);

    uint32_t clk = clock_get_hz(clk_sys);
    uint32_t div = 1;
    uint32_t wrap = clk / freq;
    while (wrap / div > 65535) div++;
    g_wrap = (uint16_t)(wrap / div);

    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&c, div);
    pwm_config_set_wrap(&c, g_wrap);
    pwm_init(g_slice, &c, true);
    g_tone_on = 1;
    snd_apply_level();
}

void jdb_snd_out_beep(int freq, int ms) {
    if (ms <= 0) return;
    jdb_snd_out_tone(freq);
    sleep_ms(ms);
    jdb_snd_out_tone(0);
}

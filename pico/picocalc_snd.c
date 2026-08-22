// The speaker: a square wave out of the PWM slice behind GP26 and
// GP27, held for the asked duration. BEEP-simple on purpose.

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#define SND_L 26
#define SND_R 27

void picocalc_snd_beep(int freq, int ms) {
    if (freq < 20 || freq > 20000 || ms <= 0) return;
    gpio_set_function(SND_L, GPIO_FUNC_PWM);
    gpio_set_function(SND_R, GPIO_FUNC_PWM);
    unsigned slice = pwm_gpio_to_slice_num(SND_L);

    uint32_t clk = clock_get_hz(clk_sys);
    uint32_t div = 1;
    uint32_t wrap = clk / freq;
    while (wrap / div > 65535) div++;
    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&c, div);
    pwm_config_set_wrap(&c, (uint16_t)(wrap / div));
    pwm_init(slice, &c, true);
    pwm_set_gpio_level(SND_L, (uint16_t)(wrap / div / 2));
    pwm_set_gpio_level(SND_R, (uint16_t)(wrap / div / 2));

    sleep_ms(ms);

    pwm_set_gpio_level(SND_L, 0);
    pwm_set_gpio_level(SND_R, 0);
    pwm_set_enabled(slice, false);
}

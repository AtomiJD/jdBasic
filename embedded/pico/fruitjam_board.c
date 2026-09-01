// The rest of what is soldered to the Fruit Jam: three buttons, five
// addressable LEDs and the infrared receiver.
//
// The buttons are plain inputs with pull-ups, pressed reads low. Button 1
// is also the BOOT button, which is only a boot function while the chip is
// coming up; afterwards it is an ordinary pin.
//
// The LEDs are WS2812, which wants a 1.25 microsecond bit cell with the
// ones and zeros told apart by pulse width. Bit-banging that would mean
// holding interrupts off long enough to disturb the scanout, so it runs
// on a state machine instead - PIO 2, which nothing else here uses: the
// USB host owns PIO 0 and the sound owns a slice of PIO 1.

#include <stdint.h>
#include <string.h>

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/psram.h"

unsigned fruitjam_psram_reserved(void);

#define BTN1_PIN 0
#define BTN2_PIN 4
#define BTN3_PIN 5

#define NEO_PIN    32
#define NEO_COUNT  5

// The receiver's output. It shares the pin the board header also calls the
// LED, so the two cannot both be had.
#define IR_PIN 29

static uint32_t g_pixel[NEO_COUNT];
static PIO      g_neo_pio = NULL;
static int      g_neo_sm = -1;

// From the SDK example, already assembled: shift a bit out, then hold the
// line high for a long or a short time depending on what it was.
static const uint16_t ws2812_prog[] = {
    0x6321, //  0: out    x, 1            side 0 [3]
    0x1223, //  1: jmp    !x, 3           side 1 [2]
    0x1200, //  2: jmp    0               side 1 [2]
    0xa242, //  3: nop                    side 0 [2]
};

static const struct pio_program ws2812_program = {
    .instructions = ws2812_prog,
    .length = 4,
    .origin = -1,
};

int fruitjam_button(int n) {
    int pin;
    switch (n) {
        case 1: pin = BTN1_PIN; break;
        case 2: pin = BTN2_PIN; break;
        case 3: pin = BTN3_PIN; break;
        default: return 0;
    }
    return gpio_get(pin) ? 0 : 1;      // pulled up, so pressed reads low
}

int fruitjam_button_count(void) { return 3; }

// Eight megabytes on QMI chip select 1, mapped rather than reached over
// SPI: the SDK's runtime init sets it up from the board header, so the
// window is simply there to be read and written. It is not part of the
// heap - malloc still hands out SRAM - so this says what the machine
// has, not what a program can ask for yet.
#define PSRAM_BASE 0x11000000u

unsigned fruitjam_psram_size(void) {
    return psram_is_available() ? (unsigned)psram_get_size() : 0;
}

// A pattern at each end, read back. What the chip does rather than what
// the board header says it should.
int fruitjam_psram_check(char* out, int cap) {
    if (!psram_is_available()) return snprintf(out, cap, "no psram");
    size_t words = psram_get_size() / 4;
    volatile uint32_t* p = (volatile uint32_t*)PSRAM_BASE;
    // Past the scanout's reservation and past the pool's list head:
    // a probe that scribbles on either takes the picture or the heap
    // with it.
    size_t low = fruitjam_psram_reserved() / 4 + 1024;
    const uint32_t first = 0xA5A5F00Du, last = 0x5A5A0FF0u;
    p[low] = first;
    p[words - 1] = last;
    uint32_t got_first = p[low], got_last = p[words - 1];
    return snprintf(out, cap, "%u KB at %08x, %s",
                    (unsigned)(psram_get_size() / 1024), PSRAM_BASE,
                    (got_first == first && got_last == last) ? "reads back" : "MISMATCH");
}

int fruitjam_ir_raw(void) { return gpio_get(IR_PIN) ? 1 : 0; }

// Colours go out green first, and the state machine wants them at the top
// of the word.
void fruitjam_neo_set(int index, int r, int g, int b) {
    if (index < 0 || index >= NEO_COUNT) return;
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    g_pixel[index] = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
}

void fruitjam_neo_show(void) {
    if (!g_neo_pio) return;
    for (int i = 0; i < NEO_COUNT; i++)
        pio_sm_put_blocking(g_neo_pio, g_neo_sm, g_pixel[i] << 8u);
    // The strip latches on a gap; anything past 50 microseconds will do.
    sleep_us(300);
}

void fruitjam_neo_clear(void) {
    memset(g_pixel, 0, sizeof g_pixel);
    fruitjam_neo_show();
}

int fruitjam_neo_count(void) { return NEO_COUNT; }

void fruitjam_board_init(void) {
    static const int btn[3] = { BTN1_PIN, BTN2_PIN, BTN3_PIN };
    for (int i = 0; i < 3; i++) {
        gpio_init(btn[i]);
        gpio_set_dir(btn[i], false);
        gpio_pull_up(btn[i]);
    }
    gpio_init(IR_PIN);
    gpio_set_dir(IR_PIN, false);
    gpio_pull_up(IR_PIN);

    g_neo_pio = pio2;
    // A PIO instance sees only 32 consecutive pins, and the LEDs sit at
    // 32. Without moving the window - and without PICO_PIO_USE_GPIO_BASE
    // to make the SDK honour the high bit - the pin number would quietly
    // lose bit 5 and the data would go to GP0 instead.
    pio_set_gpio_base(g_neo_pio, 16);
    g_neo_sm = pio_claim_unused_sm(g_neo_pio, true);
    uint off = pio_add_program(g_neo_pio, &ws2812_program);

    pio_gpio_init(g_neo_pio, NEO_PIN);
    pio_sm_set_consecutive_pindirs(g_neo_pio, g_neo_sm, NEO_PIN, 1, true);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, off, off + 3);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, NEO_PIN);
    sm_config_set_out_shift(&c, false, true, 24);   // MSB first, autopull
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    // Ten cycles a bit at 800 kHz.
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 8000000.0f);
    pio_sm_init(g_neo_pio, g_neo_sm, off, &c);
    pio_sm_set_enabled(g_neo_pio, g_neo_sm, true);

    fruitjam_neo_clear();
}

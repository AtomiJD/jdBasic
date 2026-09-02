// Sound on the Fruit Jam, which has no beeper - everything goes through a
// TLV320DAC3100 codec over I2S.
//
// The melody engine above this is board independent; it only ever asks for
// a frequency and a volume. So this file is the whole difference between
// the PicoCalc's PWM speaker and this board: set the codec up over I2C,
// shift samples out of a PIO state machine, and keep a square wave running
// at whatever frequency was last asked for.
//
// Two buffers ping-pong under DMA and an interrupt refills whichever one
// just drained, so the phase runs on unbroken across the seam. A single
// looping buffer would click once per lap wherever the loop point fell
// mid-period.
//
// The codec's PLL runs off the bit clock rather than a separate master
// clock. That is the lower-quality option - the reference port warns that
// the PLL never quite locks this way and leaves some hiss - but a master
// clock would have to be exactly 15 MHz, and 126 MHz divides no nearer
// than 15.04. For square waves out of a BASIC it is far more than enough.

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/structs/sio.h"

#define SND_I2C        i2c0
#define SND_SDA        20
#define SND_SCL        21
#define SND_RESET_PIN  22
#define CODEC_ADDR     0x18

#define I2S_DIN        24
#define I2S_BCLK       26      // WS is the next pin up, which side-set needs
#define SAMPLE_RATE    44100

// Six PIO cycles a bit, thirty-two bits a stereo frame.
#define PIO_CYCLES_PER_FRAME 192

#define BUF_FRAMES 256

static int16_t  g_buf[2][BUF_FRAMES * 2];
static int      g_dma[2] = { -1, -1 };
static PIO      g_pio = NULL;
static int      g_sm = -1;
static int      g_active = 0;
static bool     g_up = false;

static volatile uint32_t g_step = 0;   // phase increment, 16.16
static volatile int      g_amp  = 0;   // 0 when silent
static uint32_t g_phase = 0;
static volatile uint32_t g_irqs = 0;
static int      g_volume = 20;

// The instructions come straight from the reference port, already
// assembled: pull a frame, clock out sixteen bits with the word select
// low, then sixteen with it high.
static const uint16_t i2s_prog[] = {
    0x9880, //  0: pull   noblock         side 3
    0xb827, //  1: mov    x, osr          side 3
    0xf84e, //  2: set    y, 14           side 3
    0x7201, //  3: out    pins, 1         side 2 [2]
    0x1a83, //  4: jmp    y--, 3          side 3 [2]
    0x6201, //  5: out    pins, 1         side 0 [2]
    0xea4e, //  6: set    y, 14           side 1 [2]
    0x6201, //  7: out    pins, 1         side 0 [2]
    0x0a87, //  8: jmp    y--, 7          side 1 [2]
    0x7201, //  9: out    pins, 1         side 2 [2]
};

static const struct pio_program i2s_program = {
    .instructions = i2s_prog,
    .length = 10,
    .origin = -1,
};

// --- the codec ---

static int g_i2c_fails = 0;

// Timed out rather than blocking: the codec shares its reset line with
// the radio, and a chip that is still coming up can hold the bus down.
// A blocking write there never returns and takes the board with it.
#define I2C_TIMEOUT_US 2000

static void wr(uint8_t page, uint8_t reg, uint8_t val) {
    uint8_t p[2] = { 0x00, page };
    if (i2c_write_timeout_us(SND_I2C, CODEC_ADDR, p, 2, false, I2C_TIMEOUT_US) < 0)
        g_i2c_fails++;
    uint8_t d[2] = { reg, val };
    if (i2c_write_timeout_us(SND_I2C, CODEC_ADDR, d, 2, false, I2C_TIMEOUT_US) < 0)
        g_i2c_fails++;
}

// Reading a register back is the only way to know the codec is listening
// at all; a silent chip and a wrongly routed one look identical.
static int rd(uint8_t page, uint8_t reg) {
    uint8_t p[2] = { 0x00, page };
    if (i2c_write_timeout_us(SND_I2C, CODEC_ADDR, p, 2, false, I2C_TIMEOUT_US) < 0) return -1;
    if (i2c_write_timeout_us(SND_I2C, CODEC_ADDR, &reg, 1, true, I2C_TIMEOUT_US) < 0) return -2;
    uint8_t v = 0;
    if (i2c_read_timeout_us(SND_I2C, CODEC_ADDR, &v, 1, false, I2C_TIMEOUT_US) < 0) return -3;
    return v;
}

// Headphone amp or the Class-D speaker amp; the routing bits pick one or
// the other, so this is a switch rather than a pair of enables.
static int g_out = 1;   // 1 = speaker, 0 = headphone

void jdb_snd_out_route(int speaker) {
    g_out = speaker ? 1 : 0;
    // Both paths run through the output mixer rather than straight into an
    // amplifier: that is what the reference driver does, and it is the
    // route that passes through the analog volume stage. Order matters as
    // much as the values - gain first, then power, then routing, then
    // level, and the mute comes off last.
    if (g_out) {
        wr(1, 0x1F, 0x04);              // headphone drivers down
        wr(1, 0x2A, 0x0C);              // speaker gain 12 dB, still muted
        wr(1, 0x20, 0x80);              // Class-D amplifier on
        wr(1, 0x23, 0x44);              // both DACs into the mixer
        wr(1, 0x26, 0x80 | 0x10);       // mixer to speaker, some attenuation
        wr(1, 0x2A, 0x0C | 0x04);       // unmute
    } else {
        wr(1, 0x20, 0x00);              // speaker amplifier off
        wr(1, 0x28, 0x00);              // HPL 0 dB, muted
        wr(1, 0x29, 0x00);
        wr(1, 0x1F, 0xD4);              // both HP drivers on, common 1.65V
        wr(1, 0x23, 0x44);              // both DACs into the mixer
        // Headphones sit on an ear, so they get the attenuation the
        // reference driver uses rather than the full swing a speaker gets.
        wr(1, 0x24, 0x80 | 0x28);
        wr(1, 0x25, 0x80 | 0x28);
        wr(1, 0x28, 0x04);              // unmute
        wr(1, 0x29, 0x04);
    }
    sleep_ms(20);
}

int jdb_snd_out_probe(char* out, int cap) {
    int p5 = rd(0, 0x05), p4 = rd(0, 0x04), p3f = rd(0, 0x3F);
    int r23 = rd(1, 0x23), r20 = rd(1, 0x20), r1f = rd(1, 0x1F);
    // The chip's own account of which blocks are actually running. The
    // DACs only report powered when they have a valid clock, so this is
    // the readout that says whether the PLL locked.
    int fl = rd(0, 0x25), f2 = rd(0, 0x26);
    int din = rd(0, 0x36), iface = rd(0, 0x1B);
    int lv = rd(0, 0x41), vc = rd(0, 0x40), spv = rd(1, 0x26), spd = rd(1, 0x2A);
    return snprintf(out, cap,
        "fails=%d pll=%02x mux=%02x dac=%02x rt=%02x spk=%02x hp=%02x "
        "flag=%02x/%02x vol=%02x/%02x spkv=%02x spkd=%02x din=%02x if=%02x %s",
        g_i2c_fails, p5 & 0xFF, p4 & 0xFF, p3f & 0xFF,
        r23 & 0xFF, r20 & 0xFF, r1f & 0xFF,
        fl & 0xFF, f2 & 0xFF, vc & 0xFF, lv & 0xFF,
        spv & 0xFF, spd & 0xFF, din & 0xFF, iface & 0xFF, g_out ? "spk" : "hp");
}

// Whether the three I2S lines are actually moving. Sampling the pad
// input works even while the PIO drives them, and a clock that is running
// reads high about half the time.
int jdb_snd_out_pins(char* out, int cap) {
    int hi24 = 0, hi26 = 0, hi27 = 0, hi25 = 0;
    for (int i = 0; i < 2000; i++) {
        uint32_t g = sio_hw->gpio_in;
        if (g & (1u << 24)) hi24++;
        if (g & (1u << 25)) hi25++;
        if (g & (1u << 26)) hi26++;
        if (g & (1u << 27)) hi27++;
    }
    return snprintf(out, cap,
        "of 2000: din24=%d mclk25=%d bclk26=%d ws27=%d",
        hi24, hi25, hi26, hi27);
}

// The other half of the story: whether samples are actually leaving. If
// the interrupt count stands still, nothing is being clocked out at all
// and no amount of codec routing will help.
int jdb_snd_out_stat(char* out, int cap) {
    uint32_t sm_en = g_pio ? (g_pio->ctrl & (1u << g_sm)) : 0;
    uint32_t txlv = g_pio ? ((g_pio->flevel >> (g_sm * 8)) & 0x0F) : 0;
    uint32_t stall = g_pio ? (g_pio->fdebug & (1u << (24 + g_sm))) : 0;
    return snprintf(out, cap,
        "irqs=%lu sm=%d en=%lu txfifo=%lu stall=%lu amp=%d step=%lu dma=%d,%d",
        (unsigned long)g_irqs, g_sm, (unsigned long)(sm_en ? 1 : 0),
        (unsigned long)txlv, (unsigned long)(stall ? 1 : 0),
        g_amp, (unsigned long)g_step, g_dma[0], g_dma[1]);
}

// The datasheet is strict about the order here: everything off, program
// the dividers, then bring the PLL up and only afterwards hand its output
// to the codec.
static void codec_init(void) {
    // 44100 with the bit clock as PLL input, from the reference port's
    // solver: P=1 R=2 J=38 D=0, NDAC=19 MDAC=1 DOSR=128.
    const uint8_t p = 1, r = 2, j = 38;
    const uint16_t d = 0;
    const uint8_t ndac = 19, mdac = 1;
    const uint16_t dosr = 128;

    wr(0, 0x01, 0x01);              // software reset
    sleep_ms(10);

    wr(0, 0x3F, 0x00);              // DAC powered down
    wr(0, 0x05, (uint8_t)(((p & 7) << 4) | (r & 0x0F)));   // PLL off, P and R
    sleep_ms(1);

    wr(0, 0x06, (uint8_t)(j & 0x3F));
    wr(0, 0x07, (uint8_t)((d >> 8) & 0xFF));
    wr(0, 0x08, (uint8_t)(d & 0xFF));

    wr(0, 0x04, 0x04);              // PLL input = BCLK, CODEC_CLKIN not yet
    wr(0, 0x05, (uint8_t)(0x80 | ((p & 7) << 4) | (r & 0x0F)));  // PLL on
    sleep_ms(15);
    wr(0, 0x04, 0x07);              // ...now CODEC_CLKIN = PLL output

    wr(0, 0x1B, 0x00);              // I2S, 16 bit, codec is the clock slave
    // Bits 2:1 pick what the DIN pin is for. Nothing else in this file
    // touches it, and a pin left disabled would explain a codec that is
    // configured, clocked, powered and still silent.
    wr(0, 0x36, 0x02);              // DIN is the audio data input
    wr(0, 0x0B, (uint8_t)(0x80 | ndac));
    wr(0, 0x0C, (uint8_t)(0x80 | mdac));
    wr(0, 0x0D, (uint8_t)((dosr >> 8) & 0xFF));
    wr(0, 0x0E, (uint8_t)(dosr & 0xFF));

    // Both DACs on, each taking its own channel.
    wr(0, 0x3F, 0xD4);
    wr(0, 0x40, 0x00);              // unmuted
    wr(0, 0x41, 0xEC);              // -10 dB digital
    wr(0, 0x42, 0xEC);

    jdb_snd_out_route(g_out);
    sleep_ms(10);
}

// --- samples ---

static void fill(int16_t* dst) {
    const int amp = g_amp;
    const uint32_t step = g_step;
    if (!amp || !step) {
        memset(dst, 0, sizeof(int16_t) * BUF_FRAMES * 2);
        g_phase = 0;
        return;
    }
    for (int i = 0; i < BUF_FRAMES; i++) {
        g_phase += step;
        int16_t s = (g_phase & 0x80000000u) ? (int16_t)amp : (int16_t)-amp;
        *dst++ = s;
        *dst++ = s;
    }
}

static void __not_in_flash_func(snd_dma_irq)(void) {
    g_irqs++;
    for (int i = 0; i < 2; i++) {
        if (dma_hw->ints0 & (1u << g_dma[i])) {
            dma_hw->ints0 = 1u << g_dma[i];
            dma_channel_set_read_addr(g_dma[i], g_buf[i], false);
            fill(g_buf[i]);
        }
    }
}

// --- the interface the melody engine uses ---

static int amp_for(int pct) {
    // Square law, and a ceiling well short of full scale: a square wave
    // at full swing through a headphone amplifier is painfully loud.
    return (12000 * pct * pct) / 10000;
}

void jdb_snd_out_volume(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    g_volume = pct;
    if (g_amp) g_amp = amp_for(g_volume);
}

void jdb_snd_out_tone(int freq) {
    if (!g_up) return;
    if (freq <= 0) {
        g_amp = 0;
        g_step = 0;
        return;
    }
    // 16.16 phase, so the top bit flips once a period.
    g_step = (uint32_t)(((uint64_t)freq << 32) / SAMPLE_RATE);
    g_amp = amp_for(g_volume);
}

void jdb_snd_out_beep(int freq, int ms) {
    if (ms <= 0) return;
    jdb_snd_out_tone(freq);
    sleep_ms(ms);
    jdb_snd_out_tone(0);
}

int jdb_snd_out_ready(void) { return g_up ? 1 : 0; }

// The reset line is shared with the radio, so anything that pulses it
// leaves the codec blank. Only its registers need programming again;
// the PIO, the DMA pair and the interrupt are still running and must
// not be set up a second time.
void fruitjam_snd_codec_reinit(void) {
    if (!g_up) return;
    codec_init();
}

void fruitjam_snd_init(void) {
    i2c_init(SND_I2C, 400 * 1000);
    gpio_set_function(SND_SDA, GPIO_FUNC_I2C);
    gpio_set_function(SND_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SND_SDA);
    gpio_pull_up(SND_SCL);

    // The reset line is shared with the radio; both come out of reset
    // together, which is what the board expects.
    gpio_init(SND_RESET_PIN);
    gpio_set_dir(SND_RESET_PIN, true);
    gpio_put(SND_RESET_PIN, 0);
    sleep_ms(20);
    gpio_put(SND_RESET_PIN, 1);
    sleep_ms(100);

    codec_init();

    // PIO 0 belongs to the USB host, so the sound lives on PIO 1.
    g_pio = pio1;
    g_sm = pio_claim_unused_sm(g_pio, true);
    uint off = pio_add_program(g_pio, &i2s_program);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, off, off + 9);
    sm_config_set_sideset(&c, 2, false, false);
    sm_config_set_out_pins(&c, I2S_DIN, 1);
    sm_config_set_sideset_pins(&c, I2S_BCLK);
    sm_config_set_out_shift(&c, false, false, 32);   // MSB first
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) /
                             (float)(SAMPLE_RATE * PIO_CYCLES_PER_FRAME));

    pio_gpio_init(g_pio, I2S_DIN);
    pio_gpio_init(g_pio, I2S_BCLK);
    pio_gpio_init(g_pio, I2S_BCLK + 1);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, I2S_DIN, 1, true);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, I2S_BCLK, 2, true);
    pio_sm_init(g_pio, g_sm, off, &c);

    // Two channels, each handing over to the other, so the stream never
    // stops while a buffer is being refilled.
    g_dma[0] = dma_claim_unused_channel(true);
    g_dma[1] = dma_claim_unused_channel(true);
    for (int i = 0; i < 2; i++) {
        dma_channel_config dc = dma_channel_get_default_config(g_dma[i]);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, true);
        channel_config_set_write_increment(&dc, false);
        channel_config_set_dreq(&dc, pio_get_dreq(g_pio, g_sm, true));
        channel_config_set_chain_to(&dc, g_dma[1 - i]);
        dma_channel_configure(g_dma[i], &dc, &g_pio->txf[g_sm],
                              g_buf[i], BUF_FRAMES, false);
        memset(g_buf[i], 0, sizeof g_buf[i]);
        dma_channel_set_irq0_enabled(g_dma[i], true);
    }
    irq_add_shared_handler(DMA_IRQ_0, snd_dma_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);

    pio_sm_set_enabled(g_pio, g_sm, true);
    dma_channel_start(g_dma[0]);
    g_up = true;
}

// 480p60 out of the HSTX peripheral, onto the Fruit Jam's HDMI socket.
//
// The command expander drives the TMDS encoder directly, so a scanline
// costs no PIO and no core-1 time: two DMA channels ping-pong into the
// HSTX FIFO, one posting the sync command list, the next posting the
// pixels, and an interrupt per half hands the finished channel its next
// job.
//
// The picture is 320 by 240 in a 640 by 480 signal. Vertical doubling is
// free - the scanout hands the same line to two consecutive scanlines.
// Horizontal doubling is baked into the buffer instead: a pixel is
// stored as two identical bytes, so a line is already 640 bytes wide and
// the DMA reads it untouched. Drawing pays one halfword store per pixel,
// which is what a byte store would have cost anyway.
//
// One byte per pixel, RGB332: red and green get three bits, blue two.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#define MODE_H_FRONT_PORCH   16
#define MODE_H_SYNC_WIDTH    96
#define MODE_H_BACK_PORCH    48
#define MODE_H_ACTIVE_PIXELS 640

#define MODE_V_FRONT_PORCH   10
#define MODE_V_SYNC_WIDTH    2
#define MODE_V_BACK_PORCH    33
#define MODE_V_ACTIVE_LINES  480

#define MODE_V_TOTAL_LINES ( \
    MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + \
    MODE_V_BACK_PORCH  + MODE_V_ACTIVE_LINES \
)
#define MODE_V_BLANK_LINES (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES)

#define FB_W      320
#define FB_H      240
#define FB_STRIDE MODE_H_ACTIVE_PIXELS
#define FB_BYTES  ((size_t)FB_STRIDE * FB_H)

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

// Padded to the HSTX FIFO depth with NOPs, so the DMA does not ping-pong
// fast enough to trip over its own interrupts.
static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V1_H1,
    HSTX_CMD_NOP
};

static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V0_H1,
    HSTX_CMD_NOP
};

static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_TMDS       | MODE_H_ACTIVE_PIXELS
};

// The example this is cut from owns the whole chip and hardcodes channels
// 0 and 1. Here the flash store and the USB stack are on the same bus, so
// the channels are claimed rather than assumed.
static int DMACH_PING = -1;
static int DMACH_PONG = -1;

static uint8_t* g_fb = NULL;
static bool     g_dma_pong = false;
static uint     g_v_scanline = 2;
static bool     g_vactive_cmdlist_posted = false;
static uint32_t g_frames = 0;
static uint32_t g_irqs = 0;
static uint32_t g_frame_us = 0;
static uint32_t g_last_wrap = 0;

// A line of black for the window between starting the signal and the
// framebuffer existing: the monitor locks on regardless.
static uint32_t g_blank_line[FB_STRIDE / sizeof(uint32_t)];

void __scratch_x("dvi") fruitjam_dvi_irq(void) {
    int ch_num = g_dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t* ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    g_dma_pong = !g_dma_pong;
    g_irqs++;

    if (g_v_scanline >= MODE_V_FRONT_PORCH &&
        g_v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (g_v_scanline < MODE_V_BLANK_LINES) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!g_vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        g_vactive_cmdlist_posted = true;
    } else {
        // Two signal lines per stored line: the shift is the doubling.
        uint src = (g_v_scanline - MODE_V_BLANK_LINES) >> 1;
        ch->read_addr = g_fb ? (uintptr_t)(g_fb + (size_t)src * FB_STRIDE)
                             : (uintptr_t)g_blank_line;
        ch->transfer_count = FB_STRIDE / sizeof(uint32_t);
        g_vactive_cmdlist_posted = false;
    }

    if (!g_vactive_cmdlist_posted) {
        g_v_scanline = (g_v_scanline + 1) % MODE_V_TOTAL_LINES;
        if (g_v_scanline == 0) {
            uint32_t now = time_us_32();
            g_frame_us = now - g_last_wrap;
            g_last_wrap = now;
            g_frames++;
        }
    }
}

uint8_t* fruitjam_dvi_framebuffer(void) { return g_fb; }
int      fruitjam_dvi_width(void)  { return FB_W; }
int      fruitjam_dvi_height(void) { return FB_H; }
size_t   fruitjam_dvi_stride(void) { return FB_STRIDE; }
uint32_t fruitjam_dvi_frames(void) { return g_frames; }
uint32_t fruitjam_dvi_irqs(void)   { return g_irqs; }
uint32_t fruitjam_dvi_frame_us(void) { return g_frame_us; }
uint32_t fruitjam_dvi_csr(void)    { return hstx_ctrl_hw->csr; }
// The counter measures the live clock against the reference, unlike
// clock_get_hz which only reports what the SDK was told.
uint32_t fruitjam_dvi_hstx_meas(void) {
    return frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_HSTX);
}
uint32_t fruitjam_dvi_sys_meas(void) {
    return frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
}
uint32_t fruitjam_dvi_expand(void) { return hstx_ctrl_hw->expand_shift; }
uint32_t fruitjam_dvi_ctrl(void)   { return dma_hw->ch[DMACH_PING].al1_ctrl; }
uint32_t fruitjam_dvi_hstx_hz(void)  { return clock_get_hz(clk_hstx); }
uint32_t fruitjam_dvi_sys_hz(void)   { return clock_get_hz(clk_sys); }

// Two bytes per pixel is the horizontal doubling, so a whole line is one
// memset and a single pixel is one halfword.
void fruitjam_dvi_clear(uint8_t rgb332) {
    if (g_fb) memset(g_fb, rgb332, FB_BYTES);
}

void fruitjam_dvi_pset(int x, int y, uint8_t rgb332) {
    if (!g_fb || x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
    ((uint16_t*)g_fb)[(size_t)y * FB_W + x] = (uint16_t)rgb332 * 0x0101u;
}

void fruitjam_dvi_hline(int x, int y, int w, uint8_t rgb332) {
    if (!g_fb || y < 0 || y >= FB_H) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > FB_W) w = FB_W - x;
    if (w <= 0) return;
    memset(g_fb + (size_t)y * FB_STRIDE + (size_t)x * 2, rgb332, (size_t)w * 2);
}

int fruitjam_dvi_alloc(void) {
    if (g_fb) return 1;
    g_fb = (uint8_t*)malloc(FB_BYTES);
    if (!g_fb) return 0;
    memset(g_fb, 0, FB_BYTES);
    return 1;
}

void fruitjam_dvi_free(void) {
    uint8_t* p = g_fb;
    g_fb = NULL;
    free(p);
}

// A scanline interrupt that arrives late is fatal: the running channel
// has already chained into the one still holding the previous transfer
// count, which then finishes in a few words instead of a line, and the
// picture collapses into a cascade of short transfers. Interrupt enables
// are per core, so the whole scanout lives on core 1 and neither USB nor
// the interpreter can ever delay it.
#ifdef FRUITJAM_USB
void fruitjam_usb_core1_init(void);
void fruitjam_usb_core1_task(void);
#endif

void fruitjam_gfx_color(int r, int g, int b);
void fruitjam_gfx_text(int x, int y, const char* s, int scale);

// Bring-up leaves a trail on the screen. The console cannot report a hang
// that happens before the console exists.
static int g_trace_y = 4;
void fruitjam_dvi_trace(const char* s) {
    if (!g_fb) return;
    fruitjam_gfx_color(48, 252, 48);
    fruitjam_gfx_text(4, g_trace_y, s, 1);
    g_trace_y += 10;
}

static void dvi_core1_entry(void) {
    irq_set_exclusive_handler(DMA_IRQ_0, fruitjam_dvi_irq);
    irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_start(DMACH_PING);

    // The host controller belongs to the core that services it, and it has
    // to be up before core 0 touches TinyUSB at all: stdio's init brings up
    // both roles at once, and whichever gets there first sets the pins.
    fruitjam_dvi_trace("2 scanout running");
#ifdef FRUITJAM_USB
    fruitjam_usb_core1_init();
    fruitjam_dvi_trace("3 usb host up");
    while (true) fruitjam_usb_core1_task();
#else
    while (true) __wfi();
#endif
}

// The signal starts here and never stops; the framebuffer may come and
// go underneath it.
void fruitjam_dvi_init(void) {
    // clk_hstx keeps whatever runtime init gave it; a later sys-clock
    // change carries the real rate along but not the SDK's record of it.
    // Pinning it here makes the two agree at the rate 480p60 needs.
    clock_configure(clk_hstx, 0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                    clock_get_hz(clk_sys), clock_get_hz(clk_sys));


    // RGB332 out of the low 8 bits: red 3, green 3, blue 2.
    hstx_ctrl_hw->expand_tmds =
        2  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
        0  << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
        2  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
        29 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
        1  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
        26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

    // Pixels arrive four to a word; a control symbol is a whole word.
    hstx_ctrl_hw->expand_shift =
        4 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    // HSTX output bits 0..7 leave on GPIO 12..19. This board wires each
    // pair negative first, the opposite way round to the Pico DVI Sock
    // the SDK example is cut for, so the lower pin of every pair is the
    // inverted one:
    //
    //   GP12 CK-  GP13 CK+
    //   GP14 D0-  GP15 D0+
    //   GP16 D1-  GP17 D1+
    //   GP18 D2-  GP19 D2+
    hstx_ctrl_hw->bit[0] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    hstx_ctrl_hw->bit[1] = HSTX_CTRL_BIT0_CLK_BITS;
    for (uint lane = 0; lane < 3; ++lane) {
        int bit = 2 + 2 * (int)lane;
        uint32_t sel =
            (lane * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[bit    ] = sel | HSTX_CTRL_BIT0_INV_BITS;
        hstx_ctrl_hw->bit[bit + 1] = sel;
    }

    for (int i = 12; i <= 19; ++i) gpio_set_function(i, 0);

    DMACH_PING = dma_claim_unused_channel(true);
    DMACH_PONG = dma_claim_unused_channel(true);

    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PING, &c, &hstx_fifo_hw->fifo,
                          vblank_line_vsync_off,
                          count_of(vblank_line_vsync_off), false);
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PONG, &c, &hstx_fifo_hw->fifo,
                          vblank_line_vsync_off,
                          count_of(vblank_line_vsync_off), false);

    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte0 |= (1u << DMACH_PING) | (1u << DMACH_PONG);
    // Scanout must win the bus, or a line arrives late and the picture
    // tears.
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS |
                            BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    fruitjam_dvi_alloc();
    fruitjam_dvi_trace("1 hstx configured");
    multicore_launch_core1(dvi_core1_entry);
}

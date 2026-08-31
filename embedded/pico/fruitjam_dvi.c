// 480p60 out of the HSTX peripheral, onto the Fruit Jam's HDMI socket.
//
// A whole frame is described up front as a list of DMA control blocks. A
// command channel writes pairs of registers into a pixel channel, the
// pixel channel pushes one run of sync words or pixels into the HSTX
// FIFO and chains back for the next pair, and the two walk the list
// between them without the processor. The list ends in a null trigger,
// which raises the one interrupt per frame that points the command
// channel back at the top.
//
// The processor therefore has 60 interrupts a second to serve rather
// than 60000, and none of them sits on a scanline deadline. An earlier
// cut reloaded a channel per scanline, and a late interrupt there was
// fatal: the running channel had already chained into the one still
// holding the previous transfer count, which then finished in seven
// words instead of a hundred and sixty, and the picture collapsed into a
// cascade of short transfers.
//
// The picture is 320 by 240 in a 640 by 480 signal. Vertical doubling is
// free - two consecutive entries point at the same stored line.
// Horizontal doubling is baked into the buffer: a pixel is stored as two
// identical bytes, so a line is already 640 bytes wide and the DMA reads
// it untouched. Drawing pays one halfword store per pixel, which is what
// a byte store would have cost anyway.
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

// A blank line is one pair; an active line is two, sync then pixels. Plus
// the null pair that ends the frame.
#define CMD_WORDS (2 * (MODE_V_BLANK_LINES + 2 * MODE_V_ACTIVE_LINES + 1))

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

static uint8_t*  g_fb = NULL;
static uint32_t* g_cmds = NULL;
static int       g_ch_pixel = -1;
static int       g_ch_cmd = -1;

void fruitjam_con_write(const char* s, int len);
void fruitjam_con_tick(void);

static uint32_t g_frames = 0;
static uint32_t g_frame_us = 0;
static uint32_t g_last_wrap = 0;

// The null trigger at the end of the list lands here. Everything the
// picture needs has already happened; this only aims the command channel
// back at the top of the list and lets it go again.
static void __scratch_x("dvi") fruitjam_dvi_irq(void) {
    dma_hw->intr = 1u << g_ch_pixel;
    dma_hw->ch[g_ch_cmd].al3_read_addr_trig = (uintptr_t)g_cmds;

    uint32_t now = time_us_32();
    g_frame_us = now - g_last_wrap;
    g_last_wrap = now;
    g_frames++;

    fruitjam_con_tick();
}

uint8_t* fruitjam_dvi_framebuffer(void) { return g_fb; }
int      fruitjam_dvi_width(void)  { return FB_W; }
int      fruitjam_dvi_height(void) { return FB_H; }
size_t   fruitjam_dvi_stride(void) { return FB_STRIDE; }
uint32_t fruitjam_dvi_frames(void) { return g_frames; }
uint32_t fruitjam_dvi_irqs(void)   { return g_frames; }
uint32_t fruitjam_dvi_frame_us(void) { return g_frame_us; }
uint32_t fruitjam_dvi_hstx_hz(void)  { return clock_get_hz(clk_hstx); }
uint32_t fruitjam_dvi_sys_hz(void)   { return clock_get_hz(clk_sys); }
uint32_t fruitjam_dvi_csr(void)    { return hstx_ctrl_hw->csr; }
uint32_t fruitjam_dvi_expand(void) { return hstx_ctrl_hw->expand_shift; }
uint32_t fruitjam_dvi_ctrl(void)   { return dma_hw->ch[g_ch_pixel].al1_ctrl; }

// The counter measures the live clock against the reference, unlike
// clock_get_hz which only reports what the SDK was told.
uint32_t fruitjam_dvi_hstx_meas(void) {
    return frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_HSTX);
}
uint32_t fruitjam_dvi_sys_meas(void) {
    return frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
}

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

// The framebuffer is part of the machine and outlives any program: the
// command list holds a pointer into every one of its lines.
int fruitjam_dvi_alloc(void) { return g_fb != NULL; }

void fruitjam_gfx_color(int r, int g, int b);
void fruitjam_gfx_text(int x, int y, const char* s, int scale);

// Bring-up leaves a trail on the screen. A hang before the console exists
// cannot report itself any other way.
void fruitjam_dvi_trace(const char* s) {
    if (!g_fb) return;
    fruitjam_con_write(s, (int)strlen(s));
    static const char crlf[2] = { 13, 10 };
    fruitjam_con_write(crlf, 2);
}

// One pair per transfer: how many words, and where from. Writing the
// second of the two triggers the pixel channel.
static void build_command_list(void) {
    size_t w = 0;
    for (uint v = 0; v < MODE_V_TOTAL_LINES; v++) {
        if (v >= MODE_V_FRONT_PORCH && v < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH) {
            g_cmds[w++] = count_of(vblank_line_vsync_on);
            g_cmds[w++] = (uintptr_t)vblank_line_vsync_on;
        } else if (v < MODE_V_BLANK_LINES) {
            g_cmds[w++] = count_of(vblank_line_vsync_off);
            g_cmds[w++] = (uintptr_t)vblank_line_vsync_off;
        } else {
            g_cmds[w++] = count_of(vactive_line);
            g_cmds[w++] = (uintptr_t)vactive_line;
            // Two signal lines per stored line: the shift is the doubling.
            uint row = (v - MODE_V_BLANK_LINES) >> 1;
            g_cmds[w++] = FB_STRIDE / sizeof(uint32_t);
            g_cmds[w++] = (uintptr_t)(g_fb + (size_t)row * FB_STRIDE);
        }
    }
    // A null trigger ends the frame and raises the interrupt.
    g_cmds[w++] = 0;
    g_cmds[w++] = 0;
}

void fruitjam_dvi_init(void) {
    g_fb = (uint8_t*)malloc(FB_BYTES);
    g_cmds = (uint32_t*)malloc(CMD_WORDS * sizeof(uint32_t));
    if (!g_fb || !g_cmds) return;
    memset(g_fb, 0, FB_BYTES);
    build_command_list();

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

    g_ch_pixel = dma_claim_unused_channel(true);
    g_ch_cmd   = dma_claim_unused_channel(true);

    // Quiet, so that only the null trigger at the end of the list raises
    // an interrupt; chained back to the command channel, which then posts
    // the next pair.
    dma_channel_config c = dma_channel_get_default_config(g_ch_pixel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_chain_to(&c, g_ch_cmd);
    channel_config_set_irq_quiet(&c, true);
    dma_channel_configure(g_ch_pixel, &c, &hstx_fifo_hw->fifo, NULL, 0, false);

    // Reads the list straight through, writes the same two registers over
    // and over - hence the ring on the write address.
    c = dma_channel_get_default_config(g_ch_cmd);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, 3);
    dma_channel_configure(g_ch_cmd, &c,
                          &dma_hw->ch[g_ch_pixel].al3_transfer_count,
                          g_cmds, 2, false);

    dma_hw->ints1 = 1u << g_ch_pixel;
    dma_hw->inte1 = 1u << g_ch_pixel;
    irq_set_exclusive_handler(DMA_IRQ_1, fruitjam_dvi_irq);
    irq_set_priority(DMA_IRQ_1, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);

    // Scanout must win the bus, or a line arrives late and the picture
    // tears.
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS |
                            BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    // The same thing that happens at every frame boundary starts the
    // first one.
    fruitjam_dvi_irq();
}

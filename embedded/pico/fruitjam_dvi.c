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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/psram.h"

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

// The framebuffer sits in the uncached alias. Once the scanout stopped
// reading it directly the cached window lost its advantage and kept a
// cost: 150 KB streamed through an 8 KB cache every frame evicts the
// code the processor is running. Uncached, the framebuffer and the code
// stay out of each other's way, and there is no coherency question
// between what the processor writes and what the copy channel reads.
//
// It takes the bottom of the window; the pool above it is told to start
// past the reservation, so the two never describe the same bytes.
#define PSRAM_FB_ADDR     0x11000000u
#define PSRAM_FB_RESERVED ((FB_BYTES + 4095u) & ~4095u)

static int       g_fb_in_psram = 0;
static uint8_t*  g_fb = NULL;

#ifdef JDB_FB_IN_PSRAM
// The part cannot be read fast enough to feed the picture directly: a
// line is fetched twice for the vertical doubling, which is 18.4 MB/s
// against the 14 a quad read of this chip sustains, and the frame
// stretches until the monitor lets go.
//
// So the scanout never reads PSRAM. It reads two line buffers in SRAM,
// and a second pair of channels copies the next stored line into the
// one that is not on screen. Each stored line is copied once instead of
// read twice, which halves the traffic to 9.2 MB/s and fits.
//
// The copy is paced by a DMA timer rather than chained to anything: the
// pixel clock and the timer both come off clk_sys, so once the phase is
// set at the top of a frame it holds. A copy that arrives late tears one
// line; it cannot disturb the sync, because the sync comes from the
// command list and that is unchanged.
#define LINE_BUF_BYTES FB_STRIDE
static uint8_t  g_lines[2][LINE_BUF_BYTES] __attribute__((aligned(4)));
static uint32_t g_copy_scratch;

// Four words an entry, written into the copy channel's alias 2:
// control, count, source, and the destination that starts it.
#define COPY_ENTRY_WORDS 4
// Blank lines between the frame interrupt and the first pixel: 45 signal
// lines, which is 22 and a half stored-line periods. Twenty-two entries
// carry that gap and the odd half is waited out at the restart, so the
// first real copy begins exactly when the line it will replace starts
// being displayed and has the whole period to finish in. The first two
// of the lead entries are real work - they fill both buffers for the
// frame about to start - and the rest move a single word into a scratch.
#define COPY_LEAD_ENTRIES 22
#define COPY_LEAD_US      32
#define COPY_REAL_ENTRIES (FB_H - 2)
#define COPY_ENTRIES      (COPY_LEAD_ENTRIES + COPY_REAL_ENTRIES)
static uint32_t g_copycmd[COPY_ENTRIES * COPY_ENTRY_WORDS];
static int      g_ch_copy = -1;
static int      g_ch_trig = -1;
#endif
static uint32_t* g_cmds = NULL;
static int       g_ch_pixel = -1;
static int       g_ch_cmd = -1;

void fruitjam_con_tick(void);
#ifdef JDB_FB_IN_PSRAM
static void copy_restart(void);
static void copy_channels_init(void);
#endif

static uint32_t g_frames = 0;
static uint32_t g_frame_us = 0;
static uint32_t g_frame_late = 0;
static uint32_t g_frame_worst = 0;
static uint32_t g_frame_short = 0;
static uint32_t g_frame_shortest = 0xFFFFFFFFu;
static uint32_t g_last_wrap = 0;

// Two faults that would break the picture while every frame still
// arrives on time, and so would be invisible to the counters above.
// WOF latches when the DMA writes the serialiser's FIFO while it is
// full; the DMA's own error bits latch when a transfer faults on the
// bus. Both are sticky, so one glance afterwards is enough.
static uint32_t g_hstx_wof = 0;
static uint32_t g_dma_err = 0;

// FIFO level at entry to the frame interrupt, and each frame's deviation
// from the 16666.67 us period as the microsecond timer reads it.
#define FRAME_PERIOD_US 16667u
static uint32_t g_lvl_min = 0xFFFFFFFFu;
static uint32_t g_lvl_zero = 0;
static uint32_t g_lvl_zero_frame = 0;
static int32_t  g_dev_long = 0;
static int32_t  g_dev_short = 0;
static uint32_t g_dev_over2 = 0;
static uint32_t g_dev_over10 = 0;
static int32_t  g_dev_last = 0;
static uint32_t g_dev_last_frame = 0;
static uint32_t g_dev_last_lvl = 0;
static uint32_t g_meas_frames = 0;

// The null trigger at the end of the list lands here. Everything the
// picture needs has already happened; this only aims the command channel
// back at the top of the list and lets it go again.
static void __scratch_x("dvi") fruitjam_dvi_irq(void) {
    uint32_t lvl = hstx_fifo_hw->stat & HSTX_FIFO_STAT_LEVEL_BITS;
    dma_hw->intr = 1u << g_ch_pixel;
    dma_hw->ch[g_ch_cmd].al3_read_addr_trig = (uintptr_t)g_cmds;

    uint32_t now = time_us_32();
    g_frame_us = now - g_last_wrap;
    g_last_wrap = now;
    g_frames++;

    if (g_frames > 4) {
        g_meas_frames++;
        if (lvl < g_lvl_min) g_lvl_min = lvl;
        if (lvl == 0) { g_lvl_zero++; g_lvl_zero_frame = g_frames; }
        int32_t dev = (int32_t)g_frame_us - (int32_t)FRAME_PERIOD_US;
        if (dev > g_dev_long)  g_dev_long = dev;
        if (dev < g_dev_short) g_dev_short = dev;
        uint32_t mag = dev < 0 ? (uint32_t)-dev : (uint32_t)dev;
        if (mag > 2) {
            g_dev_over2++;
            g_dev_last = dev;
            g_dev_last_frame = g_frames;
            g_dev_last_lvl = lvl;
        }
        if (mag > 10) g_dev_over10++;
    }
    // A frame that took a fifth longer than it should have. One of these
    // is enough for a monitor to let go and spend a second or two
    // finding the signal again, which is what a blackout in the middle
    // of a game looks like. Counting them says whether the picture
    // really slipped or whether the monitor is being fussy.
    if (g_frame_us > 20000u) {
        g_frame_late++;
        if (g_frame_us > g_frame_worst) g_frame_worst = g_frame_us;
    }
    // And the other side of it. A frame that arrives early breaks a
    // monitor's lock exactly as a late one does, and counting only the
    // long ones would call that clean.
    if (g_frames > 4 && g_frame_us < 14000u) {
        g_frame_short++;
        if (g_frame_us < g_frame_shortest) g_frame_shortest = g_frame_us;
    }

    uint32_t st = hstx_fifo_hw->stat;
    if (st & HSTX_FIFO_STAT_WOF_BITS) {
        g_hstx_wof++;
        hstx_fifo_hw->stat = HSTX_FIFO_STAT_WOF_BITS;
    }
    g_dma_err |= (dma_hw->ch[g_ch_pixel].ctrl_trig |
                  dma_hw->ch[g_ch_cmd].ctrl_trig) &
                 DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS;

#ifdef JDB_FB_IN_PSRAM
    if (g_ch_trig >= 0) copy_restart();
#endif
    fruitjam_con_tick();
}

// The first two are reached from the frame interrupt by way of the
// console's cursor, so they live in RAM with it.
uint8_t* __not_in_flash_func(fruitjam_dvi_framebuffer)(void) { return g_fb; }
size_t   __not_in_flash_func(fruitjam_dvi_stride)(void) { return FB_STRIDE; }
int      fruitjam_dvi_width(void)  { return FB_W; }
int      fruitjam_dvi_height(void) { return FB_H; }
uint32_t fruitjam_dvi_frames(void) { return g_frames; }
uint32_t fruitjam_dvi_irqs(void)   { return g_frames; }
uint32_t fruitjam_dvi_frame_us(void) { return g_frame_us; }
uint32_t fruitjam_dvi_late(void)     { return g_frame_late; }
uint32_t fruitjam_dvi_worst(void)    { return g_frame_worst; }
uint32_t fruitjam_dvi_short(void)    { return g_frame_short; }
uint32_t fruitjam_dvi_shortest(void) { return g_frame_shortest == 0xFFFFFFFFu ? 0 : g_frame_shortest; }
uint32_t fruitjam_dvi_sys_hz(void)   { return clock_get_hz(clk_sys); }
uint32_t fruitjam_dvi_wof(void)      { return g_hstx_wof; }
uint32_t fruitjam_dvi_dma_err(void)  { return g_dma_err; }

// Lowest FIFO level at the frame interrupt, how often it was empty,
// the widest frame deviations and how many frames exceeded 2 and 10 us,
// and the last such frame with the level seen then.
int fruitjam_dvi_jitter(char* out, int cap, int reset) {
    unsigned mn = g_lvl_min == 0xFFFFFFFFu ? 0 : (unsigned)g_lvl_min;
    int w = snprintf(out, cap,
        "irq margin low %u words, empty %u (frame %u); "
        "frame %+d/%+d us, over 2us %u, over 10us %u, of %u; "
        "last %+d us at frame %u, margin %u",
        mn, (unsigned)g_lvl_zero, (unsigned)g_lvl_zero_frame,
        (int)g_dev_long, (int)g_dev_short,
        (unsigned)g_dev_over2, (unsigned)g_dev_over10, (unsigned)g_meas_frames,
        (int)g_dev_last, (unsigned)g_dev_last_frame, (unsigned)g_dev_last_lvl);
    if (reset) {
        uint32_t saved = save_and_disable_interrupts();
        g_lvl_min = 0xFFFFFFFFu;
        g_lvl_zero = 0;
        g_lvl_zero_frame = 0;
        g_dev_long = g_dev_short = 0;
        g_dev_over2 = g_dev_over10 = 0;
        g_dev_last = 0;
        g_dev_last_frame = g_dev_last_lvl = 0;
        g_meas_frames = 0;
        restore_interrupts(saved);
    }
    return w;
}
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

int fruitjam_dvi_peek(int x, int y) {
    if (!g_fb || x < 0 || y < 0 || x >= FB_W || y >= FB_H) return -1;
    return ((uint16_t*)g_fb)[(size_t)y * FB_W + x] & 0xFF;
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
#ifdef JDB_FB_IN_PSRAM
            g_cmds[w++] = (uintptr_t)g_lines[row & 1];
#else
            g_cmds[w++] = (uintptr_t)(g_fb + (size_t)row * FB_STRIDE);
#endif
        }
    }
    // A null trigger ends the frame and raises the interrupt.
    g_cmds[w++] = 0;
    g_cmds[w++] = 0;
}

#ifdef JDB_FB_IN_PSRAM
// One entry per stored line. The lead entries move a single word into a
// scratch and exist only to carry the phase across the blank lines; the
// rest copy line L into the buffer that is not being displayed while
// line L-1 is on screen.
static void build_copy_list(uint32_t copy_ctrl) {
    uint32_t* e = g_copycmd;
    for (int i = 0; i < COPY_LEAD_ENTRIES; i++) {
        *e++ = copy_ctrl;
        if (i < 2) {
            // The two buffers the frame starts on.
            *e++ = FB_STRIDE / sizeof(uint32_t);
            *e++ = (uintptr_t)(g_fb + (size_t)i * FB_STRIDE);
            *e++ = (uintptr_t)g_lines[i];
        } else {
            *e++ = 1;
            *e++ = (uintptr_t)g_fb;
            *e++ = (uintptr_t)&g_copy_scratch;
        }
    }
    for (int line = 2; line < FB_H; line++) {
        *e++ = copy_ctrl;
        *e++ = FB_STRIDE / sizeof(uint32_t);
        *e++ = (uintptr_t)(g_fb + (size_t)line * FB_STRIDE);
        *e++ = (uintptr_t)g_lines[line & 1];
    }
}

// At the top of every frame the two buffers are refilled by hand and the
// paced list starts again, so a copy that slipped cannot accumulate.
// This runs in the frame interrupt, where the blank lines leave more
// than a millisecond and the scanout is reading nothing from PSRAM.
static void __not_in_flash_func(copy_restart)(void) {
    dma_hw->abort = 1u << g_ch_trig;
    while (dma_hw->abort & (1u << g_ch_trig)) tight_loop_contents();
    // The odd half of a stored-line period that the blank lines do not
    // divide into. Cheap here: the frame has a millisecond of blanking
    // left and nothing to do in it.
    busy_wait_us_32(COPY_LEAD_US);
    dma_hw->ch[g_ch_trig].read_addr = (uintptr_t)g_copycmd;
    dma_hw->ch[g_ch_trig].write_addr = (uintptr_t)&dma_hw->ch[g_ch_copy].al2_ctrl;
    dma_hw->ch[g_ch_trig].al1_transfer_count_trig = COPY_ENTRIES * COPY_ENTRY_WORDS;
}

static void copy_channels_init(void) {
    g_ch_copy = dma_claim_unused_channel(true);
    g_ch_trig = dma_claim_unused_channel(true);

    // Straight memory to memory, as fast as the bus allows.
    dma_channel_config c = dma_channel_get_default_config(g_ch_copy);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_irq_quiet(&c, true);
    dma_channel_set_config(g_ch_copy, &c, false);
    build_copy_list(channel_config_get_ctrl_value(&c));

    // One word a tick into the copy channel's four registers, the last of
    // which starts it. Four ticks to an entry, so the timer runs at four
    // times the stored-line rate: 126 MHz over 2000 is 63 kHz, and a
    // stored line lasts two of the 31.746 us signal lines.
    dma_hw->timer[0] = (1u << 16) | 2000u;

    c = dma_channel_get_default_config(g_ch_trig);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, 4);          // the four registers
    channel_config_set_dreq(&c, DREQ_DMA_TIMER0);
    channel_config_set_irq_quiet(&c, true);
    dma_channel_configure(g_ch_trig, &c,
                          &dma_hw->ch[g_ch_copy].al2_ctrl,
                          g_copycmd, COPY_ENTRIES * COPY_ENTRY_WORDS, false);
}
#endif

// What the line cache is actually doing: the first bytes of each buffer,
// what the framebuffer holds at the same place, and how much work the
// two channels have left. Empty buffers against a written framebuffer
// says the copy never ran.
int fruitjam_dvi_cache(char* out, int cap) {
#ifdef JDB_FB_IN_PSRAM
    return snprintf(out, cap,
        "b0=%02x%02x%02x b1=%02x%02x%02x fb=%02x%02x%02x trig=%u copy=%u",
        g_lines[0][0], g_lines[0][1], g_lines[0][2],
        g_lines[1][0], g_lines[1][1], g_lines[1][2],
        g_fb[0], g_fb[1], g_fb[2],
        (unsigned)dma_hw->ch[g_ch_trig].transfer_count,
        (unsigned)dma_hw->ch[g_ch_copy].transfer_count);
#else
    return snprintf(out, cap, "no line cache in this build");
#endif
}

unsigned fruitjam_psram_reserved(void) {
    return g_fb_in_psram ? PSRAM_FB_RESERVED : 0u;
}

int fruitjam_dvi_fb_in_psram(void) { return g_fb_in_psram; }

void fruitjam_dvi_init(void) {
    // 150 KB of the 200 the board has in SRAM was the framebuffer, and
    // it is the one big allocation that does not have to be there. It
    // costs frame rate: the picture wants 18.4 MB/s and a QSPI read of
    // this part carries a 24-cycle dummy per transaction, which lands
    // around 14. Off by default for that reason, one flag away.
#ifdef JDB_FB_IN_PSRAM
    if (psram_is_available() && psram_get_size() >= PSRAM_FB_RESERVED + 64u * 1024u) {
        g_fb = (uint8_t*)PSRAM_FB_ADDR;
        g_fb_in_psram = 1;
    } else
#endif
    {
        g_fb = (uint8_t*)malloc(FB_BYTES);
    }
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
    // Above the line copy, which reads PSRAM and stalls while it does:
    // the two share the DMA's read port, and the picture is the one with
    // a deadline.
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(g_ch_pixel, &c, &hstx_fifo_hw->fifo, NULL, 0, false);

    // Reads the list straight through, writes the same two registers over
    // and over - hence the ring on the write address.
    c = dma_channel_get_default_config(g_ch_cmd);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, 3);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(g_ch_cmd, &c,
                          &dma_hw->ch[g_ch_pixel].al3_transfer_count,
                          g_cmds, 2, false);

#ifdef JDB_FB_IN_PSRAM
    // Before the first frame: the buffers the command list already
    // points at have to hold something.
    if (g_fb_in_psram) copy_channels_init();
#endif

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

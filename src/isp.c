/*
 * isp.c - ISP backend for RP2040 (hardware SPI0 optimized)
 *
 * Major optimization: uses RP2040 hardware SPI0 for SCK/MOSI/MISO.
 * GP2/GP3/GP4 are SPI0's hardware-default pins, so NO pin changes
 * are needed. RESET (GP5) stays GPIO (SPI0_CSN unused).
 *
 * This gives ~5-20x speedup over the previous gpio_put bit-bang,
 * because:
 *   1. Hardware SPI generates SCK/MOSI at full register speed (no
 *      busy_wait per bit, no SDK function-call overhead).
 *   2. Direct SIO register access for RESET and bit-bang fallback
 *      eliminates gpio_put/gpio_get overhead (~5-10 cycles/call).
 *   3. Reliable timing at high SCK (up to 1.5 MHz) - bit-bang
 *      could not achieve sub-microsecond half-periods reliably.
 *
 * For SCK < 4 kHz (below HW SPI divider range at 125 MHz), falls
 * back to direct-SIO-register bit-bang with busy_wait_us delays.
 *
 * Pin mapping (UNCHANGED):
 *   GP2 -> SCK    (SPI0_SCK)
 *   GP3 -> MOSI   (SPI0_TX)
 *   GP4 -> MISO   (SPI0_RX)
 *   GP5 -> RESET  (GPIO, manual control)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"
#include "isp.h"

#define ISP_SPI    spi0
#define ISP_SPIHW  spi_get_hw(ISP_SPI)

/* SCK frequencies (Hz) for each USBASP_ISP_SCK_* option.
 * Options 13-26 are extended frequencies (12.5 kHz steps, 200-362.5 kHz)
 * for fine-grained SCK control on 1 MHz AVRs. Select via custom command 230. */
static const uint32_t sck_freq_table[] = {
    [USBASP_ISP_SCK_0_5]   =     500,
    [USBASP_ISP_SCK_1]     =    1000,
    [USBASP_ISP_SCK_2]     =    2000,
    [USBASP_ISP_SCK_4]     =    4000,
    [USBASP_ISP_SCK_8]     =    8000,
    [USBASP_ISP_SCK_16]    =   16000,
    [USBASP_ISP_SCK_32]    =   32000,
    [USBASP_ISP_SCK_93_75] =   93750,
    [USBASP_ISP_SCK_187_5] =  187500,
    [USBASP_ISP_SCK_375]   =  375000,
    [USBASP_ISP_SCK_750]   =  750000,
    [USBASP_ISP_SCK_1500]  = 1500000,
    [USBASP_ISP_SCK_200]   =  200000,
    [USBASP_ISP_SCK_212_5] =  212500,
    [USBASP_ISP_SCK_225]   =  225000,
    [USBASP_ISP_SCK_237_5] =  237500,
    [USBASP_ISP_SCK_250]   =  250000,
    [USBASP_ISP_SCK_262_5] =  262500,
    [USBASP_ISP_SCK_275]   =  275000,
    [USBASP_ISP_SCK_287_5] =  287500,
    [USBASP_ISP_SCK_300]   =  300000,
    [USBASP_ISP_SCK_312_5] =  312500,
    [USBASP_ISP_SCK_325]   =  325000,
    [USBASP_ISP_SCK_337_5] =  337500,
    [USBASP_ISP_SCK_350]   =  350000,
    [USBASP_ISP_SCK_362_5] =  362500,
};

/* Below this frequency, the SPI0 divider cannot reach the target
 * (min ~1.94 kHz at 125 MHz). Use bit-bang instead. */
#define HW_SPI_MIN_FREQ  4000

static bool     s_use_hw_spi      = false;
static uint32_t s_sck_freq        = 250000;  /* default: 250 kHz */
static uint16_t s_bb_half_us      = 1;   /* bit-bang half-period (us) */
static bool     s_spi_initialized = false;
static uint8_t  isp_hiaddr        = 0;

/* ---- Direct SIO register helpers (1 cycle each, no SDK overhead) ---- */
#define SCK_MASK   (1u << PIN_ISP_SCK)
#define MOSI_MASK  (1u << PIN_ISP_MOSI)
#define MISO_MASK  (1u << PIN_ISP_MISO)
#define RESET_MASK (1u << PIN_ISP_RESET)

static inline void sck_high(void)    { sio_hw->gpio_set = SCK_MASK; }
static inline void sck_low(void)     { sio_hw->gpio_clr = SCK_MASK; }
static inline void mosi_set(bool v)  { if (v) sio_hw->gpio_set = MOSI_MASK; else sio_hw->gpio_clr = MOSI_MASK; }
static inline void reset_high(void)  { sio_hw->gpio_set = RESET_MASK; }
static inline void reset_low(void)   { sio_hw->gpio_clr = RESET_MASK; }
static inline bool miso_get(void)    { return (sio_hw->gpio_in >> PIN_ISP_MISO) & 1u; }

/* ---- Bit-bang delay (slow-speed fallback only) ---- */
static inline void bb_delay(void)
{
    if (s_bb_half_us > 0)
        busy_wait_us_32(s_bb_half_us);
    else
        __asm volatile ("nop; nop; nop; nop; nop; nop; nop; nop");
}

/* ---- Set SCK frequency (core helper, used by all setters) ---- */
static void set_sck_freq(uint32_t freq)
{
    s_sck_freq = freq;
    if (freq >= HW_SPI_MIN_FREQ) {
        s_use_hw_spi = true;
        if (s_spi_initialized) {
            /* Full SPI reinit via spi_init(): resets SSP block, writes
             * SSPCPSR (prescale) while SSE=0 (PL022 requires this),
             * sets format, then enables SSE. Only called before pins
             * are connected — never during an active programming session. */
            spi_init(ISP_SPI, freq);
            spi_set_format(ISP_SPI, 8, SPI_CPHA_0, SPI_CPOL_0, SPI_MSB_FIRST);
            while (ISP_SPIHW->sr & SPI_SSPSR_RNE_BITS)
                (void)ISP_SPIHW->dr;
        }
    } else {
        s_use_hw_spi = false;
        s_bb_half_us = (uint16_t)(1000000u / (2u * freq));
        if (s_bb_half_us == 0) s_bb_half_us = 1;
    }
}

/* ---- Hardware SPI single-byte full-duplex transfer ---- */
/* Writes one byte to TX FIFO, waits for RX, returns received byte.
 * Direct register access - no SDK function-call overhead. */
static inline uint8_t isp_transmit_hw(uint8_t send)
{
    spi_hw_t *hw = ISP_SPIHW;
    while (!(hw->sr & SPI_SSPSR_TNF_BITS))    /* TX not full */
        tight_loop_contents();
    hw->dr = send;                             /* write TX */
    while (!(hw->sr & SPI_SSPSR_RNE_BITS))    /* RX not empty */
        tight_loop_contents();
    return (uint8_t)hw->dr;                    /* read RX */
}

/* ---- Bit-bang single-byte transfer (direct SIO, MSB first) ---- */
/* Samples MISO on SCK rising edge (matches SPI Mode 0 timing). */
static inline uint8_t isp_transmit_bb(uint8_t send)
{
    uint8_t rec = 0;
    for (int i = 0; i < 8; i++) {
        mosi_set(send & 0x80);
        send <<= 1;
        sck_high();
        bb_delay();
        rec = (rec << 1) | miso_get();
        sck_low();
        bb_delay();
    }
    return rec;
}

/* ---- Try one programming-mode entry (0xAC 0x53 echo check) ---- */
static inline bool try_prog_entry(void)
{
    isp_transmit(0xAC);
    isp_transmit(0x53);
    uint8_t check = isp_transmit(0);
    isp_transmit(0);
    return (check == 0x53);
}

/* ---- RESET pulse helper (high then low, for retry between attempts) ---- */
static inline void reset_pulse(void)
{
    busy_wait_us_32(100);
    reset_high();
    busy_wait_us_32(100);
    reset_low();
    busy_wait_us_32(100);
}

/* ==================== Public API ==================== */

void isp_init(void)
{
    /* Tri-state all ISP pins at boot */
    gpio_init(PIN_ISP_SCK);     gpio_set_dir(PIN_ISP_SCK, GPIO_IN);
    gpio_init(PIN_ISP_MOSI);    gpio_set_dir(PIN_ISP_MOSI, GPIO_IN);
    gpio_init(PIN_ISP_MISO);    gpio_set_dir(PIN_ISP_MISO, GPIO_IN);
    gpio_init(PIN_ISP_RESET);   gpio_set_dir(PIN_ISP_RESET, GPIO_IN);
    gpio_disable_pulls(PIN_ISP_SCK);
    gpio_disable_pulls(PIN_ISP_MOSI);
    gpio_disable_pulls(PIN_ISP_MISO);
    gpio_disable_pulls(PIN_ISP_RESET);

    /* Pre-initialize SPI0 peripheral (pins not yet connected).
     * spi_init resets, sets baudrate, enables. Format = 8-bit Mode 0 MSB.
     * Default 250 kHz = fCPU/4 for 1 MHz ATtiny85 (CKDIV8 default). */
    if (!s_spi_initialized) {
        spi_init(ISP_SPI, 250000);
        spi_set_format(ISP_SPI, 8, SPI_CPHA_0, SPI_CPOL_0, SPI_MSB_FIRST);
        s_spi_initialized = true;
    }
}

void isp_set_sck_option(uint8_t option)
{
    /* AUTO: keep current frequency (set by custom command 230 or default).
     * No auto-fallback — fixed-speed mode. This lets the user set an
     * exact frequency via command 230 before running avrdude without -B. */
    if (option == USBASP_ISP_SCK_AUTO)
        return;

    /* Clamp out-of-range to 250 kHz (safe default for 1 MHz AVRs) */
    if (option > USBASP_ISP_SCK_MAX)
        option = USBASP_ISP_SCK_250;

    set_sck_freq(sck_freq_table[option]);
}

uint32_t isp_set_sck_freq_custom(uint32_t freq_hz)
{
    /* Custom frequency: set exact Hz value (not limited to discrete options).
     * Clamp to a sane range (500 Hz .. 8 MHz) to protect the SPI divider. */
    if (freq_hz < 500)    freq_hz = 500;
    if (freq_hz > 8000000) freq_hz = 8000000;

    set_sck_freq(freq_hz);
    return s_sck_freq;
}

void isp_connect(void)
{
    if (s_use_hw_spi) {
        /* Connect GP2/GP3/GP4 to SPI0 hardware peripheral */
        gpio_set_function(PIN_ISP_SCK,  GPIO_FUNC_SPI);
        gpio_set_function(PIN_ISP_MOSI, GPIO_FUNC_SPI);
        gpio_set_function(PIN_ISP_MISO, GPIO_FUNC_SPI);
        /* GP5 (RESET) stays GPIO output (NOT SPI0_CSN) */
        gpio_init(PIN_ISP_RESET);
        gpio_set_dir(PIN_ISP_RESET, GPIO_OUT);

        /* Drain any stale RX FIFO data */
        while (ISP_SPIHW->sr & SPI_SSPSR_RNE_BITS)
            (void)ISP_SPIHW->dr;

        /* Reset pulse: RESET high then low (> 2 SCK cycles) */
        reset_low();
        busy_wait_us_32(20);
        reset_high();
        busy_wait_us_32(20);
        reset_low();
        busy_wait_us_32(20);
    } else {
        /* Bit-bang mode: all GPIO (MISO input) */
        gpio_set_dir(PIN_ISP_RESET, GPIO_OUT);
        gpio_set_dir(PIN_ISP_SCK,   GPIO_OUT);
        gpio_set_dir(PIN_ISP_MOSI,  GPIO_OUT);
        gpio_set_dir(PIN_ISP_MISO,  GPIO_IN);

        reset_low();
        sck_low();
        bb_delay();
        reset_high();
        bb_delay();
        reset_low();
    }

    isp_hiaddr = 0;
}

void isp_disconnect(void)
{
    if (s_use_hw_spi) {
        /* Restore SPI pins to GPIO SIO (input/tri-state) */
        gpio_set_function(PIN_ISP_SCK,  GPIO_FUNC_SIO);
        gpio_set_function(PIN_ISP_MOSI, GPIO_FUNC_SIO);
        gpio_set_function(PIN_ISP_MISO, GPIO_FUNC_SIO);
    }

    gpio_set_dir(PIN_ISP_RESET, GPIO_IN);
    gpio_set_dir(PIN_ISP_SCK,   GPIO_IN);
    gpio_set_dir(PIN_ISP_MOSI,  GPIO_IN);
    gpio_set_dir(PIN_ISP_MISO,  GPIO_IN);

    gpio_disable_pulls(PIN_ISP_RESET);
    gpio_disable_pulls(PIN_ISP_SCK);
    gpio_disable_pulls(PIN_ISP_MOSI);
    gpio_disable_pulls(PIN_ISP_MISO);
}

uint8_t isp_transmit(uint8_t send)
{
    return s_use_hw_spi ? isp_transmit_hw(send) : isp_transmit_bb(send);
}

uint8_t isp_enter_programming_mode(void)
{
    /* Fixed-speed mode: retry up to 32 times at the current SCK frequency.
     * No auto-fallback — frequency is set once before connect via -B option,
     * custom command 230, or the 250 kHz default. This is more reliable than
     * dynamic frequency changes mid-session (which can corrupt SSP state). */
    uint8_t count = 32;
    while (count--) {
        if (try_prog_entry()) return 0;
        reset_pulse();
    }
    return 1;  /* error: no response */
}

void isp_update_extended(uint32_t address)
{
    uint8_t curr_hiaddr = (uint8_t)(address >> 17);
    if (isp_hiaddr != curr_hiaddr) {
        isp_hiaddr = curr_hiaddr;
        isp_transmit(0x4D);
        isp_transmit(0x00);
        isp_transmit(isp_hiaddr);
        isp_transmit(0x00);
    }
}

uint8_t isp_read_flash(uint32_t address)
{
    isp_update_extended(address);
    isp_transmit(0x20 | ((address & 1) << 3));
    isp_transmit((uint8_t)(address >> 9));
    isp_transmit((uint8_t)(address >> 1));
    return isp_transmit(0);
}

uint8_t isp_write_flash(uint32_t address, uint8_t data, uint8_t pollmode)
{
    isp_update_extended(address);
    isp_transmit(0x40 | ((address & 1) << 3));
    isp_transmit((uint8_t)(address >> 9));
    isp_transmit((uint8_t)(address >> 1));
    isp_transmit(data);

    if (pollmode == 0) return 0;

    if (data == 0x7F) {
        busy_wait_us_32(4800);  /* ~4.8 ms */
        return 0;
    }

    uint8_t retries = 30;
    absolute_time_t start = get_absolute_time();
    while (retries) {
        if (isp_read_flash(address) != 0x7F) return 0;
        if (absolute_time_diff_us(start, get_absolute_time()) > 320) {
            start = get_absolute_time();
            retries--;
        }
    }
    return 1;  /* error */
}

uint8_t isp_flush_page(uint32_t address, uint8_t pollvalue)
{
    isp_update_extended(address);
    isp_transmit(0x4C);
    isp_transmit((uint8_t)(address >> 9));
    isp_transmit((uint8_t)(address >> 1));
    isp_transmit(0);

    if (pollvalue == 0xFF) {
        busy_wait_us_32(4800);
        return 0;
    }

    uint8_t retries = 30;
    absolute_time_t start = get_absolute_time();
    while (retries) {
        if (isp_read_flash(address) != 0xFF) return 0;
        if (absolute_time_diff_us(start, get_absolute_time()) > 320) {
            start = get_absolute_time();
            retries--;
        }
    }
    return 1;
}

uint8_t isp_read_eeprom(uint16_t address)
{
    isp_transmit(0xA0);
    isp_transmit((uint8_t)(address >> 8));
    isp_transmit((uint8_t)address);
    return isp_transmit(0);
}

uint8_t isp_write_eeprom(uint16_t address, uint8_t data)
{
    isp_transmit(0xC0);
    isp_transmit((uint8_t)(address >> 8));
    isp_transmit((uint8_t)address);
    isp_transmit(data);
    busy_wait_us_32(9600);  /* ~9.6 ms */
    return 0;
}

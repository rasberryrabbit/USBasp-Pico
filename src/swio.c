/*
 * swio.c - SWIO (Single-Wire I/O) backend for WCH CH32V003 RISC-V MCU
 *
 * v1.10-pio2: PWM pulse-width protocol (corrected per cnlohr/rv003usb reference).
 *
 * PROTOCOL CORRECTION:
 *   The original swio.c used LEVEL encoding ('1'=high, '0'=low for 4us).
 *   The real WCH SDI protocol uses PULSE-WIDTH encoding:
 *     '1' = short low pulse (~128ns) + high remainder
 *     '0' = long low pulse  (~512ns) + high remainder
 *   Line idles HIGH (open-drain + external pull-up).
 *   The target measures the low-pulse DURATION to distinguish bits.
 *
 *   Frame: start(1) + 7-addr(MSB first) + R/W(1=write,0=read) + 32-data(MSB first)
 *   The original had start=0 (WRONG), data LSB-first (WRONG), and level
 *   encoding (WRONG). All fixed here.
 *
 *   Read mechanism: host drives a short-low marker, releases, then counts
 *   how long the target holds the line low. Short extension = '1',
 *   long extension = '0'. This matches the reference's pulse-width capture.
 *
 * Pin: GP12 (SWIO), open-drain with external 4.7k-10k pull-up.
 * No SWCLK pin - protocol is self-timed via pulse widths.
 *
 * PIO: pio0 / SM0. Clock divider = 1 (8ns/cycle at 125MHz).
 * JMP pin = SWIO (for read-path pulse-width measurement).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "swio.h"
#include "swio.pio.h"

#define SWIO_PIO         pio0
#define SWIO_SM          0

/* Read threshold: X >= READ_THRESHOLD -> bit '1', else '0'.
 * X=31 means no extension (target released immediately = '1').
 * X~7 means ~48-cycle extension (target held low = '0'). */
#define READ_THRESHOLD   16

static PIO    s_pio    = SWIO_PIO;
static uint   s_sm     = SWIO_SM;
static uint   s_offset = 0;
static bool   s_ready  = false;

void swio_init(void)
{
    if (s_ready) {
        pio_sm_set_enabled(s_pio, s_sm, true);
        pio_sm_set_consecutive_pindirs(s_pio, s_sm, PIN_SWIO, 1, false);
        busy_wait_us_32(50);
        return;
    }

    s_offset = pio_add_program(s_pio, &swio_program);
    pio_sm_claim(s_pio, s_sm);

    pio_gpio_init(s_pio, PIN_SWIO);
    gpio_pull_up(PIN_SWIO);

    pio_sm_config c = swio_program_get_default_config(s_offset);
    sm_config_set_set_pins(&c, PIN_SWIO, 1);   /* SET pins = SWIO */
    sm_config_set_in_pins(&c,  PIN_SWIO);       /* IN pin  = SWIO */
    sm_config_set_jmp_pin(&c,  PIN_SWIO);       /* JMP pin = SWIO (read path) */

    /* LSB-first shift, no autopull/push (PIO does manual pull+push). */
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_in_shift(&c, true, false, 32);

    /* Divider = 1: at 125MHz, 1 PIO cycle = 8ns.
     * '1' pulse = 16 cycles = 128ns, '0' pulse = ~63 cycles = 504ns. */
    float div = (float)clock_get_hz(clk_sys) / 125000000.0f;
    if (div < 1.0f) div = 1.0f;
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(s_pio, s_sm, s_offset, &c);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PIN_SWIO, 1, false);
    pio_sm_set_enabled(s_pio, s_sm, true);

    s_ready = true;
    busy_wait_us_32(50);
}

/* ---- Write one PWM bit: val=1 -> short pulse, val=0 -> long pulse ---- */
static inline void swio_pio_write_bit(bool val)
{
    /* Command: bit0=is_read(0), bit1=write_val */
    pio_sm_put_blocking(s_pio, s_sm, val ? 0x02 : 0x00);
}

/* ---- Read one PWM bit: returns true if target pulse was short ('1') ---- */
static inline bool swio_pio_read_bit(void)
{
    /* Command: bit0=is_read(1) */
    pio_sm_put_blocking(s_pio, s_sm, 0x01);
    uint32_t x_val = pio_sm_get_blocking(s_pio, s_sm) & 0x1F;
    return x_val >= READ_THRESHOLD;
}

/* ---- Wait for all queued write bits to finish ---- */
static inline void swio_pio_drain(void)
{
    while (!pio_sm_is_tx_fifo_empty(s_pio, s_sm))
        tight_loop_contents();
    busy_wait_us_32(2);
}

uint32_t swio_xfer(uint8_t reg, bool rw, uint32_t data)
{
    uint32_t reply = 0;

    /* ---- Start bit = '1' (short pulse) ---- */
    swio_pio_write_bit(1);

    /* ---- 7-bit register address, MSB first ---- */
    for (int i = 6; i >= 0; i--)
        swio_pio_write_bit((reg >> i) & 1);

    /* ---- R/W bit: write=1 (short), read=0 (long) ---- */
    swio_pio_write_bit(rw ? 1 : 0);

    /* ---- 32-bit data field, MSB first ---- */
    if (rw) {
        /* WRITE: host drives data bits */
        for (int i = 31; i >= 0; i--)
            swio_pio_write_bit((data >> i) & 1);
        swio_pio_drain();
        reply = data;
    } else {
        /* READ: drain header, then read 32 bits MSB first.
         * Use pipelined reads to keep markers back-to-back (minimize gaps
         * that could cause the target to lose sync). */
        swio_pio_drain();

        /* Pre-fill TX FIFO with 4 read commands so the PIO processes
         * them back-to-back while we drain RX. */
        int pushed = 0;
        for (int i = 0; i < 4; i++) {
            pio_sm_put_blocking(s_pio, s_sm, 0x01);
            pushed++;
        }

        for (int i = 31; i >= 0; i--) {
            /* Push next read command if we still have bits to go
             * (keeps TX FIFO topped up; pio_sm_put_blocking throttles
             * naturally when FIFO is full). */
            if (pushed < 32) {
                pio_sm_put_blocking(s_pio, s_sm, 0x01);
                pushed++;
            }
            uint32_t x_val = pio_sm_get_blocking(s_pio, s_sm) & 0x1F;
            if (x_val >= READ_THRESHOLD)
                reply |= (1u << i);
        }
    }

    /* Inter-frame gap: line is already HIGH (PIO idles released). */
    busy_wait_us_32(2);

    return reply;
}

void swio_reset_target(void)
{
    /* 50 x '1' pulses (short low pulses) to reset the debug module.
     * Each '1' is a real short-low pulse now (not a no-op like before). */
    for (int i = 0; i < 50; i++)
        swio_pio_write_bit(1);
    /* Terminate with a '0' (long pulse) as start-of-frame marker. */
    swio_pio_write_bit(0);
    swio_pio_drain();
}

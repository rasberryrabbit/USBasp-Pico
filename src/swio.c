/*
 * swio.c - SWIO (Single-Wire I/O) backend for WCH CH32V003 RISC-V MCU
 *
 * Implements WCH's single-wire debug protocol via RP2040 GPIO bit-bang.
 * The SWIO line is open-drain: the host and target both pull the line low
 * to send, and a 4.7k-10k external pull-up keeps it high when idle.
 *
 * Frame format (35 bits, LSB first):
 *   bit 0    : start bit (always 0)
 *   bits 1-7 : register address (7 bits, MSB first)
 *   bit 8    : R/W (0 = read, 1 = write)
 *   bits 9-40: 32-bit data field (LSB first)
 *
 * Timing: each bit is ~4 us on the wire (250 kHz). The CH32V003 datasheet
 * spec allows up to 8 MHz, but slower speeds are fine for Flash
 * programming (write delays dominate). RP2040 busy_wait_us_32 is used for
 * the half-bit timing.
 *
 * Open-drain emulation on RP2040: we toggle between "input with pull-up"
 * (line released, line goes high via external pull-up) and "output low"
 * (line driven low). This is a standard GPIO trick for bit-banged OD.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "swio.h"

/* Half-bit period in microseconds. Total bit = 2 * half = 8 us (~125 kHz).
 * Conservative timing - works on all CH32V003 chips with a 10k pull-up. */
#define SWIO_HALF_BIT_US   4

static inline void swio_drive_low(void)
{
    /* Switch to output low: pulls line down. */
    gpio_set_dir(PIN_SWIO, GPIO_OUT);
    gpio_put(PIN_SWIO, 0);
}

static inline void swio_release(void)
{
    /* Switch to input: external pull-up drives line high. We also disable
     * the internal pull-down/pull-up to avoid fighting the external pull. */
    gpio_set_dir(PIN_SWIO, GPIO_IN);
    gpio_disable_pulls(PIN_SWIO);
}

static inline bool swio_read(void)
{
    /* Sample line level (high = 1, low = 0). */
    return gpio_get(PIN_SWIO) != 0;
}

static inline void swio_half_bit(void)
{
    busy_wait_us_32(SWIO_HALF_BIT_US);
}

static void swio_write_bit(bool bit)
{
    if (bit) {
        swio_release();           /* let pull-up drive high */
    } else {
        swio_drive_low();         /* pull low */
    }
    swio_half_bit();
    swio_release();               /* return to idle (high) for next bit */
    swio_half_bit();
}

static bool swio_read_bit(void)
{
    /* To read a bit from the device:
     *   - host releases the line (start of bit time)
     *   - host samples at mid-bit (after half_bit)
     *   - host holds the line for another half_bit before next bit
     */
    swio_release();
    swio_half_bit();
    bool b = swio_read();
    swio_half_bit();
    return b;
}

void swio_init(void)
{
    gpio_init(PIN_SWIO);
    /* Idle state: input, no internal pulls (external pull-up owns the line). */
    gpio_set_dir(PIN_SWIO, GPIO_IN);
    gpio_disable_pulls(PIN_SWIO);
    /* Hold idle for at least 50 us so the target sees a clean line. */
    busy_wait_us_32(50);
}

uint32_t swio_xfer(uint8_t reg, bool rw, uint32_t data)
{
    uint32_t reply = 0;

    /* ---- Start bit (always 0) ---- */
    swio_write_bit(0);

    /* ---- 7-bit register address, MSB first ---- */
    for (int i = 6; i >= 0; i--) {
        swio_write_bit((reg >> i) & 1);
    }

    /* ---- R/W bit ---- */
    swio_write_bit(rw ? 1 : 0);

    /* ---- 32-bit data field ----
     * For writes, host sends `data`. For reads, host sends zeros (idle)
     * and the device drives the line with its reply.
     */
    for (int i = 0; i < 32; i++) {
        if (rw) {
            swio_write_bit((data >> i) & 1);
        } else {
            swio_read_bit();
            /* Device drives the line; we sample and accumulate. */
            bool b = swio_read_bit();
            if (b) reply |= (1u << i);
        }
    }

    /* For writes, the device echoes the 32-bit value. We sample it to
     * confirm. For simplicity, we just drain the 32-bit reply window. */
    if (rw) {
        for (int i = 0; i < 32; i++) {
            bool b = swio_read_bit();
            if (b) reply |= (1u << i);
        }
    }

    /* End-of-frame: return line to idle high. */
    swio_release();
    busy_wait_us_32(SWIO_HALF_BIT_US * 2);

    return reply;
}

void swio_reset_target(void)
{
    /* Assert ~50 clock pulses with data high to force the debug module
     * into a known state (per WCH application note). */
    for (int i = 0; i < 50; i++) {
        swio_write_bit(1);
    }
    /* Drop a single start bit to terminate. */
    swio_write_bit(0);
    swio_release();
}

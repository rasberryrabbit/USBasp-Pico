/*
 * jtag.c - JTAG / SWD bit-bang backend (v1.10)
 *
 * Software-clocked JTAG/SWD transport. See jtag.h for the API and pin map.
 *
 * Design notes:
 *   - All four signal pins (TCK, TMS, TDI, TDO) are plain GPIO; no PIO is
 *     used. At the default ~1 MHz software toggle rate this is more than
 *     enough for debug-entry and flash programming rates (typically
 *     100 kHz - 1 MHz).
 *   - TCK is toggled with a busy_wait loop whose length is derived from
 *     jtag_half_period_us. The minimum (1 us) gives roughly 500 kHz; longer
 *     periods slow the clock for targets that need it.
 *   - In SWD mode TMS becomes SWDIO and is bidirectional; direction is
 *     switched by jtag_swd_set_dir(). TDI/TDO are unused in SWD but kept
 *     configured so a host can flip back to JTAG without re-init.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "jtag.h"
#include "usbasp.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

/* ---- State ------------------------------------------------------------ */
static bool     jtag_active = false;
static uint8_t  jtag_proto  = JTAG_PROTO_JTAG;
static uint16_t jtag_half_period_us = 1;   /* half TCK period, default ~500 kHz */

/* ---- Helpers ---------------------------------------------------------- */

static inline void jtag_pin_out(uint8_t pin)
{
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_disable_pulls(pin);
}

static inline void jtag_pin_in(uint8_t pin)
{
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);
}

static inline void jtag_clk_pulse(void)
{
    gpio_put(PIN_JTAG_TCK, 0);
    busy_wait_us(jtag_half_period_us);
    gpio_put(PIN_JTAG_TCK, 1);
    busy_wait_us(jtag_half_period_us);
}

/* ---- Public API ------------------------------------------------------- */

void jtag_init(uint8_t proto, uint16_t speed_khz)
{
    jtag_proto = proto;
    jtag_set_speed(speed_khz);

    /* Outputs (driven low initially so nothing glitches). */
    jtag_pin_out(PIN_JTAG_TCK);   gpio_put(PIN_JTAG_TCK, 0);
    jtag_pin_out(PIN_JTAG_TDI);   gpio_put(PIN_JTAG_TDI, 0);
    /* TMS: output in JTAG, output-by-default in SWD (host flips via
     * jtag_swd_set_dir when it needs to read SWDIO). */
    jtag_pin_out(PIN_JTAG_TMS);   gpio_put(PIN_JTAG_TMS, 1);  /* idle high */
    /* TDO: input (sampled). */
    jtag_pin_in(PIN_JTAG_TDO);

    /* nTRST / nSRST: outputs, released (high = inactive, active-low). */
    jtag_pin_out(PIN_JTAG_NTRST); gpio_put(PIN_JTAG_NTRST, 1);
    jtag_pin_out(PIN_JTAG_NSRST); gpio_put(PIN_JTAG_NSRST, 1);

    jtag_active = true;
}

void jtag_release(void)
{
    if (!jtag_active) return;
    /* Tri-state everything: set as input, no pulls. */
    jtag_pin_in(PIN_JTAG_TCK);
    jtag_pin_in(PIN_JTAG_TMS);
    jtag_pin_in(PIN_JTAG_TDI);
    jtag_pin_in(PIN_JTAG_TDO);
    jtag_pin_in(PIN_JTAG_NTRST);
    jtag_pin_in(PIN_JTAG_NSRST);
    jtag_active = false;
}

void jtag_set_speed(uint16_t speed_khz)
{
    /* Map kHz to a half-period in microseconds. The software toggle can do
     * roughly 500 kHz at half_period=1us; clamp there as the fastest. Slower
     * rates use longer waits. 0 or out-of-range -> default slow-ish. */
    if (speed_khz == 0) {
        jtag_half_period_us = 2;        /* ~250 kHz */
    } else if (speed_khz >= 500) {
        jtag_half_period_us = 1;        /* ~500 kHz (max) */
    } else if (speed_khz >= 100) {
        jtag_half_period_us = 5;        /* ~100 kHz */
    } else if (speed_khz >= 50) {
        jtag_half_period_us = 10;       /* ~50 kHz */
    } else if (speed_khz >= 10) {
        jtag_half_period_us = 50;       /* ~10 kHz */
    } else {
        jtag_half_period_us = 100;      /* ~5 kHz (slowest) */
    }
}

void jtag_reset(uint8_t mask, uint8_t value)
{
    if (!jtag_active) return;
    if (mask & 0x01) gpio_put(PIN_JTAG_NTRST, (value & 0x01) ? 1 : 0);
    if (mask & 0x02) gpio_put(PIN_JTAG_NSRST, (value & 0x02) ? 1 : 0);
    /* nTRST/nSRST are active low; asserting means drive 0, which pulse
     * width is left to the host (it asserts, waits, then releases). */
}

uint8_t jtag_pinset(uint8_t drv)
{
    if (!jtag_active) return 0;
    /* Apply the driven pins first (TCK last so the edge is clean). */
    if (drv & 0x08) gpio_put(PIN_JTAG_NTRST, (drv & 0x08) ? 1 : 0); /* placeholder */
    /* Note: drv bit semantics - bit3=nTRST asserted-LOW if set means drive.
     * For remote_bitbang the host sends a value; here we reuse bit layout:
     *   bit0=TCK value, bit1=TMS value, bit2=TDI value,
     *   bit3=nTRST value, bit4=nSRST value
     * The `drv` argument encodes which pins to UPDATE in the low 5 bits and
     * the new VALUES in the same bits; we treat set bits as "drive this
     * value". This keeps the call to a single byte for remote_bitbang. */
    if (drv & 0x01) gpio_put(PIN_JTAG_TCK, 1);   else gpio_put(PIN_JTAG_TCK, 0);
    if (drv & 0x02) gpio_put(PIN_JTAG_TMS, 1);   else gpio_put(PIN_JTAG_TMS, 0);
    if (drv & 0x04) gpio_put(PIN_JTAG_TDI, 1);   else gpio_put(PIN_JTAG_TDI, 0);
    if (drv & 0x08) gpio_put(PIN_JTAG_NTRST, 0); else gpio_put(PIN_JTAG_NTRST, 1);
    if (drv & 0x10) gpio_put(PIN_JTAG_NSRST, 0); else gpio_put(PIN_JTAG_NSRST, 1);

    /* Sample TDO into bit0 of the return. */
    return (uint8_t)(gpio_get(PIN_JTAG_TDO) ? 0x01 : 0x00);
}

uint16_t jtag_seq(const uint8_t *in, uint16_t nbytes, uint8_t *out)
{
    if (!jtag_active) return 0;
    uint16_t i;
    uint8_t out_byte = 0;
    uint8_t bit_pos = 7;   /* MSB-first packing */

    for (i = 0; i < nbytes; i++) {
        uint8_t b = in[i];
        /* Drive TDI/TMS, then clock low->sample->high. */
        gpio_put(PIN_JTAG_TDI, (b & 0x01) ? 1 : 0);
        gpio_put(PIN_JTAG_TMS, (b & 0x02) ? 1 : 0);

        gpio_put(PIN_JTAG_TCK, 0);
        busy_wait_us(jtag_half_period_us);

        uint8_t tdo = gpio_get(PIN_JTAG_TDO) ? 1 : 0;
        if (tdo) out_byte |= (uint8_t)(1u << bit_pos);

        gpio_put(PIN_JTAG_TCK, 1);
        busy_wait_us(jtag_half_period_us);

        if (bit_pos == 0) {
            out[i / 8] = out_byte;   /* commit full byte */
            out_byte = 0;
            bit_pos = 7;
        } else {
            bit_pos--;
        }
    }
    /* Commit a trailing partial byte. */
    if (bit_pos != 7) {
        out[i / 8] = out_byte;
    }
    return nbytes;
}

void jtag_swd_set_dir(bool output)
{
    /* In SWD, TMS = SWDIO. Output = drive, input = read. */
    gpio_set_dir(PIN_JTAG_TMS, output ? GPIO_OUT : GPIO_IN);
    if (output) {
        gpio_disable_pulls(PIN_JTAG_TMS);
    } else {
        /* Pull up so the line isn't floating when the target isn't driving. */
        gpio_set_pulls(PIN_JTAG_TMS, true, false);
    }
}

void jtag_swd_seq(uint8_t dir_bitmap, uint8_t nbits, uint8_t data_in,
                  uint8_t *data_out)
{
    if (!jtag_active) return;
    if (nbits == 0 || nbits > 8) return;
    uint8_t out_val = 0;
    for (uint8_t i = 0; i < nbits; i++) {
        bool is_out = (dir_bitmap >> i) & 1u;
        if (is_out) {
            jtag_swd_set_dir(true);
            gpio_put(PIN_JTAG_TMS, (data_in >> i) & 1u);
        } else {
            jtag_swd_set_dir(false);
        }
        /* One SWCLK pulse: low, sample on rising edge (SWD spec), high. */
        gpio_put(PIN_JTAG_TCK, 0);
        busy_wait_us(jtag_half_period_us);
        if (!is_out) {
            if (gpio_get(PIN_JTAG_TMS)) out_val |= (uint8_t)(1u << i);
        }
        gpio_put(PIN_JTAG_TCK, 1);
        busy_wait_us(jtag_half_period_us);
    }
    if (data_out) *data_out = out_val;
    /* Leave SWDIO in input mode after a read so the host can decide. */
    if (!(dir_bitmap >> (nbits - 1)) & 1u) {
        jtag_swd_set_dir(false);
    }
}

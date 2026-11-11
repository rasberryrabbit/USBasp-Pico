/*
 * swio.h - SWIO (Single-Wire I/O) backend for WCH CH32V003 RISC-V MCU
 *
 * SWIO is WCH's single-wire debug/programming protocol. It uses one open-drain
 * bidirectional pin (with a 4.7k-10k pull-up to VCC) and a 35-bit frame:
 *   1 start bit (0) + 7 reg address bits + 1 R/W bit + 32 data bits
 *
 * This backend implements the low-level bit-bang transport and exposes
 * read/write access to the WCH debug registers. Higher-level Flash erase /
 * program / verify is done by the host tool by composing register accesses.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SWIO_H_
#define _SWIO_H_

#include <stdint.h>
#include <stdbool.h>

/* SWIO pin (RP2040-Zero friendly). GP12 is in the edge-header range and
 * does not collide with ISP (GP2-GP6), HVSP (GP7-GP11), or the WS2812 LED
 * (GP16). Edit here if you want to reassign. */
#define PIN_SWIO    12

/* Initialize the SWIO pin (open-drain output, pulled high idle). */
void swio_init(void);

/* Send the 35-bit SWIO frame and return the 32-bit data field.
 *
 * For a write (rw=1): `data` is sent on the wire; the device returns
 * an (ignored) 32-bit field.
 * For a read (rw=0): `data` is unused (send zeros); the device's reply
 * is returned.
 *
 * Returns the 32-bit data field from the device reply (writes return
 * the echoed value, which should equal `data` on success). */
uint32_t swio_xfer(uint8_t reg, bool rw, uint32_t data);

/* Convenience wrappers. */
static inline uint32_t swio_read_reg(uint8_t reg) {
    return swio_xfer(reg, /*rw=*/false, 0);
}

static inline uint32_t swio_write_reg(uint8_t reg, uint32_t value) {
    return swio_xfer(reg, /*rw=*/true, value);
}

/* Reset the target via SWIO (assert 50 SWIO clock pulses with data high
 * to force the debug module into a known state). */
void swio_reset_target(void);

#endif /* _SWIO_H_ */

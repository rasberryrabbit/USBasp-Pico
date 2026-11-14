/*
 * swio.h - SWIO (Single-Wire I/O) backend for WCH CH32V003 RISC-V MCU
 *
 * WCH SDI protocol (as implemented by picorvd):
 *   Frame = start('1') + 7-bit ~addr(MSB first) + R/W(1=read,0=write)
 *           + 32-bit ~data(MSB first). Address and data are bit-inverted
 *           on the wire; the firmware handles this internally, so callers
 *           pass plain register addresses and values.
 * One open-drain pin with a 4.7k-10k pull-up to VCC. No SWCLK.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SWIO_H_
#define _SWIO_H_

#include <stdint.h>
#include <stdbool.h>

/* SWIO pin (RP2040-Zero friendly). GP12 does not collide with ISP
 * (GP2-GP6), HVSP (GP7-GP11) or the JTAG block (GP11/GP13-GP15). */
#define PIN_SWIO    12

void swio_init(void);

/* Exchange one SDI frame with debug register `reg`.
 * rw = true  : write `data` to the register (host -> target).
 * rw = false : read the register; its value is returned.
 * Returns the register value on reads, `data` on writes. */
uint32_t swio_xfer(uint8_t reg, bool rw, uint32_t data);

static inline uint32_t swio_read_reg(uint8_t reg) {
    return swio_xfer(reg, /*rw=*/false, 0);
}

static inline uint32_t swio_write_reg(uint8_t reg, uint32_t value) {
    return swio_xfer(reg, /*rw=*/true, value);
}

/* Reset the CH32V003 debug module: >16T low pulse, then unlock the SDI
 * output (SHDWCFGR/CFGR = 0x5AA50400) and pulse DMCONTROL, exactly as
 * picorvd does. Call once after connecting power to the target. */
void swio_reset_target(void);

#endif /* _SWIO_H_ */
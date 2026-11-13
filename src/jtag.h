/*
 * jtag.h - JTAG / SWD bit-bang backend (v1.10)
 *
 * A minimal, tool-agnostic JTAG/SWD transport that the host side drives one
 * bit (or one short sequence) at a time over USBasp-Pico vendor requests
 * 220-226. Two host tools share this backend:
 *
 *   - scripts/usbasp_pico_probe.py        (pyOCD DebugProbe plugin)
 *   - scripts/usbasp_pico_jtag_bridge.py  (OpenOCD remote_bitbang TCP bridge)
 *
 * Pin assignment (defaults, see usbasp.h):
 *   GP11 = TCK / SWCLK   (output)
 *   GP13 = TMS / SWDIO   (output in JTAG; bidirectional in SWD)
 *   GP14 = TDI           (output)
 *   GP15 = TDO           (input)
 *   GP26 = nTRST         (output, optional)
 *   GP27 = nSRST         (output, optional)
 *
 * The backend never interprets JTAG/SWD protocol semantics; it only moves
 * bits. TCK is a plain GPIO toggle (software-clocked), which is plenty for
 * the few-hundred-kHz rates these tools use for debug entry and flash.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _JTAG_H_
#define _JTAG_H_

#include <stdint.h>
#include <stdbool.h>

/* Protocol selection for jtag_init(). */
#define JTAG_PROTO_JTAG   0
#define JTAG_PROTO_SWD    1

/* Initialize the JTAG/SWD pins.
 *   proto = JTAG_PROTO_JTAG or JTAG_PROTO_SWD
 *   speed_khz = desired TCK/SWCLK rate (0 = default slowest; clamped to the
 *               range the software clock can produce)
 * In JTAG mode TMS/TDI are outputs and TDO is input. In SWD mode TMS (SWDIO)
 * starts as an output; the host flips direction with jtag_swd_set_dir().
 * nTRST/nSRST are released (driven high) on init. */
void jtag_init(uint8_t proto, uint16_t speed_khz);

/* Release all JTAG/SWD pins (tri-state / input), return to idle. Safe to
 * call without a prior jtag_init(). */
void jtag_release(void);

/* Set the TCK/SWCLK period. kHz is clamped to the achievable range. */
void jtag_set_speed(uint16_t speed_khz);

/* Drive nTRST / nSRST. mask bits: bit0 = nTRST, bit1 = nSRST.
 * value bits: 0 = assert (drive low), 1 = release (drive high).
 * Only the bits set in mask are touched. */
void jtag_reset(uint8_t mask, uint8_t value);

/* Direct single-bit pin drive (remote_bitbang '0'-'7' style).
 * drv bits: bit0=TCK, bit1=TMS, bit2=TDI, bit3=nTRST, bit4=nSRST.
 * Returns the sampled TDO bit in bit0. Pins whose drv bit is 0 are left
 * unchanged. */
uint8_t jtag_pinset(uint8_t drv);

/* Run a packed JTAG bit sequence. Each input byte drives one TCK cycle:
 *   bit0 = TDI, bit1 = TMS, bit2 = TCK  (host sets TCK=0 then TCK=1)
 * For each bit the firmware:
 *   1. sets TDI and TMS from the byte
 *   2. lowers TCK
 *   3. samples TDO
 *   4. raises TCK
 * The TDO samples are packed MSB-first into the output buffer (one bit per
 * input byte). Returns the number of TDO bits written (= nbytes). */
uint16_t jtag_seq(const uint8_t *in, uint16_t nbytes, uint8_t *out);

/* SWD sequence: host supplies a direction bitmap and the data bits.
 *   dir_bitmap: bit i = 1 means bit i of data is driven OUT (Pico->target),
 *               bit i = 0 means bit i is read IN (target->Pico).
 *   nbits: number of bits in the sequence (1..8 per call; host chunks).
 *   data_in: the bits to drive when dir=1 (LSB first).
 *   data_out: receives the bits sampled when dir=0 (LSB first).
 * Each bit is one SWDIO half-cycle clocked by one SWCLK pulse. The host is
 * responsible for SWD protocol framing (start, parity, turnaround). */
void jtag_swd_seq(uint8_t dir_bitmap, uint8_t nbits, uint8_t data_in,
                  uint8_t *data_out);

/* SWD direction control: when SWDIO must be read, host sets dir=0 (input);
 * when driven, dir=1 (output). */
void jtag_swd_set_dir(bool output);

#endif /* _JTAG_H_ */

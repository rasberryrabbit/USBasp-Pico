/*
 * hvsp.h - High-Voltage Serial Programming backend (ATtiny85 etc.)
 *
 * HVSP applies 12 V to RESET to enter programming mode for AVRs where the
 * RESET pin has been disabled (RSTDISBL fuse) or the clock source has been
 * misconfigured. The protocol uses 4 pins: SDI (data in), SDO (data out),
 * SII (instruction in), SCI (clock). On the programmer side we drive
 * SDI/SII/SCI and a separate "12V_EN" pin that switches an external boost
 * converter on/off via a small MOSFET or a dedicated IC like MAX662A.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _HVSP_H_
#define _HVSP_H_

#include <stdint.h>
#include <stdbool.h>

/* Initialize HVSP GPIO pins (call once at boot) */
void hvsp_init(void);

/* Enter HVSP mode:
 *   1. Apply 5V to target VCC (provided by user; we assume target is powered)
 *   2. Drive SDI/SII/SDO low
 *   3. Pulse RESET low (must be 0 V first)
 *   4. Enable 12 V on RESET for >= 10 us
 *   5. Release SDO (input)
 *   6. Wait >= 300 us before sending serial instructions
 */
void hvsp_enter_mode(void);

/* Exit HVSP mode: power-down sequence */
void hvsp_exit_mode(void);

/* Shift out a 16-bit (instruction + data) pair and read back 8 bits.
 * Returns the 8 bits received from the target. */
uint8_t hvsp_shift_out(uint8_t instruction, uint8_t data);

/* Chip erase (HVSP command sequence) */
void hvsp_chip_erase(void);

/* Read signature byte at index 0..2 */
uint8_t hvsp_read_signature(uint8_t index);

/* Read fuse: which = 0 (low), 1 (high), 2 (extended) */
uint8_t hvsp_read_fuse(uint8_t which);

/* Write fuse: which = 0 (low), 1 (high), 2 (extended) */
void hvsp_write_fuse(uint8_t which, uint8_t value);

/* Read one flash byte at the given word address */
uint8_t hvsp_read_flash(uint16_t word_addr);

/* Write one flash word */
void hvsp_write_flash_word(uint16_t word_addr, uint16_t word);

#endif /* _HVSP_H_ */

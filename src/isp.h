/*
 * isp.h - ISP (in-system programming) backend for USBasp-Pico
 *
 * Bit-bang SPI on RP2040 GPIO pins, mirroring the AVR firmware's behavior
 * so avrdude's `-c usbasp` flow works unchanged.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _ISP_H_
#define _ISP_H_

#include <stdint.h>
#include "usbasp.h"

/* Initialize the ISP I/O subsystem (call once at boot). */
void isp_init(void);

/* Set the SCK speed option (USBASP_ISP_SCK_*). Called before ispConnect(). */
void isp_set_sck_option(uint8_t option);

/* Apply the ISP pin layout: drive RESET/SCK/MOSI as outputs, MISO as input,
 * pulse RESET negative then bring SCK low. Mirrors usbasp ispConnect(). */
void isp_connect(void);

/* Release the ISP pins (tri-state). Called on USBASP_FUNC_DISCONNECT. */
void isp_disconnect(void);

/* Send and receive one byte over ISP. Honors the configured SCK speed. */
uint8_t isp_transmit(uint8_t send);

/* Send 0xAC 0x53 and look for the echoed 0x53. Retries up to 32 times.
 * Returns 0 on success, 1 on failure. */
uint8_t isp_enter_programming_mode(void);

/* Update extended address byte (for >128 kB flash parts). */
void isp_update_extended(uint32_t address);

uint8_t isp_read_flash(uint32_t address);
uint8_t isp_write_flash(uint32_t address, uint8_t data, uint8_t pollmode);
uint8_t isp_flush_page(uint32_t address, uint8_t pollvalue);

uint8_t isp_read_eeprom(uint16_t address);
uint8_t isp_write_eeprom(uint16_t address, uint8_t data);

#endif /* _ISP_H_ */

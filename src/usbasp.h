/*
 * usbasp.h - Internal pin definitions and shared globals
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _USBASP_H_
#define _USBASP_H_

#include <stdint.h>
#include <stdbool.h>
#include "usbasp_protocol.h"
#include "pico/stdlib.h"

/* ----- Pin assignment ----------------------------------------------
 *
 * Default layout targets the Waveshare RP2040-Zero (and is also compatible
 * with the original Raspberry Pi Pico). All pins below are exposed on the
 * RP2040-Zero edge header.
 *
 *   ISP (default, 6-pin AVR ISP connector)
 *   ------------------------------------------------
 *   GP2  ->  ISP_SCK     (output)
 *   GP3  ->  ISP_MOSI    (output)
 *   GP4  ->  ISP_MISO    (input)
 *   GP5  ->  ISP_RESET   (output)
 *   GP6  ->  STATUS_LED  (output, active low, on-board Pico LED)
 *
 *   HVSP (optional, ATtiny85 12 V programming)
 *   ------------------------------------------------
 *   GP7  ->  HVSP_SCI     (output, HVSP clock)
 *   GP8  ->  HVSP_SDI     (output, HVSP data in)
 *   GP9  ->  HVSP_SDO     (input,  HVSP data out)
 *   GP10 ->  HVSP_SII     (output, HVSP instruction)
 *   GP11 ->  HVSP_12V_EN  (output, enables external 12 V boost)
 *
 *   Notes for the RP2040-Zero:
 *   - GP16 is wired to the on-board WS2812 RGB LED, so it is NOT used as a
 *     plain GPIO. We keep GP6 for the simple status LED; on the Zero you
 *     can also wire an external LED to GP6 if you want a discrete blink.
 *   - GP22-GP25 are only available as bottom solder pads on the Zero, so
 *     they are avoided. All HVSP pins are in the GP0-GP15 edge range.
 *   - GP0 and GP1 are left free for the user (or for the second UART).
 */
#define PIN_ISP_SCK          2
#define PIN_ISP_MOSI         3
#define PIN_ISP_MISO         4
#define PIN_ISP_RESET        5
#define PIN_LED              6   /* status LED (external on Zero, on-board on Pico) */

#define PIN_HVSP_SCI         7
#define PIN_HVSP_SDI         8
#define PIN_HVSP_SDO         9
#define PIN_HVSP_SII         10
#ifndef USBASP_PICO_HVSP_12V_PIN
#define USBASP_PICO_HVSP_12V_PIN  11
#endif
#define PIN_HVSP_12V_EN      USBASP_PICO_HVSP_12V_PIN

/* ----- Globals exposed from main.c --------------------------------- */
extern uint8_t prog_sck;          /* current SCK option */
extern bool    prog_address_newmode;
extern uint32_t prog_address;
extern uint16_t prog_nbytes;
extern uint16_t prog_pagesize;
extern uint8_t  prog_blockflags;
extern uint8_t  prog_pagecounter;
extern prog_state_t prog_state;

/* API consumed by the protocol layer */
void led_activity_on(void);
void led_activity_off(void);
void led_status_init(void);
void led_status_task(void);

#endif /* _USBASP_H_ */

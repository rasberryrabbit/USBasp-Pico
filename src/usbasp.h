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
 *   GP3  ->  ISP_MOSI    (output)  -- shared with HVSP SDI (see below)
 *   GP4  ->  ISP_MISO    (input)
 *   GP5  ->  ISP_RESET   (output)
 *   GP6  ->  STATUS_LED  (output, active low, on-board Pico LED)
 *
 *   HVSP (optional, ATtiny85 12 V programming)
 *   ------------------------------------------------
 *   GP7  ->  HVSP_SCI     (output, HVSP clock)
 *   GP3  ->  HVSP_SDI     (output, HVSP data in)  -- SHARED with ISP_MOSI
 *   GP8  ->  HVSP_SDO     (input,  HVSP data out)
 *   GP9  ->  HVSP_SII     (output, HVSP instruction)
 *   GP10 ->  HVSP_12V_EN  (output, enables external 12 V boost)
 *
 *   v1.9 change: MOSI/SDI pin sharing.
 *   ISP MOSI and HVSP SDI are both Pico->target outputs driven MSB-first, and
 *   ISP and HVSP are never active at the same time, so a single GPIO (GP3) can
 *   safely serve both. This frees GP8 from being the HVSP SDI pin; the rest of
 *   the HVSP signals (SDO/SII/12V_EN) are shifted down by one pin (GP9->GP8,
 *   GP10->GP9, GP11->GP10), so HVSP now occupies GP3/GP7-GP10 instead of
 *   GP7-GP11. One free GPIO (GP11) is returned to the user.
 *
 *   Notes for the RP2040-Zero:
 *   - GP16 is wired to the on-board WS2812 RGB LED, so it is NOT used as a
 *     plain GPIO. We keep GP6 for the simple status LED; on the Zero you
 *     can also wire an external LED to GP6 if you want a discrete blink.
 *   - GP22-GP25 are only available as bottom solder pads on the Zero, so
 *     they are avoided. All HVSP pins are in the GP0-GP15 edge range.
 *   - GP0 and GP1 are left free for the user (or for the second UART).
 *   - GP11 is now used for JTAG TCK (v1.10); before v1.9 it was HVSP_12V_EN.
 *
 *   JTAG / SWD (v1.10, optional, USBASP_PICO_ENABLE_JTAG=1)
 *   ------------------------------------------------
 *   GP11 ->  TCK / SWCLK   (output, JTAG clock / SWD clock)
 *   GP13 ->  TMS / SWDIO   (output in JTAG; bidirectional in SWD)
 *   GP14 ->  TDI           (output, JTAG only)
 *   GP15 ->  TDO           (input,  JTAG only)
 *   GP26 ->  nTRST         (output, optional, active low)
 *   GP27 ->  nSRST         (output, optional, active low)
 *
 *   These six pins do not overlap ISP (GP2-GP6), HVSP (GP3/GP7-GP10), or
 *   SWIO (GP12), so all four backends can coexist on one Pico and only one
 *   is active at a time. nTRST/nSRST are optional: if you only need the
 *   four-wire JTAG or SWD, leave GP26/GP27 unconnected.
 */
#define PIN_ISP_SCK          2
#define PIN_ISP_MOSI         3   /* shared with HVSP SDI */
#define PIN_ISP_MISO         4
#define PIN_ISP_RESET        5
#define PIN_LED              6   /* status LED (external on Zero, on-board on Pico) */

#define PIN_HVSP_SCI         7
#define PIN_HVSP_SDI         PIN_ISP_MOSI   /* GP3 - shared with ISP MOSI (v1.9) */
#define PIN_HVSP_SDO         8
#define PIN_HVSP_SII         9
#ifndef USBASP_PICO_HVSP_12V_PIN
#define USBASP_PICO_HVSP_12V_PIN  10
#endif
#define PIN_HVSP_12V_EN      USBASP_PICO_HVSP_12V_PIN

/* JTAG / SWD pins (v1.10). Defaults can be overridden at build time. */
#ifndef USBASP_PICO_JTAG_TCK_PIN
#define USBASP_PICO_JTAG_TCK_PIN   11   /* also SWCLK */
#endif
#ifndef USBASP_PICO_JTAG_TMS_PIN
#define USBASP_PICO_JTAG_TMS_PIN   13   /* also SWDIO (bidirectional in SWD) */
#endif
#ifndef USBASP_PICO_JTAG_TDI_PIN
#define USBASP_PICO_JTAG_TDI_PIN   14
#endif
#ifndef USBASP_PICO_JTAG_TDO_PIN
#define USBASP_PICO_JTAG_TDO_PIN   15
#endif
#ifndef USBASP_PICO_JTAG_NTRST_PIN
#define USBASP_PICO_JTAG_NTRST_PIN 26
#endif
#ifndef USBASP_PICO_JTAG_NSRST_PIN
#define USBASP_PICO_JTAG_NSRST_PIN 27
#endif
#define PIN_JTAG_TCK    USBASP_PICO_JTAG_TCK_PIN
#define PIN_JTAG_TMS    USBASP_PICO_JTAG_TMS_PIN
#define PIN_JTAG_TDI    USBASP_PICO_JTAG_TDI_PIN
#define PIN_JTAG_TDO    USBASP_PICO_JTAG_TDO_PIN
#define PIN_JTAG_NTRST  USBASP_PICO_JTAG_NTRST_PIN
#define PIN_JTAG_NSRST  USBASP_PICO_JTAG_NSRST_PIN

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

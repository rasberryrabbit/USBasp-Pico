/*
 * usbasp_protocol.h - USBasp wire protocol definitions
 *
 * Mirrors the constants in mariusgreuel/USBasp's firmware/usbasp.h so the Pico
 * presents itself to avrdude as an ordinary USBasp programmer. No protocol
 * extensions are introduced - we are a drop-in replacement.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _USBASP_PROTOCOL_H_
#define _USBASP_PROTOCOL_H_

#include <stdint.h>

/* USB function call identifiers (bmRequest = 0x40/0xC0, request = one of these)
 */
#define USBASP_FUNC_CONNECT         1
#define USBASP_FUNC_DISCONNECT      2
#define USBASP_FUNC_TRANSMIT        3
#define USBASP_FUNC_READFLASH       4
#define USBASP_FUNC_ENABLEPROG      5
#define USBASP_FUNC_WRITEFLASH      6
#define USBASP_FUNC_READEEPROM      7
#define USBASP_FUNC_WRITEEEPROM     8
#define USBASP_FUNC_SETLONGADDRESS  9
#define USBASP_FUNC_SETISPSCK       10
#define USBASP_FUNC_TPI_CONNECT     11
#define USBASP_FUNC_TPI_DISCONNECT  12
#define USBASP_FUNC_TPI_RAWREAD     13
#define USBASP_FUNC_TPI_RAWWRITE    14
#define USBASP_FUNC_TPI_READBLOCK   15
#define USBASP_FUNC_TPI_WRITEBLOCK  16
#define USBASP_FUNC_GETCAPABILITIES 127

/* USBasp-Pico HVSP extension (vendor-specific, NOT part of original USBasp).
 * These request codes are above the USBasp range so avrdude's standard
 * -c usbasp flow never reaches them. They are accessed via the companion
 * Python host tool (usbasp_pico_hvsp.py). */
#define USBASP_PICO_HVSP_CONNECT     200
#define USBASP_PICO_HVSP_DISCONNECT  201
#define USBASP_PICO_HVSP_CHIP_ERASE  202
#define USBASP_PICO_HVSP_READ_SIG    203
#define USBASP_PICO_HVSP_READ_FUSE   204
#define USBASP_PICO_HVSP_WRITE_FUSE  205
#define USBASP_PICO_HVSP_READ_FLASH  206
#define USBASP_PICO_HVSP_WRITE_FLASH 207

/* USBasp-Pico SWIO extension (vendor-specific, NOT part of original USBasp).
 * Same scheme as HVSP: codes above the USBasp range so avrdude's standard
 * -c usbasp flow never reaches them. The Python host tool
 * (usbasp_pico_swio.py) can drive SWIO either via these vendor requests
 * (pyusb, preferred) or via the CDC ACM virtual serial port (pyserial,
 * human-friendly terminal). */
#define USBASP_PICO_SWIO_INIT         210
#define USBASP_PICO_SWIO_RESET_TARGET  211
#define USBASP_PICO_SWIO_READ_REG     212
#define USBASP_PICO_SWIO_WRITE_REG    213

/* USBasp-Pico JTAG/SWD extension (vendor-specific, v1.10).
 * Codes above the SWIO range so avrdude / HVSP / SWIO flows never reach them.
 * Accessed by two host-side tools that share the same backend:
 *   - scripts/usbasp_pico_probe.py        (pyOCD DebugProbe plugin)
 *   - scripts/usbasp_pico_jtag_bridge.py  (OpenOCD remote_bitbang TCP bridge)
 *
 * The backend is tool-agnostic: it accepts a sequence of (TCK, TMS, TDI) bit
 * tuples and returns the sampled TDO bits. SWD is handled by bitbanging the
 * SWDIO line (TMS pin, direction-switched) under host control. */
#define USBASP_PICO_JTAG_INIT        220  /* OUT: wValue=protocol (0=JTAG,1=SWD),
                                          *      wIndex=speed_kHz; inits JTAG pins */
#define USBASP_PICO_JTAG_SEQ         221  /* OUT bulk: bytes of packed bits, each byte
                                          *      = (tdi<<0)|(tms<<1)|(tck<<2) per bit;
                                          *      returns TDO bits packed MSB-first */
#define USBASP_PICO_JTAG_SWD_SEQ     222  /* IN/OUT: SWD sequence, host drives direction;
                                          *      payload: [dir_bitmap, nbits, data...] */
#define USBASP_PICO_JTAG_RESET       223  /* OUT: wValue=(ntrst<<0)|(nsrst<<1);
                                          *      0=release, 1=assert */
#define USBASP_PICO_JTAG_SPEED       224  /* OUT: wValue=kHz; sets TCK period */
#define USBASP_PICO_JTAG_PINSET      225  /* OUT: direct pin drive for remote_bitbang
                                          *      single-bit ops; wValue=(tck<<0)|
                                          *      (tms<<1)|(tdi<<2)|(ntrst<<3)|(nsrst<<4);
                                          *      returns sampled (tdo<<0) */
#define USBASP_PICO_JTAG_RELEASE     226  /* OUT: tri-state all JTAG pins, return to idle */

/* Capabilities flags. bit 0 = TPI (original USBasp), bit 1 = HVSP,
 * bit 2 = SWIO, bit 3 = JTAG/SWD (v1.10). */
#define USBASP_CAP_0_TPI            0x01
#define USBASP_CAP_1_HVSP           0x02
#define USBASP_CAP_2_SWIO           0x04
#define USBASP_CAP_3_JTAG           0x08

/* Programming state machine */
typedef enum {
    PROG_STATE_IDLE        = 0,
    PROG_STATE_WRITEFLASH  = 1,
    PROG_STATE_READFLASH   = 2,
    PROG_STATE_READEEPROM  = 3,
    PROG_STATE_WRITEEEPROM = 4,
    PROG_STATE_TPI_READ    = 5,
    PROG_STATE_TPI_WRITE   = 6,
} prog_state_t;

/* Block mode flags (used by paged flash writes) */
#define PROG_BLOCKFLAG_FIRST    0x01
#define PROG_BLOCKFLAG_LAST     0x02

/* ISP SCK speed identifiers - exact values preserved for avrdude compatibility */
#define USBASP_ISP_SCK_AUTO     0
#define USBASP_ISP_SCK_0_5      1   /*   500 Hz */
#define USBASP_ISP_SCK_1        2   /*   1 kHz */
#define USBASP_ISP_SCK_2        3   /*   2 kHz */
#define USBASP_ISP_SCK_4        4   /*   4 kHz */
#define USBASP_ISP_SCK_8        5   /*   8 kHz */
#define USBASP_ISP_SCK_16      6   /*  16 kHz */
#define USBASP_ISP_SCK_32      7   /*  32 kHz */
#define USBASP_ISP_SCK_93_75   8   /*  93.75 kHz */
#define USBASP_ISP_SCK_187_5   9   /* 187.5 kHz */
#define USBASP_ISP_SCK_375     10  /* 375 kHz (default) */
#define USBASP_ISP_SCK_750     11  /* 750 kHz */
#define USBASP_ISP_SCK_1500    12  /* 1.5 MHz */

/* USB identifiers used by USBasp (matches avrdude's usbasp.c) */
#define USBASP_VID    0x16C0   /* obdev's shared VID */
#define USBASP_PID    0x05DC   /* obdev's shared PID for USBasp */
#define USBASP_BCD    0x0104   /* 1.04 */

#endif /* _USBASP_PROTOCOL_H_ */

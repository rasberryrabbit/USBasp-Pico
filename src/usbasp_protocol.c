/*
 * usbasp_protocol.c - implementation of the USBasp vendor-class protocol
 *
 * Hooks into TinyUSB's vendor class driver. The entire USBasp protocol uses
 * control transfers (EP0):
 *   - Short commands (CONNECT, TRANSMIT, ENABLEPROG, SETISPSCK, etc.) carry
 *     their payload in wValue/wIndex and reply with <= 8 bytes.
 *   - Block transfers (READFLASH, WRITEFLASH, READEEPROM, WRITEEEPROM,
 *     TPI_READBLOCK, TPI_WRITEBLOCK) carry the bulk data in the data stage
 *     of the control transfer (wLength bytes).
 *
 * TinyUSB's control-transfer primitives:
 *   tud_control_xfer(rhport, request, buffer, len)
 *       Send `len` bytes (IN direction) or receive `len` bytes (OUT
 *       direction) into `buffer` as the data stage. The buffer must be
 *       valid for the entire transfer; for OUT the data is only available
 *       after the next CONTROL_STAGE_DATA or CONTROL_STAGE_ACK callback.
 *   tud_control_status(rhport, request)
 *       Send a zero-length status stage (no data stage).
 *
 * We use a single 4 KB scratch buffer for block transfers. avrdude's
 * chunk_size is 4 KB by default, so this is sufficient.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tusb.h"
#include "pico/stdlib.h"
#include <string.h>

#include "usbasp_protocol.h"
#include "usbasp.h"
#include "isp.h"
#include "tpi.h"
#include "hvsp.h"
#include "swio.h"
#include "jtag.h"

/* ---- programming state ---- */
uint8_t  prog_sck = USBASP_ISP_SCK_AUTO;
bool     prog_address_newmode = false;
uint32_t prog_address = 0;
uint16_t prog_nbytes = 0;
uint16_t prog_pagesize = 0;
uint8_t  prog_blockflags = 0;
uint8_t  prog_pagecounter = 0;
prog_state_t prog_state = PROG_STATE_IDLE;

static uint8_t reply_buffer[8];

/* Scratch buffer for block transfers. EP0 full-speed max packet is 64 bytes;
 * avrdude requests up to 4096 bytes per control transfer. */
#define BLOCK_SCRATCH_SIZE  4096
static uint8_t block_scratch[BLOCK_SCRATCH_SIZE] __attribute__((aligned(4)));

/* Pending block write state - data arrives in the data stage and we process
 * it during the ACK stage callback. */
static bool block_write_pending = false;
static uint16_t block_write_len = 0;

/* Pending HVSP flash word write (1 word = 2 bytes) - the address is captured
 * during SETUP, the 2-byte data arrives in the data stage, and we call
 * hvsp_write_flash_word() in the ACK stage. */
#ifdef USBASP_PICO_ENABLE_HVSP
static bool hvsp_flash_pending = false;
static uint16_t hvsp_flash_addr = 0;
#endif

/* Pending SWIO register write (4 bytes) - the register address is captured
 * during SETUP, the 4-byte value arrives in the data stage, and we call
 * swio_write_reg() in the ACK stage. */
static bool swio_reg_pending = false;
static uint8_t swio_reg_addr = 0;

/* Pending JTAG bit sequence (USBASP_PICO_JTAG_SEQ). The packed (tdi,tms,tck)
 * tuples arrive in the data stage into block_scratch; on the ACK stage we
 * run jtag_seq() and store the TDO result back into block_scratch (packed
 * MSB-first, ceil(nbytes/8) bytes). The host then issues a follow-up IN
 * control transfer (USBASP_PICO_JTAG_SEQ with bmRequestType IN, 0xC0) to
 * read the TDO bytes back. We track the result byte count here. */
#ifdef USBASP_PICO_ENABLE_JTAG
static bool     jtag_seq_pending = false;
static uint16_t jtag_seq_len = 0;        /* number of input bytes (= TDO bits) */
static uint16_t jtag_seq_result_bytes = 0; /* ceil(jtag_seq_len/8) */
#endif

/* ---- TinyUSB vendor-class callback ---- */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                 tusb_control_request_t const *request)
{
    /* Ignore DATA / ACK stages unless we have a pending block write. */
    if (stage != CONTROL_STAGE_SETUP) {
        /* ACK stage of an OUT (write) block transfer: the data has now been
         * fully received into block_scratch[]. Process it here. */
        if (stage == CONTROL_STAGE_ACK && block_write_pending) {
            block_write_pending = false;
            uint16_t len = block_write_len;
            uint16_t i = 0;

            if (prog_state == PROG_STATE_WRITEFLASH) {
                for (i = 0; i < len; i++) {
                    if (prog_pagesize == 0) {
                        isp_write_flash(prog_address, block_scratch[i], 1);
                    } else {
                        isp_write_flash(prog_address, block_scratch[i], 0);
                        prog_pagecounter--;
                        if (prog_pagecounter == 0) {
                            isp_flush_page(prog_address, block_scratch[i]);
                            prog_pagecounter = (uint8_t)prog_pagesize;
                        }
                    }
                    prog_nbytes--;
                    prog_address++;
                    if (prog_nbytes == 0) {
                        prog_state = PROG_STATE_IDLE;
                        if ((prog_blockflags & PROG_BLOCKFLAG_LAST) &&
                            (prog_pagecounter != (uint8_t)prog_pagesize)) {
                            isp_flush_page(prog_address, block_scratch[i]);
                        }
                        break;
                    }
                }
            } else if (prog_state == PROG_STATE_WRITEEEPROM) {
                for (i = 0; i < len; i++) {
                    isp_write_eeprom((uint16_t)prog_address, block_scratch[i]);
                    prog_nbytes--;
                    prog_address++;
                    if (prog_nbytes == 0) {
                        prog_state = PROG_STATE_IDLE;
                        break;
                    }
                }
            } else if (prog_state == PROG_STATE_TPI_WRITE) {
                uint8_t n = (uint8_t)((len < 0xff) ? len : 0xff);
                tpi_write_block((uint16_t)prog_address, block_scratch, n);
                prog_address += n;
                prog_nbytes -= n;
                if (prog_nbytes == 0) prog_state = PROG_STATE_IDLE;
            }
        }
#ifdef USBASP_PICO_ENABLE_HVSP
        /* HVSP flash word write: data stage has delivered 2 bytes into
         * block_scratch; combine them into a 16-bit word and commit. */
        if (stage == CONTROL_STAGE_ACK && hvsp_flash_pending) {
            hvsp_flash_pending = false;
            uint16_t word = (uint16_t)block_scratch[0] |
                            ((uint16_t)block_scratch[1] << 8);
            hvsp_write_flash_word(hvsp_flash_addr, word);
        }
#endif
        /* SWIO register write: data stage has delivered 4 bytes (little-
         * endian 32-bit value) into block_scratch; commit via swio_write_reg. */
        if (stage == CONTROL_STAGE_ACK && swio_reg_pending) {
            swio_reg_pending = false;
            uint32_t value = (uint32_t)block_scratch[0]
                           | ((uint32_t)block_scratch[1] << 8)
                           | ((uint32_t)block_scratch[2] << 16)
                           | ((uint32_t)block_scratch[3] << 24);
            swio_write_reg(swio_reg_addr, value);
        }
#ifdef USBASP_PICO_ENABLE_JTAG
        /* JTAG bit sequence: data stage has delivered jtag_seq_len bytes of
         * packed (tdi,tms,tck) tuples into block_scratch. Run jtag_seq() and
         * write the TDO bits (packed MSB-first) back into block_scratch[0..].
         * The host reads them back with a follow-up IN control transfer of
         * the same request (handled in the SETUP stage below by checking
         * the direction bit). */
        if (stage == CONTROL_STAGE_ACK && jtag_seq_pending) {
            jtag_seq_pending = false;
            uint16_t nbits = jtag_seq_len;
            uint16_t out_bytes = (uint16_t)((nbits + 7) / 8);
            /* Run the sequence; TDO is written MSB-first into block_scratch.
             * We use a temporary buffer to avoid clobbering input while
             * reading - but jtag_seq reads input[i] and writes out[i/8],
             * and i/8 <= i always, so in-place is safe. */
            jtag_seq(block_scratch, nbits, block_scratch);
            /* Zero the trailing unused bits of the last byte for cleanliness. */
            if (out_bytes > 0 && (nbits & 7)) {
                uint8_t used = (uint8_t)(nbits & 7);
                uint8_t mask = (uint8_t)(0xFF << (8 - used));
                block_scratch[out_bytes - 1] &= mask;
            }
            jtag_seq_result_bytes = out_bytes;
        }
#endif
        return true;
    }

    /* SETUP stage: reject non-vendor requests */
    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) {
        return false;
    }

    uint8_t func = request->bRequest;
    uint8_t b0 = (uint8_t)(request->wValue & 0xFF);
    uint8_t b1 = (uint8_t)(request->wValue >> 8);
    uint8_t b2 = (uint8_t)(request->wIndex & 0xFF);
    uint8_t b3 = (uint8_t)(request->wIndex >> 8);
    uint16_t wlen = request->wLength;

    memset(reply_buffer, 0, sizeof(reply_buffer));

    switch (func) {
    case USBASP_FUNC_CONNECT:
        isp_set_sck_option(prog_sck);
        prog_address_newmode = false;
        led_activity_on();
        isp_connect();
        return tud_control_status(rhport, request);

    case USBASP_FUNC_DISCONNECT:
        isp_disconnect();
        led_activity_off();
        return tud_control_status(rhport, request);

    case USBASP_FUNC_TRANSMIT:
        reply_buffer[0] = isp_transmit(b0);
        reply_buffer[1] = isp_transmit(b1);
        reply_buffer[2] = isp_transmit(b2);
        reply_buffer[3] = isp_transmit(b3);
        return tud_control_xfer(rhport, request, reply_buffer, 4);

    case USBASP_FUNC_READFLASH:
        if (!prog_address_newmode)
            prog_address = ((uint32_t)b1 << 8) | b0;
        prog_nbytes = wlen;
        prog_state = PROG_STATE_READFLASH;
        {
            uint16_t to_read = wlen;
            if (to_read > BLOCK_SCRATCH_SIZE) to_read = BLOCK_SCRATCH_SIZE;
            for (uint16_t i = 0; i < to_read && prog_nbytes > 0; i++, prog_nbytes--) {
                block_scratch[i] = isp_read_flash(prog_address++);
            }
            if (prog_nbytes == 0) prog_state = PROG_STATE_IDLE;
            return tud_control_xfer(rhport, request, block_scratch, to_read);
        }

    case USBASP_FUNC_READEEPROM:
        if (!prog_address_newmode)
            prog_address = ((uint32_t)b1 << 8) | b0;
        prog_nbytes = wlen;
        prog_state = PROG_STATE_READEEPROM;
        {
            uint16_t to_read = wlen;
            if (to_read > BLOCK_SCRATCH_SIZE) to_read = BLOCK_SCRATCH_SIZE;
            for (uint16_t i = 0; i < to_read && prog_nbytes > 0; i++, prog_nbytes--) {
                block_scratch[i] = isp_read_eeprom((uint16_t)prog_address++);
            }
            if (prog_nbytes == 0) prog_state = PROG_STATE_IDLE;
            return tud_control_xfer(rhport, request, block_scratch, to_read);
        }

    case USBASP_FUNC_ENABLEPROG:
        reply_buffer[0] = isp_enter_programming_mode();
        return tud_control_xfer(rhport, request, reply_buffer, 1);

    case USBASP_FUNC_WRITEFLASH:
        if (!prog_address_newmode)
            prog_address = ((uint32_t)b1 << 8) | b0;
        prog_pagesize = b2;
        prog_blockflags = b3 & 0x0F;
        prog_pagesize += (((uint16_t)(b3 & 0xF0)) << 4);
        if (prog_blockflags & PROG_BLOCKFLAG_FIRST) {
            prog_pagecounter = (uint8_t)prog_pagesize;
        }
        prog_nbytes = wlen;
        prog_state = PROG_STATE_WRITEFLASH;
        {
            uint16_t to_recv = wlen;
            if (to_recv > BLOCK_SCRATCH_SIZE) to_recv = BLOCK_SCRATCH_SIZE;
            block_write_pending = true;
            block_write_len = to_recv;
            return tud_control_xfer(rhport, request, block_scratch, to_recv);
        }

    case USBASP_FUNC_WRITEEEPROM:
        if (!prog_address_newmode)
            prog_address = ((uint32_t)b1 << 8) | b0;
        prog_pagesize = 0;
        prog_blockflags = 0;
        prog_nbytes = wlen;
        prog_state = PROG_STATE_WRITEEEPROM;
        {
            uint16_t to_recv = wlen;
            if (to_recv > BLOCK_SCRATCH_SIZE) to_recv = BLOCK_SCRATCH_SIZE;
            block_write_pending = true;
            block_write_len = to_recv;
            return tud_control_xfer(rhport, request, block_scratch, to_recv);
        }

    case USBASP_FUNC_SETLONGADDRESS:
        prog_address_newmode = true;
        prog_address = ((uint32_t)b3 << 24) | ((uint32_t)b2 << 16) |
                       ((uint32_t)b1 << 8)  | b0;
        return tud_control_status(rhport, request);

    case USBASP_FUNC_SETISPSCK:
        prog_sck = b0;
        reply_buffer[0] = 0;
        return tud_control_xfer(rhport, request, reply_buffer, 1);

    case USBASP_PICO_ISP_SETSCK_CUSTOM: {
        /* Custom SCK frequency: wValue=freq[15:0], wIndex=freq[31:16].
         * Returns actual achieved frequency as 4-byte LE32. */
        uint32_t freq = (uint32_t)b0 | ((uint32_t)b1 << 8)
                      | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
        uint32_t actual = isp_set_sck_freq_custom(freq);
        prog_sck = USBASP_ISP_SCK_AUTO;  /* mark as custom (not a discrete option) */
        reply_buffer[0] = (uint8_t)(actual);
        reply_buffer[1] = (uint8_t)(actual >> 8);
        reply_buffer[2] = (uint8_t)(actual >> 16);
        reply_buffer[3] = (uint8_t)(actual >> 24);
        return tud_control_xfer(rhport, request, reply_buffer, 4);
    }

    case USBASP_FUNC_TPI_CONNECT:
        tpi_dly_cnt = ((uint16_t)b1 << 8) | b0;
        gpio_put(PIN_ISP_RESET, 1);
        gpio_set_dir(PIN_ISP_RESET, GPIO_OUT);
        busy_wait_us_32(1);
        gpio_put(PIN_ISP_RESET, 0);
        led_activity_on();
        busy_wait_us_32(8);
        tpi_init();
        return tud_control_status(rhport, request);

    case USBASP_FUNC_TPI_DISCONNECT:
        tpi_send_byte(TPI_OP_SSTCS(TPISR));
        tpi_send_byte(0);
        busy_wait_us_32(5);
        gpio_put(PIN_ISP_RESET, 1);
        busy_wait_us_32(3);
        gpio_put(PIN_ISP_RESET, 0);
        busy_wait_us_32(3);
        gpio_set_dir(PIN_ISP_RESET, GPIO_IN);
        gpio_set_dir(PIN_ISP_SCK, GPIO_IN);
        gpio_set_dir(PIN_ISP_MOSI, GPIO_IN);
        led_activity_off();
        return tud_control_status(rhport, request);

    case USBASP_FUNC_TPI_RAWREAD:
        reply_buffer[0] = tpi_recv_byte();
        return tud_control_xfer(rhport, request, reply_buffer, 1);

    case USBASP_FUNC_TPI_RAWWRITE:
        tpi_send_byte(b0);
        return tud_control_status(rhport, request);

    case USBASP_FUNC_TPI_READBLOCK:
        prog_address = ((uint32_t)b1 << 8) | b0;
        prog_nbytes = wlen;
        prog_state = PROG_STATE_TPI_READ;
        {
            uint16_t to_read = wlen;
            if (to_read > BLOCK_SCRATCH_SIZE) to_read = BLOCK_SCRATCH_SIZE;
            uint8_t n = (uint8_t)((to_read < 0xff) ? to_read : 0xff);
            tpi_read_block((uint16_t)prog_address, block_scratch, n);
            prog_address += n;
            prog_nbytes -= n;
            if (prog_nbytes == 0) prog_state = PROG_STATE_IDLE;
            return tud_control_xfer(rhport, request, block_scratch, to_read);
        }

    case USBASP_FUNC_TPI_WRITEBLOCK:
        prog_address = ((uint32_t)b1 << 8) | b0;
        prog_nbytes = wlen;
        prog_state = PROG_STATE_TPI_WRITE;
        {
            uint16_t to_recv = wlen;
            if (to_recv > BLOCK_SCRATCH_SIZE) to_recv = BLOCK_SCRATCH_SIZE;
            block_write_pending = true;
            block_write_len = to_recv;
            return tud_control_xfer(rhport, request, block_scratch, to_recv);
        }

    case USBASP_FUNC_GETCAPABILITIES:
        /* Advertise TPI (original USBasp), HVSP (Pico ext), SWIO (Pico ext),
         * and JTAG/SWD (Pico ext, v1.10). */
        reply_buffer[0] = USBASP_CAP_0_TPI
                        | USBASP_CAP_1_HVSP
                        | USBASP_CAP_2_SWIO
#ifdef USBASP_PICO_ENABLE_JTAG
                        | USBASP_CAP_3_JTAG
#endif
                        ;
        reply_buffer[1] = 0;
        reply_buffer[2] = 0;
        reply_buffer[3] = 0;
        return tud_control_xfer(rhport, request, reply_buffer, 4);

#ifdef USBASP_PICO_ENABLE_HVSP
    /* ---- HVSP extension (USBasp-Pico only, avrdude -c usbasp ignores) ---- */

    case USBASP_PICO_HVSP_CONNECT:
        /* Enter HVSP mode: applies 12 V to RESET via external boost. */
        hvsp_enter_mode();
        return tud_control_status(rhport, request);

    case USBASP_PICO_HVSP_DISCONNECT:
        /* Exit HVSP mode: disables 12 V, releases HVSP pins. */
        hvsp_exit_mode();
        return tud_control_status(rhport, request);

    case USBASP_PICO_HVSP_CHIP_ERASE:
        hvsp_chip_erase();
        return tud_control_status(rhport, request);

    case USBASP_PICO_HVSP_READ_SIG:
        /* b0 = signature byte index (0, 1, or 2). */
        reply_buffer[0] = hvsp_read_signature(b0);
        return tud_control_xfer(rhport, request, reply_buffer, 1);

    case USBASP_PICO_HVSP_READ_FUSE:
        /* b0 = which fuse (0=low, 1=high, 2=extended). */
        reply_buffer[0] = hvsp_read_fuse(b0);
        return tud_control_xfer(rhport, request, reply_buffer, 1);

    case USBASP_PICO_HVSP_WRITE_FUSE:
        /* b0 = which fuse (0=low, 1=high, 2=extended), b1 = value. */
        hvsp_write_fuse(b0, b1);
        return tud_control_status(rhport, request);

    case USBASP_PICO_HVSP_READ_FLASH:
        /* wValue low byte  = address high; wIndex low byte = address low.
         * Returns 1 byte (one HVSP flash byte per request, MSB-aligned). */
        {
            uint16_t word_addr = ((uint16_t)b0 << 8) | b1;
            reply_buffer[0] = hvsp_read_flash(word_addr);
            return tud_control_xfer(rhport, request, reply_buffer, 1);
        }

    case USBASP_PICO_HVSP_WRITE_FLASH:
        /* wValue low byte = addr high, wIndex low byte = addr low.
         * The 2 data bytes (low word, high word) are sent via the data stage
         * and committed during the ACK stage. */
        hvsp_flash_addr = ((uint16_t)b0 << 8) | b1;
        hvsp_flash_pending = true;
        return tud_control_xfer(rhport, request, block_scratch, 2);
#endif /* USBASP_PICO_ENABLE_HVSP */

    /* ---- SWIO extension (USBasp-Pico only, avrdude -c usbasp ignores) ----
     * These requests drive the WCH CH32V003 RISC-V SWIO interface. The
     * companion Python tool `usbasp_pico_swio.py` uses pyusb to send them.
     */

    case USBASP_PICO_SWIO_INIT:
        /* Initialize the SWIO pin (open-drain, idle high). */
        swio_init();
        return tud_control_status(rhport, request);

    case USBASP_PICO_SWIO_RESET_TARGET:
        /* Pulse ~50 high clocks to force the CH32V003 debug module into
         * a known state. */
        swio_reset_target();
        return tud_control_status(rhport, request);

    case USBASP_PICO_SWIO_READ_REG:
        /* wValue low byte = register address (0x00-0x7F).
         * Returns 4 bytes (little-endian 32-bit value). */
        {
            uint32_t val = swio_read_reg(b0);
            reply_buffer[0] = (uint8_t)(val & 0xFF);
            reply_buffer[1] = (uint8_t)((val >> 8) & 0xFF);
            reply_buffer[2] = (uint8_t)((val >> 16) & 0xFF);
            reply_buffer[3] = (uint8_t)((val >> 24) & 0xFF);
            return tud_control_xfer(rhport, request, reply_buffer, 4);
        }

    case USBASP_PICO_SWIO_WRITE_REG:
        /* wValue low byte = register address (0x00-0x7F).
         * The 4-byte little-endian value is sent via the data stage and
         * committed during the ACK stage (swio_write_reg is called there). */
        swio_reg_addr = b0;
        swio_reg_pending = true;
        return tud_control_xfer(rhport, request, block_scratch, 4);

#ifdef USBASP_PICO_ENABLE_JTAG
    /* ---- JTAG / SWD extension (v1.10, USBasp-Pico only) ----
     * Tool-agnostic bit-bang backend shared by:
     *   - scripts/usbasp_pico_probe.py        (pyOCD DebugProbe plugin)
     *   - scripts/usbasp_pico_jtag_bridge.py  (OpenOCD remote_bitbang bridge)
     */

    case USBASP_PICO_JTAG_INIT:
        /* b0 = protocol (0=JTAG, 1=SWD), b2 = speed in kHz (low byte). */
        jtag_init(b0, (uint16_t)b2);
        return tud_control_status(rhport, request);

    case USBASP_PICO_JTAG_RELEASE:
        /* Tri-state all JTAG/SWD pins, return to idle. */
        jtag_release();
        return tud_control_status(rhport, request);

    case USBASP_PICO_JTAG_SPEED:
        /* b0+b1 = speed in kHz (little-endian). */
        jtag_set_speed((uint16_t)((uint16_t)b0 | ((uint16_t)b1 << 8)));
        return tud_control_status(rhport, request);

    case USBASP_PICO_JTAG_RESET:
        /* b0 = mask (bit0=nTRST, bit1=nSRST), b1 = value (0=assert, 1=release). */
        jtag_reset(b0, b1);
        return tud_control_status(rhport, request);

    case USBASP_PICO_JTAG_PINSET:
        /* b0 = drive value bitmap (bit0=TCK, bit1=TMS, bit2=TDI,
         * bit3=nTRST(assert-low), bit4=nSRST(assert-low)).
         * Returns sampled TDO in bit0. */
        reply_buffer[0] = jtag_pinset(b0);
        return tud_control_xfer(rhport, request, reply_buffer, 1);

    case USBASP_PICO_JTAG_SEQ:
        /* Bidirectional control transfer:
         *   OUT (0x40): wLength bytes of packed (tdi,tms,tck) tuples arrive
         *               in the data stage; we clock them on the ACK stage
         *               and stash TDO bits in block_scratch (MSB-first).
         *   IN  (0xC0): host reads back the TDO result bytes. We return
         *               jtag_seq_result_bytes from the last OUT run. */
        if (request->bmRequestType_bit.direction == TUSB_DIR_IN) {
            uint16_t to_ret = jtag_seq_result_bytes;
            if (to_ret > BLOCK_SCRATCH_SIZE) to_ret = BLOCK_SCRATCH_SIZE;
            return tud_control_xfer(rhport, request, block_scratch, to_ret);
        }
        /* OUT direction: receive the input tuples into block_scratch. */
        jtag_seq_pending = true;
        jtag_seq_len = (uint16_t)(wlen < BLOCK_SCRATCH_SIZE ? wlen : BLOCK_SCRATCH_SIZE);
        return tud_control_xfer(rhport, request, block_scratch, jtag_seq_len);

    case USBASP_PICO_JTAG_SWD_SEQ:
        /* One-byte SWD sequence. b0 = dir_bitmap, b1 = nbits (1..8),
         * b2 = data_in. Returns sampled data_out in b0 of the reply. */
        {
            uint8_t dout = 0;
            jtag_swd_seq(b0, b1, b2, &dout);
            reply_buffer[0] = dout;
            return tud_control_xfer(rhport, request, reply_buffer, 1);
        }
#endif /* USBASP_PICO_ENABLE_JTAG */

    default:
        /* Unknown request - stall */
        return false;
    }
}

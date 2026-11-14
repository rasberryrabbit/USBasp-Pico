/*
 * swio.c - SWIO (Single-Wire I/O) backend for WCH CH32V003 RISC-V MCU
 *
 * v1.11: fully ported from picorvd (PicoSWIO.cpp + singlewire.pio).
 *
 * Compared to the previous v1.10 backend this version fixes the actual
 * WCH SDI protocol, which the old code got wrong in several ways:
 *
 *   1. Address and data must be BIT-INVERTED on the wire
 *      (old code sent them non-inverted -> target never decoded frames).
 *   2. R/W bit on the wire: 1 = read, 0 = write.
 *   3. Bit timing is a hardware-generated ~2T/8T pulse pair
 *      (clkdiv = 12 -> 96ns/tick, WCH "normal" mode; the old bit-bang
 *      commands produced jittered gaps and a '0' pulse below the
 *      6T minimum, both of which can desync the target).
 *   4. Reads are sampled by the PIO itself (in pins + autopush),
 *      not by software side-counting.
 *   5. Target reset is picorvd's sequence: short low pulse, then
 *      SHDWCFGR/CFGR unlock (0x5AA50400) and DMCONTROL reset - without
 *      the unlock the CH32V003 debug module stays silent.
 *
 * Frame (MSB-first): start('1') + 7-bit ~addr + R/W + 32-bit ~data.
 *
 * Pin: GP12 (SWIO), open-drain with external 4.7k-10k pull-up.
 * PIO: pio0 / SM0 (sole user of pio0). OUT pin level = 0 always, so
 * enabling the pin direction (sideset) drives the line low.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "swio.h"
#include "swio.pio.h"

#define SWIO_PIO    pio0
#define SWIO_SM     0

/* WCH-specific debug interface config registers */
#define WCH_DM_CFGR     0x7D
#define WCH_DM_SHDWCFGR 0x7E

/* RISC-V debug spec registers */
#define DM_DMCONTROL    0x10

static uint   s_offset = 0;
static bool   s_ready  = false;

uint32_t swio_xfer(uint8_t reg, bool rw, uint32_t data);

static inline uint32_t swio_get(uint8_t addr)
{
    /* Wire: ((~addr)<<1) | 1 -> READ. Reply comes back MSB-first via
     * autopush; one RX word per frame, FIFO-ordered with commands. */
    pio_sm_put_blocking(SWIO_PIO, SWIO_SM,
                        (((~addr) & 0x7F) << 1) | 1);
    return pio_sm_get_blocking(SWIO_PIO, SWIO_SM);
}

static inline void swio_put(uint8_t addr, uint32_t data)
{
    /* Wire: ((~addr)<<1) | 0 -> WRITE, then the inverted data word.
     * The PIO stalls on the second pull until the first frame is out,
     * so back-to-back writes stay correctly ordered. */
    pio_sm_put_blocking(SWIO_PIO, SWIO_SM,
                        (((~addr) & 0x7F) << 1) | 0);
    pio_sm_put_blocking(SWIO_PIO, SWIO_SM, ~data);
}

void swio_init(void)
{
    /* Repeated calls (CDC 'p' / SWIO_INIT request): restart cleanly
     * without re-uploading the program. */
    if (s_ready) {
        pio_sm_set_enabled(SWIO_PIO, SWIO_SM, false);
        pio_sm_clear_fifos(SWIO_PIO, SWIO_SM);
        pio_sm_restart(SWIO_PIO, SWIO_SM);
        pio_sm_set_consecutive_pindirs(SWIO_PIO, SWIO_SM, PIN_SWIO, 1, false);
        pio_sm_set_enabled(SWIO_PIO, SWIO_SM, true);
        busy_wait_us_32(20);
        return;
    }

    /* We are pio0's only user - clear its instruction memory like picorvd. */
    pio_sm_claim(SWIO_PIO, SWIO_SM);
    pio_clear_instruction_memory(SWIO_PIO);
    s_offset = pio_add_program(SWIO_PIO, &swio_program);

    /* GPIO: low drive strength + slow slew on the open-drain line. */
    gpio_set_drive_strength(PIN_SWIO, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_slew_rate(PIN_SWIO, GPIO_SLEW_RATE_SLOW);
    gpio_set_function(PIN_SWIO, GPIO_FUNC_PIO0);
    gpio_pull_up(PIN_SWIO);

    pio_sm_config c = swio_program_get_default_config(s_offset);
    sm_config_set_sideset(&c, 1, /*optional*/ false, /*pindirs*/ true);
    sm_config_set_out_pins    (&c, PIN_SWIO, 1);
    sm_config_set_in_pins     (&c, PIN_SWIO);
    sm_config_set_set_pins    (&c, PIN_SWIO, 1);
    sm_config_set_sideset_pins(&c, PIN_SWIO);

    /* MSB-first shifts; autopush(32) on reads, manual pull on writes. */
    sm_config_set_out_shift(&c, /*shift_right*/ false, /*autopull*/ false, 32);
    sm_config_set_in_shift(&c, /*shift_right*/ false, /*autopush*/ true, 32);

    /* 125MHz / 12 = 96ns per tick - matches WCH "normal" mode (T~100ns). */
    sm_config_set_clkdiv(&c, 12);

    pio_sm_init(SWIO_PIO, SWIO_SM, s_offset, &c);

    /* OUT pin level 0: when the SM enables the pin (side 1) it drives
     * LOW; when released (side 0) the pull-up idles the line HIGH. */
    pio_sm_set_pins(SWIO_PIO, SWIO_SM, 0);
    pio_sm_set_consecutive_pindirs(SWIO_PIO, SWIO_SM, PIN_SWIO, 1, false);
    pio_sm_set_enabled(SWIO_PIO, SWIO_SM, true);

    s_ready = true;
    busy_wait_us_32(20);
}

uint32_t swio_xfer(uint8_t reg, bool rw, uint32_t data)
{
    if (rw) {
        /* Host writes `data` to register `reg`. */
        swio_put(reg, data);
        return data;
    } else {
        /* Host reads register `reg`; the target's reply is returned as-is
         * (same as picorvd: PicoSWIO::get returns the raw PIO word). */
        return swio_get(reg);
    }
}

void swio_reset_target(void)
{
    /* Jitter-free long low pulse via raw SIO registers (per picorvd):
     * >16 SWIO clock periods at HSI/3 = 8MHz forces the debug module
     * into reset. 10us is a wide margin above the 2us minimum. */
    sio_hw->gpio_clr    = (1u << PIN_SWIO);
    sio_hw->gpio_oe_set = (1u << PIN_SWIO);
    io_bank0_hw->io[PIN_SWIO].ctrl =
        GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    busy_wait_us_32(10);
    sio_hw->gpio_oe_clr = (1u << PIN_SWIO);
    io_bank0_hw->io[PIN_SWIO].ctrl =
        GPIO_FUNC_PIO0 << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    busy_wait_us_32(10);

    /* picorvd's debug-module bring-up: unlock the SDI output pin and
     * reset the DM. Without the SHDWCFGR/CFGR unlock the target's debug
     * interface does not respond to register accesses. */
    swio_put(WCH_DM_SHDWCFGR, 0x5AA50400);
    swio_put(WCH_DM_CFGR,     0x5AA50400);
    swio_put(DM_DMCONTROL,    0x00000000);
    swio_put(DM_DMCONTROL,    0x00000001);
    busy_wait_us_32(20);
}
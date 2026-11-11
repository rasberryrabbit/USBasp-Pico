/*
 * hvsp.c - High-Voltage Serial Programming (HVSP) backend for ATtiny85 etc.
 *
 * This backend implements the protocol described in Microchip's ATtiny25/45/85
 * datasheet "High-Voltage Serial Programming" section. It is enabled at
 * compile time with -DUSBASP_PICO_ENABLE_HVSP=1 and is *not* part of the
 * standard USBasp protocol - avrdude does not have a "usbasp HVSP" mode.
 * Instead, this backend is exposed via a private vendor command and a small
 * host-side tool (scripts/usbasp_pico_hvsp.py in the source tree) is provided
 * to drive fuse recovery.
 *
 * The 12 V is produced externally - either by a small boost converter (e.g.
 * MAX662A) controlled by the PIN_HVSP_12V_EN GPIO, or by a manual 12 V supply
 * the user toggles. The hardware enable pin is optional; if
 * USBASP_PICO_HVSP_12V_PIN is set to 0xff, the firmware assumes the 12 V is
 * always present during HVSP mode and only manages the digital protocol
 * pins.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hvsp.h"
#include "usbasp.h"

#ifdef USBASP_PICO_ENABLE_HVSP

void hvsp_init(void)
{
    /* SCI/SDI/SII as outputs, SDO as input, 12V_EN as output (off) */
    gpio_init(PIN_HVSP_SCI);  gpio_set_dir(PIN_HVSP_SCI, GPIO_OUT);  gpio_put(PIN_HVSP_SCI, 0);
    gpio_init(PIN_HVSP_SDI);  gpio_set_dir(PIN_HVSP_SDI, GPIO_OUT);  gpio_put(PIN_HVSP_SDI, 0);
    gpio_init(PIN_HVSP_SII);  gpio_set_dir(PIN_HVSP_SII, GPIO_OUT);  gpio_put(PIN_HVSP_SII, 0);
    gpio_init(PIN_HVSP_SDO);  gpio_set_dir(PIN_HVSP_SDO, GPIO_IN);
    gpio_disable_pulls(PIN_HVSP_SDO);

#if USBASP_PICO_HVSP_12V_PIN != 0xff
    gpio_init(PIN_HVSP_12V_EN);  gpio_set_dir(PIN_HVSP_12V_EN, GPIO_OUT);
    gpio_put(PIN_HVSP_12V_EN, 0);  /* 12 V OFF */
#endif
}

static inline void hvsp_clock(void)
{
    busy_wait_us_32(2);
    gpio_put(PIN_HVSP_SCI, 1);
    busy_wait_us_32(2);
    gpio_put(PIN_HVSP_SCI, 0);
    busy_wait_us_32(2);
}

void hvsp_enter_mode(void)
{
    /* Drive SDI/SII/SDO low (and SDO is set to output initially) */
    gpio_set_dir(PIN_HVSP_SDO, GPIO_OUT);
    gpio_put(PIN_HVSP_SDI, 0);
    gpio_put(PIN_HVSP_SII, 0);
    gpio_put(PIN_HVSP_SDO, 0);
    busy_wait_us_32(30);

    /* Apply 12 V to RESET */
#if USBASP_PICO_HVSP_12V_PIN != 0xff
    gpio_put(PIN_HVSP_12V_EN, 1);
#endif
    busy_wait_us_32(20);

    /* Release SDO so target can drive it */
    gpio_set_dir(PIN_HVSP_SDO, GPIO_IN);
    busy_wait_us_32(300);
}

void hvsp_exit_mode(void)
{
#if USBASP_PICO_HVSP_12V_PIN != 0xff
    gpio_put(PIN_HVSP_12V_EN, 0);  /* 12 V OFF */
#endif
    busy_wait_us_32(20);

    gpio_set_dir(PIN_HVSP_SDI, GPIO_IN);
    gpio_set_dir(PIN_HVSP_SII, GPIO_IN);
    gpio_set_dir(PIN_HVSP_SDO, GPIO_IN);
    gpio_set_dir(PIN_HVSP_SCI, GPIO_IN);
}

uint8_t hvsp_shift_out(uint8_t instruction, uint8_t data)
{
    /* Wait for SDO high (idle / ready) */
    int timeout = 5000;
    while (!gpio_get(PIN_HVSP_SDO) && --timeout > 0) { tight_loop_contents(); }
    if (timeout == 0) return 0xFF;

    /* Build 11-bit instruction + 11-bit data frames (per ATtiny85 datasheet
     * Fig 26-15: 11 bits each. We pack as (val<<1) into the leading 10 bits
     * plus the trailing MSB. The exact bit positions vary by AVR family;
     * this implementation targets ATtiny25/45/85. */
    uint16_t dout = (uint16_t)data << 2;
    uint16_t iout = (uint16_t)instruction << 2;
    uint8_t in_bits = 0;

    for (int i = 10; i >= 0; i--) {
        gpio_put(PIN_HVSP_SDI, (dout >> i) & 1);
        gpio_put(PIN_HVSP_SII, (iout >> i) & 1);
        in_bits <<= 1;
        if (gpio_get(PIN_HVSP_SDO)) in_bits |= 1;
        hvsp_clock();
    }
    return in_bits >> 2;
}

/* ATtiny85 HVSP command bytes (instruction field) - per datasheet Table 26-6 */
#define HVSP_CMD_CHIP_ERASE        0x80
#define HVSP_CMD_POLL_READY        0x00  /* SDO reads busy bit */
#define HVSP_CMD_READ_SIG          0x08
#define HVSP_CMD_READ_FUSE_LOW     0x04
#define HVSP_CMD_READ_FUSE_HIGH    0x04
#define HVSP_CMD_READ_FUSE_EXT     0x04
#define HVSP_CMD_WRITE_FUSE_LOW   0x40
#define HVSP_CMD_WRITE_FUSE_HIGH  0x40
#define HVSP_CMD_WRITE_FUSE_EXT   0x40

void hvsp_chip_erase(void)
{
    hvsp_shift_out(HVSP_CMD_CHIP_ERASE, 0x00);
    hvsp_shift_out(0x00, 0x4C);
    hvsp_shift_out(0x00, 0x64);
    hvsp_shift_out(0x00, 0x6C);
    /* Wait for completion */
    uint8_t r;
    do { r = hvsp_shift_out(HVSP_CMD_POLL_READY, 0x00); } while (!(r & 0x02));
}

uint8_t hvsp_read_signature(uint8_t index)
{
    hvsp_shift_out(HVSP_CMD_READ_SIG, 0x4C);
    hvsp_shift_out(index, 0x0C);
    hvsp_shift_out(0x00, 0x68);
    return hvsp_shift_out(0x00, 0x6C);
}

uint8_t hvsp_read_fuse(uint8_t which)
{
    hvsp_shift_out(HVSP_CMD_READ_FUSE_LOW, 0x4C);
    if (which == 0) hvsp_shift_out(0x00, 0x68);          /* low */
    else if (which == 1) hvsp_shift_out(0x00, 0x7A);     /* high */
    else hvsp_shift_out(0x00, 0x6A);                     /* extended */
    uint8_t data_field = (which == 0) ? 0x6C : (which == 1 ? 0x7E : 0x6E);
    return hvsp_shift_out(0x00, data_field);
}

void hvsp_write_fuse(uint8_t which, uint8_t value)
{
    hvsp_shift_out(HVSP_CMD_WRITE_FUSE_LOW, 0x4C);
    hvsp_shift_out(value, 0x2C);
    if (which == 0) hvsp_shift_out(0x00, 0x00);
    else if (which == 1) hvsp_shift_out(0x00, (uint8_t)(0x01 << 1));
    else hvsp_shift_out(0x00, (uint8_t)(0x02 << 1));

    /* Wait for completion */
    uint8_t r;
    do { r = hvsp_shift_out(HVSP_CMD_POLL_READY, 0x00); } while (!(r & 0x02));
}

uint8_t hvsp_read_flash(uint16_t word_addr)
{
    hvsp_shift_out(0x02, 0x4C);
    hvsp_shift_out((uint8_t)(word_addr >> 8), 0x0C);
    hvsp_shift_out((uint8_t)word_addr, 0x20);
    hvsp_shift_out(0x00, 0x28);
    return hvsp_shift_out(0x00, 0x20);
}

void hvsp_write_flash_word(uint16_t word_addr, uint16_t word)
{
    hvsp_shift_out(0x10, 0x4C);
    hvsp_shift_out((uint8_t)(word_addr >> 8), 0x0C);
    hvsp_shift_out((uint8_t)word_addr, 0x2C);
    hvsp_shift_out((uint8_t)(word & 0xFF), 0x64);
    hvsp_shift_out((uint8_t)(word >> 8), 0x6C);

    uint8_t r;
    do { r = hvsp_shift_out(HVSP_CMD_POLL_READY, 0x00); } while (!(r & 0x02));
}

#else  /* !USBASP_PICO_ENABLE_HVSP */

/* Stubs when HVSP is disabled at compile time */
void hvsp_init(void) {}
void hvsp_enter_mode(void) {}
void hvsp_exit_mode(void) {}
uint8_t hvsp_shift_out(uint8_t instruction, uint8_t data) { (void)instruction; (void)data; return 0; }
void hvsp_chip_erase(void) {}
uint8_t hvsp_read_signature(uint8_t index) { (void)index; return 0xFF; }
uint8_t hvsp_read_fuse(uint8_t which) { (void)which; return 0xFF; }
void hvsp_write_fuse(uint8_t which, uint8_t value) { (void)which; (void)value; }
uint8_t hvsp_read_flash(uint16_t word_addr) { (void)word_addr; return 0xFF; }
void hvsp_write_flash_word(uint16_t word_addr, uint16_t word) { (void)word_addr; (void)word; }

#endif /* USBASP_PICO_ENABLE_HVSP */

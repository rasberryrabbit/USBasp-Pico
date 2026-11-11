/*
 * isp.c - bit-bang ISP backend (RP2040)
 *
 * Translates the original ATmega8 USBasp isp.c to GPIO bit-banging on the
 * Pico. RP2040 has no dedicated "AVR SPI" peripheral in mind, so we drive
 * SCK/MOSI/RESET directly and read MISO. This is sufficient for all the
 * SCK options the original USBasp supports (500 Hz .. 1.5 MHz).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "isp.h"

/* Half-period (us) for the SW delay loop. Two half-periods make one SCK
 * cycle, so e.g. USBASP_ISP_SCK_375 (375 kHz -> 2.667 us cycle -> 1.333 us
 * half). Original USBasp derives these from a 12 MHz timer tick (320 us
 * prescaler); here we use absolute microsecond delays from busy_wait_us. */
typedef struct {
    uint16_t half_period_us;   /* half-clock period for SW bit-bang */
    bool     hw_fast;          /* if true, use the fastest possible bit-bang (~1.5 MHz) */
} sck_table_entry_t;

static const sck_table_entry_t sck_table[] = {
    [USBASP_ISP_SCK_0_5]    = { .half_period_us = 1000, .hw_fast = false },  /*  500 Hz  */
    [USBASP_ISP_SCK_1]      = { .half_period_us = 500,  .hw_fast = false },  /*  1 kHz   */
    [USBASP_ISP_SCK_2]      = { .half_period_us = 250,  .hw_fast = false },  /*  2 kHz   */
    [USBASP_ISP_SCK_4]      = { .half_period_us = 125,  .hw_fast = false },  /*  4 kHz   */
    [USBASP_ISP_SCK_8]      = { .half_period_us = 62,   .hw_fast = false },  /*  8 kHz   */
    [USBASP_ISP_SCK_16]     = { .half_period_us = 31,   .hw_fast = false },  /* 16 kHz   */
    [USBASP_ISP_SCK_32]     = { .half_period_us = 16,   .hw_fast = false },  /* 32 kHz   */
    [USBASP_ISP_SCK_93_75]  = { .half_period_us = 5,    .hw_fast = false },  /* 93.75 kHz */
    [USBASP_ISP_SCK_187_5]  = { .half_period_us = 3,    .hw_fast = false },  /* 187.5 kHz */
    [USBASP_ISP_SCK_375]    = { .half_period_us = 1,    .hw_fast = false },  /* 375 kHz */
    [USBASP_ISP_SCK_750]    = { .half_period_us = 0,    .hw_fast = true  },  /* 750 kHz (approx) */
    [USBASP_ISP_SCK_1500]   = { .half_period_us = 0,    .hw_fast = true  },  /* 1.5 MHz (approx) */
};

static uint16_t sck_half_period_us = 1;
static bool     sck_hw_fast = false;
static uint8_t  isp_hiaddr = 0;

/* helper: scale up to busy_wait_us_32 if needed, otherwise yield tight loop */
static inline void sck_delay(void)
{
    if (sck_hw_fast) {
        /* ~1.5 MHz: 4 cycles at 125 MHz = 32 ns... in practice we need a few
         * NOPs to keep the line valid against capacitive load. */
        for (volatile int i = 0; i < 6; i++) __asm volatile ("nop");
    } else if (sck_half_period_us == 0) {
        for (volatile int i = 0; i < 16; i++) __asm volatile ("nop");
    } else {
        busy_wait_us_32(sck_half_period_us);
    }
}

void isp_init(void)
{
    /* Pins are configured here once. Initial state: tri-stated (input, no
     * pull), set on ispConnect(). */
    gpio_init(PIN_ISP_SCK);     gpio_set_dir(PIN_ISP_SCK, GPIO_IN);
    gpio_init(PIN_ISP_MOSI);    gpio_set_dir(PIN_ISP_MOSI, GPIO_IN);
    gpio_init(PIN_ISP_MISO);    gpio_set_dir(PIN_ISP_MISO, GPIO_IN);
    gpio_init(PIN_ISP_RESET);  gpio_set_dir(PIN_ISP_RESET, GPIO_IN);

    /* Disable any pulls - the target drives MISO and our tri-state lines */
    gpio_disable_pulls(PIN_ISP_SCK);
    gpio_disable_pulls(PIN_ISP_MOSI);
    gpio_disable_pulls(PIN_ISP_MISO);
    gpio_disable_pulls(PIN_ISP_RESET);
}

void isp_set_sck_option(uint8_t option)
{
    if (option == USBASP_ISP_SCK_AUTO)
        option = USBASP_ISP_SCK_375;

    if (option > USBASP_ISP_SCK_1500)
        option = USBASP_ISP_SCK_375;

    sck_half_period_us = sck_table[option].half_period_us;
    sck_hw_fast        = sck_table[option].hw_fast;
}

void isp_connect(void)
{
    /* All ISP pins become outputs (MISO stays input) */
    gpio_set_dir(PIN_ISP_RESET, GPIO_OUT);
    gpio_set_dir(PIN_ISP_SCK,   GPIO_OUT);
    gpio_set_dir(PIN_ISP_MOSI, GPIO_OUT);
    gpio_set_dir(PIN_ISP_MISO, GPIO_IN);

    /* Reset device: bring RESET low, SCK low */
    gpio_put(PIN_ISP_RESET, 0);
    gpio_put(PIN_ISP_SCK, 0);

    /* positive reset pulse > 2 SCK cycles */
    sck_delay();
    gpio_put(PIN_ISP_RESET, 1);
    sck_delay();
    gpio_put(PIN_ISP_RESET, 0);

    isp_hiaddr = 0;
}

void isp_disconnect(void)
{
    /* Set all ISP pins back to inputs and pull-ups off */
    gpio_set_dir(PIN_ISP_RESET, GPIO_IN);
    gpio_set_dir(PIN_ISP_SCK,   GPIO_IN);
    gpio_set_dir(PIN_ISP_MOSI, GPIO_IN);
    gpio_set_dir(PIN_ISP_MISO,  GPIO_IN);

    gpio_disable_pulls(PIN_ISP_RESET);
    gpio_disable_pulls(PIN_ISP_SCK);
    gpio_disable_pulls(PIN_ISP_MOSI);
    gpio_disable_pulls(PIN_ISP_MISO);
}

uint8_t isp_transmit(uint8_t send_byte)
{
    uint8_t rec_byte = 0;

    for (int i = 0; i < 8; i++) {
        /* Set MSB on MOSI */
        gpio_put(PIN_ISP_MOSI, (send_byte & 0x80) != 0);
        send_byte <<= 1;

        /* Sample MISO */
        rec_byte <<= 1;
        if (gpio_get(PIN_ISP_MISO)) rec_byte |= 1;

        /* Pulse SCK high then low */
        gpio_put(PIN_ISP_SCK, 1);
        sck_delay();
        gpio_put(PIN_ISP_SCK, 0);
        sck_delay();
    }

    return rec_byte;
}

uint8_t isp_enter_programming_mode(void)
{
    uint8_t check;
    uint8_t count = 32;

    while (count--) {
        isp_transmit(0xAC);
        isp_transmit(0x53);
        check = isp_transmit(0);
        isp_transmit(0);

        if (check == 0x53) return 0;

        /* Pulse RESET */
        sck_delay();
        gpio_put(PIN_ISP_RESET, 1);
        sck_delay();
        gpio_put(PIN_ISP_RESET, 0);
        sck_delay();
    }
    return 1; /* error: no response */
}

void isp_update_extended(uint32_t address)
{
    uint8_t curr_hiaddr = (uint8_t)(address >> 17);
    if (isp_hiaddr != curr_hiaddr) {
        isp_hiaddr = curr_hiaddr;
        isp_transmit(0x4D);
        isp_transmit(0x00);
        isp_transmit(isp_hiaddr);
        isp_transmit(0x00);
    }
}

uint8_t isp_read_flash(uint32_t address)
{
    isp_update_extended(address);
    isp_transmit(0x20 | ((address & 1) << 3));
    isp_transmit((uint8_t)(address >> 9));
    isp_transmit((uint8_t)(address >> 1));
    return isp_transmit(0);
}

uint8_t isp_write_flash(uint32_t address, uint8_t data, uint8_t pollmode)
{
    isp_update_extended(address);
    isp_transmit(0x40 | ((address & 1) << 3));
    isp_transmit((uint8_t)(address >> 9));
    isp_transmit((uint8_t)(address >> 1));
    isp_transmit(data);

    if (pollmode == 0) return 0;

    if (data == 0x7F) {
        busy_wait_us_32(4800);  /* ~4.8 ms */
        return 0;
    }

    uint8_t retries = 30;
    absolute_time_t start = get_absolute_time();
    while (retries) {
        if (isp_read_flash(address) != 0x7F) return 0;
        if (absolute_time_diff_us(start, get_absolute_time()) > 320) {
            start = get_absolute_time();
            retries--;
        }
    }
    return 1; /* error */
}

uint8_t isp_flush_page(uint32_t address, uint8_t pollvalue)
{
    isp_update_extended(address);
    isp_transmit(0x4C);
    isp_transmit((uint8_t)(address >> 9));
    isp_transmit((uint8_t)(address >> 1));
    isp_transmit(0);

    if (pollvalue == 0xFF) {
        busy_wait_us_32(4800);
        return 0;
    }

    uint8_t retries = 30;
    absolute_time_t start = get_absolute_time();
    while (retries) {
        if (isp_read_flash(address) != 0xFF) return 0;
        if (absolute_time_diff_us(start, get_absolute_time()) > 320) {
            start = get_absolute_time();
            retries--;
        }
    }
    return 1;
}

uint8_t isp_read_eeprom(uint16_t address)
{
    isp_transmit(0xA0);
    isp_transmit((uint8_t)(address >> 8));
    isp_transmit((uint8_t)address);
    return isp_transmit(0);
}

uint8_t isp_write_eeprom(uint16_t address, uint8_t data)
{
    isp_transmit(0xC0);
    isp_transmit((uint8_t)(address >> 8));
    isp_transmit((uint8_t)address);
    isp_transmit(data);
    busy_wait_us_32(9600);  /* ~9.6 ms */
    return 0;
}

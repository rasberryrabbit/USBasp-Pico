/*
 * tpi.c - Tiny Programming Interface backend (RP2040)
 *
 * TPI uses a single bi-directional data line (TPIDATA). On the original
 * USBasp this is achieved by connecting SDO and SDI through a 1 k resistor.
 * On the Pico we can do the same; or, for convenience, we can dynamically
 * switch a single GPIO between output (writing) and input (reading). The
 * firmware chooses the latter so only the SCK + RESET + TPIDATA pins are
 * required, no resistor hack.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "tpi.h"
#include "usbasp.h"

uint16_t tpi_dly_cnt = 0;

/* TPI uses ISP_SCK and ISP_MOSI (which we treat as TPIDATA) */
#define TPI_SCK    PIN_ISP_SCK
#define TPI_DATA   PIN_ISP_MOSI

static inline void tpi_delay(void)
{
    /* tpi_dly_cnt is set by USBASP_FUNC_TPI_CONNECT and represents the
     * number of iterations of a delay loop. We approximate with busy_wait_us
     * by using tpi_dly_cnt as a count of microseconds/4. */
    if (tpi_dly_cnt == 0) {
        for (volatile int i = 0; i < 16; i++) __asm volatile ("nop");
    } else {
        busy_wait_us_32(tpi_dly_cnt >> 2);
    }
}

void tpi_init(void)
{
    /* Configure SCK as output low; TPIDATA as output (high idle) */
    gpio_set_dir(TPI_SCK, GPIO_OUT);
    gpio_set_dir(TPI_DATA, GPIO_OUT);
    gpio_put(TPI_SCK, 0);
    gpio_put(TPI_DATA, 1);

    /* Per ATtiny TPI datasheet, send >16 clock pulses with TPIDATA high */
    for (int i = 0; i < 16; i++) {
        gpio_put(TPI_SCK, 1);
        tpi_delay();
        gpio_put(TPI_SCK, 0);
        tpi_delay();
    }

    /* Set guard time to 128 bits, then NVMEN */
    tpi_send_byte(TPI_OP_SSTCS(TPIPCR));
    tpi_send_byte(TPIPCR_GT_0b);

    /* SKEY sequence: 0xFF 0x88 0x00 0x33 0x55 0xAA 0x66 0x99 0xFF */
    tpi_send_byte(TPI_OP_SKEY);
    tpi_send_byte(0xFF);
    tpi_send_byte(0x88);
    tpi_send_byte(0x00);
    tpi_send_byte(0x33);
    tpi_send_byte(0x55);
    tpi_send_byte(0xAA);
    tpi_send_byte(0x66);
    tpi_send_byte(0x99);
    tpi_send_byte(0xFF);
}

void tpi_send_byte(uint8_t b)
{
    /* TPI frame: 1 start bit (0), 8 data bits (LSB first), 1 parity, 2 stop bits (1).
     * Total 12 bits. We then add 4 leading idle (1) bits before the start, which
     * the read function uses to detect the next start bit cleanly. */
    uint8_t parity = 0;
    uint8_t v = b;
    for (int i = 0; i < 8; i++) {
        parity ^= (v & 1);
        v >>= 1;
    }

    gpio_set_dir(TPI_DATA, GPIO_OUT);
    /* Idle high for 4 bits */
    for (int i = 0; i < 4; i++) {
        gpio_put(TPI_DATA, 1);
        gpio_put(TPI_SCK, 1); tpi_delay();
        gpio_put(TPI_SCK, 0); tpi_delay();
    }
    /* Start bit */
    gpio_put(TPI_DATA, 0);
    gpio_put(TPI_SCK, 1); tpi_delay();
    gpio_put(TPI_SCK, 0); tpi_delay();
    /* Data bits, LSB first */
    for (int i = 0; i < 8; i++) {
        gpio_put(TPI_DATA, (b >> i) & 1);
        gpio_put(TPI_SCK, 1); tpi_delay();
        gpio_put(TPI_SCK, 0); tpi_delay();
    }
    /* Parity */
    gpio_put(TPI_DATA, parity);
    gpio_put(TPI_SCK, 1); tpi_delay();
    gpio_put(TPI_SCK, 0); tpi_delay();
    /* Stop bits */
    gpio_put(TPI_DATA, 1);
    gpio_put(TPI_SCK, 1); tpi_delay();
    gpio_put(TPI_SCK, 0); tpi_delay();
    gpio_put(TPI_SCK, 1); tpi_delay();
    gpio_put(TPI_SCK, 0); tpi_delay();
}

uint8_t tpi_recv_byte(void)
{
    /* Switch data line to input; watch for start bit; read 8 data + 1 parity + 2 stop */
    gpio_set_dir(TPI_DATA, GPIO_IN);
    gpio_pull_up(TPI_DATA);  /* idle high */

    /* Wait for start bit (line low) */
    int timeout = 10000;
    while (gpio_get(TPI_DATA) && --timeout > 0) {
        gpio_put(TPI_SCK, 1); tpi_delay();
        gpio_put(TPI_SCK, 0); tpi_delay();
    }
    if (timeout == 0) return 0xFF;

    /* Start bit already sampled; clock through 8 data bits */
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) {
        gpio_put(TPI_SCK, 1); tpi_delay();
        if (gpio_get(TPI_DATA)) b |= (1 << i);
        gpio_put(TPI_SCK, 0); tpi_delay();
    }
    /* Parity */
    gpio_put(TPI_SCK, 1); tpi_delay();
    uint8_t parity = gpio_get(TPI_DATA) ? 1 : 0;
    gpio_put(TPI_SCK, 0); tpi_delay();
    /* Stop bits */
    gpio_put(TPI_SCK, 1); tpi_delay();
    gpio_put(TPI_SCK, 0); tpi_delay();
    gpio_put(TPI_SCK, 1); tpi_delay();
    gpio_put(TPI_SCK, 0); tpi_delay();

    (void)parity;
    return b;
}

void tpi_read_block(uint16_t addr, uint8_t *dptr, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        /* SIN(addr) returns the byte at addr */
        tpi_send_byte(TPI_OP_SIN(addr + i));
        dptr[i] = tpi_recv_byte();
    }
}

void tpi_write_block(uint16_t addr, const uint8_t *sptr, uint8_t len)
{
    /* Set pointer to addr, then SST_INC for each byte */
    tpi_send_byte(TPI_OP_SSTPR(0));
    tpi_send_byte((uint8_t)(addr & 0xFF));
    tpi_send_byte(TPI_OP_SSTPR(1));
    tpi_send_byte((uint8_t)(addr >> 8));

    for (uint8_t i = 0; i < len; i++) {
        /* Issue word-write command then store data via SST_INC */
        tpi_send_byte(TPI_OP_SSTCS(NVMCMD));
        tpi_send_byte(NVMCMD_WORD_WRITE);
        tpi_send_byte(TPI_OP_SST_INC);
        tpi_send_byte(sptr[i]);
        /* Wait for NVMCSR.BSY to clear */
        uint8_t csr;
        do {
            tpi_send_byte(TPI_OP_SLDCS(NVMCSR));
            csr = tpi_recv_byte();
        } while (csr & NVMCSR_BSY);
    }
    tpi_send_byte(TPI_OP_SSTCS(NVMCMD));
    tpi_send_byte(NVMCMD_NOP);
}

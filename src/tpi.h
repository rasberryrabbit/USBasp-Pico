/*
 * tpi.h - Tiny Programming Interface backend
 *
 * TPI is a single-wire protocol used by ATtiny 4/5/9/10/20/40 to program
 * them via three pins (RESET, SCK, data on TPIDATA which is shared with
 * MOSI/MISO via a 1k resistor).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _TPI_H_
#define _TPI_H_

#include <stdint.h>

extern uint16_t tpi_dly_cnt;

void tpi_init(void);
void tpi_send_byte(uint8_t b);
uint8_t tpi_recv_byte(void);
void tpi_read_block(uint16_t addr, uint8_t *dptr, uint8_t len);
void tpi_write_block(uint16_t addr, const uint8_t *sptr, uint8_t len);

/* TPI instruction set */
#define TPI_OP_SLD        0x20
#define TPI_OP_SLD_INC   0x24
#define TPI_OP_SST        0x60
#define TPI_OP_SST_INC   0x64
#define TPI_OP_SSTPR(a)  (0x68 | (a))
#define TPI_OP_SIN(a)    (0x10 | (((a) << 1) & 0x60) | ((a) & 0x0F))
#define TPI_OP_SOUT(a)   (0x90 | (((a) << 1) & 0x60) | ((a) & 0x0F))
#define TPI_OP_SLDCS(a)  (0x80 | ((a) & 0x0F))
#define TPI_OP_SSTCS(a)  (0xC0 | ((a) & 0x0F))
#define TPI_OP_SKEY      0xE0

#define TPIIR   0x0F
#define TPIPCR  0x02
#define TPISR   0x00

#define TPIPCR_GT_0b     0x07

#define TPISR_NVMEN      0x02

#define NVMCSR           0x32
#define NVMCMD           0x33

#define NVMCSR_BSY       0x80

#define NVMCMD_NOP           0x00
#define NVMCMD_CHIP_ERASE    0x10
#define NVMCMD_SECTION_ERASE 0x14
#define NVMCMD_WORD_WRITE    0x1D

#endif /* _TPI_H_ */

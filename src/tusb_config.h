/*
 * tusb_config.h - TinyUSB configuration for USBasp-Pico
 *
 * Configures TinyUSB as a Vendor-class device (no HID/CDC/MSC/MIDI) so we can
 * implement the USBasp protocol directly on EP0 vendor requests + a single
 * bulk endpoint for fast flash/EEPROM streaming.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Root hub port used as a device (Pico has only one). */
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

/* ---- Common ---- */
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined (provided by the Pico SDK / tinyusb_device)
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))
#endif

/* ---- Endpoint 0 ---- */
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE  64
#endif

/* ---- Class configuration ----
 * CDC 비활성화 빌드: avrdude 호환성 테스트용
 * 이 빌드는 Vendor 인터페이스만 단일 디바이스로 제공하여
 * Windows에서 복합 디바이스 관련 인식 문제를 회피합니다.
 */
#define CFG_TUD_HID               0
#define CFG_TUD_CDC               0   /* CDC 비활성화 */
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            1

/* Vendor class endpoint buffer size.
 * USBasp uses small EP0 control transfers (<= 8 bytes payload) plus a single
 * bulk endpoint for fast flash/EEPROM streaming. 64 bytes is the standard FS bulk
 * max packet size; the host avrdude driver doesn't actually expect a separate
 * bulk endpoint for normal USBasp operations, but the mariusgreuel fork uses
 * one for high-speed firmware transfer. We keep it for compatibility. */
#define CFG_TUD_VENDOR_EP_BUFSIZE 64

/* CDC buffers (unused in this build, but kept for source compatibility). */
#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    256
#define CFG_TUD_CDC_EP_BUFSIZE    64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */

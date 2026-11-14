/*
 * pgm-usbasp-pico.c - minichlink programmer backend for USBasp-Pico firmware
 *
 * This file adds support for the USBasp-Pico firmware's SWIO backend to
 * minichlink, so you can use a Raspberry Pi Pico (or Waveshare RP2040-Zero)
 * running USBasp-Pico as a CH32V003 SWIO programmer via libusb.
 *
 * Protocol: USBasp vendor control transfers (codes 210-213).
 * No CDC serial port needed - this works with the v1.7 CDC-disabled build.
 *
 * To integrate:
 *   1. Copy this file into minichlink/
 *   2. Add `int TryInit_USBaspPico(struct PortalStruct *dev);` declaration
 *      in minichlink.h (or wherever TryInit_* are declared)
 *   3. Call TryInit_USBaspPico(dev) in the programmer detection sequence
 *      (in minichlink.c, near the other TryInit_* calls).
 *   4. Add this file to the Makefile build list.
 *
 * License: GPL v2 (matching minichlink)
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* libusb - the same backend minichlink already uses for WCHLinkE */
#include <libusb-1.0/libusb.h>

#include "minichlink.h"

/* USB identifiers - same as USBasp */
#define USBASP_VID  0x16C0
#define USBASP_PID  0x05DC

/* USBasp-Pico SWIO vendor request codes (must match usbasp_protocol.h) */
#define USBASP_PICO_SWIO_INIT          210
#define USBASP_PICO_SWIO_RESET_TARGET   211
#define USBASP_PICO_SWIO_READ_REG      212
#define USBASP_PICO_SWIO_WRITE_REG    213

/* bmRequestType for vendor control transfers */
#define REQ_OUT  0x40   /* vendor, host-to-device, device recipient */
#define REQ_IN   0xC0   /* vendor, device-to-host, device recipient */

typedef struct {
    libusb_device_handle *handle;
    int interface_claimed;
} usbasp_pico_ctx_t;

static usbasp_pico_ctx_t g_usbasp_pico_ctx;
static libusb_context *g_usbasp_libusb_ctx = NULL;

/* ---- MiniChlinkFunctions implementations ---- */

static int USBaspPicoReadReg32(void *dev, uint8_t reg_7_bit, uint32_t *commandresp)
{
    usbasp_pico_ctx_t *ctx = (usbasp_pico_ctx_t *)dev;
    uint8_t buf[4];
    int transferred;

    int rc = libusb_control_transfer(
        ctx->handle,
        REQ_IN,                          /* bmRequestType */
        USBASP_PICO_SWIO_READ_REG,       /* bRequest */
        reg_7_bit & 0x7F,                /* wValue: register address */
        0,                                /* wIndex: unused */
        buf,                              /* data buffer */
        4,                                /* wLength */
        5000                              /* timeout ms */
    );

    if (rc < 0) {
        fprintf(stderr, "USBaspPico: read reg 0x%02x failed: %s\n",
                reg_7_bit, libusb_error_name(rc));
        return -1;
    }
    if (rc != 4) {
        fprintf(stderr, "USBaspPico: read reg 0x%02x short read (%d bytes)\n",
                reg_7_bit, rc);
        return -1;
    }

    *commandresp = (uint32_t)buf[0]
                 | ((uint32_t)buf[1] << 8)
                 | ((uint32_t)buf[2] << 16)
                 | ((uint32_t)buf[3] << 24);
    return 0;
}

static int USBaspPicoWriteReg32(void *dev, uint8_t reg_7_bit, uint32_t command)
{
    usbasp_pico_ctx_t *ctx = (usbasp_pico_ctx_t *)dev;
    uint8_t buf[4];
    buf[0] = (uint8_t)(command & 0xFF);
    buf[1] = (uint8_t)((command >> 8) & 0xFF);
    buf[2] = (uint8_t)((command >> 16) & 0xFF);
    buf[3] = (uint8_t)((command >> 24) & 0xFF);

    int rc = libusb_control_transfer(
        ctx->handle,
        REQ_OUT,                          /* bmRequestType */
        USBASP_PICO_SWIO_WRITE_REG,       /* bRequest */
        reg_7_bit & 0x7F,                 /* wValue: register address */
        0,                                 /* wIndex: unused */
        buf,                               /* data: 4-byte LE value */
        4,                                 /* wLength */
        5000                               /* timeout ms */
    );

    if (rc < 0) {
        fprintf(stderr, "USBaspPico: write reg 0x%02x failed: %s\n",
                reg_7_bit, libusb_error_name(rc));
        return -1;
    }
    if (rc != 4) {
        fprintf(stderr, "USBaspPico: write reg 0x%02x short write (%d bytes)\n",
                reg_7_bit, rc);
        return -1;
    }
    return 0;
}

static int USBaspPicoSetupInterface(void *dev)
{
    usbasp_pico_ctx_t *ctx = (usbasp_pico_ctx_t *)dev;

    /* Initialize the SWIO pin on the Pico (open-drain, idle high). */
    int rc = libusb_control_transfer(
        ctx->handle, REQ_OUT, USBASP_PICO_SWIO_INIT, 0, 0, NULL, 0, 5000);
    if (rc < 0) {
        fprintf(stderr, "USBaspPico: SWIO init failed: %s\n",
                libusb_error_name(rc));
        return -1;
    }

    /* Reset the CH32V003 debug module (50 high clocks). */
    rc = libusb_control_transfer(
        ctx->handle, REQ_OUT, USBASP_PICO_SWIO_RESET_TARGET, 0, 0, NULL, 0, 5000);
    if (rc < 0) {
        fprintf(stderr, "USBaspPico: SWIO target reset failed: %s\n",
                libusb_error_name(rc));
        /* not fatal - try to continue */
    }

    return 0;
}

static int USBaspPicoFlushLLCommands(void *dev)
{
    /* Vendor requests are synchronous (each one waits for completion),
     * so there's nothing to flush. */
    (void)dev;
    return 0;
}

static int USBaspPicoControl3v3(void *dev, int bOn)
{
    /* USBasp-Pico doesn't have a 3.3V power-control vendor request yet;
     * the target is assumed to be powered externally. Return success
     * so the high-level code in minichlink doesn't abort. */
    (void)dev;
    (void)bOn;
    return 0;
}

static int USBaspPicoExit(void *dev)
{
    usbasp_pico_ctx_t *ctx = (usbasp_pico_ctx_t *)dev;
    if (ctx->handle) {
        if (ctx->interface_claimed) {
            libusb_release_interface(ctx->handle, 0);
            ctx->interface_claimed = 0;
        }
        libusb_close(ctx->handle);
        ctx->handle = NULL;
    }
    return 0;
}

/* ---- Programmer detection ---- */

int TryInit_USBaspPico(struct PortalStruct *dev)
{
    libusb_device **list;
    ssize_t count;
    int rc, found = 0;

    /* Initialize libusb if needed (other backends may have already done it). */
    if (!g_usbasp_libusb_ctx) {
        rc = libusb_init(&g_usbasp_libusb_ctx);
        if (rc < 0) {
            fprintf(stderr, "USBaspPico: libusb_init failed: %s\n",
                    libusb_error_name(rc));
            return -1;
        }
    }

    count = libusb_get_device_list(g_usbasp_libusb_ctx, &list);
    if (count < 0) {
        fprintf(stderr, "USBaspPico: libusb_get_device_list failed: %s\n",
                libusb_error_name((int)count));
        return -1;
    }

    /* Look for the USBasp-Pico by VID/PID. */
    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        rc = libusb_get_device_descriptor(list[i], &desc);
        if (rc < 0) continue;
        if (desc.idVendor == USBASP_VID && desc.idProduct == USBASP_PID) {
            /* Found - open it. */
            rc = libusb_open(list[i], &g_usbasp_pico_ctx.handle);
            if (rc < 0) {
                fprintf(stderr, "USBaspPico: libusb_open failed: %s\n",
                        libusb_error_name(rc));
                continue;
            }
            /* Claim interface 0 (vendor). */
            rc = libusb_claim_interface(g_usbasp_pico_ctx.handle, 0);
            if (rc < 0) {
                /* On Linux, may need to detach kernel driver first. */
                rc = libusb_detach_kernel_driver(g_usbasp_pico_ctx.handle, 0);
                if (rc == 0 || rc == LIBUSB_ERROR_NOT_FOUND) {
                    rc = libusb_claim_interface(g_usbasp_pico_ctx.handle, 0);
                }
            }
            if (rc < 0) {
                fprintf(stderr, "USBaspPico: cannot claim interface 0: %s\n",
                        libusb_error_name(rc));
                libusb_close(g_usbasp_pico_ctx.handle);
                g_usbasp_pico_ctx.handle = NULL;
                continue;
            }
            g_usbasp_pico_ctx.interface_claimed = 1;
            found = 1;
            break;
        }
    }
    libusb_free_device_list(list, 1);

    if (!found) {
        return -1;  /* No USBasp-Pico found - try next programmer type. */
    }

    /* Wire up the function pointers. */
    memset(&dev->pi, 0, sizeof(dev->pi));
    dev->pi.ReadReg32       = USBaspPicoReadReg32;
    dev->pi.WriteReg32      = USBaspPicoWriteReg32;
    dev->pi.SetupInterface  = USBaspPicoSetupInterface;
    dev->pi.FlushLLCommands = USBaspPicoFlushLLCommands;
    dev->pi.Control3v3      = USBaspPicoControl3v3;
    /* High-level functions (Erase, WriteBinaryBlob, ReadBinaryBlob, etc.) are
     * auto-filled by SetupAutomaticHighLevelFunctions() in minichlink.c using
     * the primitives we wired up here. */

    dev->handle = &g_usbasp_pico_ctx;
    fprintf(stderr, "USBasp-Pico SWIO programmer detected (VID=0x%04X PID=0x%04X)\n",
            USBASP_VID, USBASP_PID);
    return 0;
}

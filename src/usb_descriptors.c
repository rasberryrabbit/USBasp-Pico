/*
 * usb_descriptors.c - USB descriptors for USBasp-Pico (Vendor class)
 *
 * Identifies as a USBasp programmer (VID=0x16C0, PID=0x05DC) so avrdude
 * recognizes it without any custom udev rules.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tusb.h"
#include "usbasp_protocol.h"

#define USB_BCD   0x0110   /* USB 1.1 - matches USBasp */

/* String descriptor indices */
#define STR_LANG     0
#define STR_MFR       1
#define STR_PRODUCT   2
#define STR_SERIAL   3

/* USBasp vendor name string ("www.fischl.de") to keep avrdude happy */
static const char *const g_string_mfr     = "www.fischl.de";
static const char *const g_string_product = "USBasp";

/* Serial number: filled in main.c from the Pico's unique id */
static char g_serial_str[17];

/* Device descriptor */
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    /* Composite device: class 0xEF (Misc), subclass 0x02 (Common), protocol
     * 0x01 (IAD). This tells Windows the device has multiple interfaces that
     * should each get their own driver (libusb-win32 for Vendor interface,
     * usbser.sys for CDC interface). Without this, Windows treats the whole
     * device as a single vendor-specific device and Zadig ends up binding
     * both interfaces to WinUSB, killing the CDC COM port. */
    .bDeviceClass       = 0xEF,
    .bDeviceSubClass    = 0x02,
    .bDeviceProtocol    = 0x01,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USBASP_VID,
    .idProduct          = USBASP_PID,
    .bcdDevice          = USBASP_BCD,

    .iManufacturer      = STR_MFR,
    .iProduct           = STR_PRODUCT,
    .iSerialNumber      = STR_SERIAL,

    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

/* Vendor class + CDC ACM configuration descriptor (composite device).
 *
 * Layout:
 *   Interface 0: Vendor-specific (USBasp) - bulk OUT 0x01 + bulk IN 0x81
 *   Interface 1+2: CDC ACM (control + data) - SWIO ASCII console
 *
 * EP0 is implicitly present (control).
 */
#define ITF_NUM_VENDOR  0
#define ITF_NUM_CDC     1
#define ITF_NUM_CDC_DATA 2
#define ITF_NUM_TOTAL   3

#define EPNUM_VENDOR_OUT  0x01
#define EPNUM_VENDOR_IN   0x81
#define EPNUM_CDC_NOTIF   0x82
#define EPNUM_CDC_OUT     0x03
#define EPNUM_CDC_IN      0x83

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + \
                           TUD_VENDOR_DESC_LEN + \
                           TUD_CDC_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                           TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    /* Interface 0: Vendor-specific (USBasp) */
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 0,
                          EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, 64),

    /* Interface 1+2: CDC ACM (SWIO console) */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4,
                       EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

/* ---- String descriptors ---- */

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t   chr_count = 0;
    const char *str = NULL;

    switch (index) {
        case STR_LANG:
            _desc_str[1] = 0x0409; /* English */
            chr_count = 1;
            break;
        case STR_MFR:
            str = g_string_mfr;
            break;
        case STR_PRODUCT:
            str = g_string_product;
            break;
        case STR_SERIAL:
            str = g_serial_str;
            break;
        default:
            return NULL;
    }

    if (str != NULL) {
        size_t n = strlen(str);
        if (n > 31) n = 31;
        for (size_t i = 0; i < n; i++) {
            _desc_str[1 + i] = str[i];
        }
        chr_count = n;
    }

    /* First byte: length, second byte: descriptor type (0x03 = string) */
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}

/* Called from main.c to fill the serial number string from the unique id */
void usbasp_set_serial(const char *serial_hex)
{
    if (serial_hex != NULL) {
        size_t n = strlen(serial_hex);
        if (n > 16) n = 16;
        memcpy(g_serial_str, serial_hex, n);
        g_serial_str[n] = '\0';
    }
}

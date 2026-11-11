/*
 * main.c - USBasp-Pico firmware entry point
 *
 * Brings up the Pico's USB stack as a Vendor + CDC composite device,
 * initializes the ISP/TPI/HVSP/SWIO backends, and runs the TinyUSB polling
 * loop. The CDC interface implements an ASCII command protocol that drives
 * the SWIO backend for WCH CH32V003 programming.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/clocks.h"
#include "tusb.h"

#include "usbasp.h"
#include "isp.h"
#include "tpi.h"
#include "hvsp.h"
#include "swio.h"

/* Forward declaration from usb_descriptors.c */
extern void usbasp_set_serial(const char *serial_hex);

/* ---- Status LED ---- */
static absolute_time_t led_off_time;
static bool led_is_on = false;

void led_status_init(void)
{
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 1);  /* Pico LED is active-low */
    led_is_on = false;
}

void led_activity_on(void)
{
    gpio_put(PIN_LED, 0);
    led_is_on = true;
    led_off_time = make_timeout_time_ms(50);
}

void led_activity_off(void)
{
    gpio_put(PIN_LED, 1);
    led_is_on = false;
}

void led_status_task(void)
{
    if (led_is_on && time_reached(led_off_time)) {
        gpio_put(PIN_LED, 1);
        led_is_on = false;
    }
}

/* ---- Unique-id helper for the serial string ---- */
static void fill_serial(void)
{
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    char s[17];
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) {
        s[2 * i]     = hex[(id.id[i] >> 4) & 0xF];
        s[2 * i + 1] = hex[id.id[i] & 0xF];
    }
    s[16] = '\0';
    usbasp_set_serial(s);
}

/* ---- CDC SWIO command processor ----
 *
 * Two protocol modes coexist on the same CDC ACM virtual serial port:
 *
 * 1) Binary mode (Ardulink / minichlink compatible)
 *    First byte selects the operation; no newline is sent or expected.
 *
 *    'r' <reg7>          -> 4 bytes little-endian value (no ack)
 *    'w' <reg7> <u32 le> -> '+' (1 byte)
 *    'p'                 -> '+' (power on / SWIO init)
 *    'P'                 -> '+' (power off)
 *
 *    On startup, the firmware emits '!' as a sync character (matches the
 *    Ardulink protocol that minichlink's TryInit_Ardulink expects).
 *
 * 2) ASCII mode (human-friendly terminal / our Python tool)
 *    Lines are terminated by \n. The '?' / 'i' / 's' commands stay in
 *    ASCII; 'r' / 'w' / 'p' / 'P' fall through to binary mode because the
 *    first byte selects the protocol.
 *
 *    '?'                 -> "+\n"
 *    'i'                 -> "!\n"  (SWIO init)
 *    's'                 -> "+\n"  (SWIO reset, 50 high clocks)
 */
#define CDC_LINE_MAX 32
static char    cdc_line[CDC_LINE_MAX];
static uint8_t cdc_line_len = 0;

/* Binary-mode state: when we see a binary command byte, we need to read
 * a fixed number of additional bytes before we can act. */
typedef enum {
    BIN_IDLE = 0,
    BIN_R_READ_REG,    /* got 'r', need 1 byte reg */
    BIN_W_READ_REG,    /* got 'w', need 1 byte reg */
    BIN_W_READ_VAL0,   /* got 'w'+reg, need 4 bytes value */
    BIN_W_READ_VAL1,
    BIN_W_READ_VAL2,
    BIN_W_READ_VAL3,
} bin_state_t;

static bin_state_t bin_state = BIN_IDLE;
static uint8_t    bin_reg = 0;
static uint32_t   bin_val = 0;

static void cdc_send(const char *s)
{
    tud_cdc_write_str(s);
    tud_cdc_write_flush();
}

static void cdc_send_raw(const uint8_t *buf, uint32_t n)
{
    tud_cdc_write(buf, n);
    tud_cdc_write_flush();
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex(const char *s, int len, uint32_t *out)
{
    uint32_t v = 0;
    int n = 0;
    for (int i = 0; i < len; i++) {
        int d = hex_nibble(s[i]);
        if (d < 0) break;
        v = (v << 4) | (uint32_t)d;
        n++;
    }
    *out = v;
    return n;
}

/* ASCII line processing - only handles '?', 'i', 's' here. Other ASCII
 * commands are not needed because the binary mode covers them. */
static void cdc_process_line(char *line, int len)
{
    if (len == 0) return;

    int i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len) return;

    char cmd = line[i];
    switch (cmd) {
    case '?':
        cdc_send("+\n");
        break;
    case 'i':
        swio_init();
        cdc_send("!\n");
        break;
    case 's':
        swio_reset_target();
        cdc_send("+\n");
        break;
    default:
        /* Unknown ASCII line - ignore. Binary commands (r/w/p/P) are
         * handled in the byte-level state machine below, not here. */
        break;
    }
}

/* Process one incoming byte through the binary-mode state machine.
 * Returns true if the byte was consumed by binary mode; false if it
 * should fall through to ASCII line buffering. */
static bool cdc_binary_step(uint8_t c)
{
    switch (bin_state) {
    case BIN_IDLE:
        switch (c) {
        case 'r':
            bin_state = BIN_R_READ_REG;
            return true;
        case 'w':
            bin_state = BIN_W_READ_REG;
            return true;
        case 'p':
            swio_init();
            cdc_send_raw((const uint8_t *)"+", 1);
            return true;
        case 'P':
            swio_init();
            cdc_send_raw((const uint8_t *)"+", 1);
            return true;
        default:
            /* Not a binary command - fall back to ASCII line buffering. */
            return false;
        }
    case BIN_R_READ_REG:
        /* c is the 7-bit register address */
        {
            uint32_t val = swio_read_reg(c & 0x7F);
            uint8_t out[4];
            out[0] = (uint8_t)(val & 0xFF);
            out[1] = (uint8_t)((val >> 8) & 0xFF);
            out[2] = (uint8_t)((val >> 16) & 0xFF);
            out[3] = (uint8_t)((val >> 24) & 0xFF);
            cdc_send_raw(out, 4);
        }
        bin_state = BIN_IDLE;
        return true;
    case BIN_W_READ_REG:
        bin_reg = c & 0x7F;
        bin_val = 0;
        bin_state = BIN_W_READ_VAL0;
        return true;
    case BIN_W_READ_VAL0:
        bin_val |= (uint32_t)c;
        bin_state = BIN_W_READ_VAL1;
        return true;
    case BIN_W_READ_VAL1:
        bin_val |= (uint32_t)c << 8;
        bin_state = BIN_W_READ_VAL2;
        return true;
    case BIN_W_READ_VAL2:
        bin_val |= (uint32_t)c << 16;
        bin_state = BIN_W_READ_VAL3;
        return true;
    case BIN_W_READ_VAL3:
        bin_val |= (uint32_t)c << 24;
        swio_write_reg(bin_reg, bin_val);
        cdc_send_raw((const uint8_t *)"+", 1);
        bin_state = BIN_IDLE;
        return true;
    default:
        bin_state = BIN_IDLE;
        return false;
    }
}

/* Invoked from TinyUSB when CDC data arrives. We feed each byte through
 * the binary-mode state machine first; if it returns false (the byte was
 * not a binary command), we accumulate it into a line buffer and dispatch
 * on newline. */
void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;
    uint8_t buf[64];
    while (tud_cdc_available()) {
        uint32_t n = tud_cdc_read(buf, sizeof(buf));
        for (uint32_t k = 0; k < n; k++) {
            uint8_t c = buf[k];

            /* If we're mid-binary-transaction, keep eating bytes. */
            if (bin_state != BIN_IDLE) {
                cdc_binary_step(c);
                continue;
            }

            /* Try to start a binary transaction. */
            if (cdc_binary_step(c)) {
                /* Byte was consumed by binary mode. If the state machine
                 * is now BIN_IDLE, also clear any half-built ASCII line so
                 * a stale partial line doesn't dispatch later. */
                if (bin_state == BIN_IDLE) cdc_line_len = 0;
                continue;
            }

            /* ASCII mode: buffer up to newline. */
            if (c == '\r' || c == '\n') {
                cdc_line[cdc_line_len] = '\0';
                cdc_process_line(cdc_line, cdc_line_len);
                cdc_line_len = 0;
            } else if (cdc_line_len < CDC_LINE_MAX - 1) {
                cdc_line[cdc_line_len++] = (char)c;
            }
        }
    }
}

/* ---- Main ---- */
int main(void)
{
    /* Set system clock to 125 MHz (default), but ensure USB PLL is on */
    set_sys_clock_khz(125000, true);

    board_init();
    led_status_init();

    /* Unique-id -> serial number string */
    fill_serial();

    /* Initialize ISP backend */
    isp_init();

#ifdef USBASP_PICO_ENABLE_HVSP
    hvsp_init();
#endif

    /* Initialize SWIO backend */
    swio_init();

    /* Initialize TinyUSB device stack (vendor + CDC composite) */
    const tusb_rhport_init_t rh_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    tud_rhport_init(BOARD_TUD_RHPORT, &rh_init);
    board_init_after_tusb();

    /* Emit the Ardulink / minichlink sync character '!'.
     * minichlink's TryInit_Ardulink waits up to 3 seconds for this byte
     * after opening the serial port. */
    tud_cdc_write_str("!");
    tud_cdc_write_flush();

    /* Main loop */
    while (1) {
        tud_task();          /* TinyUSB device task */
        led_status_task();

        /* Drain any CDC data the rx_cb couldn't process inline. */
        if (tud_cdc_available()) {
            tud_cdc_rx_cb(0);
        }
    }
}

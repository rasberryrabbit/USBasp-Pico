/*
 * pgm-usbasp-pico.c - minichlink programmer backend for USBasp-Pico SWIO firmware
 *
 * Lets you use a Raspberry Pi Pico (or RP2040-Zero) running the USBasp-Pico
 * firmware as a CH32V SWIO programmer via libusb vendor control transfers
 * (requests 210-213, matching usbasp_protocol.h in the firmware).
 * No CDC serial port needed - works with the CDC-disabled build.
 *
 * License: GPL v2 (matching minichlink)
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libusb.h"
#include "minichlink.h"

/* USB identifiers - shared with all USBasp devices! */
#define USBASP_VID  0x16C0
#define USBASP_PID  0x05DC

/* USBasp-Pico SWIO vendor request codes (must match usbasp_protocol.h) */
#define USBASP_PICO_SWIO_INIT         210
#define USBASP_PICO_SWIO_RESET_TARGET 211
#define USBASP_PICO_SWIO_READ_REG     212
#define USBASP_PICO_SWIO_WRITE_REG    213

/* bmRequestType for vendor control transfers */
#define REQ_OUT  0x40   /* vendor, host-to-device, device recipient */
#define REQ_IN   0xC0   /* vendor, device-to-host, device recipient */

#define USBASP_PICO_TIMEOUT 5000

struct USBaspPicoProgrammerStruct
{
	void *internal;  /* must be first - ProgrammerStructBase */
	libusb_device_handle * handle;
	int interface_claimed;
};

static int usbasp_pico_ctrl_out( struct USBaspPicoProgrammerStruct * eps, uint8_t request, uint16_t wValue, const uint8_t * data, uint16_t wLength )
{
	int rc = libusb_control_transfer( eps->handle, REQ_OUT, request, wValue, 0, (uint8_t*)data, wLength, USBASP_PICO_TIMEOUT );
	if( rc < 0 )
	{
		fprintf( stderr, "USBaspPico: control OUT req %d failed: %s\n", request, libusb_error_name( rc ) );
		return -1;
	}
	if( rc != wLength )
	{
		fprintf( stderr, "USBaspPico: control OUT req %d short write (%d/%d bytes)\n", request, rc, wLength );
		return -1;
	}
	return 0;
}

static int USBaspPicoReadReg32( void * d, uint8_t reg_7_bit, uint32_t * commandresp )
{
	struct USBaspPicoProgrammerStruct * eps = (struct USBaspPicoProgrammerStruct *)d;
	uint8_t buf[4];
	int rc = libusb_control_transfer( eps->handle, REQ_IN, USBASP_PICO_SWIO_READ_REG, reg_7_bit & 0x7F, 0, buf, 4, USBASP_PICO_TIMEOUT );
	if( rc < 0 )
	{
		fprintf( stderr, "USBaspPico: read reg 0x%02x failed: %s\n", reg_7_bit, libusb_error_name( rc ) );
		return -1;
	}
	if( rc != 4 )
	{
		fprintf( stderr, "USBaspPico: read reg 0x%02x short read (%d bytes)\n", reg_7_bit, rc );
		return -1;
	}
	*commandresp = (uint32_t)buf[0]
	             | ((uint32_t)buf[1] << 8)
	             | ((uint32_t)buf[2] << 16)
	             | ((uint32_t)buf[3] << 24);
	return 0;
}

static int USBaspPicoWriteReg32( void * d, uint8_t reg_7_bit, uint32_t command )
{
	struct USBaspPicoProgrammerStruct * eps = (struct USBaspPicoProgrammerStruct *)d;
	uint8_t buf[4];
	buf[0] = (uint8_t)( command & 0xFF );
	buf[1] = (uint8_t)(( command >> 8 ) & 0xFF );
	buf[2] = (uint8_t)(( command >> 16 ) & 0xFF );
	buf[3] = (uint8_t)(( command >> 24 ) & 0xFF );
	return usbasp_pico_ctrl_out( eps, USBASP_PICO_SWIO_WRITE_REG, reg_7_bit & 0x7F, buf, 4 );
}

static int USBaspPicoSetupInterface( void * d )
{
	struct USBaspPicoProgrammerStruct * eps = (struct USBaspPicoProgrammerStruct *)d;

	/* Initialize the SWIO pin on the Pico (open-drain, idle high). */
	if( usbasp_pico_ctrl_out( eps, USBASP_PICO_SWIO_INIT, 0, NULL, 0 ) )
		return -1;

	/* Reset the CH32V003 debug module (50 high clocks). Non-fatal. */
	if( usbasp_pico_ctrl_out( eps, USBASP_PICO_SWIO_RESET_TARGET, 0, NULL, 0 ) )
		fprintf( stderr, "USBaspPico: SWIO target reset failed (non-fatal)\n" );

	return 0;
}

static int USBaspPicoFlushLLCommands( void * d )
{
	/* Vendor requests are synchronous - nothing to flush. */
	(void)d;
	return 0;
}

static int USBaspPicoControl3v3( void * d, int bOn )
{
	/* USBasp-Pico has no 3.3V power-control request; target is powered
	 * externally. Return success so minichlink doesn't abort. */
	(void)d;
	(void)bOn;
	return 0;
}

static int USBaspPicoExit( void * d )
{
	struct USBaspPicoProgrammerStruct * eps = (struct USBaspPicoProgrammerStruct *)d;
	if( eps->handle )
	{
		if( eps->interface_claimed )
		{
			libusb_release_interface( eps->handle, 0 );
			eps->interface_claimed = 0;
		}
		libusb_close( eps->handle );
		eps->handle = NULL;
	}
	return 0;
}

void * TryInit_USBaspPico( void )
{
	libusb_device_handle * handle = libusb_open_device_with_vid_pid( NULL, USBASP_VID, USBASP_PID );
	if( !handle ) return 0;

	int rc = libusb_claim_interface( handle, 0 );
	if( rc < 0 )
	{
#if !defined( _WIN32 )
		/* On Linux the usbasp kernel driver may be bound to the device. */
		if( libusb_detach_kernel_driver( handle, 0 ) == 0 )
			rc = libusb_claim_interface( handle, 0 );
#endif
	}
	if( rc < 0 )
	{
		fprintf( stderr, "USBaspPico: cannot claim interface 0: %s\n", libusb_error_name( rc ) );
		libusb_close( handle );
		return 0;
	}

	struct USBaspPicoProgrammerStruct * eps = calloc( 1, sizeof( *eps ) );
	eps->handle = handle;
	eps->interface_claimed = 1;

	/* Probe: VID/PID 16C0:05DC is shared with every plain AVR USBasp.
	 * Only the SWIO firmware understands request 210 - a regular USBasp
	 * will stall here, and we politely back off. */
	if( usbasp_pico_ctrl_out( eps, USBASP_PICO_SWIO_INIT, 0, NULL, 0 ) < 0 )
	{
		libusb_release_interface( eps->handle, 0 );
		libusb_close( eps->handle );
		free( eps );
		return 0;
	}

	MCF.ReadReg32       = USBaspPicoReadReg32;
	MCF.WriteReg32      = USBaspPicoWriteReg32;
	MCF.FlushLLCommands = USBaspPicoFlushLLCommands;
	MCF.SetupInterface  = USBaspPicoSetupInterface;
	MCF.Control3v3      = USBaspPicoControl3v3;
	MCF.Exit            = USBaspPicoExit;
	/* Erase/WriteBinaryBlob/ReadBinaryBlob/etc. are auto-generated by
	 * SetupAutomaticHighLevelFunctions() from the primitives above. */

	return eps;
}
# USBasp EP0 control-transfer fix (stock avrdude compatible)

## Root cause fixed

The upstream avrdude USBasp driver sends `USBASP_FUNC_SETLONGADDRESS` as an
IN vendor control transfer with a 4-byte requested response buffer and a
5,000 ms libusb timeout. It sends this command before each READFLASH block.
The firmware previously completed this command with `tud_control_status()`,
which skips the IN data stage. The host therefore waits for the IN data stage
until the control-transfer timeout. With the common 200-byte read block size,
an 8 KiB read can incur about 41 * 5 seconds = 205 seconds of avoidable delay.

The firmware now replies to empty USBasp IN commands with a zero-length IN
data packet (ZLP), using `tud_control_xfer(..., reply_buffer, 0)`. Commands
with `wLength == 0` continue to use `tud_control_status()`.

## Scope

- Keeps VID/PID, descriptors, endpoint definitions, request IDs, and USBasp
  wire protocol unchanged.
- Keeps the existing EP0 transfer model and stock avrdude compatibility.
- Applies the correct empty-IN response to CONNECT, DISCONNECT,
  SETLONGADDRESS, and TPI no-payload commands.
- Does not modify SWIO signaling, register access, LED pins, or LED behavior.
  The shared EP0 callback now clears only stale pending state when a new SETUP
  aborts an unfinished control transfer.
- Does not add a new Bulk protocol, DMA, or asynchronous target-read path.

## Expected impact

The repeated 5-second timeout per READFLASH block is removed. The actual AVR
ISP read time remains governed by SCK. At 187.5 kHz, 8 KiB of flash read data
requires approximately 1.40 seconds of raw ISP SPI wire time (4 SPI bytes per
flash byte), plus command and software overhead.

## Validation

Source-level change only. A full Pico SDK build and RP2040/avrdude hardware
test are still required. Confirm that the 8 KiB read completes in a few seconds
and that the returned bytes match the target flash.


## Additional correctness fixes

- **Aborted EP0 writes:** a new SETUP packet now cancels pending block, HVSP,
  SWIO-register, and JTAG-sequence actions from an unfinished control transfer.
  This prevents a later ACK from applying stale bytes from the shared scratch
  buffer to a target.
- **TPI block lengths:** TPI reads and writes are split into chunks of at most
  255 bytes, matching the TPI backend's 8-bit length parameter. TPI requests
  larger than the 4 KiB scratch buffer are rejected instead of returning or
  consuming incomplete data.
- **SWD direction:** corrected the final-bit test so SWDIO is left in input
  mode when the final bit is a read bit.
- **HVSP ready polling:** ready waits are now bounded to one second and use an
  explicit success/failure result, so an SDO timeout cannot be mistaken for a
  ready response. Chip erase and fuse-write report a failed control request on
  timeout; flash-word writes no longer poll forever.

## Additional validation

Changed C files passed GCC syntax checks with warnings enabled using local
Pico/TinyUSB interface stubs. This is not a substitute for a full Pico SDK
build or target-hardware testing; those still need to be performed.

#!/usr/bin/env python3
"""
usbasp_pico_swio.py - host-side tool to drive the SWIO backend of the
USBasp-Pico firmware for WCH CH32V003 RISC-V programming.

Two transports are supported and auto-detected:

  --vendor (default)   Use USBasp vendor control transfers (pyusb, no
                        /dev/ttyACMx port needed). Uses vendor requests
                        210-213 (USBasp-Pico extension).

  --cdc                 Use the CDC ACM virtual serial port (pyserial).
                        Speaks the binary Ardulink protocol so the same
                        CDC port is also compatible with the `minichlink`
                        tool from cnlohr/ch32v003fun. After opening the
                        port, the firmware emits '!' as a sync byte.

Both transports drive the same SWIO backend in the firmware.

Requirements (install both, or just one depending on transport):

    pip install pyusb pyserial

Usage:

    # Auto-detect: try vendor (pyusb) first, fall back to CDC (pyserial)
    python3 usbasp_pico_swio.py init
    python3 usbasp_pico_swio.py reset
    python3 usbasp_pico_swio.py r 06           # read WCH debug reg 0x06
    python3 usbasp_pico_swio.py w 7F 12345678   # write reg 0x7F = 0x12345678

    # Force a specific transport:
    python3 usbasp_pico_swio.py --vendor init
    python3 usbasp_pico_swio.py --cdc --port /dev/ttyACM0 init

minichlink compatibility:

    The CDC port speaks the Ardulink binary protocol used by
    minichlink's TryInit_Ardulink, so you can use minichlink directly:

        minichlink -b -w /path/to/firmware.bin 0x08000000

    (Replace -b with your minichlink flag that forces serial / Ardulink
    mode; the exact flag depends on the minichlink version.)

Common WCH CH32V003 debug register addresses (per the WCH reference manual):
    0x06 - data transfer register
    0x07 - status / control
    0x0F - flash control register (erase/program)
    0x1F - device ID / signature

For higher-level Flash erase/program/verify, see the ch32v003fun or
minichlink projects - this tool only exposes the register read/write
primitives that they need.
"""
import sys
import argparse
import time

# ---- Constants -----------------------------------------------------------
VID = 0x16C0
PID = 0x05DC

# Vendor request codes (must match usbasp_protocol.h)
SWIO_INIT          = 210
SWIO_RESET_TARGET  = 211
SWIO_READ_REG      = 212
SWIO_WRITE_REG     = 213

# bmRequestType values for vendor requests
REQ_OUT = 0x40   # vendor, host-to-device, device recipient
REQ_IN  = 0xC0   # vendor, device-to-host, device recipient


# ---- Vendor transport (pyusb) ------------------------------------------
class VendorBackend:
    def __init__(self):
        import usb.core
        import usb.util
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is None:
            sys.exit("USBasp-Pico not found (VID=0x16C0 PID=0x05DC). "
                     "Check that the firmware is flashed.")
        try:
            dev.set_configuration()
        except usb.core.USBError as e:
            if "Resource busy" not in str(e):
                raise
        self.dev = dev
        self.transport = "vendor"

    def ctrl_in(self, request, value=0, index=0, length=4):
        data = self.dev.ctrl_transfer(REQ_IN, request, value, index, length, 5000)
        return bytes(data)

    def ctrl_out(self, request, value=0, index=0, data=None):
        if data is None:
            self.dev.ctrl_transfer(REQ_OUT, request, value, index, None, 5000)
        else:
            self.dev.ctrl_transfer(REQ_OUT, request, value, index, data, 5000)

    def init(self):
        self.ctrl_out(SWIO_INIT)
        print("[+] SWIO initialized (vendor)")

    def reset(self):
        self.ctrl_out(SWIO_RESET_TARGET)
        print("[+] SWIO target reset (50 high clocks)")

    def read_reg(self, reg):
        data = self.ctrl_in(SWIO_READ_REG, value=reg, length=4)
        val = int.from_bytes(data, byteorder="little")
        print(f"[+] read reg 0x{reg:02x}: 0x{val:08X}")
        return val

    def write_reg(self, reg, value):
        data = (value & 0xFFFFFFFF).to_bytes(4, byteorder="little")
        self.ctrl_out(SWIO_WRITE_REG, value=reg, data=data)
        print(f"[+] write reg 0x{reg:02x} <- 0x{value:08X}")


# ---- CDC transport (pyserial, Ardulink binary protocol) ----------------
class CDCBackend:
    """Talks to the firmware CDC port using the binary Ardulink protocol.

    This is the same protocol that `minichlink` uses for the Ardulink
    programmer (see pgm-ardulink.c in ch32v003fun). So once you've
    flashed the USBasp-Pico firmware, you can use minichlink directly:

        minichlink -A -b /dev/ttyACM0 ...

    (the -A / --ardulink flag name depends on the minichlink build)
    """

    def __init__(self, port=None):
        import serial
        if port is None:
            port = self._auto_detect()
            if port is None:
                sys.exit("No CDC serial port found for USBasp-Pico. "
                         "Pass --port /dev/ttyACM0 explicitly.")
        try:
            self.ser = serial.Serial(port, baudrate=115200, timeout=2.0,
                                      bytesize=serial.EIGHTBITS,
                                      parity=serial.PARITY_NONE,
                                      stopbits=serial.STOPBITS_ONE)
        except serial.SerialException as e:
            sys.exit(f"Cannot open {port}: {e}")
        # Drain startup banner, expect '!' sync byte (Ardulink-compatible).
        time.sleep(0.3)
        banner = self.ser.read(64)
        if banner and banner[0:1] == b"!":
            sys.stderr.write(f"[i] {port}: sync '!' received\n")
        else:
            sys.stderr.write(f"[i] {port}: no '!' banner; got {banner!r}\n")
        self.transport = "cdc"
        self.port = port

    @staticmethod
    def _auto_detect():
        import glob
        for pattern in ["/dev/ttyACM*", "/dev/cu.usbmodem*"]:
            for p in sorted(glob.glob(pattern)):
                return p
        return None

    def _write(self, data):
        self.ser.write(data)

    def _read(self, n):
        return self.ser.read(n)

    def init(self):
        # The firmware already initialized SWIO at boot. Send 'p' (power on)
        # in Ardulink binary protocol to re-init, expect '+' reply.
        self._write(b"p")
        r = self._read(1)
        if r == b"+":
            print(f"[+] SWIO initialized (CDC) on {self.port}")
        else:
            print(f"[!] init: unexpected reply {r!r}")

    def reset(self):
        # Use ASCII 's\n' (SWIO reset, 50 high clocks). The firmware keeps
        # this as an ASCII command, separate from the binary protocol.
        self._write(b"s\n")
        r = self._read(2)
        if r == b"+\n":
            print("[+] SWIO target reset (50 high clocks)")
        else:
            print(f"[!] reset: unexpected reply {r!r}")

    def read_reg(self, reg):
        # Binary 'r' <reg7> -> 4 bytes little-endian value.
        self._write(bytes([ord("r"), reg & 0x7F]))
        data = self._read(4)
        if len(data) != 4:
            print(f"[!] read reg 0x{reg:02x}: short reply {data!r}")
            return None
        val = int.from_bytes(data, byteorder="little")
        print(f"[+] read reg 0x{reg:02x}: 0x{val:08X}")
        return val

    def write_reg(self, reg, value):
        # Binary 'w' <reg7> <u32 le> -> '+' reply.
        data = bytes([ord("w"), reg & 0x7F]) + (value & 0xFFFFFFFF).to_bytes(4, "little")
        self._write(data)
        r = self._read(1)
        if r == b"+":
            print(f"[+] write reg 0x{reg:02x} <- 0x{value:08X}")
        else:
            print(f"[!] write reg 0x{reg:02x}: unexpected reply {r!r}")


# ---- Main ----------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="USBasp-Pico SWIO host tool (CH32V003 programming)")
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--vendor", action="store_true",
                            help="Force vendor (pyusb) transport (default)")
    transport.add_argument("--cdc", action="store_true",
                            help="Force CDC ACM (pyserial) transport. "
                                 "The CDC port speaks the binary Ardulink "
                                 "protocol, also usable by minichlink.")
    parser.add_argument("--port",
                        help="CDC serial port (e.g. /dev/ttyACM0). "
                             "Only used with --cdc.")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("init",   help="Initialize SWIO pin")
    sub.add_parser("reset",  help="SWIO target reset (50 high clocks)")

    rp = sub.add_parser("r", help="Read WCH debug register")
    rp.add_argument("reg", type=lambda x: int(x, 0), help="Register address 0x00-0x7F")

    wp = sub.add_parser("w", help="Write WCH debug register")
    wp.add_argument("reg",   type=lambda x: int(x, 0), help="Register address")
    wp.add_argument("value", type=lambda x: int(x, 0), help="32-bit value")

    args = parser.parse_args()

    backend = None
    use_vendor = args.vendor or not args.cdc

    if use_vendor:
        try:
            backend = VendorBackend()
        except ImportError:
            sys.stderr.write("pyusb not installed; falling back to CDC.\n")
            backend = None

    if backend is None:
        try:
            backend = CDCBackend(args.port)
        except ImportError:
            sys.exit("Neither pyusb nor pyserial is installed. "
                     "Run: pip install pyusb pyserial")

    try:
        if args.command == "init":
            backend.init()
        elif args.command == "reset":
            backend.reset()
        elif args.command == "r":
            backend.read_reg(args.reg)
        elif args.command == "w":
            backend.write_reg(args.reg, args.value)
        else:
            sys.exit(f"Unknown command: {args.command}")
    finally:
        if hasattr(backend, "ser"):
            backend.ser.close()


if __name__ == "__main__":
    main()

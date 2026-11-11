#!/usr/bin/env python3
"""
usbasp_pico_hvsp.py - host-side tool to drive the HVSP (12 V) mode of the
USBasp-Pico firmware.

This tool is NOT compatible with the original USBasp firmware - it uses
vendor request codes 200-207 that are USBasp-Pico specific. It depends on
the `pyusb` package:

    pip install pyusb

Common operations:

    # Read signature of an ATtiny85 in HVSP mode (verifies 12 V entry)
    python3 usbasp_pico_hvsp.py readsig

    # Read all three fuses
    python3 usbasp_pico_hvsp.py readfuses

    # Erase the chip (also clears lock bits)
    python3 usbasp_pico_hvsp.py erase

    # Write low fuse to 0x62 (factory default for ATtiny85)
    python3 usbasp_pico_hvsp.py writefuse low 0x62
    python3 usbasp_pico_hvsp.py writefuse high 0xD7
    python3 usbasp_pico_hvsp.py writefuse ext 0xFE

    # Dump first 256 bytes of flash
    python3 usbasp_pico_hvsp.py dumpflash 0 256 > flash.bin

    # Write a single 16-bit word to flash address 0x0010
    python3 usbasp_pico_hvsp.py writeflashword 0x0010 0xBEEF

Procedure the tool follows:
    1. Open the USBasp-Pico by VID 0x16C0 / PID 0x05DC.
    2. Send USBASP_PICO_HVSP_CONNECT to enable 12 V boost and enter HVSP mode.
    3. Run the requested operation(s).
    4. Send USBASP_PICO_HVSP_DISCONNECT to drop 12 V and release pins.

CAUTION: 12 V is applied to RESET only. Make sure VCC stays at 5 V.
"""
import sys
import argparse
import usb.core
import usb.util

VID = 0x16C0
PID = 0x05DC

# Vendor request codes (must match usbasp_protocol.h)
HVSP_CONNECT     = 200
HVSP_DISCONNECT  = 201
HVSP_CHIP_ERASE  = 202
HVSP_READ_SIG    = 203
HVSP_READ_FUSE   = 204
HVSP_WRITE_FUSE  = 205
HVSP_READ_FLASH  = 206
HVSP_WRITE_FLASH = 207

# bmRequestType values: vendor request, device recipient
REQ_OUT = 0x40   # host-to-device
REQ_IN  = 0xC0   # device-to-host


def open_device():
    """Find and configure the USBasp-Pico device."""
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("USBasp-Pico not found (VID=0x16C0 PID=0x05DC). "
                 "Check that the firmware is flashed and udev rules are set.")
    try:
        dev.set_configuration()
    except usb.core.USBError as e:
        if "Resource busy" not in str(e):
            raise
    return dev


def control_in(dev, request, value=0, index=0, length=1, timeout=5000):
    """Send a vendor IN control transfer and return the received bytes."""
    data = dev.ctrl_transfer(REQ_IN, request, value, index, length, timeout)
    return bytes(data)


def control_out(dev, request, value=0, index=0, data=None, timeout=5000):
    """Send a vendor OUT control transfer (optionally with data stage)."""
    if data is None:
        dev.ctrl_transfer(REQ_OUT, request, value, index, None, timeout)
    else:
        dev.ctrl_transfer(REQ_OUT, request, value, index, data, timeout)


def cmd_connect(dev):
    control_out(dev, HVSP_CONNECT)
    print("[+] HVSP mode entered (12 V on RESET)")


def cmd_disconnect(dev):
    control_out(dev, HVSP_DISCONNECT)
    print("[+] HVSP mode exited (12 V off)")


def cmd_erase(dev):
    control_out(dev, HVSP_CHIP_ERASE)
    print("[+] Chip erased")


def cmd_read_sig(dev):
    sig = bytearray()
    for i in range(3):
        b = control_in(dev, HVSP_READ_SIG, value=i, length=1)
        sig += b
    print(f"[+] Signature: {sig[0]:02X} {sig[1]:02X} {sig[2]:02X}")
    if sig == b"\x1E\x93\x0B":
        print("    (matches ATtiny25/45/85)")
    elif sig == b"\x1E\x93\x12":
        print("    (matches ATtiny24/44/84)")
    elif sig == b"\x1E\x90\x07":
        print("    (matches ATtiny13)")
    return sig


def cmd_read_fuse(dev, which):
    data = control_in(dev, HVSP_READ_FUSE, value=which, length=1)
    return data[0]


def cmd_read_fuses(dev):
    low  = cmd_read_fuse(dev, 0)
    high = cmd_read_fuse(dev, 1)
    ext  = cmd_read_fuse(dev, 2)
    print(f"[+] Low fuse:        0x{low:02X}")
    print(f"[+] High fuse:       0x{high:02X}")
    print(f"[+] Extended fuse:   0x{ext:02X}")


def cmd_write_fuse(dev, which_str, value):
    which_map = {"low": 0, "high": 1, "ext": 2, "extended": 2}
    which = which_map.get(which_str.lower())
    if which is None:
        sys.exit(f"Unknown fuse name: {which_str}")
    # wValue low byte = which fuse, wValue high byte = value
    control_out(dev, HVSP_WRITE_FUSE, value=which | (value << 8))
    print(f"[+] Wrote {which_str} fuse = 0x{value:02X}")


def cmd_read_flash(dev, addr, count):
    """Read `count` bytes from flash starting at byte address `addr`."""
    out = bytearray()
    # The firmware HVSP flash read uses word_addr; one byte returned per
    # request. We expose this as a byte stream: each word_addr returns one
    # byte (the byte index within the word is implicit LSB first).
    for i in range(count):
        word_addr = (addr + i) // 2
        b = control_in(dev, HVSP_READ_FLASH,
                       value=(word_addr >> 8) & 0xFF,
                       index=word_addr & 0xFF,
                       length=1)
        out += b
    return bytes(out)


def cmd_write_flash_word(dev, word_addr, word):
    low  = word & 0xFF
    high = (word >> 8) & 0xFF
    control_out(dev, HVSP_WRITE_FLASH,
                value=(word_addr >> 8) & 0xFF,
                index=word_addr & 0xFF,
                data=bytes([low, high]))
    print(f"[+] Wrote flash word 0x{word:04X} at addr 0x{word_addr:04X}")


def main():
    parser = argparse.ArgumentParser(
        description="USBasp-Pico HVSP host tool (12 V mode for ATtiny85 etc.)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("connect", help="Enter HVSP mode (12 V on RESET)")
    sub.add_parser("disconnect", help="Exit HVSP mode")
    sub.add_parser("erase", help="Chip erase (also clears lock bits)")
    sub.add_parser("readsig", help="Read signature bytes (3 bytes)")
    sub.add_parser("readfuses", help="Read all three fuses")
    sub.add_parser("readlow", help="Read low fuse only").set_defaults(fuse_which=0)
    sub.add_parser("readhigh", help="Read high fuse only").set_defaults(fuse_which=1)
    sub.add_parser("readext", help="Read extended fuse only").set_defaults(fuse_which=2)

    wf = sub.add_parser("writefuse", help="Write a fuse (low/high/ext + value)")
    wf.add_argument("which", choices=["low", "high", "ext", "extended"])
    wf.add_argument("value", type=lambda x: int(x, 0),
                    help="Fuse value (hex or decimal, e.g. 0x62)")

    df = sub.add_parser("dumpflash", help="Dump flash bytes to stdout (binary)")
    df.add_argument("addr", type=lambda x: int(x, 0), help="Start byte address")
    df.add_argument("count", type=lambda x: int(x, 0), help="Number of bytes")

    wfw = sub.add_parser("writeflashword", help="Write a 16-bit word to flash")
    wfw.add_argument("addr", type=lambda x: int(x, 0), help="Word address")
    wfw.add_argument("word", type=lambda x: int(x, 0), help="16-bit word value")

    args = parser.parse_args()

    if args.command == "connect":
        dev = open_device()
        cmd_connect(dev)
        return
    if args.command == "disconnect":
        dev = open_device()
        cmd_disconnect(dev)
        return

    # All other commands auto-enter HVSP, do the operation, auto-exit
    dev = open_device()
    try:
        cmd_connect(dev)
        if args.command == "erase":
            cmd_erase(dev)
        elif args.command == "readsig":
            cmd_read_sig(dev)
        elif args.command == "readfuses":
            cmd_read_fuses(dev)
        elif args.command in ("readlow", "readhigh", "readext"):
            which = {"readlow": 0, "readhigh": 1, "readext": 2}[args.command]
            v = cmd_read_fuse(dev, which)
            label = ["Low", "High", "Extended"][which]
            print(f"[+] {label} fuse: 0x{v:02X}")
        elif args.command == "writefuse":
            cmd_write_fuse(dev, args.which, args.value)
        elif args.command == "dumpflash":
            data = cmd_read_flash(dev, args.addr, args.count)
            sys.stdout.buffer.write(data)
            sys.stderr.write(f"[+] Read {len(data)} bytes from 0x{args.addr:04X}\n")
        elif args.command == "writeflashword":
            cmd_write_flash_word(dev, args.addr, args.word)
        else:
            sys.exit(f"Unknown command: {args.command}")
    finally:
        cmd_disconnect(dev)


if __name__ == "__main__":
    main()

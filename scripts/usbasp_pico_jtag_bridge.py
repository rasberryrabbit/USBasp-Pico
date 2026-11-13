#!/usr/bin/env python3
"""
usbasp_pico_jtag_bridge.py - OpenOCD remote_bitbang bridge for USBasp-Pico

A small TCP server that speaks OpenOCD's `remote_bitbang` ASCII protocol on
localhost:3335 and translates each command into USBasp-Pico JTAG/SWD vendor
requests (220-226). This lets OpenOCD drive the Pico as a JTAG adapter
without any OpenOCD source modification - the `remote_bitbang` driver is
built into OpenOCD 0.10+.

Setup:
    pip install pyusb
    # run this bridge in one terminal
    python3 usbasp_pico_jtag_bridge.py

    # in another terminal
    openocd -f interface/remote_bitbang.cfg \
            -c "remote_bitbang_port 3335" \
            -f target/stm32f1x.cfg

The bridge auto-detects JTAG vs SWD from the first command OpenOCD sends
(`J` selects JTAG, `S` selects SWD). It stays a single Vendor device on
USB, so Windows keeps a single libusb-win32 bind - no composite-device
regression.

remote_bitbang protocol (subset, from openocd/src/jtag/drivers/remote_bitbang.c):
  - '0'..'7' : one bit, set TCK/TMS/TDI from bits 0/1/2 of the char; sample TDO
  - 'B'     : bit sequence: len byte follows, then len bytes each = (tms<<3)|(tdi<<2)|tck...
              actually each char is (tck<<2)|(tms<<1)|tdi, len bits
  - 'b'     : like B but TDI held 0
  - 'R'/'r' : assert/release nTRST
  - 'S'/'s' : assert/release nSRST
  - 'Q'     : quit
  - 'J'     : select JTAG (no-op here, we set pins on first use)
  - 'S'     : (uppercase, ambiguous) - OpenOCD uses 'S' for nSRST assert;
              we handle both contextually.
Returns one byte per sampled bit: '0' or '1'.

SPDX-License-Identifier: BSD-3-Clause
"""

from __future__ import annotations

import logging
import socket
import struct
import sys
import threading

import usb.core
import usb.util

LOG = logging.getLogger("usbasp_pico_bridge")

# ---- USB identifiers ----
VID = 0x16C0
PID = 0x05DC

# ---- Vendor request codes (match src/usbasp_protocol.h) ----
USBASP_PICO_JTAG_INIT     = 220
USBASP_PICO_JTAG_SEQ      = 221
USBASP_PICO_JTAG_SWD_SEQ  = 222
USBASP_PICO_JTAG_RESET    = 223
USBASP_PICO_JTAG_SPEED    = 224
USBASP_PICO_JTAG_PINSET   = 225
USBASP_PICO_JTAG_RELEASE  = 226

REQ_OUT = 0x40
REQ_IN  = 0xC0

PROTO_JTAG = 0
PROTO_SWD  = 1

# PINSET bit layout (firmware): bit0=TCK, bit1=TMS, bit2=TDI,
# bit3=nTRST(assert-low), bit4=nSRST(assert-low). Set bit = drive that value.
PIN_TCK   = 0x01
PIN_TMS   = 0x02
PIN_TDI   = 0x04
PIN_NTRST = 0x08
PIN_NSRST = 0x10

DEFAULT_PORT = 3335
DEFAULT_SPEED_KHZ = 1000


class USBaspPicoJtagBridge:
    def __init__(self):
        self.dev = None
        self.protocol = None
        self.speed_khz = DEFAULT_SPEED_KHZ
        # Track current pin states so we only send deltas.
        self.cur_tck = 0
        self.cur_tms = 0
        self.cur_tdi = 0
        self.cur_ntrst = 1   # released (high)
        self.cur_nsrst = 1   # released (high)

    # ---- USB ----
    def open_usb(self):
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is None:
            raise RuntimeError("USBasp-Pico not found (VID 16C0 PID 05DC)")
        try:
            dev.set_configuration()
        except usb.core.USBError:
            pass
        usb.util.claim_interface(dev, 0)
        self.dev = dev
        # Verify it's our USBasp (not some other obdev device).
        try:
            prod = usb.util.get_string(dev, dev.iProduct)
        except Exception:
            prod = None
        if prod != "USBasp":
            usb.util.release_interface(dev, 0)
            raise RuntimeError(f"Device at VID/PID is not USBasp (iProduct={prod!r})")
        LOG.info("USBasp-Pico opened: %s", prod)

    def close_usb(self):
        if self.dev is not None:
            try:
                self.ctrl_out(USBASP_PICO_JTAG_RELEASE, 0, 0)
            except Exception:
                pass
            try:
                usb.util.release_interface(self.dev, 0)
            except Exception:
                pass
            self.dev = None

    def ctrl_out(self, bRequest, wValue, wIndex):
        self.dev.ctrl_transfer(REQ_OUT, bRequest, wValue, wIndex, None)

    def ctrl_out_data(self, bRequest, wValue, wIndex, data: bytes):
        self.dev.ctrl_transfer(REQ_OUT, bRequest, wValue, wIndex, data)

    def ctrl_in(self, bRequest, wValue, wIndex, length):
        ret = self.dev.ctrl_transfer(REQ_IN, bRequest, wValue, wIndex, length)
        return bytes(ret)

    # ---- Protocol init ----
    def init_jtag(self):
        if self.protocol == "jtag":
            return
        self.ctrl_out(USBASP_PICO_JTAG_INIT, PROTO_JTAG, self.speed_khz)
        self.protocol = "jtag"
        LOG.info("JTAG initialized, %d kHz", self.speed_khz)

    def init_swd(self):
        if self.protocol == "swd":
            return
        self.ctrl_out(USBASP_PICO_JTAG_INIT, PROTO_SWD, self.speed_khz)
        self.protocol = "swd"
        LOG.info("SWD initialized, %d kHz", self.speed_khz)

    # ---- Single-bit ops (remote_bitbang '0'-'7') ----
    def bit_one(self, ch: str) -> str:
        """char '0'-'7': bits = ch - '0'; bit0=TCK, bit1=TMS, bit2=TDI.
        Sample TDO and return '0' or '1'."""
        bits = ord(ch) - ord('0')
        # remote_bitbang semantics: the host specifies the NEXT state of each
        # pin. TCK is toggled by OpenOCD itself (it sends '0' then '4' for a
        # low->high clock). So we just drive the pins as given and sample TDO.
        tck = bits & 1
        tms = (bits >> 1) & 1
        tdi = (bits >> 2) & 1
        drv = (tck * PIN_TCK) | (tms * PIN_TMS) | (tdi * PIN_TDI)
        # Preserve nTRST/nSRST states in the PINSET call.
        if not self.cur_ntrst:
            drv |= PIN_NTRST
        if not self.cur_nsrst:
            drv |= PIN_NSRST
        ret = self.ctrl_in(USBASP_PICO_JTAG_PINSET, drv, 0, 1)
        self.cur_tck, self.cur_tms, self.cur_tdi = tck, tms, tdi
        return '1' if (ret[0] & 1) else '0'

    # ---- Bulk bit sequence ('B'/'b'/'R') ----
    # remote_bitbang 'B': next byte = length (in bits), then length bytes each
    # encoding (tck<<2)|(tms<<1)|tdi for one bit. Returns length bytes of '0'/'1'.
    def bit_bulk(self, tdi_fixed_zero: bool) -> bytes:
        length = self._read1()  # number of bits
        if length is None:
            return b''
        # Read `length` bytes, each = (tck<<2)|(tms<<1)|tdi
        seq_in = self._read_exact(length)
        if seq_in is None or len(seq_in) != length:
            return b''
        # Build packed bytes for JTAG_SEQ: each byte = bit0=TDI, bit1=TMS.
        # Firmware toggles TCK itself, so we ignore the tck bit from OpenOCD
        # and rely on jtag_seq() to produce one TCK pulse per input byte.
        packed = bytearray(length)
        for i in range(length):
            b = seq_in[i]
            tdi = 0 if tdi_fixed_zero else (b >> 0) & 1
            tms = (b >> 1) & 1
            packed[i] = (tdi & 1) | ((tms & 1) << 1)
        # Send OUT, then read back TDO packed MSB-first.
        self.ctrl_out_data(USBASP_PICO_JTAG_SEQ, 0, 0, bytes(packed))
        out_bytes = (length + 7) // 8
        result = self.ctrl_in(USBASP_PICO_JTAG_SEQ, 0, 0, out_bytes)
        # Unpack TDO bits MSB-first into one '0'/'1' per bit.
        out = bytearray(length)
        for i in range(length):
            byte_idx = i // 8
            bit_in_byte = 7 - (i % 8)
            bit = (result[byte_idx] >> bit_in_byte) & 1
            out[i] = ord('1') if bit else ord('0')
        return bytes(out)

    # ---- Resets ----
    def assert_trst(self):
        self.cur_ntrst = 0
        self.ctrl_out(USBASP_PICO_JTAG_RESET, PIN_NTRST, 0)

    def release_trst(self):
        self.cur_ntrst = 1
        self.ctrl_out(USBASP_PICO_JTAG_RESET, PIN_NTRST, PIN_NTRST)

    def assert_srst(self):
        self.cur_nsrst = 0
        self.ctrl_out(USBASP_PICO_JTAG_RESET, PIN_NSRST, 0)

    def release_srst(self):
        self.cur_nsrst = 1
        self.ctrl_out(USBASP_PICO_JTAG_RESET, PIN_NSRST, PIN_NSRST)

    # ---- TCP server ----
    def serve(self, port: int = DEFAULT_PORT):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(1)
        LOG.info("remote_bitbang bridge listening on 127.0.0.1:%d", port)
        LOG.info("Connect OpenOCD with: remote_bitbang_port %d", port)
        try:
            while True:
                conn, addr = srv.accept()
                LOG.info("OpenOCD connected from %s", addr)
                self._handle_client(conn)
        except KeyboardInterrupt:
            LOG.info("Shutting down")
        finally:
            srv.close()
            self.close_usb()

    def _handle_client(self, conn: socket.socket):
        self._sock = conn
        self._rbuf = b""
        try:
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                self._rbuf += data
                if not self._process_buffer():
                    break  # 'Q' received
        except (ConnectionResetError, BrokenPipeError):
            LOG.info("OpenOCD disconnected")
        finally:
            conn.close()
            self._sock = None
            self._rbuf = b""
            # Release the target when OpenOCD disconnects.
            try:
                self.ctrl_out(USBASP_PICO_JTAG_RELEASE, 0, 0)
                self.protocol = None
            except Exception:
                pass

    def _read1(self) -> int | None:
        """Read one byte from the socket buffer."""
        if not self._rbuf:
            return None
        b = self._rbuf[0]
        self._rbuf = self._rbuf[1:]
        return b

    def _read_exact(self, n: int) -> bytes | None:
        while len(self._rbuf) < n:
            data = self._sock.recv(4096)
            if not data:
                return None
            self._rbuf += data
        out = self._rbuf[:n]
        self._rbuf = self._rbuf[n:]
        return out

    def _process_buffer(self) -> bool:
        """Process complete commands in self._rbuf. Returns False on 'Q'."""
        while self._rbuf:
            ch = chr(self._rbuf[0])
            self._rbuf = self._rbuf[1:]
            if ch in "01234567":
                resp = self.bit_one(ch)
                self._sock.sendall(resp.encode("ascii"))
            elif ch == 'B':
                resp = self.bit_bulk(tdi_fixed_zero=False)
                if resp:
                    self._sock.sendall(resp)
                else:
                    return False
            elif ch == 'b':
                resp = self.bit_bulk(tdi_fixed_zero=True)
                if resp:
                    self._sock.sendall(resp)
                else:
                    return False
            elif ch == 'R':
                self.assert_trst()
            elif ch == 'r':
                self.release_trst()
            elif ch == 'S':
                self.assert_srst()
            elif ch == 's':
                self.release_srst()
            elif ch == 'J':
                self.init_jtag()
            elif ch == 'T':  # SWD select (some OpenOCD versions)
                self.init_swd()
            elif ch == 'Q':
                LOG.info("OpenOCD sent Q (quit)")
                return False
            elif ch in "\r\n":
                continue
            else:
                LOG.warning("Unknown remote_bitbang command: %r", ch)
        return True


def main():
    import argparse
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    p = argparse.ArgumentParser(description="USBasp-Pico OpenOCD remote_bitbang bridge")
    p.add_argument("-p", "--port", type=int, default=DEFAULT_PORT,
                   help=f"TCP port (default {DEFAULT_PORT})")
    p.add_argument("-s", "--speed", type=int, default=DEFAULT_SPEED_KHZ,
                   help=f"Initial TCK speed in kHz (default {DEFAULT_SPEED_KHZ})")
    args = p.parse_args()

    bridge = USBaspPicoJtagBridge()
    bridge.speed_khz = args.speed
    bridge.open_usb()
    try:
        bridge.serve(args.port)
    finally:
        bridge.close_usb()


if __name__ == "__main__":
    main()

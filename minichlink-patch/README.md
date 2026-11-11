# USBasp-Pico minichlink Integration

This patch adds **USBasp-Pico** as a programmer backend to minichlink,
so you can use a Raspberry Pi Pico (or RP2040-Zero) running the USBasp-Pico
firmware as a SWIO programmer for the CH32V003.

## Why this is better than CDC

| Approach | avrdude | minichlink | Windows | Both at once |
|----------|---------|------------|---------|---------------|
| CDC binary (v1.6) | ⚠️ composite device issues | ✅ | ⚠️ Zadig splits CDC | ⚠️ |
| **Vendor request (v1.7) + this patch** | ✅ | ✅ | ✅ single device | ✅ |

The Vendor-request approach is preferred:
- USBasp is a single vendor device (class 0xFF) → avrdude and libusb both work cleanly on Windows
- minichlink talks to the same Vendor interface via libusb (no COM port needed)
- Both tools can be used simultaneously (avrdude for ISP, minichlink for SWIO)

## How to integrate

### 1. Copy the programmer file

```bash
cp pgm-usbasp-pico.c /path/to/ch32v003fun/minichlink/
```

### 2. Add the declaration

In `minichlink.h` (near the other `TryInit_*` declarations):

```c
void * TryInit_Ardulink(const init_hints_t*);
void * TryInit_USBaspPico(void);
```

### 3. Call the detector in minichlink.c

In `minichlink.c`, find the programmer detection sequence (it tries
`TryInit_WCHLinkE`, `TryInit_Ardulink`, etc.). Add this in the middle:

```c
		else if( strcmp( specpgm, "usbasppico" ) == 0 )
			dev = TryInit_USBaspPico();
```
  
```c
		else if ((dev = TryInit_USBaspPico()))
		{
			fprintf( stderr, "Found USBasp-Pico SWIO Programmer\n" );
		}
```

### 4. Update the Makefile

In `minichlink/Makefile`, add `pgm-usbasp-pico.c` to the source list:

```make
C_S:=minichlink.c pgm-wch-linke.c pgm-wch-isp.c pgm-esp32s2-ch32xx.c nhc-link042.c ardulink.c serial_dev.c pgm-b003fun.c pgm-usbasp-pico.c minichgdb.c chips.c ch5xx.c
```

### 5. Build

```bash
cd /path/to/ch32v003fun/minichlink
make
```

### 6. Test

```bash
# Detect USBasp-Pico (auto-detected among other programmer types)
./minichlink -i
# → "USBasp-Pico SWIO programmer detected (VID=0x16C0 PID=0x05DC)"

# Flash a CH32V003 binary
./minichlink -w firmware.bin 0x08000000

# Erase
./minichlink -E

# Read flash
./minichlink -r flash.bin 0x0 0x4000
```

## USBasp-Pico SWIO pin

- **GP12** = SWIO (open-drain, requires 4.7k-10k pull-up to 3.3V)
- **GND** = common ground with CH32V003
- **3V3** = CH32V003 power (3.3V supply)

## Protocol summary

All SWIO operations use USBasp vendor control transfers:

| Request | Direction | wValue | wIndex | Data | Response |
|---------|-----------|--------|--------|------|----------|
| 210 (SWIO_INIT) | OUT | 0 | 0 | none | status |
| 211 (SWIO_RESET_TARGET) | OUT | 0 | 0 | none | status |
| 212 (SWIO_READ_REG) | IN | reg7 | 0 | 4 bytes | LE value |
| 213 (SWIO_WRITE_REG) | OUT | reg7 | 0 | 4 bytes | status |

This matches `usbasp_protocol.h` in the USBasp-Pico firmware.

## Windows driver setup

For the Vendor interface, install **libusb-win32** (not WinUSB) via Zadig:

1. Plug in the Pico (with v1.8 firmware flashed)
2. Run Zadig → Options → List All Devices
3. Select "USBasp" (only one interface appears - no CDC)
4. Install `libusb-win32 (v1.2.6.0)` driver
5. Both avrdude and minichlink will work via libusb

## Linux udev rule

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="05dc", MODE="0666", GROUP="plugdev"' | \
  sudo tee /etc/udev/rules.d/99-usbasp.rules
sudo udevadm control --reload-rules
```

## File layout

```
minichlink-patch/
├── README.md             (this file)
└── pgm-usbasp-pico.c    (drop-in programmer backend)
```

## License

GPL v2 (matching minichlink and USBasp).

## Reference

- USBasp-Pico firmware: USBasp-Pico project (this repo)
- minichlink: https://github.com/cnlohr/ch32v003fun/tree/master/minichlink
- ch32v003fun: https://github.com/cnlohr/ch32v003fun

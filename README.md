# USBasp-Pico

USBasp-compatible AVR programmer firmware with HVSP (12V) and SWIO (CH32V003)
support. Runs on a Raspberry Pi Pico or Waveshare RP2040-Zero.

## Device identification

The firmware presents itself to USB as **"USBasp"** (manufacturer string
`www.fischl.de`, VID=0x16C0, PID=0x05DC), so avrdude's `-c usbasp` flow
recognizes it without any custom udev rules or drivers.

## Main features

- **USBasp fully compatible** (VID=0x16C0, PID=0x05DC)
- **High-capacity AVR support** (ATmega2560 etc., up to 256 KB flash)
- **TPI mode** (ATtiny 4/5/9/10/20/40)
- **HVSP 12V mode** (ATtiny85 recovery, vendor req 200-207)
- **SWIO mode** (WCH CH32V003 RISC-V)
  - Vendor request (210-213, pyusb) — default
  - CDC binary (Ardulink-compatible, **works directly with minichlink**)
  - CDC ASCII (terminal debugging)
- RP2040-Zero optimized pinout (avoids GP16 WS2812 LED, GP22-25 solder pads)
- UF2 build (drag-and-drop flashing via BOOTSEL)

## Pin assignment (RP2040-Zero)

### ISP (GP2-GP6): AVR ISP 6-pin
| GP2 | GP3 | GP4 | GP5 | GP6 |
|-----|-----|-----|-----|-----|
| SCK | MOSI | MISO | RESET | LED |

### HVSP (GP7-GP11): ATtiny85 12V recovery
| GP7 | GP8 | GP9 | GP10 | GP11 |
|-----|-----|-----|------|------|
| SCI | SDI | SDO | SII | 12V_EN |

### SWIO (GP12): CH32V003 RISC-V
| GP12 |
|------|
| SWIO (open-drain, 4.7k-10k pull-up required) |

## Usage

### 1. Standard AVR (avrdude)
```bash
avrdude -c usbasp -p m328p -U flash:w:firmware.hex
```

### 2. HVSP (ATtiny85 recovery)
```bash
pip install pyusb
python3 scripts/usbasp_pico_hvsp.py readsig
python3 scripts/usbasp_pico_hvsp.py readfuses
python3 scripts/usbasp_pico_hvsp.py writefuse low 0x62
```

### 3. SWIO (CH32V003) — three methods

**a) Python tool (vendor request, default)**
```bash
pip install pyusb
python3 scripts/usbasp_pico_swio.py init
python3 scripts/usbasp_pico_swio.py r 06
python3 scripts/usbasp_pico_swio.py w 7F 12345678
```

**b) Python tool (CDC binary)**
```bash
pip install pyusb pyserial
python3 scripts/usbasp_pico_swio.py --cdc --port /dev/ttyACM0 init
python3 scripts/usbasp_pico_swio.py --cdc --port /dev/ttyACM0 r 06
```

**c) minichlink direct (CH32V003 flash programming)**
```bash
git clone https://github.com/cnlohr/ch32v003fun
cd ch32v003fun/minichlink && make

minichlink -A -d /dev/ttyACM0 -w firmware.bin 0x08000000
minichlink -A -d /dev/ttyACM0 -i
minichlink -A -d /dev/ttyACM0 -E
```

See `docs/USBasp-Pico_문서.pdf` for full details.

## Windows driver setup

1. Plug in the Pico with the new firmware
2. Run Zadig → Options → List All Devices
3. Find **"USBasp"** → install **WinUSB** for **Interface 0 only**
4. Leave Interface 1+2 (CDC) on Windows built-in `usbser.sys`
5. Both avrdude and the COM port work simultaneously

## Build

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
export PATH=$PATH:/path/to/arm-none-eabi-gcc/bin
cd $PICO_SDK_PATH && git submodule update --init lib/tinyusb

cd usbasp-pico
cmake -B build -G Ninja
cmake --build build
# Output: build/usbasp-pico.uf2 (~87 KB, ISP+TPI+HVSP+SWIO+minichlink)
```

## License

- Firmware: GNU GPL v2 (inherited from USBasp)
- TinyUSB: MIT, Pico SDK: BSD-3, arm-gcc: GPL v3 + runtime exception

## References

- USBasp (Thomas Fischl): https://www.fischl.de/usbasp/
- mariusgreuel/USBasp fork: https://github.com/mariusgreuel/USBasp
- minichlink: https://github.com/cnlohr/ch32v003fun/tree/master/minichlink
- picorvd (Pico SWIO): https://github.com/aappleby/picorvd
- RP2040-Zero Wiki: https://www.waveshare.com/wiki/RP2040-Zero

---

Z.ai / 2026-11-11 / v1.6 (USBasp product name + composite device class)

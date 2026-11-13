# USBasp-Pico

USBasp 호환 AVR 프로그래머 + SWIO (CH32V003) + JTAG/SWD 펌웨어 (CDC 비활성화 빌드)
+ minichlink 통합 패치 포함
+ v1.9: MOSI/SDI 핀 공유 (HVSP 전용 핀 1개 절약)
+ v1.10: JTAG/SWD 백엔드 추가 (pyOCD + OpenOCD 호환)

## 주요 특징

- **USBasp 완벽 호환** (VID=0x16C0, PID=0x05DC)
- **단일 Vendor 디바이스** (bDeviceClass=0xFF, 복합 디바이스 아님)
- **avrdude Windows 호환성 최우선** (CDC 없음 = 복합 디바이스 문제 없음)
- **고용량 AVR 지원** (ATmega2560 등 256KB 플래시)
- **TPI 모드** (ATtiny 4/5/9/10/20/40)
- **HVSP 12V 모드** (ATtiny85 복구, vendor req 200-207)
- **SWIO 모드** (WCH CH32V003, vendor req 210-213)
  - **minichlink 직접 통합** (`minichlink-patch/` 폴더 참고)
  - Python 도구 (`usbasp_pico_swio.py`)
- **JTAG/SWD 모드** (v1.10, vendor req 220-226)
  - pyOCD 플러그인 (`usbasp_pico_probe.py`) — Cortex-M
  - OpenOCD remote_bitbang 브릿지 (`usbasp_pico_jtag_bridge.py`) — RISC-V/ESP32/FPGA
  - 펌웨어 백엔드는 도구 무관 (동일 vendor request 공유)
- RP2040-Zero 핀아웃 최적화

## 핀 할당 (RP2040-Zero)

### ISP (GP2-GP6): AVR ISP 6-pin
| GP2 | GP3 | GP4 | GP5 | GP6 |
|-----|-----|-----|-----|-----|
| SCK | MOSI / **HVSP SDI 공유** | MISO | RESET | LED |

### HVSP (GP3, GP7-GP10): ATtiny85 12V 복구
| GP3 | GP7 | GP8 | GP9 | GP10 |
|-----|-----|-----|-----|------|
| SDI (**ISP MOSI와 공유**) | SCI | SDO | SII | 12V_EN |

### SWIO (GP12): CH32V003 (vendor request)
| GP12 |
|------|
| SWIO (오픈드레인, 4.7k-10k 풀업 필요) |

### JTAG / SWD (GP11, GP13-GP15, GP26-GP27): v1.10 추가
| GP11 | GP13 | GP14 | GP15 | GP26 | GP27 |
|------|------|------|------|------|------|
| TCK / SWCLK | TMS / SWDIO | TDI | TDO | nTRST (옵션) | nSRST (옵션) |

- ISP/HVSP/SWIO 핀과 미중복 → 네 백엔드 동시 공존, 한 번에 하나만 활성
- SWD 모드에서 GP13(SWDIO)은 양방향 (호스트가 방향 전환)
- nTRST/nSRST는 옵션: 4-wire JTAG/SWD만 쓰면 GP26/GP27 미접속

### v1.9 핀 공유 변경 사항

**v1.9에서 MOSI/SDI 핀을 공유합니다.**

- ISP MOSI (GP3) 와 HVSP SDI 가 **동일한 GP3** 핀을 사용합니다.
- 두 신호는 모두 Pico → 타겟 출력이며 MSB 우선 직렬 전송입니다.
- ISP 와 HVSP 는 동시에 활성화되지 않으므로 충돌이 없습니다.
- 이로 인해 HVSP 전용 핀이 5개(GP7-GP11)에서 4개(GP7, GP8, GP9, GP10)로
  감소했고, **GP11 이 JTAG TCK 로 재할당**되었습니다 (v1.10).
- HVSP 의 SDO/SII/12V_EN 이 한 핀씩 아래로 이동했습니다
  (이전 GP9→GP8, GP10→GP9, GP11→GP10).
- 기존 ISP 동작에는 변경이 없습니다. avrdude / TPI / pyusb 도구 모두 동일.

## 사용법

### 1. 일반 AVR (avrdude)
```cmd
avrdude -c usbasp -p m328p -U flash:w:firmware.hex
avrdude -c usbasp -p t85
```

### 2. HVSP (ATtiny85 복구)
```bash
pip install pyusb
python3 scripts/usbasp_pico_hvsp.py readsig
python3 scripts/usbasp_pico_hvsp.py readfuses
```

### 3. SWIO - Python 도구
```bash
pip install pyusb
python3 scripts/usbasp_pico_swio.py init
python3 scripts/usbasp_pico_swio.py r 06
python3 scripts/usbasp_pico_swio.py w 7F 12345678
```

### 4. SWIO - minichlink 직접 사용 (NEW!)

**minichlink-patch/ 폴더의 패치를 적용하면 minichlink에서 USBasp-Pico를
직접 SWIO 프로그래머로 사용할 수 있습니다.**

```bash
# 1. ch32v003fun 저장소 클론
git clone https://github.com/cnlohr/ch32v003fun
cd ch32v003fun/minichlink

# 2. 패치 파일 복사
cp /path/to/usbasp-pico/minichlink-patch/pgm-usbasp-pico.c .

# 3. minichlink.h 에 선언 추가
# (README.md 참고)

# 4. minichlink.c 의 초기화 시퀀스에 TryInit_USBaspPico 호출 추가

# 5. Makefile 에 소스 추가

# 6. 빌드
make

# 7. CH32V003 플래시 프로그래밍
./minichlink -i
./minichlink -w firmware.bin 0x08000000
./minichlink -E
```

자세한 통합 방법은 `minichlink-patch/README.md` 참고.

### 5. JTAG / SWD - pyOCD 플러그인 (v1.10, Cortex-M)

pyOCD용 커스텀 DebugProbe 플러그인. 별도 브릿지 데몬 없이 pyOCD 명령 한 번으로 동작합니다.

```bash
pip install pyocd pyusb
cd scripts/
pip install -e .          # 플러그인 등록 (pyproject.toml)

# 프로브 인식 확인
pyocd list --probes        # "usbasp_pico" 표시

# STM32 플래시 (JTAG 또는 SWD)
pyocd cmd -t stm32f103rb -b usbasp_pico
pyocd flash firmware.hex -t stm32f103rb -b usbasp_pico
```

배선: `GP11→TCK/SWCLK`, `GP13→TMS/SWDIO`, `GP14→TDI`, `GP15→TDO`,
`GND→GND`, `3V3→VCC`. SWD만 쓸 때는 GP11/GP13/GND/VCC 4선만 연결.

### 6. JTAG / SWD - OpenOCD remote_bitbang 브릿지 (v1.10, RISC-V/ESP32/FPGA)

OpenOCD용 TCP 브릿지. 동일한 펌웨어 백엔드(vendor req 220-226)를 사용하지만
호스트 측을 remote_bitbang ASCII 프로토콜로 변환합니다.

```bash
pip install pyusb

# 터미널 1: 브릿지 실행
python3 scripts/usbasp_pico_jtag_bridge.py -p 3335 -s 1000

# 터미널 2: OpenOCD (수정 불필요, remote_bitbang 내장 드라이버)
openocd -f scripts/usbasp_pico_openocd.cfg -f target/stm32f1x.cfg
# 또는 RISC-V:
openocd -f scripts/usbasp_pico_openocd.cfg -f target/rv32m1.cfg
```

브릿지는 OpenOCD가 보내는 `0`-`7`(단일 비트), `B`/`b`(비트 시퀀스),
`R`/`r`(nTRST), `S`/`s`(nSRST), `J`(JTAG 선택), `Q`(종료) 명령을
vendor request로 변환합니다.

### 7. JTAG/SWD 타겟 선택 가이드 (v1.10)

| 타겟 | 추천 도구 | 이유 |
|------|----------|------|
| STM32 (Cortex-M) | pyOCD 플러그인 | 명령 1줄, SWD 네이티브 |
| nRF52/LPC/Kinetis | pyOCD 플러그인 | Cortex-M 표준 |
| CH32V307 (RISC-V) | OpenOCD 브릿지 | RISC-V 디버그 |
| ESP32 (Xtensa) | OpenOCD 브릿지 | Xtensa 디버그 |
| ECP5/iCE40 (FPGA) | OpenOCD 브릿지 | FPGA JTAG |

**펌웨어는 한 번만 플래시.** 타겟에 따라 호스트 도구만 선택하면 됩니다.

## 장점: 동시 사용

| 도구 | 인터페이스 | 사용 가능 |
|------|-----------|---------|
| avrdude (ISP) | Vendor (libusb) | ✅ |
| usbasp_pico_hvsp.py (HVSP) | Vendor (pyusb) | ✅ |
| usbasp_pico_swio.py (SWIO) | Vendor (pyusb) | ✅ |
| minichlink (SWIO) | Vendor (libusb) | ✅ |
| pyOCD + usbasp_pico_probe.py (JTAG/SWD) | Vendor (pyusb) | ✅ |
| OpenOCD + usbasp_pico_jtag_bridge.py (JTAG/SWD) | Vendor (pyusb→TCP) | ✅ |

**모든 도구가 동일한 Vendor 인터페이스(Interface 0)를 사용하므로
Windows에서도 충돌 없이 동시에 사용 가능합니다.**

## Windows 드라이버 설정

1. UF2 파일을 Pico에 복사 (BOOTSEL 모드)
2. 장치 관리자에서 "USBasp" 확인 (단일 디바이스)
3. Zadig 실행:
   - Options → List All Devices
   - "USBasp" 선택 (복합 디바이스 아님, 인터페이스 1개)
   - 드라이버: **libusb-win32 (v1.2.6.0)**
   - Replace Driver
4. avrdude + minichlink 모두 동작

## 빌드

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
export PATH=$PATH:/path/to/arm-none-eabi-gcc/bin
cd $PICO_SDK_PATH && git submodule update --init lib/tinyusb

cd usbasp-pico
cmake -B build -G Ninja
cmake --build build
# 결과물: build/usbasp-pico.uf2 (~81KB)
```

## 디렉토리 구조

```
usbasp-pico/
├── firmware/                # 빌드된 바이너리 (CDC 비활성화)
│   └── usbasp-pico.uf2
├── src/                     # 펌웨어 소스
│   ├── jtag.c / jtag.h      # v1.10: JTAG/SWD 백엔드
│   └── ...
├── scripts/                 # Python 호스트 도구
│   ├── usbasp_pico_hvsp.py          # HVSP (ATtiny85)
│   ├── usbasp_pico_swio.py          # SWIO (CH32V003)
│   ├── usbasp_pico_probe.py         # v1.10: pyOCD 플러그인
│   ├── usbasp_pico_jtag_bridge.py   # v1.10: OpenOCD remote_bitbang 브릿지
│   ├── usbasp_pico_openocd.cfg      # v1.10: OpenOCD 설정
│   └── pyproject.toml               # v1.10: pyOCD 플러그인 패키징
├── minichlink-patch/        # minichlink 통합 패치
│   ├── README.md
│   └── pgm-usbasp-pico.c
├── docs/                    # 회로도 + PDF 문서
└── README.md                # 이 파일
```

## 라이선스

- 펌웨어: GNU GPL v2 (USBasp 원본 계승)
- minichlink 패치: GNU GPL v2 (minichlink 라이선스 계승)
- pyOCD/OpenOCD 호스트 도구: BSD-3-Clause
- TinyUSB: MIT, Pico SDK: BSD-3

## 참고

- USBasp (Thomas Fischl): https://www.fischl.de/usbasp/
- mariusgreuel/USBasp 포크: https://github.com/mariusgreuel/USBasp
- minichlink: https://github.com/cnlohr/ch32v003fun/tree/master/minichlink
- ch32v003fun: https://github.com/cnlohr/ch32v003fun
- pyOCD: https://pyocd.io/
- OpenOCD remote_bitbang: https://openocd.org/doc/html/Debug-Adapter-Configuration.html
- RP2040-Zero Wiki: https://www.waveshare.com/wiki/RP2040-Zero

---

Z.ai / 2026-11-13 / v1.10 (CDC 비활성화 + minichlink 통합 + MOSI/SDI 공유 + JTAG/SWD 백엔드)

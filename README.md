# USBasp-Pico (CDC-disabled build)

USBasp 호환 AVR 프로그래머 펌웨어 (CDC 비활성화 버전)

## 이 버전의 목적

Windows에서 avrdude가 복합 디바이스(Vendor + CDC) 인식에 실패하는
문제를 해결하기 위해 **CDC를 비활성화**한 빌드입니다.

- **Vendor 인터페이스만** 제공 (단일 디바이스, 클래스 0xFF)
- **CDC COM 포트 없음** (SWIO/minichlink 호환성 제거)
- avrdude 호환성 최우선

## 디바이스 식별

- 제품명: "USBasp"
- 제조사: "www.fischl.de"
- VID=0x16C0, PID=0x05DC
- bDeviceClass=0xFF (Vendor-specific, 단일 디바이스)

## 지원 기능 (이 빌드)

- ✅ avrdude ISP 프로그래밍 (ATmega, ATtiny 시리즈)
- ✅ TPI 모드 (ATtiny 4/5/9/10/20/40)
- ✅ HVSP 12V 모드 (ATtiny85 복구, vendor request 200-207)
- ✅ SWIO (CH32V003, vendor request만 - CDC 없음)
- ❌ SWIO CDC (이 빌드에 없음)
- ❌ minichlink (CDC 없으므로 사용 불가)

## 핀 할당 (RP2040-Zero)

### ISP (GP2-GP6): AVR ISP 6-pin
| GP2 | GP3 | GP4 | GP5 | GP6 |
|-----|-----|-----|-----|-----|
| SCK | MOSI | MISO | RESET | LED |

### HVSP (GP7-GP11): ATtiny85 12V 복구
| GP7 | GP8 | GP9 | GP10 | GP11 |
|-----|-----|-----|------|------|
| SCI | SDI | SDO | SII | 12V_EN |

### SWIO (GP12): CH32V003 (vendor request 전용)
| GP12 |
|------|
| SWIO (오픈드레인, 4.7k-10k 풀업 필요) |

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

### 3. SWIO (CH32V003, vendor request만)
```bash
pip install pyusb
python3 scripts/usbasp_pico_swio.py init
python3 scripts/usbasp_pico_swio.py r 06
python3 scripts/usbasp_pico_swio.py w 7F 12345678
# --cdc 옵션은 이 빌드에서 동작 안 함 (CDC 없음)
```

## Windows 드라이버 설정

이 빌드는 단일 디바이스이므로 Zadig 설정이 단순합니다:

1. UF2 파일을 Pico에 복사 (BOOTSEL 모드)
2. 장치 관리자에서 "USBasp" 확인
3. Zadig 실행:
   - Options → List All Devices
   - "USBasp" 선택 (Interface 0만 보임 - 복합 디바이스 아님)
   - 드라이버: **libusb-win32 (v1.2.6.0)** 설치
4. avrdude 테스트:
   ```cmd
   avrdude -c usbasp -p t85
   ```

## CDC 필요한 경우

CDC COM 포트와 minichlink 호환성이 필요하면 v1.6 빌드를 사용하세요.
하지만 Windows에서 avrdude 인식 문제가 있을 수 있습니다.

## 빌드

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
export PATH=$PATH:/path/to/arm-none-eabi-gcc/bin
cd $PICO_SDK_PATH && git submodule update --init lib/tinyusb

cd usbasp-pico
cmake -B build -G Ninja
cmake --build build
# 결과물: build/usbasp-pico.uf2 (~81KB, Vendor only)
```

## 라이선스

- 펌웨어: GNU GPL v2 (USBasp 원본 계승)
- TinyUSB: MIT, Pico SDK: BSD-3, arm-gcc: GPL v3 + runtime exception

## 참고

- USBasp (Thomas Fischl): https://www.fischl.de/usbasp/
- mariusgreuel/USBasp 포크: https://github.com/mariusgreuel/USBasp

---

Z.ai / 2026-11-11 / v1.7 (CDC 비활성화, avrdude 호환성 우선)

# CMakeLists.txt - USBasp-compat firmware for Raspberry Pi Pico (RP2040)
#
# Build:
#   export PICO_SDK_PATH=/path/to/pico-sdk
#   cmake -B build -S .
#   cmake --build build
# Output:
#   build/usbasp-pico.elf
#   build/usbasp-pico.uf2

cmake_minimum_required(VERSION 3.13)

# Pull in Pico SDK before project() so the SDK initialization runs first.
include(pico_sdk_import.cmake)

project(usbasp_pico C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK (configures toolchain, tinyusb path, etc.)
pico_sdk_init()

# --- Firmware target -----------------------------------------------------

add_executable(usbasp-pico
    src/main.c
    src/usb_descriptors.c
    src/isp.c
    src/tpi.c
    src/hvsp.c
    src/swio.c
    src/jtag.c
    src/usbasp_protocol.c
)

# v1.10-pio: generate swio.pio.h from the PIO assembly (swio.pio).
# The generated header is included by swio.c and provides swio_program
# and swio_program_get_default_config().
pico_generate_pio_header(usbasp-pico ${CMAKE_CURRENT_LIST_DIR}/src/swio.pio)

# Project-local includes (tusb_config.h lives here)
target_include_directories(usbasp-pico PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(usbasp-pico PRIVATE
    pico_stdlib
    hardware_gpio
    hardware_pio
    hardware_timer
    hardware_clocks
    hardware_pll
    hardware_vreg
    hardware_watchdog
    hardware_resets
    hardware_irq
    pico_unique_id
    tinyusb_device
    tinyusb_board
)

# TinyUSB OS abstraction: use Pico SDK's
target_compile_definitions(usbasp-pico PRIVATE
    CFG_TUSB_OS=OPT_OS_PICO
)

# Compile-time configuration toggles (override with -D on the cmake command line)
#   USBASP_PICO_ENABLE_HVSP=1         -> enable ATtiny85 / HVSP (12 V) support
#   USBASP_PICO_HVSP_12V_PIN=<gpio>   -> GPIO driving the 12 V enable (default 10, v1.9)
#   USBASP_PICO_ENABLE_JTAG=1         -> enable JTAG/SWD backend (v1.10)
#   USBASP_PICO_JTAG_*_PIN=<gpio>     -> override JTAG TCK/TMS/TDI/TDO/nTRST/nSRST
#   HVSP pinout (v1.9, Waveshare RP2040-Zero friendly):
#       GP3=SDI(shared w/ ISP MOSI), GP7=SCI, GP8=SDO, GP9=SII, GP10=12V_EN
#   JTAG/SWD pinout (v1.10):
#       GP11=TCK/SWCLK, GP13=TMS/SWDIO, GP14=TDI, GP15=TDO,
#       GP26=nTRST(opt), GP27=nSRST(opt)
target_compile_definitions(usbasp-pico PRIVATE
    USBASP_PICO_ENABLE_HVSP=1
    USBASP_PICO_HVSP_12V_PIN=10
    USBASP_PICO_ENABLE_JTAG=1
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
)

# Generate .uf2, .map, .dis, and embed program URL (helpful for debugging)
pico_add_extra_outputs(usbasp-pico)

# Print the firmware build info on the console
message(STATUS "USBasp-Pico firmware target built.")

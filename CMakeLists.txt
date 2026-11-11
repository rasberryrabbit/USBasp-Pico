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
    src/usbasp_protocol.c
)

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
#   USBASP_PICO_HVSP_12V_PIN=<gpio>   -> GPIO driving the 12 V enable (default 11)
#   HVSP pinout (Waveshare RP2040-Zero friendly): GP7=SCI, GP8=SDI, GP9=SDO,
#                                                 GP10=SII, GP11=12V_EN
target_compile_definitions(usbasp-pico PRIVATE
    USBASP_PICO_ENABLE_HVSP=1
    USBASP_PICO_HVSP_12V_PIN=11
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
)

# Generate .uf2, .map, .dis, and embed program URL (helpful for debugging)
pico_add_extra_outputs(usbasp-pico)

# Print the firmware build info on the console
message(STATUS "USBasp-Pico firmware target built.")

cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(Pico-ResTouch-X_X_code) 
pico_sdk_init()

add_subdirectory(lib/config)
add_subdirectory(lib/lcd)
add_subdirectory(lib/font)
add_subdirectory(examples)
add_subdirectory(lib/fatfs)
add_subdirectory(lib/sdcard)

include_directories(examples)
include_directories(./lib/config)
include_directories(./lib/lcd)
include_directories(./lib/font)
include_directories(./lib/fatfs)
include_directories(./lib/sdcard)

add_executable(main 
main.c
)

# enable usb output, disable uart output
pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(main)

target_link_libraries(main examples lcd font config pico_stdlib hardware_spi fatfs)

# Diagnostic-only: dumps register state after a known-working LCD_Init(),
# to compare against TinyOS's own bare-metal driver. See regdump.c.
add_executable(regdump regdump.c)
pico_enable_stdio_usb(regdump 1)
pico_enable_stdio_uart(regdump 0)
pico_add_extra_outputs(regdump)
target_link_libraries(regdump lcd font config pico_stdlib hardware_spi fatfs)

# Diagnostic-only: runs TinyOS's own bare-metal lcd.c (copied verbatim as
# tinyos_lcd.c/.h) inside a normal pico-sdk environment, to isolate whether
# the bug is in lcd.c itself or in TinyOS's boot/clock environment.
add_executable(tinyos_lcd_test tinyos_lcd_test.c tinyos_lcd.c tinyos_clocks_override.c)
pico_enable_stdio_usb(tinyos_lcd_test 1)
pico_enable_stdio_uart(tinyos_lcd_test 0)
pico_add_extra_outputs(tinyos_lcd_test)
target_link_libraries(tinyos_lcd_test pico_stdlib hardware_clocks hardware_pll hardware_xosc hardware_vreg hardware_resets)

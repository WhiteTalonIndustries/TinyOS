cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(tinyos_rp2350)
pico_sdk_init()

# Waveshare's own DEV_Config/LCD_Driver, proven working on this exact board
# -- see resources/c/examples/lcd_test.c for the original reference. Touch
# (LCD_Touch.c) is intentionally excluded; GUI/Bmp/fatfs/sdcard too, since
# this is a debugging-output display, not a full graphics stack (yet).
add_library(lcd
    lib/config/DEV_Config.c
    lib/lcd/LCD_Driver.c
)
target_include_directories(lcd PUBLIC lib/config lib/lcd)
target_link_libraries(lcd PUBLIC pico_stdlib hardware_spi)

add_executable(tinyos_rp2350 main.c status.c)
pico_enable_stdio_usb(tinyos_rp2350 1)
pico_enable_stdio_uart(tinyos_rp2350 0)
pico_add_extra_outputs(tinyos_rp2350)
target_link_libraries(tinyos_rp2350 lcd pico_stdlib)

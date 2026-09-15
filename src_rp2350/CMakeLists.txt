cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(tinyos_rp2350)
pico_sdk_init()

# Waveshare's own DEV_Config/LCD_Driver/LCD_GUI, proven working on this
# exact board -- see resources/c/examples/lcd_test.c for the original
# reference. Touch (LCD_Touch.c) and Bmp/fatfs/sdcard are still excluded;
# LCD_GUI.c (text rendering, for lcd_console.c) is now included, using just
# Font12 rather than the full font set.
add_library(lcd
    lib/config/DEV_Config.c
    lib/lcd/LCD_Driver.c
    lib/lcd/LCD_GUI.c
    lib/font/font12.c
    lib/font/font24.c
)
target_include_directories(lcd PUBLIC lib/config lib/lcd lib/font)
target_link_libraries(lcd PUBLIC pico_stdlib hardware_spi)

# 2040-parity modules: usb.c/flash.c/adc.c are pico-sdk-backed
# reimplementations of src_2040's interfaces; fs.c/editor.c/script.c are
# reused verbatim from src_2040 (pure logic, no register access).
add_executable(tinyos_rp2350
    main.c
    status.c
    lcd_console.c
    usb.c
    flash.c
    fs.c
    editor.c
    script.c
    adc.c
)
target_include_directories(tinyos_rp2350 PRIVATE .)
pico_enable_stdio_usb(tinyos_rp2350 1)
pico_enable_stdio_uart(tinyos_rp2350 0)
pico_add_extra_outputs(tinyos_rp2350)
target_link_libraries(tinyos_rp2350 lcd pico_stdlib hardware_flash hardware_adc)

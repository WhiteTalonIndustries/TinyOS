cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(tinyos_rp2350)
pico_sdk_init()

# Waveshare's own DEV_Config/LCD_Driver/LCD_GUI, proven working on this
# exact board -- see resources/c/examples/lcd_test.c for the original
# reference. Touch (LCD_Touch.c) and Bmp are still excluded; LCD_GUI.c
# (text rendering, for lcd_console.c) uses just Font12/Font24, not the
# full font set.
add_library(lcd
    lib/config/DEV_Config.c
    lib/lcd/LCD_Driver.c
    lib/lcd/LCD_GUI.c
    lib/font/font12.c
    lib/font/font24.c
)
target_include_directories(lcd PUBLIC lib/config lib/lcd lib/font)
target_link_libraries(lcd PUBLIC pico_stdlib hardware_spi)

# microSD card (Waveshare's MMC_SD.c SPI driver) + FatFs -- this is now
# TinyOS's user storage (see fs.c); the onboard flash stays reserved for
# the system image itself. Same SPI1 bus as the LCD, separate chip-select.
add_library(sdcard
    lib/sdcard/MMC_SD.c
    lib/fatfs/ff.c
    lib/fatfs/diskio.c
)
target_include_directories(sdcard PUBLIC lib/sdcard lib/fatfs lib/config)
target_link_libraries(sdcard PUBLIC pico_stdlib hardware_spi)

# 2040-parity modules: usb.c/adc.c are pico-sdk-backed reimplementations of
# src_2040's interfaces; editor.c/script.c are reused verbatim from
# src_2040 (pure logic, no register access). fs.c is a from-scratch
# SD/FatFs-backed implementation of src_2040's fs.h interface (not reused
# from src_2040, which is flash-backed). usb_descriptors.c/msc_disk.c make
# this a composite CDC+MSC device (console + "mount"/"unmount" of the SD
# card) -- NOT using pico_enable_stdio_usb, since pico-sdk's own
# pico_stdio_usb bakes in a fixed CDC-only descriptor set that can't
# coexist with our own composite one (TinyUSB allows only one). wifi.c
# brings up the onboard CYW43439 (this board is the "W" variant) via
# pico_cyw43_arch_lwip_poll -- NO_SYS=1 lwIP, polled from usb.c's idle
# loops (wifi_poll()) instead of a background IRQ thread, matching how
# TinyUSB is already driven here. lwipopts.h configures lwIP itself.
add_executable(tinyos_rp2350
    main.c
    status.c
    lcd_console.c
    usb.c
    usb_descriptors.c
    msc_disk.c
    fs.c
    editor.c
    script.c
    adc.c
    wifi.c
)
target_include_directories(tinyos_rp2350 PRIVATE .)
pico_enable_stdio_uart(tinyos_rp2350 0)
pico_add_extra_outputs(tinyos_rp2350)
target_link_libraries(tinyos_rp2350
    lcd
    sdcard
    pico_stdlib
    hardware_adc
    tinyusb_device
    pico_unique_id
    pico_cyw43_arch_lwip_poll
)

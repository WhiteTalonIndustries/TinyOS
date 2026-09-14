import gc
from machine import Pin, SPI
# from st7789_4bit import *
from nanoguilib.st7789 import *

SSD = ST7789

pdc = Pin(8, Pin.OUT, value=0)
pcs = Pin(9, Pin.OUT, value=1)
prst = Pin(15, Pin.OUT, value=1)
pbl = Pin(13, Pin.OUT, value=1)

gc.collect()  # Precaution before instantiating framebuf
# Max baudrate produced by Pico is 31_250_000. ST7789 datasheet allows <= 62.5MHz.
# Note non-standard MISO pin. This works, verified by SD card.
spi = SPI(1, 60_000_000, sck=Pin(10), mosi=Pin(11), miso=Pin(12))


# Define the display
# For landscape mode:
ssd = SSD(spi, height=240, width=320, disp_mode=4, dc=pdc, cs=pcs, rst=prst)
ssd.fill(ssd.rgb(0xFF,0xFF,0xFF))
ssd.rect(270, 0, 50, 50, ssd.rgb(0x00,0x00,0xff))
ssd.fill_rect(0, 0, 60, 50, ssd.rgb(0xFF,0xff,0x00))
ssd.text('Pico-ResTouch-LCD', 90, 70, ssd.rgb(0xFF,0x00,0x00))
ssd.line(60, 90, 320,240, ssd.rgb(0x00,0x00,0x00))
ssd.line(320, 90, 60,240, ssd.rgb(0x00,0x00,0x00))
ssd.hline(0, 90, 320, ssd.rgb(0x00,0xff,0x00))
ssd.vline(60, 90, 150, ssd.rgb(0x00,0x00,0xff))
ssd.show()
# ssd.scroll(10,10)
# ssd.show()

# # For portrait mode:
# ssd = SSD(spi, height=320, width=240, disp_mode=0, dc=pdc, cs=pcs, rst=prst)
# ssd.fill(ssd.rgb(0xFF,0xFF,0xFF))
# ssd.rect(190, 0, 50, 50, ssd.rgb(0x00,0x00,0xff))
# ssd.fill_rect(0, 0, 60, 50, ssd.rgb(0xFF,0xff,0x00))
# ssd.text('Pico-ResTouch-LCD', 50, 70, ssd.rgb(0xFF,0x00,0x00))
# ssd.line(60, 90, 240,320, ssd.rgb(0x00,0x00,0x00))
# ssd.line(240, 90, 60,320, ssd.rgb(0x00,0x00,0x00))
# ssd.hline(0, 90, 240, ssd.rgb(0x00,0xff,0x00))
# ssd.vline(60, 90, 230, ssd.rgb(0x00,0x00,0xff))
# ssd.show()
# # ssd.scroll(10,10)
# # ssd.show()

# Optional use of SD card. Requires official driver. In my testing the
# 31.25MHz baudrate works. Other SD cards may have different ideas.
# from sdcard import SDCard
# import os
# sd = SDCard(spi, Pin(22, Pin.OUT), 30_000_000)
# os.mount(sd, '/sd')
# with open("/sd/test.txt", "w") as f:
#     f.write("Hello world!\r\n")
# 
# def print_directory(path, tabs = 0):
#     for file in os.listdir(path):
#         stats = os.stat(path+"/"+file)
#         filesize = stats[6]
#         isdir = stats[0] & 0x4000
#     
#         if filesize < 1000:
#             sizestr = str(filesize) + " by"
#         elif filesize < 1000000:
#             sizestr = "%0.1f KB" % (filesize/1000)
#         else:
#             sizestr = "%0.1f MB" % (filesize/1000000)
#     
#         prettyprintname = ""
#         for i in range(tabs):
#             prettyprintname += "   "
#         prettyprintname += file
#         if isdir:
#             prettyprintname += "/"
#         print('{0:<40} Size: {1:>10}'.format(prettyprintname, sizestr))
#         
#         # recursively print directory contents
#         if isdir:
#             print_directory(path+"/"+file, tabs+1)
# 
# 
# print("Files on filesystem:")
# print("====================")
# print_directory("/sd")






# alevel.py Test/demo "spirit level" program.
# Requires Pyboard for accelerometer.
# Tested with Adafruit ssd1351 OLED display.

# Released under the MIT License (MIT). See LICENSE.
# Copyright (c) 2018-2020 Peter Hinch

# Initialise hardware and framebuf before importing modules.
from nanoguilib.color_setup import ssd  # Create a display instance

from nanoguilib.nanogui import refresh
from nanoguilib.dial import Dial, Pointer
refresh(ssd, True)  # Initialise and clear display.

# Now import other modules

import utime
# import pyb
from nanoguilib.writer import CWriter
import nanoguilib.arial10 as arial10
from nanoguilib.colors import *

def main():
    print('alevel test is running.')
    CWriter.set_textpos(ssd, 0, 0)  # In case previous tests have altered it
    wri = CWriter(ssd, arial10, GREEN, BLACK, verbose=False)
    wri.set_clip(True, True, False)
    acc = 1
    dial = Dial(wri, 5, 5, height = 75, ticks = 12, bdcolor=None,
                label='Tilt Pyboard', style = Dial.COMPASS, pip=YELLOW)  # Border in fg color
    ptr = Pointer(dial)
    scale = 1/40
    while True:
        x, y, z = 5,10,3
        # Depending on relative alignment of display and Pyboard this line may
        # need changing: swap x and y or change signs so arrow points in direction
        # board is tilted.
        ptr.value(-y*scale + 1j*x*scale, YELLOW)
        refresh(ssd)
        utime.sleep_ms(200)

main()

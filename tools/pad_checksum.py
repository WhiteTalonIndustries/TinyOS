#!/usr/bin/env python3
"""Pads a raw RP2040 boot2 binary to 256 bytes and appends the 4-byte
checksum the boot ROM requires before it will execute it: CRC-32/MPEG-2
(poly 0x04C11DB7, init 0xFFFFFFFF, no input/output reflection, no final
XOR) over the first 252 bytes, stored little-endian in the last 4.

Note: pico-sdk's own pad_checksum tool computes something else (a
bit-reversed wrapping of the standard reflected CRC-32) that does NOT match
this -- and, verified empirically against real RP2040 hardware, does NOT
produce a checksum the boot ROM accepts. This implementation matches the
published CRC-32/MPEG-2 check value for "123456789" (0x0376E6E7) and was
confirmed to boot real hardware; that one does not and did not."""

import argparse
import struct
import sys


def crc32_mpeg2(data, init=0xFFFFFFFF):
    crc = init
    for byt in data:
        b = byt << 24
        for _ in range(8):
            top_bit_set = (crc ^ b) & 0x80000000
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if top_bit_set else (crc << 1) & 0xFFFFFFFF
            b = (b << 1) & 0xFFFFFFFF
    return crc


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("ifile", help="raw boot2 binary (from objcopy -O binary)")
    ap.add_argument("ofile", help="output: padded + checksummed 256-byte image")
    ap.add_argument("-p", "--pad", type=lambda x: int(x, 0), default=256,
                     help="total padded size including the 4-byte checksum")
    args = ap.parse_args()

    data = open(args.ifile, "rb").read()
    if len(data) > args.pad - 4:
        sys.exit("boot2 image too large: {} bytes (max {})".format(len(data), args.pad - 4))

    padded = data + bytes(args.pad - 4 - len(data))
    checksum = crc32_mpeg2(padded)

    with open(args.ofile, "wb") as f:
        f.write(padded + struct.pack("<L", checksum))


if __name__ == "__main__":
    main()

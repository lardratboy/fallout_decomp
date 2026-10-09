#!/usr/bin/env python3
"""Render every graphic stored in FALLOUT.EXE into one PNG sheet.

usage: asset_sheet.py FALLOUT_UNPACKED.EXE OUT.png
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png import write_png

W, H = 320, 200


def main(exe, out):
    d = open(exe, 'rb').read()
    ds = d[struct.unpack_from('<H', d, 8)[0] * 16:][0x7EC0:]
    pal = ds[0x568:0x568 + 768]
    bright = max(range(256), key=lambda i: sum(pal[3 * i:3 * i + 3]))
    bg = min(range(1, 256), key=lambda i: abs(pal[3 * i] - 8) + abs(pal[3 * i + 1] - 8) + abs(pal[3 * i + 2] - 12))
    canvas = bytearray([bg]) * (W * H)

    def put(img, off, w, h, x, y, mask=None):
        for r in range(h):
            for c in range(w):
                v = img[off + r * w + c]
                if v:
                    canvas[(y + r) * W + x + c] = mask if mask is not None else v

    put(ds, 0x00A8, 81, 15, 4, 4)                                   # logo
    for k in range(4):                                              # bricks
        put(ds, 0x3D1A + k * 100, 10, 10, 100 + k * 12, 6)
    for k in range(100):                                            # gem tiles
        put(ds, 0x0868 + k * 121, 11, 11, 4 + (k % 25) * 12, 24 + (k // 25) * 12)
    for k in range(5):                                              # explosion
        put(ds, 0x38EC + k * 121, 11, 11, 4 + k * 12, 76)
    for k in range(18):                                             # bombs
        put(ds, 0x5F0A + k * 121, 11, 11, 4 + k * 12, 92)
        put(ds, 0x67C2 + k * 121, 11, 11, 4 + k * 12, 106)
    for k in range(39):                                             # 8x8 font
        put(ds, 0x4EC3 + k * 64, 8, 8, 4 + (k % 26) * 10, 124 + (k // 26) * 10, bright)
    for k in range(42):                                             # 6x6 font
        put(ds, 0x58D0 + k * 36, 6, 6, 4 + (k % 26) * 8, 148 + (k // 26) * 8, bright)
    for i in range(256):                                            # palette
        x, y = 4 + (i % 64) * 4, 168 + (i // 64) * 6
        for r in range(5):
            for c in range(4):
                canvas[(y + r) * W + x + c] = i
    write_png(out, W, H, bytes(canvas), pal, 3)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

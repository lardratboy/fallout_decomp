#!/usr/bin/env python3
"""Convert raw 320x200 VRAM dumps (from the native harness, SHOT_TICKS) to PNG
using the game's palette taken from the executable.

usage: shots.py FALLOUT_UNPACKED.EXE SHOT_DIR [scale]
"""
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png import write_png


def palette(exe):
    d = open(exe, 'rb').read()
    img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
    return img[0x7EC0 + 0x568:0x7EC0 + 0x568 + 768]


def main(exe, shot_dir, scale=2):
    pal = palette(exe)
    for raw in sorted(glob.glob(os.path.join(shot_dir, '*.raw'))):
        out = raw[:-4] + '.png'
        write_png(out, 320, 200, open(raw, 'rb').read(), pal, scale)
        print(out)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 2)

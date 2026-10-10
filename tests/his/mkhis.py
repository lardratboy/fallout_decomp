#!/usr/bin/env python3
"""Write the FALLOUT.HIS fixtures used by tests/t_his_*.keys.

FALLOUT.HIS is the game's high-score table as it is in memory: 10 entries of
14 bytes (name[8], 32-bit score, 16-bit slot), sorted ascending, XORed with
2, 4, 6, ... (the key is a byte, so it wraps after 127 bytes).

Names only use bytes below 0x80: draw_char() indexes its font table with a
signed char, so higher bytes would read outside the table.

  full.his   140 bytes.  Scores around the -WOW- threshold (> 60000), past
             16 bits and with the top bit set; bytes after the NULs; the top
             name has no NUL, so the game draws on into its score and slot
             bytes ("ABCDEFGH12345").
  short.his  130 bytes: the read stops inside the last entry.  The rest of the
             default table, still in memory, is scrambled along with the file
             (its top score comes out as -WOW-).

usage: tests/his/mkhis.py   (writes next to itself)
"""
import os
import struct

TABLE = [                                   # name (8 bytes), score, slot
    (b'LOW\0QRST', 300, 0x0102),
    (b'Z9-Z\0\x01\x02\x03', 1500, 0xFFFF),
    (b'MID\0\0\0\0\0', 2000, 7),
    (b'SIXTY\0\0\0', 60000, 3),
    (b'WOW\0\0\0\0\0', 60001, 4),
    (b'BIG\0\0\0\0\0', 66000, 5),
    (b'MILLION\0', 1000000, 6),
    (b'SIGN\0\0\0\0', 0x80000005, 7),
    (b'HUGE\0\0\0\0', 4000000000, 8),
    (b'ABCDEFGH', 0x34333231, 0x0035),      # "1234", then "5" and a NUL
]


def scramble(data):
    out, k = bytearray(data), 0
    for i in range(len(out)):
        k = (k + 2) & 0xFF
        out[i] ^= k
    return bytes(out)


def table(entries):
    return b''.join(name + struct.pack('<IH', score, slot)
                    for name, score, slot in entries)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    full = table(TABLE)
    assert len(full) == 140
    short = table(TABLE[:9] + [(b'JO\0\0\0\0\0\0', 0, 0)])[:130]
    for name, data in (('full.his', full), ('short.his', short)):
        with open(os.path.join(here, name), 'wb') as f:
            f.write(scramble(data))


if __name__ == '__main__':
    main()

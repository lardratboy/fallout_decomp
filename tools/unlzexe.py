#!/usr/bin/env python3
"""Unpack an LZEXE 0.90/0.91 compressed DOS MZ executable."""
import struct, sys

def unpack(data):
    hdr = list(struct.unpack_from('<14H', data, 0))
    sig = data[0x1c:0x20]
    if sig not in (b'LZ91', b'LZ09'):
        raise SystemExit('not LZEXE: %r' % sig)
    hdr_paras, e_ip, e_cs = hdr[4], hdr[10], hdr[11]
    ldr = (hdr_paras + e_cs) * 16             # file offset of loader segment
    r_ip, r_cs, r_sp, r_ss, comp_paras, inc_paras, ldr_size, csum = struct.unpack_from('<8H', data, ldr)

    # --- relocations -----------------------------------------------------
    relocs = []
    if sig == b'LZ91':
        p = ldr + 0x158
        seg = off = 0
        while True:
            span = data[p]; p += 1
            if span == 0:
                span = struct.unpack_from('<H', data, p)[0]; p += 2
                if span == 0:
                    seg += 0x0fff; continue
                if span == 1:
                    break
            off += span
            seg += (off & ~0x0f) >> 4
            off &= 0x0f
            relocs.append((off, seg))
    else:
        p = ldr + 0x19d
        seg = 0
        while True:
            n = struct.unpack_from('<H', data, p)[0]; p += 2
            for _ in range(n):
                o = struct.unpack_from('<H', data, p)[0]; p += 2
                relocs.append((o, seg))
            if seg >= 0xf000: break
            seg += 0x1000

    # --- decompress -------------------------------------------------------
    p = (e_cs - comp_paras + hdr_paras) * 16
    out = bytearray()
    st = {'p': p}
    def getw():
        v = struct.unpack_from('<H', data, st['p'])[0]; st['p'] += 2; return v
    def getb():
        v = data[st['p']]; st['p'] += 1; return v
    bits = {'buf': getw(), 'cnt': 16}
    def getbit():
        b = bits['buf'] & 1
        bits['cnt'] -= 1
        if bits['cnt'] == 0:
            bits['buf'] = getw(); bits['cnt'] = 16
        else:
            bits['buf'] >>= 1
        return b
    while True:
        if getbit():
            out.append(getb()); continue
        if getbit() == 0:
            ln = ((getbit() << 1) | getbit()) + 2
            span = getb() | 0xff00
        else:
            lo = getb(); hi = getb()
            span = lo | ((hi & ~0x07) << 5) | 0xe000
            ln = (hi & 0x07) + 2
            if ln == 2:
                ln = getb()
                if ln == 0: break
                if ln == 1: continue
                ln += 1
        span -= 0x10000          # negative displacement
        for _ in range(ln):
            out.append(out[len(out) + span])

    # --- build new MZ -----------------------------------------------------
    nrel = len(relocs)
    hsize = 0x1c + nrel * 4
    hsize = (hsize + 0x1ff) & ~0x1ff if False else (hsize + 15) & ~15
    total = hsize + len(out)
    min_alloc = hdr[5] - (inc_paras + ((ldr_size + 16 - 1) >> 4) + 9)
    if min_alloc < 0: min_alloc = 0
    # simpler: keep original minalloc adjusted the way unlzexe does
    min_alloc = max(0, hdr[5] - (inc_paras + ((ldr_size + 15) >> 4) + 9)) if False else hdr[5]
    nh = struct.pack('<14H', 0x5a4d, total % 512, (total + 511) // 512, nrel,
                     hsize // 16, min_alloc, hdr[6], r_ss, r_sp, 0, r_ip, r_cs, 0x1c, 0)
    nh += b''.join(struct.pack('<HH', o, s) for o, s in relocs)
    nh += b'\0' * (hsize - len(nh))
    info = dict(ip=r_ip, cs=r_cs, sp=r_sp, ss=r_ss, nrel=nrel, size=len(out))
    return nh + bytes(out), info

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    exe, info = unpack(d)
    open(sys.argv[2], 'wb').write(exe)
    print(info)

#!/usr/bin/env python3
"""Find Turbo C switch jump tables (dense and sparse) in a code range and
emit Ghidra 'jt' annotation lines plus a human-readable value->target map."""
import struct, sys

def main(path, lo, hi):
    d = open(path, 'rb').read()
    hs = struct.unpack_from('<H', d, 8)[0] * 16
    img = d[hs:]
    w = lambda o: struct.unpack_from('<H', img, o)[0]
    for i in range(lo, hi - 4):
        # dense: cs: jmp [bx+disp16]
        if img[i:i+3] == b'\x2e\xff\xa7':
            tab = w(i + 3)
            n = None
            for k in range(i - 16, i):
                if img[k:k+2] == b'\x83\xfb': n = img[k+2] + 1
                if img[k:k+2] == b'\x81\xfb': n = w(k + 2) + 1
            tg = [w(tab + 2 * j) for j in range(n)]
            print('# dense  %x: %s' % (i, ' '.join('%d->%x' % (j, t) for j, t in enumerate(tg))))
            print('jt %x %s' % (i, ' '.join('%x' % t for t in dict.fromkeys(tg))))
        # sparse: cs: jmp [bx+disp8], preceded by mov cx,n ; mov bx,tab
        if img[i:i+3] == b'\x2e\xff\x67':
            for k in range(i - 24, i):
                if img[k] == 0xb9 and img[k+3] == 0xbb:
                    n, tab = w(k + 1), w(k + 4)
                    if img[i+3] == 2 * n:
                        vals = [w(tab + 2 * j) for j in range(n)]
                        tg = [w(tab + 2 * n + 2 * j) for j in range(n)]
                        print('# sparse %x: %s' % (i, ' '.join('%#x->%x' % vt for vt in zip(vals, tg))))
                        print('jt %x %s' % (i, ' '.join('%x' % t for t in dict.fromkeys(tg))))
                        break

if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16))

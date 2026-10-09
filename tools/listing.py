#!/usr/bin/env python3
"""Produce an annotated 16-bit disassembly listing of the C module, one block
per function, with call targets and DGROUP references named from symbols.txt."""
import bisect, re, struct, sys
from capstone import *

def load_syms(path):
    code, data = {}, []
    for line in open(path):
        p = line.split()
        if not p or p[0].startswith('#'): continue
        if p[0] == 'c': code[int(p[1], 16)] = p[2]
        elif p[0] == 'd': data.append((int(p[1], 16), p[2], int(p[3])))
    data.sort()
    return code, data

def main(exe, symfile, lo, hi, jt_ranges=()):
    d = open(exe, 'rb').read()
    img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
    code, data = load_syms(symfile)
    starts = [a for a, _, _ in data]
    def dname(v):
        i = bisect.bisect_right(starts, v) - 1
        if i >= 0:
            a, n, sz = data[i]
            if a <= v < a + max(sz, 1):
                return n if v == a else '%s+%d' % (n, v - a)
        return None
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    funcs = sorted(a for a in code if lo <= a < hi)
    for k, f in enumerate(funcs):
        end = funcs[k + 1] if k + 1 < len(funcs) else hi
        print('\n;================ %s (%04x) ================' % (code[f], f))
        off = f
        while off < end:
            # skip switch tables embedded in code
            skip = next((b for a, b in jt_ranges if a == off), None)
            if skip:
                print('%05x: <switch table %x bytes>' % (off, skip - off)); off = skip; continue
            ins = next(md.disasm(img[off:end], off), None)
            if ins is None:
                print('%05x: db %02x' % (off, img[off])); off += 1; continue
            ops = ins.op_str
            note = ''
            m = re.match(r'(call|jmp) (0x[0-9a-f]+)$', ins.mnemonic + ' ' + ops)
            if ins.mnemonic == 'call' and ops.startswith('0x'):
                t = int(ops, 16); ops = code.get(t, ops)
            else:
                for h in re.findall(r'0x[0-9a-f]+', ops):
                    v = int(h, 16)
                    n = dname(v) if v >= 0xa8 else None
                    if n and ('[' in ops or ins.mnemonic in ('mov', 'push')):
                        note += ' ; ' + n
            print('%05x: %-7s %s%s' % (ins.address, ins.mnemonic, ops, note))
            off += ins.size

if __name__ == '__main__':
    # switch tables in code: (start, end)
    jt = [(0x1536, 0x1550), (0x1550, 0x1550 + 29 * 4), (0x32a0, 0x32a0 + 40), (0x4061, 0x4061 + 16)]
    main(sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16), jt)

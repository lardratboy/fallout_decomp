#!/usr/bin/env python3
"""Collect DGROUP addresses referenced by user code (memory operands and
immediates) so data can be carved into variables."""
import struct, sys, collections
from capstone import *
from capstone.x86 import *

d = open(sys.argv[1], 'rb').read()
img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
LO, HI = 0xa8, 0x892a
md = Cs(CS_ARCH_X86, CS_MODE_16); md.detail = True
refs = collections.defaultdict(set)

def sweep(lo, hi):
    off = lo
    while off < hi:
        got = False
        for i in md.disasm(img[off:hi], off):
            got = True
            off = i.address + i.size
            for op in i.operands:
                if op.type == X86_OP_MEM and op.mem.segment in (0, X86_REG_DS, X86_REG_SS) and op.mem.base != X86_REG_BP:
                    v = op.mem.disp & 0xffff
                    if LO <= v < HI: refs[v].add('%x:mem' % i.address)
                elif op.type == X86_OP_IMM:
                    v = op.imm & 0xffff
                    if LO <= v < HI and i.mnemonic in ('mov', 'push'): refs[v].add('%x:imm' % i.address)
        if not got: off += 1

sweep(0x239, 0x43ae)
# asm module: skip the unrolled blit bodies
for a, b in [(0x43ae, 0x4786), (0x4f5a, 0x4faa), (0x577d, 0x5813), (0x5e58, 0x5fe4)]:
    sweep(a, b)
for v in sorted(refs):
    print('%04x %s' % (v, ' '.join(sorted(refs[v]))))

#!/usr/bin/env python3
"""Run the ORIGINAL (unpacked) FALLOUT.EXE in a Unicorn 8086 emulator with a
minimal, deterministic BIOS/DOS, driven by a key script, and write a trace.

Deterministic model (shared with the native harness in harness.c):
  * time: every call of the game's get_ticks() (INT 1Ah/00h issued from code
    offset 0x46A7) returns the current tick counter; the counter advances
    after every TICK_DIV calls (environment variable, default 1).
  * keyboard: a script of (tick, byte) pairs.  A byte becomes available once
    the tick counter has reached its tick.  INT 21h/0Bh reports availability,
    INT 21h/07h returns the next byte; if nothing is available it jumps the
    clock forward to the next scripted byte, or ends the run when the script
    is exhausted.
  * INT 21h/0Bh (kbhit): after 1000 consecutive polls with no key ready the
    clock jumps to the next scripted key (some loops never read the clock).
  * delay(), sound(), nosound() from the C library are patched to `ret`;
    their calls are logged.
  * the run also ends after --max-ticks ticks or at program exit.

Trace: one line per get_ticks() call:  "<tick> <crc32 of A000:0000-F9FF>
<score> <level> <bonus> <total>", the same state at every delay() call
("delay <ms> <crc32> ..."; the end-of-game tally runs on delay() alone),
"sound <freq>" and "nosound" for the speaker, plus key-event lines.

usage: run_original.py UNPACKED.EXE SCRIPT OUT.trace [--max-ticks N]
"""
import argparse
import struct
import os
import sys
import zlib

from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_CODE
from unicorn.x86_const import *

LOAD_SEG = 0x1000            # load module segment (PSP at LOAD_SEG-0x10)
PSP_SEG = LOAD_SEG - 0x10
ENV_SEG = 0x0F00
BUF_SEGS = [0x3000, 0x4000]  # what INT 21h/48h hands out (same as harness.c)
DGROUP = LOAD_SEG + 0x07EC
GET_TICKS_INT = 0x46A7       # offset of "int 1Ah" inside get_ticks()
SRAND = 0x6333
SOUND = 0x7110
NOSOUND = 0x713C
DROP_PIECE = 0x0F4B
DELAY = 0x6CAB
IDLE_POLLS = 1000
TICK_DIV = int(os.environ.get('TICK_DIV', '1'))   # get_ticks() calls per tick
DUMP_TICKS = {int(x) for x in os.environ.get('DUMP_TICKS', '').split(',') if x}
LIB_PATCH = {0x6CAB: 'delay', 0x7110: 'sound', 0x713C: 'nosound'}


def load_script(path):
    keys = []
    for line in open(path):
        line = line.split('#')[0].strip()
        if not line:
            continue
        tick, rest = line.split(None, 1)
        for b in parse_keys(rest):
            keys.append((int(tick), b))
    return keys


def parse_keys(s):
    """Key tokens: 'c' (a character), 0xNN (raw byte), F10/UP/... (extended)."""
    ext = {'F1': 0x3B, 'F10': 0x44, 'UP': 0x48, 'LEFT': 0x4B, 'PAD5': 0x4C,
           'RIGHT': 0x4D, 'DOWN': 0x50}
    named = {'ENTER': 13, 'ESC': 27, 'BS': 8, 'TAB': 9, 'SPACE': 32}
    out = []
    for tok in s.split():
        if tok in ext:
            out += [0, ext[tok]]
        elif tok in named:
            out.append(named[tok])
        elif tok.lower().startswith('0x'):
            out.append(int(tok, 16))
        else:
            out += [ord(c) for c in tok]
    return out


class Machine:
    def __init__(self, exe, script, trace, max_ticks, bot=None, record=None):
        self.keys = list(script)
        self.bot = bot
        self.record = record
        self.dgroup = DGROUP
        self.ticks = 0
        self.calls = 0
        self.idle = 0
        self.last_poll_tick = -1
        self.max_ticks = max_ticks
        self.trace = trace
        self.srand_seed = None
        self.alloc = list(BUF_SEGS)
        mu = self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x110000)

        d = open(exe, 'rb').read()
        h = struct.unpack_from('<14H', d, 0)
        img = bytearray(d[h[4] * 16:])
        for i in range(h[3]):                    # relocations
            off, seg = struct.unpack_from('<HH', d, h[12] + 4 * i)
            a = seg * 16 + off
            struct.pack_into('<H', img, a, (struct.unpack_from('<H', img, a)[0] + LOAD_SEG) & 0xFFFF)
        for off in LIB_PATCH:
            img[off] = 0xC3                      # ret
        mu.mem_write(LOAD_SEG * 16, bytes(img))

        # PSP + environment
        psp = bytearray(256)
        psp[0:2] = b'\xCD\x20'
        struct.pack_into('<H', psp, 2, 0x9FFF)   # top of memory
        struct.pack_into('<H', psp, 0x2C, ENV_SEG)
        psp[0x80] = 0
        psp[0x81] = 0x0D
        mu.mem_write(PSP_SEG * 16, bytes(psp))
        env = b'COMSPEC=C:\\COMMAND.COM\0PATH=C:\\\0\0\x01\0C:\\FALLOUT.EXE\0'
        mu.mem_write(ENV_SEG * 16, env)
        mu.mem_write(0x48A, b'\x00' if os.environ.get('NO_VGA') else b'\x08')

        mu.reg_write(UC_X86_REG_CS, LOAD_SEG + h[11])
        mu.reg_write(UC_X86_REG_IP, h[10])
        mu.reg_write(UC_X86_REG_SS, LOAD_SEG + h[7])
        mu.reg_write(UC_X86_REG_SP, h[8])
        mu.reg_write(UC_X86_REG_DS, PSP_SEG)
        mu.reg_write(UC_X86_REG_ES, PSP_SEG)
        mu.hook_add(UC_HOOK_INTR, self.on_int)
        a = LOAD_SEG * 16 + SRAND
        mu.hook_add(UC_HOOK_CODE, self.on_srand, begin=a, end=a)
        a = LOAD_SEG * 16 + DELAY
        mu.hook_add(UC_HOOK_CODE, self.on_delay, begin=a, end=a)
        a = LOAD_SEG * 16 + SOUND
        mu.hook_add(UC_HOOK_CODE, self.on_sound, begin=a, end=a)
        a = LOAD_SEG * 16 + NOSOUND
        mu.hook_add(UC_HOOK_CODE, self.on_nosound, begin=a, end=a)
        if bot:
            a = LOAD_SEG * 16 + DROP_PIECE
            mu.hook_add(UC_HOOK_CODE, self.on_drop_piece, begin=a, end=a)
        self.stop_reason = None

    # ---- helpers ---------------------------------------------------------
    def r(self, reg):
        return self.mu.reg_read(reg)

    def w(self, reg, v):
        self.mu.reg_write(reg, v & 0xFFFF)

    def set_cf(self, cf):
        f = self.r(UC_X86_REG_EFLAGS)
        self.mu.reg_write(UC_X86_REG_EFLAGS, (f | 1) if cf else (f & ~1))

    def ds_word(self, off):
        return struct.unpack('<H', self.mu.mem_read(DGROUP * 16 + off, 2))[0]

    def ds_long(self, off):
        return struct.unpack('<I', self.mu.mem_read(DGROUP * 16 + off, 4))[0]

    def stop(self, why):
        if not self.stop_reason:
            self.stop_reason = why
        self.mu.emu_stop()

    def on_drop_piece(self, mu, addr, size, user):
        self.bot.new_piece = True

    def bot_keys(self, keys, later=0):
        for b in keys:
            self.keys.append((self.ticks + later, b))
            self.record.write('%d 0x%02x\n' % (self.ticks + later, b))

    def kbhit(self):
        # Some loops poll the keyboard without reading the clock; after
        # IDLE_POLLS empty polls the clock jumps to the next scripted key.
        # The bot only types on the first poll of a tick, so that replaying
        # its recorded keys (available from that tick on) is identical.
        # In loops that never read the clock it may also type just before the
        # idle clock jump, stamping the key with the tick the jump goes to.
        if self.bot and not self.keys:
            later = None
            if self.ticks != self.last_poll_tick:
                later = 0
            elif self.idle == IDLE_POLLS - 1:
                later = 1
            if later is not None:
                sp = self.r(UC_X86_REG_SS) * 16 + self.r(UC_X86_REG_SP)
                ret = struct.unpack('<H', self.mu.mem_read(sp, 2))[0]   # caller of kbhit()
                self.bot_keys(self.bot.on_poll(self, ret, self.idle), later)
        self.last_poll_tick = self.ticks
        if self.key_ready():
            self.idle = 0
            return True
        self.idle += 1
        if self.idle >= IDLE_POLLS:
            self.idle = 0
            if not self.keys:
                self.stop('script exhausted')
                return False
            self.ticks = max(self.ticks, self.keys[0][0])
        return False

    def key_ready(self):
        return bool(self.keys) and self.keys[0][0] <= self.ticks

    def dump_state(self, t):
        b = bytes(self.mu.mem_read(DGROUP * 16 + 0x7863, 153))
        p = bytes(self.mu.mem_read(DGROUP * 16 + 0x7860, 3))
        self.trace.write('state %d orient=%d pos=%d,%d piece=%s\n' % (
            t, self.ds_word(0x3B79), self.ds_word(0x741A), self.ds_word(0x741C), p.hex()))
        for r in range(17):
            self.trace.write('  ' + b[r * 9:r * 9 + 9].hex(' ') + '\n')

    def state(self):
        vram = bytes(self.mu.mem_read(0xA0000, 64000))
        return '%08x %d %d %d %d' % (
            zlib.crc32(vram), self.ds_long(0x3B6F), self.ds_word(0x3B85),
            self.ds_word(0x3B73), self.ds_long(0x3B6B))

    def log_tick(self, t):
        if t in DUMP_TICKS:
            self.dump_state(t)
        self.trace.write('%d %s\n' % (t, self.state()))

    def on_delay(self, mu, addr, size, user):
        sp = self.r(UC_X86_REG_SS) * 16 + self.r(UC_X86_REG_SP)
        ms = struct.unpack('<H', mu.mem_read(sp + 2, 2))[0]
        self.trace.write('delay %d %s\n' % (ms, self.state()))

    def on_sound(self, mu, addr, size, user):
        sp = self.r(UC_X86_REG_SS) * 16 + self.r(UC_X86_REG_SP)
        freq = struct.unpack('<H', mu.mem_read(sp + 2, 2))[0]
        self.trace.write('sound %d\n' % freq)

    def on_nosound(self, mu, addr, size, user):
        self.trace.write('nosound\n')

    def on_srand(self, mu, addr, size, user):
        sp = self.r(UC_X86_REG_SS) * 16 + self.r(UC_X86_REG_SP)
        self.srand_seed = struct.unpack('<H', mu.mem_read(sp + 2, 2))[0]
        self.trace.write('srand %d\n' % self.srand_seed)

    # ---- interrupts ----------------------------------------------------------
    def on_int(self, mu, intno, user):
        ah = self.r(UC_X86_REG_AH)
        al = self.r(UC_X86_REG_AL)
        ip = self.r(UC_X86_REG_IP)
        if intno == 0x1A:
            if ip - 2 == GET_TICKS_INT:
                if self.ticks >= self.max_ticks:
                    return self.stop('max ticks')
                t = self.ticks
                self.log_tick(t)
                self.calls += 1
                if self.calls % TICK_DIV == 0:
                    self.ticks += 1
            else:
                t = 0                            # C0 startup
            self.w(UC_X86_REG_CX, t >> 16)
            self.w(UC_X86_REG_DX, t)
            self.w(UC_X86_REG_AX, 0)
        elif intno == 0x10:
            ax = self.r(UC_X86_REG_AX)
            if ah == 0x00 and (al & 0x7F) == 0x13:
                mu.mem_write(0xA0000, b'\0' * 65536)
            self.trace.write('int10 %04x\n' % ax)
        elif intno == 0x21:
            self.dos(ah, al)
        else:
            self.stop('unhandled int %02x at %04x' % (intno, ip))

    def dos(self, ah, al):
        R = UC_X86_REG_AX
        if ah == 0x30:
            self.w(R, 0x0005)
        elif ah == 0x4A:
            self.set_cf(0)
        elif ah == 0x35:
            self.w(UC_X86_REG_BX, 0); self.w(UC_X86_REG_ES, 0)
        elif ah == 0x25:
            pass
        elif ah == 0x44:                         # IOCTL: report a device
            self.w(UC_X86_REG_DX, 0x80); self.set_cf(0)
        elif ah == 0x0B:
            self.w(R, (ah << 8) | (0xFF if self.kbhit() else 0))
        elif ah == 0x07:
            if self.bot and not self.keys:
                # one tick later, so a keyboard flush at this tick cannot see it
                self.bot_keys(self.bot.on_getkey(self), later=1)
            if not self.key_ready():
                if not self.keys:
                    return self.stop('script exhausted')
                self.ticks = max(self.ticks, self.keys[0][0])
            t, b = self.keys.pop(0)
            self.trace.write('key %d %02x\n' % (self.ticks, b))
            self.w(R, (ah << 8) | b)
        elif ah == 0x09:
            self.trace.write('puts\n')
        elif ah == 0x02:
            pass
        elif ah == 0x2A:                         # date: 10 Oct 1991, Thursday
            self.w(UC_X86_REG_CX, 1991); self.w(UC_X86_REG_DX, 0x0A0A); self.w(R, 0x2A04)
        elif ah == 0x2C:                         # time 12:00:00.00
            self.w(UC_X86_REG_CX, 0x0C00); self.w(UC_X86_REG_DX, 0)
        elif ah == 0x48:
            if os.environ.get('ALLOC_FAIL') and len(self.alloc) <= int(os.environ['ALLOC_FAIL']):
                self.w(R, 8); self.w(UC_X86_REG_BX, 0x100); self.set_cf(1)
            else:
                self.w(R, self.alloc.pop(0)); self.set_cf(0)
        elif ah == 0x49:
            self.set_cf(0)
        elif ah == 0x3D:                         # open: file not found
            self.w(R, 2); self.set_cf(1)
        elif ah == 0x3C:
            self.w(R, 5); self.set_cf(0)
        elif ah == 0x40:
            self.w(R, self.r(UC_X86_REG_CX)); self.set_cf(0)
        elif ah == 0x3E:
            self.set_cf(0)
        elif ah == 0x29:                         # parse FCB (system())
            self.w(R, 0)
        elif ah == 0x37:                         # switch char (system())
            self.w(UC_X86_REG_DX, ord('/')); self.w(R, 0)
        elif ah == 0x4B:                         # EXEC (system()): succeed
            self.trace.write('exec\n'); self.set_cf(0); self.w(R, 0)
        elif ah == 0x4D:
            self.w(R, 0)
        elif ah == 0x4C:
            self.trace.write('exit %d\n' % al)
            self.stop('exit')
        else:
            self.stop('unhandled DOS fn %02x' % ah)

    def run(self):
        cs = self.r(UC_X86_REG_CS)
        try:
            self.mu.emu_start(cs * 16 + self.r(UC_X86_REG_IP), 0xFFFFF)
        except UcError as e:
            self.stop_reason = 'cpu error: %s at %04x:%04x' % (
                e, self.r(UC_X86_REG_CS), self.r(UC_X86_REG_IP))
        return self.stop_reason


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('script')
    ap.add_argument('trace')
    ap.add_argument('--max-ticks', type=int, default=200000)
    ap.add_argument('--bot', metavar='RECORD.keys',
                    help='play with the bot (script = menu keys) and record its keys')
    ap.add_argument('--games', type=int, default=1)
    ap.add_argument('--lose-at-level', type=int, metavar='N',
                    help='bot: stop playing well once the level reaches N')
    a = ap.parse_args()
    bot = record = None
    keys = load_script(a.script)
    if a.bot:
        import bot as botmod
        bot = botmod.Bot([b for _, b in keys], a.games, a.lose_at_level)
        keys = []
        record = open(a.bot, 'w')
        record.write('# recorded by run_original.py --bot\n# TICK_DIV=%d\n' % TICK_DIV)
    with open(a.trace, 'w') as tr:
        m = Machine(a.exe, keys, tr, a.max_ticks, bot, record)
        why = m.run()
        tr.write('end %s\n' % why)
    if record:
        record.close()
    print('original: %s after %d ticks (srand seed %s)' % (why, m.ticks, m.srand_seed))


if __name__ == '__main__':
    main()

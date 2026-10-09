"""A simple FallOut-playing bot for run_original.py (--bot).

It plays the ORIGINAL game inside the emulator: whenever a new piece appears
it evaluates every column / orientation / symbol order, simulates the drop and
the resulting cascade, and types the keys for the best one.  Menus, the pause
screen, the title screen and initials entry get canned keys.  Every key it
types is recorded as an ordinary key script, which the native harness then
replays -- so the comparison remains a plain lockstep test.
"""
import struct

# code ranges (unpacked offsets) of the functions that poll the keyboard
CONTEXTS = [(0x0F41, 0x0F4B, 'game'), (0x27DF, 0x299E, 'title'),
            (0x2F18, 0x32C8, 'menu'), (0x34F9, 0x37BB, 'difficulty'),
            (0x3B69, 0x3C7F, 'pause'), (0x3DAD, 0x4071, 'initials')]


def find_runs(b):
    m = [0] * 153
    def run(i, d):
        if b[i] and b[i] == b[i + d] == b[i + 2 * d]:
            m[i] = m[i + d] = m[i + 2 * d] = 1
    for r in range(17):
        for c in range(7):
            run(r * 9 + c, 1)
    for c in range(9):
        for r in range(15):
            run(r * 9 + c, 9)
    for c in range(7):
        for r in range(15):
            run(r * 9 + c, 10)
    for c in range(8, 1, -1):
        for r in range(15):
            run(r * 9 + c, 8)
    return m


def collapse(b):
    for c in range(9):
        col = [b[r * 9 + c] for r in range(17) if b[r * 9 + c]]
        col = [0] * (17 - len(col)) + col
        for r in range(17):
            b[r * 9 + c] = col[r]


def cascade(b):
    total = 0
    while True:
        m = find_runs(b)
        n = sum(m)
        if not n:
            return total
        total += n
        for i in range(153):
            if m[i]:
                b[i] = 0
        collapse(b)


def top(b, c):
    for r in range(17):
        if b[r * 9 + c]:
            return r
    return 17


class Bot:
    def __init__(self, menu_keys, games):
        self.menu = list(menu_keys)
        self.games = games
        self.new_piece = False
        self.initials = []

    @staticmethod
    def context(ret):
        for lo, hi, name in CONTEXTS:
            if lo <= ret < hi:
                return name
        return None

    def plan(self, m):
        ds = m.mu.mem_read
        base = (m.dgroup) * 16
        b = [x if x < 128 else x - 256 for x in ds(base + 0x7863, 153)]
        p = list(ds(base + 0x7860, 3))
        bomb_active = m.ds_word(0x77B4)
        bomb_col = m.ds_word(0x77B2)
        bombs = m.ds_word(0x3B89)
        best = None
        for swaps in range(3):
            pc = list(p)
            for _ in range(swaps):
                pc = [pc[2], pc[0], pc[1]]
            for orient in ('v', 'h'):
                for c in (range(9) if orient == 'v' else range(1, 8)):
                    bb = list(b)
                    if orient == 'v':
                        r = top(bb, c) - 1
                        if r < 2:
                            continue
                        bb[(r - 2) * 9 + c], bb[(r - 1) * 9 + c], bb[r * 9 + c] = pc
                    else:
                        r = min(top(bb, c - 1), top(bb, c), top(bb, c + 1)) - 1
                        if r < 1:
                            continue
                        bb[r * 9 + c - 1], bb[r * 9 + c], bb[r * 9 + c + 1] = pc
                        collapse(bb)
                    cleared = cascade(bb)
                    hs = [17 - top(bb, k) for k in range(9)]
                    score = cleared * 20 - sum(h * h for h in hs) / 8.0 - 40 * (hs[4] > 12)
                    if bomb_active and (c == bomb_col if orient == 'v' else abs(c - bomb_col) <= 1):
                        score += 15
                    cost = swaps + (orient == 'h') + abs(c - 4)
                    key = (score, -cost)
                    if best is None or key > best[0]:
                        best = (key, swaps, orient, c)
        keys = []
        if bombs and max(17 - top(b, k) for k in range(9)) >= 11:
            keys.append(ord('b'))
        if best:
            _, swaps, orient, c = best
            keys += [ord('5')] * swaps
            if orient == 'h':
                keys.append(ord('i'))
            keys += [ord('4') if c < 4 else ord('6')] * abs(c - 4)
        keys.append(ord('2'))
        return keys

    def on_poll(self, m, ret, idle):
        """Called when the game polls the keyboard and nothing is queued."""
        ctx = self.context(ret)
        if ctx == 'game':
            if self.new_piece:
                self.new_piece = False
                return self.plan(m)
            return []
        if ctx in ('menu', 'title') and idle < 200:   # let the screen animate a while
            return []
        if ctx in ('menu', 'difficulty') and self.menu:
            return [self.menu.pop(0)]
        if ctx == 'menu':
            if self.games > 0:
                self.games -= 1
                return [ord('1')]
            return [ord('Q')]
        if ctx == 'difficulty':
            return [ord('1')]
        return [ord('x')]                   # title / pause: any key

    def on_getkey(self, m):
        """Blocking getkey with nothing queued: only initials entry does that."""
        if not self.initials:
            self.initials = [ord(c) for c in 'BOT'] + [13]
        return [self.initials.pop(0)]

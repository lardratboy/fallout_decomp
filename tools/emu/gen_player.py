#!/usr/bin/env python3
"""Generate a key script that "plays": choose difficulty, start a level, then
for every piece shift it a random distance, maybe rotate/swap, and drop it.
Optionally switches cheat mode on (frequent bombs).

usage: gen_player.py SEED PIECES [--symbols 1-9] [--level 1-7] [--cheat] > x.keys
"""
import argparse
import random

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('seed', type=int)
    ap.add_argument('pieces', type=int)
    ap.add_argument('--symbols', default='1')
    ap.add_argument('--level', default='1')
    ap.add_argument('--cheat', action='store_true')
    ap.add_argument('--bomb-rate', type=float, default=0.02)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    t = 30
    out = []
    def key(k, gap=3):
        nonlocal t
        t += gap
        out.append('%d %s' % (t, k))
    key('D'); key('B' if a.cheat else 'x'); key(a.symbols)
    key(a.level, 10)
    for n in range(a.pieces):
        r = rng.random()
        if r < a.bomb_rate:
            key(rng.choice(['b', 'B']))             # use a bomb
        elif r < a.bomb_rate + 0.01:
            key('+')
        elif r < a.bomb_rate + 0.015:
            key('p'); key('x', 50)                   # pause, then resume
        elif r < a.bomb_rate + 0.02:
            key('s')
        shift = rng.randint(-4, 4)
        for _ in range(abs(shift)):
            key(rng.choice(['LEFT', '4', 'j']) if shift < 0 else rng.choice(['RIGHT', '6', 'l']), 2)
        for _ in range(rng.choice([0, 0, 1, 2])):
            key(rng.choice(['5', 'k', 'PAD5']), 2)
        if rng.random() < 0.3:
            key(rng.choice(['8', 'UP', 'TAB', 'i']), 2)
            for _ in range(rng.randint(0, 3)):
                key(rng.choice(['LEFT', 'RIGHT']), 2)
        key(rng.choice(['2', 'DOWN', 'SPACE', 'ENTER']), rng.choice([2, 4, 8]))
        key('x', rng.choice([6, 10, 20]))           # idle key (ignored in play)
    print('\n'.join(out))

if __name__ == '__main__':
    main()

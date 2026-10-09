#!/bin/sh
# Differential test: run the original FALLOUT.EXE (emulated) and the
# reconstructed C sources (native) on the same key script and compare traces.
#
# usage: tools/emu/difftest.sh SCRIPT [MAX_TICKS]
# needs: python3 with `unicorn` (pip install unicorn), a C compiler, zlib.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PY=${PYTHON:-python3}
SCRIPT=$1
MAX=${2:-200000}
OUT=$ROOT/build/difftest
mkdir -p "$OUT"
NAME=$(basename "$SCRIPT" .keys)

# unpacked original
[ -f "$ROOT/build/FALLOUT_UNPACKED.EXE" ] ||
    $PY "$ROOT/tools/unlzexe.py" "$ROOT/FALLOUT.EXE" "$ROOT/build/FALLOUT_UNPACKED.EXE" >/dev/null

# native build of the reconstructed sources
CFLAGS="-O1 -fsigned-char -w -I$ROOT/tools/emu/include"
# _BSS globals packed in their original order (see mkbss.py)
$PY "$ROOT/tools/emu/mkbss.py" "$ROOT/src/fallout.c" "$OUT"
cc $CFLAGS -I"$ROOT/src" -Dmain=game_main -c "$OUT/fallout_bss.c" -o "$OUT/fallout.o"
cc $CFLAGS -c "$ROOT/src/gfx.c" -o "$OUT/gfx.o"
cc $CFLAGS -c "$ROOT/tools/emu/harness.c" -o "$OUT/harness.o"
cc "$OUT/fallout.o" "$OUT/gfx.o" "$OUT/harness.o" -lz -o "$OUT/native_harness"

$PY "$ROOT/tools/emu/run_original.py" "$ROOT/build/FALLOUT_UNPACKED.EXE" \
    "$SCRIPT" "$OUT/$NAME.orig.trace" --max-ticks "$MAX"
SEED=$(sed -n 's/^srand //p' "$OUT/$NAME.orig.trace" | head -1)
"$OUT/native_harness" "$SCRIPT" "$OUT/$NAME.native.trace" "$SEED" "$MAX"

if cmp -s "$OUT/$NAME.orig.trace" "$OUT/$NAME.native.trace"; then
    echo "MATCH: $NAME ($(grep -c '^[0-9]' "$OUT/$NAME.orig.trace") ticks compared)"
else
    echo "DIFFER: $NAME -- first differences:"
    diff "$OUT/$NAME.orig.trace" "$OUT/$NAME.native.trace" | head -20
    exit 1
fi

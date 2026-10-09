#!/bin/sh
# Line coverage of the reconstructed sources over a set of key scripts
# (run tools/emu/difftest.sh on the same scripts to verify them).
# usage: tools/emu/coverage.sh SCRIPT...
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$ROOT/build/coverage
PY=${PYTHON:-python3}
mkdir -p "$OUT"; rm -f "$OUT"/*.profraw
CFLAGS="-O0 -fsigned-char -w -I$ROOT/tools/emu/include -fprofile-instr-generate -fcoverage-mapping -DCOVERAGE"
$PY "$ROOT/tools/emu/mkport.py" "$ROOT/src/fallout.c" "$OUT"
cc $CFLAGS -I"$ROOT/src" -Dmain=game_main -c "$OUT/fallout_port.c" -o "$OUT/fallout.o"
cc $CFLAGS -c "$ROOT/src/gfx.c" -o "$OUT/gfx.o"
cc $CFLAGS -c "$ROOT/tools/emu/harness.c" -o "$OUT/harness.o"
cc -fprofile-instr-generate "$OUT"/fallout.o "$OUT"/gfx.o "$OUT"/harness.o -lz -o "$OUT/cov_harness"
n=0
for s in "$@"; do
    n=$((n+1))
    seed=$(sed -n 's/^srand //p' "$ROOT/build/difftest/$(basename "$s" .keys).orig.trace" 2>/dev/null | head -1)
    div=$(sed -n 's/^# TICK_DIV=//p' "$s")
    TICK_DIV=${div:-1} LLVM_PROFILE_FILE="$OUT/run$n.profraw" "$OUT/cov_harness" "$s" /dev/null "${seed:-30976}" ${MAX_TICKS:-400000} >/dev/null
done
xcrun llvm-profdata merge -o "$OUT/all.profdata" "$OUT"/*.profraw
xcrun llvm-cov report "$OUT/cov_harness" -instr-profile="$OUT/all.profdata" \
    "$OUT/fallout_port.c" "$ROOT/src/gfx.c"
xcrun llvm-cov show "$OUT/cov_harness" -instr-profile="$OUT/all.profdata" \
    "$OUT/fallout_port.c" "$ROOT/src/gfx.c" > "$OUT/coverage.txt"

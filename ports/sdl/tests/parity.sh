#!/bin/sh
# Replay every tests/*.keys script through the port (fallout --harness) and
# check its trace against the ORIGINAL game's trace:
#   - by SHA-256, against tests/golden.txt (no emulator needed), and
#   - byte for byte, when build/difftest/NAME.orig.trace exists (written by
#     tools/emu/difftest.sh), showing the first differences.
#
# usage: ports/sdl/tests/parity.sh FALLOUT_BIN [SCRIPT.keys ...]
# Script headers are honoured like tools/emu/run_all.sh:
#   # TICK_DIV=n      # ENV=NAME=value (paths relative to the repository root)
abspath() { case $1 in /*) echo "$1" ;; *) echo "$PWD/$1" ;; esac; }
BIN=$(abspath "$1")
shift
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
GOLDEN=$ROOT/ports/sdl/tests/golden.txt
OUT=$(abspath "${PARITY_OUT:-$ROOT/build/parity}")
mkdir -p "$OUT"
[ $# -gt 0 ] || set -- "$ROOT"/tests/*.keys

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

fail=0
for s in "$@"; do
    s=$(abspath "$s")
    name=$(basename "$s" .keys)
    entry=$(grep "^$name " "$GOLDEN")
    if [ -z "$entry" ]; then
        echo "SKIP:   $name (not in golden.txt)"
        continue
    fi
    seed=$(echo "$entry" | cut -d' ' -f2)
    max=$(echo "$entry" | cut -d' ' -f3)
    want=$(echo "$entry" | cut -d' ' -f4)
    div=$(sed -n 's/^# TICK_DIV=//p' "$s")
    extra=$(sed -n 's/^# ENV=//p' "$s")
    trace=$OUT/$name.trace
    (cd "$ROOT" && env TICK_DIV="${div:-1}" $extra "$BIN" --harness "$s" "$trace" "$seed" "$max") >/dev/null
    if [ "$(sha256 "$trace")" = "$want" ]; then
        echo "MATCH:  $name ($(grep -c '^[0-9]' "$trace") ticks compared)"
    else
        fail=1
        echo "DIFFER: $name"
        orig=$ROOT/build/difftest/$name.orig.trace
        [ -f "$orig" ] && diff "$orig" "$trace" | head -20
    fi
done
exit $fail

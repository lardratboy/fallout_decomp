#!/bin/sh
# Run the differential test on every tests/*.keys script.
# Header lines a script may contain:
#   # TICK_DIV=n      clock rate (get_ticks() calls per tick)
#   # ENV=NAME=value  extra environment (e.g. NO_VGA=1, ALLOC_FAIL=1)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
fail=0
for s in "$ROOT"/tests/*.keys; do
    div=$(sed -n 's/^# TICK_DIV=//p' "$s")
    extra=$(sed -n 's/^# ENV=//p' "$s")
    if ! env TICK_DIV=${div:-1} $extra "$ROOT/tools/emu/difftest.sh" "$s" "${MAX_TICKS:-40000}" \
            > "$ROOT/build/last.log" 2>&1; then
        fail=1
    fi
    grep -E '^(MATCH|DIFFER|<|>)' "$ROOT/build/last.log"
done
exit $fail

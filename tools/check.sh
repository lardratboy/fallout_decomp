#!/bin/sh
# Type-check the reconstructed DOS sources with the host compiler, using the
# Turbo C header shims in tools/shim.  This does not produce a DOS program.
cd "$(dirname "$0")/../src" || exit 1
for f in fallout.c gfx.c; do
    cc -fsyntax-only -std=c89 -pedantic -Wall -Wextra -Wno-unused-parameter \
       -Wno-unused-but-set-variable -Wno-unused-variable -Wno-long-long \
       -Wno-overlength-strings -Wno-char-subscripts -Wno-pointer-to-int-cast -Wno-void-pointer-to-int-cast \
       -I ../tools/shim "$f" || exit 1
done
echo "syntax check OK"

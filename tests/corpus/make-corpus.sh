#!/usr/bin/env bash
# Generates the test corpus under tests/corpus/data (not versioned: regenerable).
# Large files are created with truncate + block writes, so they do not use RAM.
set -euo pipefail

cd "$(dirname "$0")"
out=data
rm -rf "$out"
mkdir -p "$out/text" "$out/binary" "$out/empty" "$out/utf8"

# Text: repetitive and non-repetitive, so methods treat them differently.
{
    for i in $(seq 1 500); do
        echo "line $i: FreeArc-ex compatibility test for the FreeArc 0.67.1 .arc format"
    done
} >"$out/text/repetitive.txt"

head -c 131072 /dev/urandom >"$out/binary/random.bin"
printf 'ABCD' >"$out/binary/four.bin"
: >"$out/empty/zero.dat"
: >"$out/empty/.hidden"

# UTF-8 names and special characters.
printf 'accents: compression, cancellation, child\n' >"$out/utf8/canción-y-compresión.txt"
printf 'spaces and symbols\n' >"$out/utf8/name with spaces & symbols.txt"

# A sparse file above 4 GiB (does not consume real disk until written).
truncate -s 4400M "$out/binary/large-4g.bin" 2>/dev/null || true

echo "corpus generated in tests/corpus/$out"
find "$out" -type f -printf '  %10s  %p\n' | sort -k2

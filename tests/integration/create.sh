#!/usr/bin/env bash
# Creates an archive with arc-ex, then verifies it with arc-ex itself: `t`
# (per-file CRCs), `lb` (bare names) and `x` (extracted tree compared).
# Usage: create.sh <arc-ex> <method>
set -euo pipefail

arc_ex=$1
method=$2

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-create.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

mkdir -p src/sub src/empty
printf 'FreeArc-ex creation test\nsecond line\n' >src/a.txt
seq 1 3000 >src/b.txt
printf 'ABCDABCDABCD' >src/sub/c.bin
: >src/zero.dat
touch -d '2020-01-02 03:04:05' src/a.txt src/b.txt src/sub/c.bin src/zero.dat

"$arc_ex" a out.arc src -m"$method" >/dev/null
"$arc_ex" t out.arc >/dev/null

# Bare listing has to contain every entry, directories included.
"$arc_ex" lb out.arc | tr -d '\r' >lb.txt
for entry in src src/a.txt src/b.txt src/sub src/sub/c.bin src/empty src/zero.dat; do
    grep -qx "$entry" lb.txt || { echo "FAIL: '$entry' missing from lb"; exit 1; }
done

mkdir out
"$arc_ex" x out.arc -dpout >/dev/null
diff -r src out/src >/dev/null || { echo "FAIL: extracted tree differs"; exit 1; }

echo "create OK: -m$method"

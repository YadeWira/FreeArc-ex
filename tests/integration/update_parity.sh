#!/usr/bin/env bash
# Differential update: after modifying, adding and deleting files, the block
# structure must match what Arc.exe produces (same sizes and file counts; the
# compressed sizes may differ by a few bytes).
# Usage: update_parity.sh <arc-ex> <Arc.exe> <wine>
set -euo pipefail

arc_ex=$(realpath "$1")
oracle=$2
wine=$3
export WINEDEBUG=-all

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-uparity.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
mkdir src
for i in 1 2 3 4 5 6; do seq 1 $((i * 400)) >"src/f$i.txt"; done
cp -r src base

"$arc_ex" a ours.arc src -mlzma -s2f >/dev/null
"$wine" "$oracle" a ref.arc src -mlzma -s2f >/dev/null 2>&1

# Change one file, add one, delete one. The mtimes are set explicitly: FreeArc
# compares modification times with coarse granularity, so a same-second rewrite
# can slip through (we also compare sizes, which is safer; that difference is
# documented in COMPAT.md).
rm -rf src && cp -r base src
seq 100 600 >src/f3.txt
touch -d '2030-01-01' src/f3.txt
printf 'nuevo\n' >src/new.txt
touch -d '2030-01-01' src/new.txt
rm -f src/f5.txt

"$arc_ex" u ours.arc src -mlzma -s2f >/dev/null
"$wine" "$oracle" u ref.arc src -mlzma -s2f >/dev/null 2>&1

structure() {
    "$arc_ex" lt "$1" | grep '^  pos=' | sed 's/.*orig=\([0-9]*\).*files=\([0-9]*\).*/\1\/\2/'
}
ours=$(structure ours.arc)
refs=$(structure ref.arc)
if [ "$ours" != "$refs" ]; then
    echo "FAIL: update structure differs from the oracle"
    echo "ours: $ours"
    echo "ref:  $refs"
    exit 1
fi

"$arc_ex" t ours.arc >/dev/null
echo "update parity OK"

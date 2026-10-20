#!/usr/bin/env bash
# Recovery record (-rr): the archive carries the parity blocks, our own 'r'
# repairs a damaged sector, and (when the oracle is available) arc.exe agrees
# both on the record and on the repair.
# Usage: recovery.sh <arc-ex> [<Arc.exe> <wine>]
set -euo pipefail

arc_ex=$(realpath "$1")
oracle=${2:-}
wine=${3:-}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-rr.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
seq 1 20000 >a.txt

"$arc_ex" a ours.arc a.txt -m0 -rr4% >/dev/null
# Two recovery blocks must be listed by the technical listing.
recovery_blocks=$("$arc_ex" lt ours.arc | grep -c 'type=5')
if [ "$recovery_blocks" -ne 2 ]; then
    echo "FAIL: expected 2 recovery blocks, got $recovery_blocks"
    exit 1
fi
"$arc_ex" t ours.arc >/dev/null

corrupt() { # file offset
    python3 - "$1" "$2" <<'PY'
import sys
path, start = sys.argv[1], int(sys.argv[2])
data = bytearray(open(path, 'rb').read())
for i in range(start, start + 100):
    data[i] ^= 0xFF
open(path, 'wb').write(data)
PY
}

# Our own repair: corrupt a sector, repair, and the result must verify and be
# byte-identical to the original.
cp ours.arc mine.arc
corrupt mine.arc 5000
"$arc_ex" r mine.arc repaired.arc >/dev/null
"$arc_ex" t repaired.arc >/dev/null
cmp -s ours.arc repaired.arc || { echo "FAIL: repaired archive differs from the original"; exit 1; }

if [ -n "$oracle" ] && [ -n "$wine" ]; then
    export WINEDEBUG=-all
    "$wine" "$oracle" lt ours.arc 2>&1 | tr -d '\r' | grep -q "recovery sectors" || {
        echo "FAIL: Arc.exe does not see the recovery record"; exit 1; }

    cp ours.arc broken.arc
    corrupt broken.arc 5000
    "$wine" "$oracle" r broken.arc >/dev/null 2>&1
    test -f fixed.broken.arc || { echo "FAIL: no repaired archive"; exit 1; }
    "$wine" "$oracle" t fixed.broken.arc 2>&1 | tr -d '\r' | grep -q "All OK" || {
        echo "FAIL: repaired archive still fails"; exit 1; }
fi

echo "recovery OK"

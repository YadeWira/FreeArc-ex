#!/usr/bin/env bash
# Block reuse on update: with -s2f only the block holding the changed file is
# recompressed; the others are copied verbatim (same compressed size).
# Usage: reuse.sh <arc-ex>
set -euo pipefail

arc_ex=$(realpath "$1")

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-reuse.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
mkdir src
for i in 1 2 3 4 5 6; do seq 1 $((i * 500)) >"src/f$i.txt"; done

"$arc_ex" a a.arc src -mlzma -s2f >/dev/null
before=$("$arc_ex" lt a.arc | grep '^  pos=' | sed 's/.*comp=\([0-9]*\).*/\1/')

seq 100 700 >src/f3.txt
"$arc_ex" u a.arc src -mlzma -s2f >/dev/null
after=$("$arc_ex" lt a.arc | grep '^  pos=' | sed 's/.*comp=\([0-9]*\).*/\1/')

if [ "$(echo "$before" | wc -l)" != "$(echo "$after" | wc -l)" ]; then
    echo "FAIL: block count changed"
    exit 1
fi

# The changed block is recompressed (one size disappears, another appears); every
# other block must keep its exact compressed size. Compare as multisets because
# reused blocks are emitted in archive order, not in their old position.
diff_lines=$(comm -3 <(echo "$before" | sort) <(echo "$after" | sort) | wc -l)
if [ "$diff_lines" -ne 2 ]; then
    echo "FAIL: expected exactly one recompressed block, got $diff_lines differing sizes"
    echo "before: $(echo "$before" | tr '\n' ' ')"
    echo "after:  $(echo "$after" | tr '\n' ' ')"
    exit 1
fi

"$arc_ex" t a.arc >/dev/null
echo "reuse OK"

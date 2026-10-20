#!/usr/bin/env bash
# Differential test of the writer: arc-ex creates an archive and the original
# Arc.exe has to test and extract it, with an identical tree.
# Usage: oracle.sh <arc-ex> <Arc.exe> <wine> <method>
set -euo pipefail

arc_ex=$1
oracle=$2
wine=$3
method=$4

export WINEDEBUG=-all
tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-oracle-write.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

mkdir -p src/sub src/empty
printf 'FreeArc-ex writer vs oracle\n' >src/a.txt
seq 1 2000 >src/b.txt
printf 'ABCDABCD' >src/sub/c.bin
: >src/zero.dat

"$arc_ex" a out.arc src -m"$method" >/dev/null

test_out=$("$wine" "$oracle" t out.arc 2>&1 | tr -d '\r')
if ! printf '%s\n' "$test_out" | grep -q "All OK"; then
    echo "FAIL: Arc.exe did not report All OK for -m$method"
    printf '%s\n' "$test_out" | tail -5
    exit 1
fi

mkdir extracted
(cd extracted && "$wine" "$oracle" x ../out.arc >/dev/null 2>&1)
diff -r src extracted/src >/dev/null || {
    echo "FAIL: Arc.exe extracted a different tree for -m$method"
    exit 1
}

echo "oracle OK: Arc.exe reads and extracts what arc-ex wrote with -m$method"

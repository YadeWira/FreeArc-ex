#!/usr/bin/env bash
# Checks that the compatibility oracle (Arc.exe 0.67.1 under wine) responds and
# that its a/l/t/x cycle produces the expected data. This is M0's requirement.
set -euo pipefail

oracle=${FA_ARC_ORACLE:-/mnt/IA_LAB/agentes/arc/bin/Arc.exe}
unarc=${FA_UNARC:-/mnt/IA_LAB/agentes/arc/bin/unarc.i386}
if [ ! -f "$oracle" ]; then
    echo "ERROR: oracle not found at '$oracle'"
    echo "       point to another copy with FA_ARC_ORACLE=/path/to/Arc.exe"
    exit 1
fi

expected_sha=1b23732e158540eb159563b557ab1d396b12cd56178ea9387e696d054d3e2172
actual_sha=$(sha256sum "$oracle" | cut -d' ' -f1)
if [ "$actual_sha" != "$expected_sha" ]; then
    echo "WARNING: the oracle binary is not the one recorded in docs/COMPAT.md"
    echo "         expected $expected_sha"
    echo "         got      $actual_sha"
fi

export WINEDEBUG=-all
work=$(mktemp -d "${TMPDIR:-/tmp}/fa-oracle.XXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"

version=$(wine "$oracle" 2>&1 | head -1 | tr -d '\r' || true)
echo "oracle: $version"

mkdir -p in/sub
printf 'FreeArc-ex: oracle smoke test\n' >in/a.txt
head -c 65536 /dev/urandom >in/sub/b.bin
: >in/empty.dat

wine "$oracle" a -m1 out.arc in >arc-add.log 2>&1
wine "$oracle" t out.arc >arc-test.log 2>&1 || { echo "ERROR: oracle 't' failed"; cat arc-test.log; exit 1; }
wine "$oracle" x out.arc -dp"outdir" >arc-extract.log 2>&1 || { echo "ERROR: oracle 'x' failed"; cat arc-extract.log; exit 1; }

for f in a.txt sub/b.bin empty.dat; do
    if ! cmp -s "in/$f" "outdir/in/$f"; then
        echo "ERROR: extracted '$f' does not match"
        exit 1
    fi
done

# Extraction oracle: unarc (the 0.67 unpacker) has to accept the same archive.
unarc_out=$(wine "$unarc" t out.arc 2>&1 | tr -d '\r' | tail -1 || true)
if [ "$unarc_out" != "All OK" ]; then
    echo "ERROR: unarc did not report All OK (got: $unarc_out)"
    exit 1
fi

echo "oracle OK: a/t/x with -m1, three files verified; unarc test OK"

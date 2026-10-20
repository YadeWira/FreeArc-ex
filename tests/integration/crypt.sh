#!/usr/bin/env bash
# Data encryption (-p): round trip, wrong password rejected, and (when the
# oracle is available) interoperability in both directions.
# Usage: crypt.sh <arc-ex> [<Arc.exe> <wine>]
set -euo pipefail

arc_ex=$(realpath "$1")
oracle=${2:-}
wine=${3:-}
pass=clave123

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-crypt.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
mkdir src
printf 'top secret\n' >src/a.txt
seq 1 300 >src/b.txt

"$arc_ex" a ours.arc src -mlzma -p"$pass" >/dev/null
"$arc_ex" t ours.arc -p"$pass" >/dev/null

mkdir out
"$arc_ex" x ours.arc -dpout -p"$pass" >/dev/null
diff -r src out/src >/dev/null

if "$arc_ex" t ours.arc -pwrong >/dev/null 2>&1; then
    echo "FAIL: wrong password accepted"
    exit 1
fi

if [ -n "$oracle" ] && [ -n "$wine" ]; then
    export WINEDEBUG=-all
    # The oracle must be able to read what we wrote...
    "$wine" "$oracle" t ours.arc -p"$pass" 2>&1 | tr -d '\r' | grep -q "All OK" || {
        echo "FAIL: Arc.exe cannot decrypt our archive"; exit 1; }
    # ...and we must read what the oracle wrote.
    rm -f ref.arc
    "$wine" "$oracle" a ref.arc src -mlzma -p"$pass" >/dev/null 2>&1
    "$arc_ex" t ref.arc -p"$pass" >/dev/null
    rm -rf out2 && mkdir out2
    "$arc_ex" x ref.arc -dpout2 -p"$pass" >/dev/null
    diff -r src out2/src >/dev/null
fi

echo "crypt OK"

#!/usr/bin/env bash
# Creating with a method the engine cannot handle has to fail with a clear
# message, never crash or write a broken archive.
# Usage: expect_unsupported.sh <arc-ex> <method>
set -euo pipefail

arc_ex=$1
method=$2

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-unsupported.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
printf 'data\n' >file.txt

if "$arc_ex" a out.arc file.txt -m"$method" >log 2>&1; then
    echo "FAIL: '$method' was accepted on this architecture"
    exit 1
fi
if [ -f out.arc ]; then
    echo "FAIL: a partial archive was left behind"
    exit 1
fi
grep -q "not supported" log || { echo "FAIL: message is not clear"; cat log; exit 1; }

echo "ok: '$method' rejected with a clear message"

#!/usr/bin/env bash
# Update operations: u (add/replace), f (replace only), d (delete).
# Usage: update.sh <arc-ex> <method>
set -euo pipefail

arc_ex=$1
method=$2

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-update.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

mkdir src
printf 'version 1\n' >src/a.txt
seq 1 500 >src/b.txt
"$arc_ex" a test.arc src -m"$method" >/dev/null

# u: changed + new file
printf 'version 2 changed\n' >src/a.txt
printf 'new\n' >src/c.txt
"$arc_ex" u test.arc src -m"$method" >/dev/null
mkdir out
"$arc_ex" x test.arc -dpout >/dev/null
grep -q "version 2 changed" out/src/a.txt
test -f out/src/c.txt

# f: a new file on disk must NOT be added
printf 'ignored\n' >src/d.txt
"$arc_ex" f test.arc src -m"$method" >/dev/null
if "$arc_ex" lb test.arc | tr -d '\r' | grep -qx 'src/d.txt'; then
    echo "FAIL: f added a new entry"
    exit 1
fi

# d: delete by name
"$arc_ex" d test.arc src/c.txt -m"$method" >/dev/null
if "$arc_ex" lb test.arc | tr -d '\r' | grep -qx 'src/c.txt'; then
    echo "FAIL: d did not delete the entry"
    exit 1
fi

"$arc_ex" t test.arc >/dev/null
echo "update OK: -m$method"

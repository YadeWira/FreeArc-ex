#!/usr/bin/env bash
# CLI behaviour: masks, -x exclusions, overwrite policy, -dm and exit codes.
# Usage: cli.sh <arc-ex>
set -euo pipefail

arc_ex=$(realpath "$1")

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-cli.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

mkdir -p src/sub
printf 'a\n' >src/a.txt
printf 'b\n' >src/b.bin
printf 'c\n' >src/sub/c.txt

# -dm: the directory block is compressed, and the archive still verifies.
"$arc_ex" a d.arc src -mlzma -dmlzma >/dev/null
"$arc_ex" lt d.arc | grep 'type=3' | grep -q 'lzma' || { echo "FAIL: -dm ignored"; exit 1; }
"$arc_ex" t d.arc >/dev/null

# Include masks select by name (full path or basename).
mkdir out_mask
"$arc_ex" x d.arc -dpout_mask '*.txt' >/dev/null
test -f out_mask/src/a.txt && test -f out_mask/src/sub/c.txt
test ! -f out_mask/src/b.bin || { echo "FAIL: mask extracted a non-matching file"; exit 1; }

# -x excludes.
mkdir out_ex
"$arc_ex" x d.arc -dpout_ex -x'*.txt' >/dev/null
test -f out_ex/src/b.bin
test ! -f out_ex/src/a.txt || { echo "FAIL: -x did not exclude"; exit 1; }

# Overwrite policy.
mkdir out_ow
"$arc_ex" x d.arc -dpout_ow >/dev/null
printf 'keep me\n' >out_ow/src/a.txt
"$arc_ex" x d.arc -dpout_ow -o- >/dev/null
grep -q 'keep me' out_ow/src/a.txt || { echo "FAIL: -o- overwrote"; exit 1; }
"$arc_ex" x d.arc -dpout_ow -o+ >/dev/null
grep -q 'keep me' out_ow/src/a.txt && { echo "FAIL: -o+ did not overwrite"; exit 1; }

# Exit code for a wrong password (21, as the original).
"$arc_ex" a enc.arc src -mlzma -pclave >/dev/null
set +e
"$arc_ex" t enc.arc -pmala >/dev/null 2>&1
code=$?
set -e
if [ "$code" -ne 21 ]; then
    echo "FAIL: wrong password exit code $code, expected 21"
    exit 1
fi

# -ap (archive prefix) and -ad (extract under a directory named after the archive).
"$arc_ex" a prefixed.arc src -mlzma -apbase >/dev/null
"$arc_ex" lb prefixed.arc | tr -d '\r' | grep -qx 'base/src/a.txt' || {
    echo "FAIL: -ap did not prefix the names"; exit 1; }
mkdir out_ad
"$arc_ex" x prefixed.arc -dpout_ad -ad >/dev/null
test -f out_ad/prefixed/base/src/a.txt || { echo "FAIL: -ad extraction path"; exit 1; }

# -ae: another cipher.
"$arc_ex" a bf.arc src -mlzma -pclave -aeblowfish >/dev/null
"$arc_ex" lt bf.arc | grep -q 'blowfish-448' || { echo "FAIL: -ae ignored"; exit 1; }
"$arc_ex" t bf.arc -pclave >/dev/null

# -ep9: store only the base names.
mkdir -p src/sub
printf 'hola\n' > src/sub/notas.dat
"$arc_ex" a e9.arc src -mlzma -ep9 >/dev/null
"$arc_ex" lb e9.arc | tr -d '\r' | grep -qx 'notas.dat' || { echo "FAIL: -ep9"; exit 1; }

# -ma: without a name match, a text file is classified as text; -ma- disables it.
"$arc_ex" a ma.arc src -m4 >/dev/null
"$arc_ex" lt ma.arc | grep '^  pos=' | grep -q 'ppmd' || {
    echo "FAIL: -ma did not detect the text file"; exit 1; }
"$arc_ex" a ma0.arc src -m4 -ma- >/dev/null
if "$arc_ex" lt ma0.arc | grep '^  pos=' | grep -q 'ppmd'; then
    echo "FAIL: -ma- still applied content detection"
    exit 1
fi

echo "cli OK"

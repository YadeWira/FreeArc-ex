#!/usr/bin/env bash
# Solid block grouping: -s (all), -s- (per file), -sNf (by count), -sSIZE.
# Usage: solid.sh <arc-ex> [<Arc.exe> <wine>]
set -euo pipefail

arc_ex=$1
oracle=${2:-}
wine=${3:-}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-solid.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
mkdir src
for i in 1 2 3 4 5 6; do seq 1 $((i * 200)) >"src/f$i.txt"; done

# Number of data blocks (lines with "pos=" in the technical listing).
blocks() { "$arc_ex" lt "$1" | grep -c '^  pos='; }

check() { # option expected
    local opt=$1 expected=$2
    "$arc_ex" a "ours.arc" src -mlzma -s"$opt" >/dev/null
    local got
    got=$(blocks ours.arc)
    if [ "$got" != "$expected" ]; then
        echo "FAIL: -s$opt produced $got blocks, expected $expected"
        exit 1
    fi
    "$arc_ex" t ours.arc >/dev/null
}

check "" 2
check "-" 7
check "2f" 4
check "1m" 2

# Differential check when the oracle is available: same block structure.
if [ -n "$oracle" ] && [ -n "$wine" ]; then
    export WINEDEBUG=-all
    for opt in "" "-" "2f" "4k" "10k" "1m"; do
        "$arc_ex" a "ours.arc" src -mlzma -s"$opt" >/dev/null
        rm -f "ref.arc"
        "$wine" "$oracle" a ref.arc src -mlzma -s"$opt" >/dev/null 2>&1
        ours=$("$arc_ex" lt ours.arc | grep '^  pos=' | sed 's/.*orig=\([0-9]*\).*files=\([0-9]*\).*/\1\/\2/')
        refs=$("$arc_ex" lt ref.arc | grep '^  pos=' | sed 's/.*orig=\([0-9]*\).*files=\([0-9]*\).*/\1\/\2/')
        if [ "$ours" != "$refs" ]; then
            echo "FAIL: -s$opt structure differs from the oracle"
            echo "ours: $ours"
            echo "ref:  $refs"
            exit 1
        fi
    done
fi

echo "solid OK"

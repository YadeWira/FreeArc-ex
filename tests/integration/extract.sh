#!/usr/bin/env bash
# Extracts an archive and verifies every file against a sha256 list.
# Usage: extract.sh <arc-ex> <archive> <expected.sha256>
set -euo pipefail

arc_ex=$1
archive=$2
expected=$3

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-extract.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

"$arc_ex" x "$archive" -dp"$tmp" >/dev/null
(cd "$tmp" && sha256sum -c --quiet "$expected")

# Directory entries must exist too (the fixture archives one empty directory).
if [ ! -d "$tmp/src/empty" ]; then
    echo "FAIL: directory entry src/empty was not extracted"
    exit 1
fi

echo "extract OK: $(basename "$archive")"

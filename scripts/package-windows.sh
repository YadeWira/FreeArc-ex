#!/usr/bin/env bash
# Stages the Windows files and builds the installer with Inno Setup.
#
# The installer is produced with Inno Setup 7.1.0 (ISCC.exe). On Linux that means
# running ISCC under wine, or running this script inside a Windows machine. The
# staged files come from the mingw cross build.
#
# Usage: scripts/package-windows.sh [arch]     (arch: x64 (default) or x86)
#   ISCC=/path/to/ISCC.exe  to point at the compiler (default: iscc, iscc.exe)
set -euo pipefail

cd "$(dirname "$0")/.."
arch=${1:-x64}
target="mingw-${arch/x64/x64}"
case "$arch" in
    x64) target=mingw-x64 ;;
    x86) target=mingw-x86 ;;
    *) echo "usage: $0 [x64|x86]"; exit 2 ;;
esac

version=0.1.0-pre
stage=packaging/windows/stage
rm -rf "$stage"
mkdir -p "$stage" packaging/windows/output

echo "== building the CLI for $target =="
scripts/build-matrix.sh "$target" >/dev/null

exe="build/$target/src/cli/arc-ex.exe"
[ -f "$exe" ] || { echo "ERROR: $exe not found"; exit 1; }

cp "$exe" "$stage/arc-ex.exe"
cp LICENSE README.md data/arc.groups "$stage/"
# The GUI is packaged only if a Windows build exists (see docs/INSTALLER.md).
if [ -f "build/$target/src/gui/freearc-ex.exe" ]; then
    cp "build/$target/src/gui/freearc-ex.exe" "$stage/freearc-ex.exe"
    echo "GUI included"
fi

cat >"$stage/PRERELEASE.txt" <<'TXT'
FreeArc-ex 0.1.0-pre

This is a PRE-RELEASE build for testing. It is not feature complete and the
archive format is the FreeArc 0.67.1 one, so archives are compatible in both
directions, but treat it as beta software.

What is included in this build:
  - the console archiver arc-ex.exe (a, u, f, d, l, lb, t, x, r)

Not included yet: the GTK3 GUI (needs a Windows build with MSYS2), SFX modules,
the Explorer extension and multi-volume archives.

Report problems at https://github.com/YadeWira/FreeArc-ex/issues
TXT

echo "== running Inno Setup =="
iscc=${ISCC:-}
if [ -z "$iscc" ]; then
    for candidate in iscc iscc.exe "wine iscc.exe"; do
        if command -v "${candidate%% *}" >/dev/null 2>&1; then iscc=$candidate; break; fi
    done
fi
if [ -z "$iscc" ]; then
    echo "ISCC not found. Install Inno Setup 7.1.0 and set ISCC=/path/to/ISCC.exe,"
    echo "or run packaging/windows/freearc-ex.iss from the Inno Setup IDE."
    echo "The staged files are ready in $stage"
    exit 3
fi

(cd packaging/windows && $iscc freearc-ex.iss)
ls -l packaging/windows/output/*.exe
echo "installer built: packaging/windows/output/FreeArc-ex-${version}-setup.exe"

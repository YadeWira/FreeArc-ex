#!/usr/bin/env bash
# Build FreeArc-ex for all five targets and run the tests where they can run.
# Usage: scripts/build-matrix.sh [target...]   (no arguments: all five)
set -euo pipefail

cd "$(dirname "$0")/.."

targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then
    targets=(linux-x64 linux-i686 linux-aarch64 mingw-x86 mingw-x64)
fi

jobs=$(nproc 2>/dev/null || echo 4)
failed=()
mkdir -p build

for target in "${targets[@]}"; do
    toolchain="cmake/toolchains/${target}.cmake"
    if [ ! -f "$toolchain" ]; then
        echo "ERROR: missing $toolchain"
        failed+=("$target")
        continue
    fi

    echo "=== ${target} ==="
    builddir="build/${target}"

    if ! cmake -S . -B "$builddir" \
            -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
            -DCMAKE_BUILD_TYPE=Release \
            -DFA_WERROR=ON >"${builddir}.configure.log" 2>&1; then
        echo "  configure failed; see ${builddir}.configure.log"
        failed+=("$target")
        continue
    fi

    if ! cmake --build "$builddir" -j "$jobs"; then
        echo "  build failed"
        failed+=("$target")
        continue
    fi

    if (cd "$builddir" && ctest --output-on-failure >ctest.log 2>&1); then
        echo "  tests OK"
    else
        echo "  tests failed; see ${builddir}/ctest.log"
        failed+=("$target")
    fi
done

echo
if [ ${#failed[@]} -ne 0 ]; then
    echo "FAILED: ${failed[*]}"
    exit 1
fi
echo "Matrix complete: ${targets[*]}"

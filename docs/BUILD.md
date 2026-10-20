# Build

## Dependencies

Common:

- CMake >= 3.25, GNU Make or Ninja
- A C++20 compiler (GCC 13+ or Clang 17+)
- Python 3 (only for the file-comparison scripts)

Per target (Debian 13):

```sh
# Linux x64 (native)
sudo apt install build-essential cmake

# Linux i686 (multilib)
sudo apt install g++-multilib

# Linux aarch64 (cross)
sudo apt install g++-aarch64-linux-gnu qemu-user

# Windows x86 and x64 (cross; also used to run the tests with wine)
sudo apt install g++-mingw-w64-i686 g++-mingw-w64-x86-64 wine
```

The GUI (milestone M5) additionally needs `libgtkmm-3.0-dev`. On Windows, GTK3 is
built with MSYS2 mingw-w64 or shipped as DLLs next to the executable.

## Building one target

```sh
cmake -S . -B build/linux-x64 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-x64.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-x64 -j"$(nproc)"
(cd build/linux-x64 && ctest --output-on-failure)
```

Available targets: `linux-x64`, `linux-i686`, `linux-aarch64`, `mingw-x86`,
`mingw-x64`.

## Full matrix

```sh
scripts/build-matrix.sh              # all five
scripts/build-matrix.sh linux-x64    # just one
```

Cross-compiled tests run under `qemu-aarch64` (aarch64) and `wine` (Windows), as
configured in the toolchains. `qemu-user` **does not measure speed**: it only
confirms the binary works. Speed measurements belong to the `arm64` VM.

## Compatibility oracle

The oracle is `Arc.exe` 0.67.1 under wine:

```
/mnt/IA_LAB/agentes/PArc/ref/Arc.exe
sha256 1b23732e158540eb159563b557ab1d396b12cd56178ea9387e696d054d3e2172
FreeArc 0.67.1 (March 07 2021, krinkels.org build)
```

Quick check that the oracle responds:

```sh
scripts/oracle-smoke.sh
```

Another binary can be used with `FA_ARC_ORACLE=/path/to/Arc.exe`.

## CMake options

| option | default | effect |
|---|---|---|
| `FA_BUILD_CLI` | ON | builds `arc-ex` |
| `FA_BUILD_GUI` | OFF | builds the GTK3 GUI (M5) |
| `FA_BUILD_TESTS` | ON | builds and registers the tests |
| `FA_WERROR` | OFF | treat warnings as errors (the matrix enables it) |

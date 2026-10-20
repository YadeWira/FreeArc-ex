# FreeArc-ex

Extended support for [FreeArc 0.67](https://sourceforge.net/projects/freearc/):
a maintained version of the archiver with new platforms, updated libraries,
bug fixes and a GTK3 GUI.

**Status: M1 in progress (read side).** `arc-ex l` lists real archives created by
`arc.exe` when the directory is stored uncompressed (`-dm0`); compressed control
blocks, creation and extraction are the next steps. The milestone plan is in
[`docs/PLAN.md`](docs/PLAN.md).

## Targets

| platform | architectures | minimum |
|---|---|---|
| Linux | x86_64, i686, aarch64 | Debian 12/13 glibc |
| Windows | x86_64, i686 | Windows 7 SP1 |

- Read and write compatibility with FreeArc 0.67.1 `.arc` archives.
- CLI with option and exit-code parity with `arc.exe` 0.67.1.
- `arc.ini` and Lua 5.1 scripts, SFX/`unarc.dll`, Explorer extension, and
  opening `.7z`/`.zip`/`.rar`.
- GTK3 GUI (gtkmm 3.24) with feature parity with the original GUI.
- Updated codecs: modern LZMA SDK, LZ4 1.10, LibTomCrypt 1.18.2, and no
  x86-specific assembly.

## Building

```sh
cmake -S . -B build/linux-x64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-x64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-x64 -j
(cd build/linux-x64 && ctest --output-on-failure)
```

Full matrix (all five targets):

```sh
scripts/build-matrix.sh
```

Dependencies and cross-verification details are in [`docs/BUILD.md`](docs/BUILD.md).

## Layout

```
src/core/      .arc format engine (C++20)
src/codecs/    codecs inherited from FreeArc 0.67, being modernized
src/cli/       arc-ex
src/gui/       freearc-ex (gtkmm 3.24, milestone M5)
src/lua/       embedded Lua 5.1 (script compatibility)
src/unarc/     SFX, unarc.dll and 7z-unarc (Windows)
src/shellext/  Explorer COM extension (Windows)
legacy/        original FreeArc 0.67 sources: Haskell (reference) and HsLua
docs/          plan, decisions, compatibility notes and original documentation
tests/         unit tests and the compatibility harness against arc.exe
```

## License

GPLv2, the same as FreeArc 0.67 (see [`LICENSE`](LICENSE)).

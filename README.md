# FreeArc-ex

Extended support for [FreeArc 0.67](https://sourceforge.net/projects/freearc/):
a maintained version of the archiver with new platforms, updated libraries,
bug fixes and a GTK3 GUI.

**Status: 0.1.0 pre-release.** The console archiver is feature complete for the
core workflow and verified against the original binary; the GTK3 GUI runs on
Linux and is not packaged for Windows yet.

## Install (Windows)

Download the pre-release installer from
[Releases](https://github.com/YadeWira/FreeArc-ex/releases/tag/v0.1.0-pre):
`FreeArc-ex-0.1.0-pre-setup.exe` (64-bit, Inno Setup 7.1.0, classic wizard like
the original FreeArc setup). It installs `arc-ex.exe`, `arc.groups` and the
docs, with an optional PATH entry and desktop shortcut.

It is beta software: use it on copies of your files.

## Targets

| platform | architectures | minimum |
|---|---|---|
| Linux | x86_64, i686, aarch64 | Debian 12/13 glibc |
| Windows | x86_64, i686 | Windows 7 SP1 |

- Read and write compatibility with FreeArc 0.67.1 `.arc` archives.
- CLI with option and exit-code parity with `arc.exe` 0.67.1.
- GTK3 GUI (gtkmm 3.24).
- FreeArc's own codecs, kept as-is: `lzma`, `rep`, `4x4`, `tor`, `dict`, `lzp`,
  `grzip`, `tta`, `mm`, `delta`, `exe`, `dispack`, `ppmd`, in any chain.
- Encryption (`-p`, `-hp`) with AES, Blowfish, Serpent and Twofish; recovery
  record (`-rr`) and repair (`r`).
- `arc.ini` method definitions and `arc.*.lua` event hooks (Lua 5.1).

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

GUI (needs `libgtkmm-3.0-dev`, native builds only):

```sh
cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Release -DFA_BUILD_GUI=ON
cmake --build build/gui -j
./build/gui/src/gui/freearc-ex archive.arc
```

Windows installer:

```sh
scripts/package-windows.sh x64            # stages the mingw build
cd packaging/windows && ISCC.exe freearc-ex.iss
```

Details in [`docs/BUILD.md`](docs/BUILD.md) and [`docs/INSTALLER.md`](docs/INSTALLER.md).

## What works today

- `a`, `u`, `f`, `d`, `l`, `lb`, `t`, `x`, `r`; masks, `-x`, `-o+/-`, `-dp`,
  `-ad`, `-ap`, `-ep`, `-ae`, `-ma`, `-dm`, `-s` (solid blocks), `-rr`,
  `-p`/`-hp`, `-cfg`.
- Presets `-m0`..`-m9` resolved through FreeArc's definition table and
  `arc.groups` classes, with one solid block per resolved method.
- Updates keep the original file order, reuse untouched blocks in place and
  match the original's block structure.
- Differential tests against `arc.exe` 0.67.1 for reading, writing, updating,
  solid blocks, encryption and recovery.

Not yet: the GTK3 GUI for Windows (needs MSYS2), SFX modules, the Explorer shell
extension, multi-volume archives. See [`docs/COMPAT.md`](docs/COMPAT.md) for the
full compatibility statement.

## Layout

```
src/core/      .arc format engine (C++20)
src/codecs/    FreeArc 0.67 codecs, unmodified
src/cli/       arc-ex
src/gui/       freearc-ex (gtkmm 3.24)
src/lua/       embedded Lua 5.1 (event scripts)
src/unarc/     SFX, unarc.dll and 7z-unarc (Windows)
src/shellext/  Explorer COM extension (Windows)
legacy/        original FreeArc 0.67 sources (Haskell reference, not compiled)
docs/          plan, decisions, compatibility notes and original documentation
tests/         unit tests and the differential harness against arc.exe
```

## License

GPLv2, the same as FreeArc 0.67 (see [`LICENSE`](LICENSE)).

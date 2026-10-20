# FreeArc-ex — milestone plan

Goal: an extended-support version of FreeArc 0.67.1, compatible with the `.arc`
format and the original CLI, on five platforms and with a GTK3 GUI.

Base decisions (ADR [0001](adr/0001-language-and-toolkit.md)):

- Engine, CLI and GUI in **C++20**; GUI with **gtkmm 3.24**.
- The original Haskell source stays in `legacy/haskell/` as a reference and as a
  behavioural oracle; it is not compiled.
- A single **CMake** build with five toolchains.
- Compatibility is verified **against `arc.exe` 0.67.1 running under wine**, not
  against what "looks right" (ADR [0002](adr/0002-compatibility-oracle.md)).

## Milestones

| milestone | contents | acceptance criterion | status |
|---|---|---|---|
| **M0** | Scaffolding: layout, CMake, five toolchains, `arc.exe` oracle, local CI, ADRs | The build matrix compiles and tests pass on all five targets; the oracle compresses, lists and extracts under wine | ✅ |
| **M1** | Vertical slice: `a/l/x/t` with `storing` and `lzma` on Linux x64 | Round trip over the corpus; `arc.exe` extracts what `arc-ex` wrote and vice versa | 🚧 read side done: `l`, `t`, `x` on real archives (storing + lzma, directory and data); `a` pending |
| **M2** | Full engine: `a/u/f/d`, solid blocks and `-s`, filters, AES encryption, `-rr`, `arc.ini`, Lua hooks, complete CLI | Order, update and write matrices against `arc.exe`; update fuzzing with no divergence | |
| **M3** | All five targets | Real testing in the `win7`, `win10` and `arm64` VMs | |
| **M4** | Updated libraries and codecs | Modern LZMA SDK, LZ4 1.10 (CVE-2014-4611), LibTomCrypt 1.18.2, CRC/AES without x86 asm | |
| **M5** | GTK3 GUI | Feature parity with the 0.67 GUI on Linux and Windows | |
| **M6** | SFX, `unarc.dll`, Explorer extension, packaging (installer with Inno Setup 7.1.0, see [INSTALLER.md](INSTALLER.md)) | Clean install and smoke test in all three VMs | |

## Working rules

1. **Nothing is declared compatible without passing the oracle**
   (`scripts/oracle-smoke.sh` and the matrices in `tests/`).
2. Every fix for a 0.67 bug ships with its reproduction in `tests/` and a line in
   `docs/COMPAT.md` (what was fixed and how it is verified).
3. Codecs are ported one at a time, with a round-trip test against the original
   binary; the ones that define the format do not change their bytes.
4. `legacy/haskell/` is never modified: it is the reference.

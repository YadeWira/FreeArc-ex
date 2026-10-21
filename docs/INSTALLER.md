# Windows installer

Decision (2026-10-20): the FreeArc-ex Windows installer will be built with
**Inno Setup 7.1.0** (released 2026-08-12). This supersedes the version the
original scripts were written for.

## Why 7.1.0

- Inno Setup 7 still supports every Windows release since Windows 7, which is the
  project's minimum (`docs/COMPAT.md`).
- IS7 ships both 32-bit and 64-bit editions of the compiler; the 64-bit one can
  build large installers without the 2 GB limits of old 32-bit tools.
- IS7 is Unicode-only. The 0.67-era scripts carried branches for ANSI builds and
  for 5.3.0-and-earlier (`PAnsiChar = PChar`, `AnsiString = String`); all of that
  can be deleted.
- We pin the exact version so installer builds are reproducible.

## What the original did (for reference)

`src/unarc/Unarc/InnoSetup/` holds two sample scripts:

| file | purpose |
|---|---|
| `FreeArc_Example.iss` | minimal example: extract bundled `.arc` files during install with a progress bar |
| `FreeArc_Example-Ext.iss` | extended example: components/tasks, per-component extraction, precomp payloads, splash lines |

Both call **`unarc.dll`** through Pascal Script and use **`InnoCallback.dll`** for
the progress callback. They required **Inno Setup QuickStart Pack 5.2.3+** (the
"QuickStart Pack" only because of the Inno Setup Preprocessor, ISPP) and were
tested on 5.2.3, 5.2.3.e7 (ResTools build), 5.2.4, 5.3.2 beta and
5.3.2-beta-Unicode.

## Plan for FreeArc-ex (milestone M6)

1. Build `unarc.dll` for x86 and x64 from `src/unarc/` with the mingw toolchains.
2. Port `FreeArc_Example-Ext.iss` to IS7: drop the ANSI/5.3.0 compatibility
   branches, keep UTF-8, keep `{#SourceToProgress}` and the components/tasks
   logic.
3. Ship both a 32-bit and a 64-bit installer, each with its matching `unarc.dll`,
   and the GTK3 runtime DLLs for the GUI (see M5).
4. Verify in the `win7` VM (x64 plus WoW64) and in `win10`: clean install,
   upgrade over a previous version, uninstall leaving no leftovers.
5. Record the Inno Setup build used (version 7.1.0 and the installer's sha256) in
   this file when the toolchain is installed in the build environment.

## Not decided yet

- Whether the GUI and CLI ship in one installer or two.
- Whether to keep the `unarc.dll` progress integration or use a plain file-based
  install with Inno's own compression (the DLL path exists to reuse `.arc`
  payloads; it is required only if we ship `.arc` data).

## Building the pre-release installer (2026-10-21)

The 0.1.0-pre installer was built and verified with Inno Setup 7.1.0:

```sh
# 1. stage the Windows files (cross build + docs)
scripts/package-windows.sh x64

# 2. compile the installer with ISCC (Inno Setup 7.1.0)
cd packaging/windows
wine "/path/to/Inno Setup 7/ISCC.exe" freearc-ex.iss
# -> output/FreeArc-ex-0.1.0-pre-setup.exe
```

On this machine ISCC runs under wine (a dedicated `WINEPREFIX`), which is also how
the installer was smoke-tested: silent install with `/VERYSILENT`, then the
installed `arc-ex.exe --version` responds. The Windows VM route (`vm claim
win10`) was not needed; it remains the reference for testing on real Windows.

Contents of this build: the console archiver (`arc-ex.exe`), `arc.groups`,
README, LICENSE and `PRERELEASE.txt`. The GTK3 GUI is **not** included yet: there
is no Windows build of it (it needs MSYS2 with gtkmm, see the plan). The `.iss`
already includes `freearc-ex.exe` with `skipifsourcedoesntexist`, so dropping the
GUI binary into `stage/` is enough to ship it.

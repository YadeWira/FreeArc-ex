# ADR 0001 — C++20 for the engine, CLI and GUI (gtkmm 3.24)

- Date: 2026-10-20
- Status: accepted

## Context

FreeArc 0.67 (upstream of this repository) is an archiver whose entire frontend
is written in Haskell (18,695 lines, 35 modules, GHC 6.10/6.12 era) and whose
codecs are in C/C++ (~58,000 lines). The GUI is Gtk2Hs 0.10.1, i.e. GTK **2**.
`arc.ini` scripting uses a custom Lua 5.1 binding (HsLua 0.3), and the Haskell↔C
bridge is TABI, a home-grown FFI layer over `hsc2hs`.

The FreeArc-ex request includes support for **Windows x86 as well as x64, with
Windows 7 as the minimum**, **Linux x86/x64 and arm64**, and a **GTK3 GUI**.

Verified facts that drive the decision:

1. **No 32-bit Windows GHC exists** in any modern version. On
   `downloads.haskell.org`, 9.2.8, 9.4.8, 9.6.6 and 9.8.4 publish only
   `x86_64-unknown-mingw32`. The last i386 build is from the GHC 7.x era.
2. GTK3 in Haskell means `gi-gtk` through `haskell-gi`: a heavy dependency with
   GObject introspection, and it is not installed on the development machine.
3. The code does not compile with a modern GHC without porting:
   `Control.OldException` (28 uses, module removed), `System.Time` (11 uses),
   `-fglasgow-exts`, plus the 2007-era HsLua/TABI.
4. 32-bit Linux targets do exist (i386 bindists), but they are second-class
   citizens with no distribution packages.

## Decision

- **Format engine, CLI and GUI in C++20**, with CMake and a single tree.
- **GUI with gtkmm 3.24** (not GTK4: GTK4 requires Windows 10 and drops Windows 7;
  not gtk2hs: that is GTK2).
- **The Haskell source is not maintained**: it stays in `legacy/haskell/` as a
  reference and behavioural oracle.
- **The existing C/C++ codecs are kept** and modernized in place.
- **Lua 5.1 stays embedded** (no migration to 5.4) for script compatibility.

Rust was rejected: it would require wrapping all 58,000 lines of codecs through
FFI, and Rust's GTK3 bindings (`gtk-rs`) are in maintenance mode while gtkmm 3.x
is still maintained. The cross toolchain required (mingw-w64 i686 and x86_64,
aarch64) is already available and proven on the development machine.

## Consequences

- **Pros**: one toolchain for all five targets; no extra runtime on Windows
  (msvcrt + static linking); real GTK3; no cost of maintaining GHC 6.10→9.x.
- **Cons**: ~13,400 lines of format/CLI logic must be ported from Haskell to
  C++20. That is the bulk of the schedule (milestones M1–M2) and the main
  technical risk; it is mitigated by differential testing against `arc.exe` at
  every step.
- The `.arc` format does not change: it is the same as 0.67.1.

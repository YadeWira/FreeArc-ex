# ADR 0002 — The compatibility oracle is `arc.exe` 0.67.1 under wine

- Date: 2026-10-20
- Status: accepted

## Context

Porting 13,400 lines of format logic and adding new platforms creates a dangerous
temptation: accepting as "correct" whatever our own code produces and consumes.
An archiver that only understands itself looks fine in every test and is useless
in the real world: the value of FreeArc-ex depends on `.arc` archives remaining
interchangeable with 0.67.1.

## Decision

No feature is declared compatible without passing a **differential** test against
the original binary:

- `Arc.exe` 0.67.1 (krinkels.org build, 2021-03-07) under wine, sha256
  `1b23732e158540eb159563b557ab1d396b12cd56178ea9387e696d054d3e2172`.
- Forward: what `arc-ex` writes is listed/tested/extracted by `arc.exe`.
- Backward: what `arc.exe` writes is read by `arc-ex`, with an identical
  extracted tree (hash, permissions and dates).
- Structure: block types, methods, sizes and offsets compared against the
  reference reader.

## Consequences

- Every milestone has a binary, automatable criterion (`ctest` plus comparison
  scripts), not an eyeball review.
- There is a development dependency: wine plus the 0.67.1 binary. It does not
  affect the product, only the test harness (`scripts/oracle-smoke.sh`,
  `FA_ARC_ORACLE` to point at another copy).
- The 0.67 bugs we choose to fix must be demonstrated by a test that reproduces
  the old behaviour and proves the new one, not by a comment.

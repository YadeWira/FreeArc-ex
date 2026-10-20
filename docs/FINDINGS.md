# Confirmed behaviour and inherited findings

Notes collected while planning FreeArc-ex. Each item is a **lead**, not a fact:
per the working rules, nothing enters the engine until it is reproduced against
the oracle (`arc.exe` 0.67.1 under wine) and covered by a test.

## From PArc's research (2026-10-20)

PArc is a separate, from-scratch Free Pascal rewrite of FreeArc 0.67
(`/mnt/OSR_D6/PArc`). Its author confirmed these behaviours after differential
testing against the original binary, and dropped `.arc` support and its GUI, so
`.arc` compatibility is FreeArc-ex's responsibility only.

| topic | finding |
|---|---|
| `rep+tor:16` | The **encoder** of `arc.exe` 0.67.1 crashes (sometimes exits 0 with a broken archive); the decoder reads such archives fine. Do not reproduce the encoder bug. |
| Update semantics | The binary does not fully follow the published source: `d` with an explicit `-m` recompresses everything (`is_COPYING_COMMAND`); an explicit `-s` keeps old blocks whole (`oldLen`/`oldPos`/`minLen`); "all together" rebuilds one block per group. |
| Canonical method text | `4x4` is stored as `4x4:tor:3:<size>`; Tornado uses presets 0..16; after an external compressor, the dictionaries of later stages are not shrunk. |
| SREP | The source in the 0.67 tree is 3.93 beta (2013), but FreeArc uses 3.93a (2014): in v3/v4 it stores `BASE_LEN=0` and may emit matches shorter than `BASE_LEN` with the length wrapping in 32 bits. |
| `arc.ini` quoting | The distribution's `arc.ini` uses single quotes around `packcmd`/`unpackcmd` for srep; under wine that breaks the external compressor, with double quotes it works. |
| Source tree | PArc no longer keeps the full FreeArc 0.67 tree at HEAD (only `reference/freearc-0.67/Compression`); the rest is in its history at commit `82ea8d9`. |

## Shared development resources

- Oracle binary: `/mnt/IA_LAB/agentes/PArc/ref/Arc.exe`
- Wine prefix used by PArc's tests: `/mnt/IA_LAB/agentes/PArc/wine`

Both belong to PArc's workspace. **Do not delete or modify them**; FreeArc-ex only
reads them, and `FA_ARC_ORACLE` can point at a private copy if isolation is ever
needed.

## FreeArc 0.67 bugs worth fixing

Kept in [`docs/COMPAT.md`](COMPAT.md#known-067-bugs-that-freearc-ex-fixes), with
severity and milestone. The full survey is PArc's
`/mnt/OSR_D6/PArc/docs/BUGS-FREEARC.md` (GPLv2, reusable with attribution).

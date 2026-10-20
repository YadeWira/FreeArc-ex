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

## Legacy codec engine (2026-10-20)

FreeArc's own compression library (`src/codecs`) is used as the codec engine
instead of reimplementing each codec. Things that mattered:

| topic | finding |
|---|---|
| Link | The codecs register themselves through static initializers, so the library must be linked **whole** (`$<LINK_LIBRARY:WHOLE_ARCHIVE>`); a plain static archive drops the methods nobody references by symbol. |
| Language level | Pinned to C++11 for that target: at C++20 the chain decoder misbehaves. |
| Method string | The legacy parser **writes into the method string** while splitting it, so it needs a mutable copy (`c_str()` corrupts the caller's string). |
| 64 bits | Defining `FREEARC_64BIT` fixes the Tornado match finder; the multithreaded match finders store 32-bit pointer deltas, so they are disabled on 64-bit and `MultiThreading.cpp` is compiled separately. |
| Windows cross | The sources include `<ShObjIdl.h>` and `<Wincrypt.h>` with capitalisation that only works on case-insensitive file systems: `src/codecs/compat/` provides shims. `_UNICODE`/`UNICODE` must be defined. |
| arm64 | `CPU_Is_Aes_Supported()` is x86-only; declared/defined in `compat/fa_arch_compat.h` and `fa_compat.cpp` as "no AES instructions". |
| Empty blocks | A data block with orig=0/comp=0 (all entries are directories) must be short-circuited: some codecs reject it. |
| PPMd | The patched PPMd is 32-bit-only by construction (see the PPMd section below); guarded. |
| LZ4 | The bundled 2012 codec rejects real 0.67.1 blocks (error -7). Guarded until the 1.10 update. |

## Presets and file classes (2026-10-20)

| topic | finding |
|---|---|
| Definitions | The preset table is the `builtinMethodSubsts` list from `Compression.hs`: `#` is replaced by each digit 1..9 **in both the name and the value**, and alternatives are separated by `/` (`$obj=`, `$text=` or positional `default/binary/text`). |
| Synonyms | `#bx = #xb` and `#tx = #xt` must be present or chains stop expanding half way (the symptom: a literal `4bx` survives into the method text). |
| Classification | `arc.groups` is an ordered list of sections (`$text`, `$obj`, ...) with wildcards; the first matching section wins. |
| Dictionary limiting | `LimitDictionary()` only understands a **single stage**: chains have to be limited stage by stage and reassembled. |
| ABI | `MemSize` is `size_t`: declaring the entry point with `uint64_t` works on x86-64 but corrupts the stack on i686 (crash inside `ShowCompressionMethod`). |
| Block order | The directory assumes each block's files are **contiguous** in the file list. Grouping by class without reordering the entries produces archives that look fine and decode wrong. |

## PPMd on 64-bit (solved, 2026-10-20)

Two independent defects made PPMd look "32-bit only":

1. **Strict aliasing (both architectures).** The codecs pun pointers heavily
   (`(DWORD*)pc1`, `(WORD&)s1`, `(STATE&)SummFreq`) and GCC at `-O2` or above
   miscompiles PPMd decompression when strict aliasing is on: it produces a few
   correct bytes and then diverges. The legacy codec target is now built with
   `-fno-strict-aliasing` (mandatory).
2. **32-bit layout.** The model requires `PPM_CONTEXT` to be 12 bytes and
   `STATE` 6 bytes, because the allocator hands out 12-byte units and
   `oneState()` overlays a state on the context header. On 64-bit builds:
   - the heap is mapped **below 4 GiB** (`mmap` at `0x10000000`, or `VirtualAlloc`
     on Windows; a fallback checks the address) so 32-bit references are valid;
   - all heap references are stored as **32-bit addresses**
     (`fa_ptr32<T>`): `Successor`, `Stats`, `Suffix` — and also the
     **allocator's `BLK_NODE::next`**, which was the last piece: an 8-byte link
     in a 12-byte unit overflowed every free list.

With both fixes PPMd decodes a reference `arc.exe` archive **byte-exactly**
(sha256 of the block matches the oracle's extraction), compresses archives that
`Arc.exe t` accepts, and the text presets (`-m4`, which end in `ppmd`) work on
all five targets.

Remaining known gap: `lz4` (the 2012 codec rejects real blocks with error -7;
the 1.10 decoder arrives with the library update).

## Encryption (2026-10-20)

- The codec only takes the **key** (`:k<hex>`); key derivation lives in the
  frontend: `PBKDF2-HMAC-SHA512(password, salt, n, keySize+2)`, key = first
  `keySize` bytes, check-code = the last two.
- What goes into the archive's method text is the stage **without** `:k`
  (`aes-256/ctr:n1000:r0:i<iv>:s<salt>:c<code>`); the key is derived again from
  the password when reading. The legacy library already exposes `Pbkdf2Hmac`,
  so no new crypto code was needed.
- A wrong password is detected by comparing the derived check-code with `:c`
  before touching the data.
- `-hp` (header encryption) additionally encrypts the header/directory and puts
  `:s/:c/:i` in the plaintext footer; not implemented yet.

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

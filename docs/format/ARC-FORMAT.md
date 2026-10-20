> Reference copy of the PArc project spec (`/mnt/OSR_D6/PArc/docs/formato/FORMATO-ARC.md`,
> GPLv2, surveyed on 2026-09-24 and validated byte by byte against real files).
> It is kept here as the specification of the format that FreeArc-ex has to reproduce;
> in case of doubt, the 0.67 code is authoritative (`legacy/haskell/`).
> This is a translation of the Spanish original. The source paths quoted throughout refer
> to this repository's `legacy/haskell/` and `src/codecs/` directories.

# FreeArc archive format (`.arc`) — specification for FreeArc-ex

> The FreeArc paths that appear in this document (`Compression/...`, `Unarc/...`,
> `Arc.hs`, ...) are relative to `reference/freearc-0.67/`. Only `Compression/` is
> preserved in the repo (codec reference and test oracle); the rest of the FreeArc 0.67
> source (Haskell, `Unarc/`, `ArcShellExt/`, `Documentation/`) is in the git history
> (commit `82ea8d9`) and in the original FreeArc distribution.

Status: **v0.1 (2026-09-24)**, derived from the FreeArc 0.67 sources
(`ArhiveStructure.hs`, `ArhiveDirectory.hs`, `ByteStream.hs`, `Compression.hs`,
`Unarc/ArcStructure.h`, `Compression/LZMA2/C_LZMA.cpp`) and **validated byte by byte**
against two real files from the corpus (`archive/freeArc/example.arc` and `data2.arc`)
with the prototype `tools/arcls.py`.

Every multibyte number is **little-endian**.

---

## 1. Overview

An archive is a sequence of **blocks**. After every control block (all of them except the
data blocks) a **local descriptor** is written that makes it possible to locate the block
by reading backwards. The archive is read **from the end**.

```
[optional SFX]
HEADER_BLOCK        "ArC\x01" + version (8 bytes, storing)
  descriptor
DATA_BLOCK 0        compressed data of one solid block (no descriptor)
DATA_BLOCK 1 ...
DIR_BLOCK           directory of the preceding DATA_BLOCKs (usually lzma)
  descriptor
[DATA_BLOCK... DIR_BLOCK+descriptor]*   (an archive can have several DIR_BLOCKs)
[RECOVERY_BLOCK + descriptor]*          (optional, -rr)
FOOTER_BLOCK        list of control blocks (usually lzma)
  descriptor        <- it has to start within the last 4096 bytes
```

Real example (`example.arc`, 255,358 bytes):

| block    | pos     | orig  | comp   | method                         |
|----------|---------|-------|--------|--------------------------------|
| HEADER   | 0       | 8     | 8      | `storing`                      |
| DATA     | 31      | —     | 244729 | `rep:244kb+tor:244kb:h1mb:c3`  |
| DIR      | 244760  | 81    | 88     | `lzma:1mb:normal:bt4:32`       |
| RECOVERY | 244989  | 8192  | 8192   | `storing`                      |
| RECOVERY | 253206  | 1995  | 1995   | `storing`                      |
| FOOTER   | 255226  | 95    | 94     | `lzma:1mb:normal:bt4:32`       |

## 2. Primitive types

### 2.1 `VInt`: variable-length integer (1 to 9 bytes)
The number of **consecutive 1 bits in the low part of the first byte** indicates how many
extra bytes follow. The value is stored shifted left by (n+1) bits inside a little-endian
word of n+1 bytes.

| first byte (low bits)    | total bytes   | value range      | decoding                            |
|--------------------------|---------------|------------------|-------------------------------------|
| `xxxxxxx0`               | 1             | < 2^7            | `LE(1) >> 1`                        |
| `xxxxxx01`               | 2             | < 2^14           | `LE(2) >> 2`                        |
| `xxxxx011`               | 3             | < 2^21           | `LE(3) >> 3`                        |
| `xxxx0111`               | 4             | < 2^28           | `LE(4) >> 4`                        |
| `xxx01111`               | 5             | < 2^35           | `LE(5) >> 5`                        |
| `xx011111`               | 6             | < 2^42           | `LE(6) >> 6`                        |
| `x0111111`               | 7             | < 2^49           | `LE(7) >> 7`                        |
| `01111111`               | 8             | < 2^56           | `LE(8) >> 8`                        |
| `11111111`               | 9             | < 2^64           | `LE(8)` of bytes 1..8               |

The encoder **always** picks the shortest form; when writing, FreeArc-ex has to do the same in
order to produce identical archives. Negative values do not exist.

Examples: `0x10` → 8; `2d 1f` → 0x1f2d>>2 = 1995; `0x02` → 1.

### 2.2 Other types
| name     | encoding                                                                  |
|----------|---------------------------------------------------------------------------|
| `U8`     | 1 byte                                                                    |
| `U32`    | 4 bytes LE                                                                |
| `CStr`   | UTF-8 bytes terminated by `0x00` (cannot contain NUL)                     |
| `Bool`   | 1 byte (0/1). **Bool lists are NOT bit-packed**: 1 byte per element        |
| `List<T>`| `VInt n` + n elements of type `T`                                         |
| `CRC`    | `U32`: CRC-32 IEEE (poly 0xEDB88320, init 0xFFFFFFFF, final xor 0xFFFFFFFF) = `zlib.crc32` |
| `Time`   | `U32`: Unix seconds (UTC). `0` = 1970-01-01 (used with `-tsN`/without dates) |
| `Method` | `CStr` with the stages joined by `+`, e.g. `rep:214mb+exe+delta+lzma:103mb`|

> Note on `Time`: in Haskell it is `CTime`, which in the reference Win32 build occupies
> 4 bytes (the C++ Unarc reader uses `read4`). It is a **signed** 32-bit integer,
> so FreeArc-ex must read it as `Int32` and reject/saturate when writing dates
> > 2038-01-19 (or define the extension in `.parc`; see §8).

## 3. Local block descriptor

It goes immediately **after** the data of the block it describes.

| field       | type    | notes                                                       |
|-------------|---------|-------------------------------------------------------------|
| signature   | `U32`   | `41 72 43 01` = `"ArC\x01"`                                 |
| type        | `VInt`  | see §3.1                                                    |
| method      | `CStr`  | block compressor (`storing` if not compressed)              |
| origsize    | `VInt`  | uncompressed size                                           |
| compsize    | `VInt`  | compressed size = data bytes before this descriptor         |
| crc         | `CRC`   | CRC of the **uncompressed** data                            |
| descr_crc   | `CRC`   | CRC of all the preceding bytes of the descriptor (signature..crc) |

Block position = `pos(descriptor) − compsize`.

Validation (Unarc): correct `descr_crc`, correct signature, `origsize>0`, `compsize>0`,
`compsize <= pos(descriptor)`.

### 3.1 Block types (fixed values, only appended at the end)
| value | name             | description                                      |
|-------|------------------|--------------------------------------------------|
| 0     | `DESCR_BLOCK`    | (internal descriptor tag; does not appear in lists) |
| 1     | `HEADER_BLOCK`   | header/signature of the archive                  |
| 2     | `DATA_BLOCK`     | solid data block                                 |
| 3     | `DIR_BLOCK`      | directory                                        |
| 4     | `FOOTER_BLOCK`   | final index                                      |
| 5     | `RECOVERY_BLOCK` | recovery information                             |
| 6+    | unknown          | ignore it when reading                           |

## 4. HEADER_BLOCK

8 bytes, `storing`: `U32 "ArC\x01"` + `U32` version. In FreeArc 0.67 the version is
written as bytes `00 00 06 07` (`make4byte 0 0 6 7`) = "0.67". The archive starts
with `ArC\x01` unless it has an SFX in front.

SFX size = position of the first block in the footer list (= position of the HEADER).

## 5. Locating and reading the FOOTER

1. Read the last `min(4096, size)` bytes.
2. Search for the `ArC\x01` signature **from the end backwards**. Unarc takes the last
   occurrence; `arc.exe` additionally validates and, if the descriptor is not valid, keeps
   searching backwards (the latter is more robust against false signatures).
3. Parse the descriptor (§3); it has to be `type = FOOTER_BLOCK`.
4. Read `compsize` bytes at `pos`, decompress with `method` and verify size + `crc`.

### 5.1 FOOTER contents (uncompressed)
| field            | type                | notes                                           |
|------------------|---------------------|-------------------------------------------------|
| blocks           | `List<BlockRef>`    | control blocks, in archive order (without the footer itself) |
| locked           | `Bool`              | `-k`: archive locked against changes            |
| old_comment_len  | `VInt`              | old comment (UCS-4): `4·len` bytes follow; FreeArc writes 0 |
| *— optional (if not EOF) —* |          |                                                 |
| recovery         | `CStr`              | `-rr` configuration, e.g. `"4%"`; `""` if there is none |
| *— optional (if not EOF) —* |          |                                                 |
| comment_len      | `VInt`              |                                                 |
| comment          | `comment_len` bytes | UTF-8, may contain NUL                          |

`BlockRef` (`blockToTuple`):
| field    | type   | notes                                                    |
|----------|--------|----------------------------------------------------------|
| type     | `VInt` |                                                          |
| method   | `CStr` |                                                          |
| rel_pos  | `VInt` | **absolute position = pos(FOOTER) − rel_pos**            |
| origsize | `VInt` |                                                          |
| compsize | `VInt` |                                                          |
| crc      | `CRC`  |                                                          |

## 6. DIR_BLOCK (uncompressed)

All the per-file fields go **column by column** (struct-of-arrays), not row by row. The
files appear in the order in which they are inside the solid blocks.

| # | field          | type                      | notes                                    |
|---|----------------|---------------------------|------------------------------------------|
| 1 | nblocks        | `VInt`                    | number of DATA_BLOCKs it describes       |
| 2 | files_in_block | `nblocks × VInt`          | files per solid block                    |
| 3 | methods        | `nblocks × CStr`          | compressor of each block                 |
| 4 | rel_offsets    | `nblocks × VInt`          | **absolute pos = pos(DIR_BLOCK) − off**  |
| 5 | compsizes      | `nblocks × VInt`          | compressed size of each block            |
| 6 | dirs           | `List<CStr>`              | directory names, separator `/`; `""` = root |
| — | N = Σ files_in_block |                     |                                          |
| 7 | names          | `N × CStr`                | base name (without directory)            |
| 8 | dir_index      | `N × VInt`                | index into `dirs`                        |
| 9 | sizes          | `N × VInt`                | original size                            |
|10 | times          | `N × Time`                | mtime                                    |
|11 | is_dir         | `N × Bool`                | directory entry                          |
|12 | crcs           | `N × CRC`                 | CRC of the file data                     |
|13 | tag_end        | `VInt` = 0                | end of optional fields (there are none today) |

Derived data (not stored):
- Full name = `dirs[dir_index]` + `/` + `name` (no `/` if the dir is `""`).
- Offset of a file inside its solid block = sum of the preceding `sizes` in that block.
- `origsize` of a DATA_BLOCK = sum of the `sizes` of its files. DATA_BLOCKs have no CRC
  of their own and no descriptor.
- File attributes are **not stored** (`fiAttr = 0`).
- When reading, `dirs` has to be **sanitised**: strip `..` components and absolute roots
  (`remove_unsafe_dirs`) before extracting (protection against path traversal).

Optional fields (reserved, not implemented in 0.67): `VInt tag`, `Bool mandatory`,
`VInt len`, `len` bytes; repeated until `tag = 0`. A reader has to fail if a
`mandatory` tag is unknown and skip the others.

## 7. Compression methods

A `Method` is a chain of stages separated by `+`, applied **left to right when
compressing**; when decompressing they are applied **right to left**. Each stage is
`name[:param]*`.

The minimum needed to **list** (footer and directory): `storing` and `lzma`.

### 7.1 `lzma` (control blocks)
**Raw LZMA1** stream: without the 5-byte header of `.lzma`, without size and without an
end marker (the decoder stops when it reaches `origsize`). The properties come from the
method string:

| parameter                       | effect                   | default |
|---------------------------------|--------------------------|---------|
| `d<mem>` or `<mem>` with suffix  | dictionary size          | 64mb    |
| `lc<n>` / `lp<n>` / `pb<n>`     | literal/pos properties   | 3/0/2 |
| bare number (`32`)              | numFastBytes (compressor only) |    |
| `bt4`, `hc4`, `normal`, `max`…  | compressor only          |         |

`<mem>`: number + `b`/`k`/`kb`/`m`/`mb`/`g`/`gb`.

### 7.1b `lzma` in DATA blocks (streaming)
Different from control blocks: the size of each intermediate stage is not known, so the
stream **ends with an end marker** (distance 0xFFFFFFFF) and the decoder uses a
**circular dictionary** of `d` bytes (from the method; default 64mb). `lzma_decompress2`
in stream mode fails if the input runs out without an end marker.

### 7.1c `exe` (BCJ x86)
Filter with no parameters. State (`ip`, mask) continuous across buffers. **FreeArc quirk**
(`bcj_x86_de_compress`): a run of ≤5 bytes is emitted unconverted. Since the compressor
receives full buffers, that only happens at the end of the stream, so the decoder **has
to fill its whole buffer before converting**. If it converts the first thing the previous
stage gives it, in the middle of the stream it can leave a run of ≤5 bytes unconverted
and the output ends up corrupt. It is a real bug seen in practice; the
compatibility tests must cover it.

### 7.1d `delta` (Bulat Ziganshin)
Sequence of blocks until EOF, all of them `U32` LE integers:
`DataSize`, `TableSize` (bytes, multiple of 4), `TSkip[TableSize]`, `TType[TableSize]`,
`TRows[TableSize]`, `Data[DataSize]`. For each table `i`: advance `skip`, decode
`type` (the highest bit gives the width N; the lower bits mark unchanged columns),
"un-shuffle" (the unchanged columns were stored together first) and add to each row the
previous one, byte by byte LSB first, carrying only between contiguous mutable columns.
Parameters (`b<mem>`, `x`): they only affect the compressor.

### 7.2 Methods seen in real data
`rep`, `tor`, `dict`, `lzp`, `ppmd`, `exe`, `delta`, `lzma` (and, according to the code,
`4x4`, `grzip`, `lz4`, `srep`, `precomp`, external ones). Extracting needs all of them;
listing needs none.

### 7.3 Encryption

Sources: `Encryption.hs` (key derivation and method construction),
`Compression/_Encryption/C_Encryption.cpp` (`parse_ENCRYPTION`, `docrypt`) and the
LibTomCrypt 1.18.2 ciphers embedded in `Compression/_Encryption/`.

Encryption **is not a separate method: it is one more stage of the chain**, joined with
`+` after the compressor. In a real archive:

```
grzip:198kb:m1:l32:h15+aes-256/ctr:n1000:r0:i<32 hex>:s<64 hex>:c<4 hex>
```

#### 7.3.1 Stage grammar

```
<cipher>-<key-bits>/<mode>[:f]:n<iterations>:r<rounds>
    [:k<key-hex>] [:i<iv-hex>] [:s<salt-hex>] [:c<checkcode-hex>]
```

| field | meaning | default |
|---|---|---|
| `cipher` | `aes`, `blowfish`, `serpent`, `twofish` | `aes` (chosen with `--encryption=` / `-ae`; the profile canonicalises it to `aes-256/ctr`) |
| `key-bits` | key size in **bits**, after `-` | the cipher's maximum (see table) |
| `mode` | `ctr` (0) or `cfb` (1), after `/` | `ctr` |
| `f` | the password is converted from Unicode to UTF-8 before deriving | absent = the password bytes as they are |
| `n` | PBKDF2 iterations | 1000 |
| `r` | cipher rounds | 0 (the cipher's own) |
| `k` | the key in hex (**only appears in already-decrypted headers**) | — |
| `i` | IV in hex | — |
| `s` | salt in hex | — |
| `c` | check-code in hex (2 bytes) | — |

#### 7.3.2 Ciphers and sizes

| cipher | max key (bits) | block / IV (bytes) |
|---|---|---|
| `aes` | 256 (128 and 192 are valid) | 16 |
| `blowfish` | 448 | 8 |
| `serpent` | 256 | 16 |
| `twofish` | 256 | 16 |

#### 7.3.3 Modes

- **CTR** with a **little-endian** counter (`CTR_COUNTER_LITTLE_ENDIAN`): keystream =
  `E(IV)` and successive increments.
- Exception: `aes` + `ctr` with `r0` uses 7-Zip's optimised AES-CTR
  (`7zAES/Aes.c`), which is equivalent but runs the counter **from IV-1** and does a
  *pre*increment. Careful: it only processes multiples of 16 bytes; `docrypt` takes care
  of leaving the remainder for the next cycle (that is why the last block is rounded
  **up** and the remainder is trimmed when writing).
- **CFB** of the block size (16, or 8 with blowfish).
- Padding does not exist: the output length is the input length. Whatever falls outside
  the cipher block is copied as is (`RemainderSize`).

#### 7.3.4 Key derivation (the central point)

```
dk       = PBKDF2-HMAC-SHA512(password, salt, iterations, keySize + 2)
key      = dk[0 .. keySize-1]
checkCode= dk[keySize .. keySize+1]          (2 bytes)
```

`Encryption.hs::deriveKey` splits the `pbkdf2Hmac` result with `splitAt keySize`, so the
key and the check-code come out of **a single** derivation. The `salt` is exactly
`keySize` bytes long and the IV `ivSize`.

With `-hp`, the **plaintext footer** (and its backup copy) carries
`:s<salt>:c<check>:i<iv>`; the decrypted directory/header additionally carry the
`:k<key>`, so that the password does not have to be asked for again.

Verified on real 0.67.1 archives: with `--encryption=aes-128` the salt is 16 bytes, with
`aes-192` 24, with `aes-256` 32, with `blowfish` 56 (448 bits) and its IV 8 bytes, and
with serpent/twofish 32 and 16.

#### 7.3.5 Detecting a wrong password

`generateDecryption` rebuilds the key with each candidate password and compares
the derived check-code with the `:c` of the archive. If they do not match, the password
is wrong (and FreeArc asks again / aborts). There is **no** verification of the
key against the ciphertext: the check-code is the only thing that distinguishes a
bad password before attempting to decrypt. It also supports *keyfiles*: passwords
concatenated with the contents of each keyfile are tried.

`f` (Unicode passwords): without `:f`, the password bytes are used as they are
(historical FreeArc); with `:f`, they are converted from Unicode to UTF-8.

#### 7.3.6 Critical detail: the hex of `:k` and `:i` is decoded "wrongly" on purpose

`Compression/Common.h` defines two digit converters:

```c
static inline int char2int(char c)       { return isdigit(c)? c-'0' : tolower(c)-'a'+10; }
static inline int buggy_char2int(char c) { return isdigit(c)? c-'0' : tolower(c)-'a';    }
```

that is, the second one **does not add 10** for letters: with it `'a'..'f'` are worth
0..5 instead of 10..15.  And `ENCRYPTION_METHOD::decompress`/`compress` do

```c
fixed? decode16 (key, key_bytes) : buggy_decode16 (key, key_bytes);
```

so **the key and the IV are decoded with the "buggy" converter** unless the method
carries the `:f` flag.  Since the *encoder* used exactly the same path, the key (and the
IV) actually used for encrypting are that "mis-decoded" decoding of the correct hex that
Haskell writes with `encode16`/`decode16` (`Utils.hs`).

The **salt** and the **check-code**, on the other hand, are decoded by Haskell with
`decode16` (normal hex): those are the ones that go into PBKDF2.

Practical consequence: both decoders have to be implemented and chosen according to
the `f` flag.  If the normal hex is used for the key, the derivation and the check-code
come out right (the salt is fine) but decryption produces garbage.

#### 7.3.7 Several blocks, several parameter sets

An encrypted archive has **more than one block** (the directory one and the data one) and
**each block carries its own IV, salt and check-code** in its own method string.  Real
example (two rows of `arc.exe lt`):

```
*  31        0        0   storing+aes-256/ctr:n1000:r0:i<b13c8d..>:s<69d2fd..>:c<7357>
*  31  200,000  200,000   storing+aes-256/ctr:n1000:r0:i<eaf24a..>:s<2ad189..>:c<131e>
```

and each one is decrypted with the key derived from *its* salt (same password).

#### 7.3.8 What FreeArc-ex has to do

1. Take `s`, `c`, `n`, `f` and the cipher/mode from the encryption stage of the `Method`.
2. `PBKDF2-HMAC-SHA512(password, salt, n, keySize+2)` and compare `dk[keySize..]`
   with the `c` of the archive: if they do not match → clear password error.
3. If the header is encrypted (`-hp`): read `s`/`c`/`i` from the plaintext footer,
   derive the key and decrypt the header/directory; the `:k` comes out of there.
4. Decrypt the data block with `cipher` + `mode` + key + IV, taking into
   account the AES-CTR/7-Zip detail and the handling of the remainder.

#### 7.4 RECOVERY_BLOCK (`-rr`)

Sources: `ArcRecover.hs` (writing, verification and repair). In the C++ only
the `RECOVERY_BLOCK` constant appears (`Unarc/FarPlugin.cpp:166`); the real logic
is all in the Haskell.

**What it is.** A parity record that allows the archive to be *repaired*, not just
detected as broken. **Two** consecutive `RECOVERY_BLOCK` control blocks are written,
both with method `aNO_COMPRESSION`:

1. the **recovery sectors** block (`rec_sectors_size` bytes), and
2. the **CRCs + info** block (`crcs_size` bytes), which starts with the control info
   and continues with the CRCs.

They are written at `rrPos` (the end of the data) and `lt` reports them as
"N recovery sectors (X bytes) present".

**Provisional footer** (`ArcvProcessRead.hs:85-93`). Before the RECOVERY_BLOCKs a
complete FOOTER_BLOCK is written with the list `header + directory` and recovery `""`;
`rrPos` is left *after* it, so the record protects it too. After the
two RECOVERY_BLOCKs the definitive footer is written (list `header + directory +
recovery`, without the provisional one, and the text of `-rr`). Without the provisional
one the archive is valid but not identical to FreeArc's: `arcsize` and the parity of the
tail change.

**`-rr` grammar** (`ArcRecover.hs:104-112`, `parseNumber` from `Utils.hs`):
`N;SS` = amount N with sector SS; `N*SS` = exactly N·SS bytes with sector SS
(both with `parseSize`: bytes, or `k`/`m`/`g`); otherwise `parseNumber N 's'`: no
suffix or `s` = N·512, `b`/`k`/`m` = bytes, `%`/`p` = N·⌈arcsize/100⌉ (`divRoundUp`).
`-rr0.1%` and `-rr0.01%` are stored as `0*4kb` and `0*64kb` (that is what arc.exe 0.67.1
does; the source says `0*4096`/`0*65536`). `-rr4096*8` (8-byte sector) makes FreeArc run
out of memory; FreeArc-ex rejects it. Verified: with `-m0`, archives byte-identical to
arc.exe's for `1%`, `0.1%`, `0.01%`, `8*4096`, `10`, `4%`, `3%;4096`, `2`,
`1m` and `20000b`.

**Parameter derivation** (`ArcRecover.hs:104-135`). With `arcsize` = bytes to
protect and `wanted_rec_size` = what was requested in `-rr`:

| amount | formula |
|---|---|
| `wanted_rec_size` | suffix `b` = bytes, `s` (or none) = n·512, `%`/`p` = n·⌈arcsize/100⌉ |
| `sector_size` | `2^floor(log2(40·arcsize div wanted_rec_size))`, minimum 512; with `-rr0%` it is 4096 |
| `arc_sectors` | `ceil(arcsize / sector_size)` |
| `crcs_size0` | `arc_sectors · 4` (one 4-byte CRC per sector) |
| `rec_size` | `max(wanted_rec_size, crcs_size0)` |
| `rec_sectors` | `ceil((rec_size − crcs_size0) / sector_size)` |
| `rec_sectors_size` | `rec_sectors · sector_size` |
| `crcs_size` | `crcs_size0 + rec_sectors · 4` |

(watch out for `40·arcsize div wanted_rec_size`: it is integer division *before* the
logarithm)

Validated against real files from `arc.exe` 0.67.1: with 272,000 bytes of data and
`-rr4%` the formulas give `sector_size = 512`, `rec_sectors = 7` and
`rec_sectors_size = 3584`, and `arc.exe lt` reports exactly "7 recovery sectors
(3584 bytes)"; with `-rr1%` they give `sector_size = 2048`, `rec_sectors = 1`, and it
reports "1 recovery sectors (2 kbytes)". The relation is the opposite of what one would
expect: more percentage ⇒ smaller sectors (4% → 512, 2% → 1024, 1% → 2048).

**Parity algorithm.** It is a **rotating XOR**, not Reed-Solomon:

- The archive is walked from `init_pos` in chunks of `sector_size` (the last one
  may be shorter).
- Each chunk is **XOR-ed** into a recovery sector that rotates:
  `i := (i+1) mod rec_sectors`, starting at `i0 = (-arc_sectors) mod rec_sectors`.
  That `i0` is what makes the distribution independent of where the protection
  starts, and **it has to be respected the same way when verifying**.
- A **4-byte CRC** of each chunk is also computed, and it is stored in order.
- At the end the CRC of **each recovery sector** is computed and appended
  too, after the CRCs of the data.

**Verification** (`scanArchive`). Both blocks are read, the archive is walked
the same way as when writing (same rotating XOR and same CRCs) and every sector whose CRC
does not match is a damaged sector. Besides the bad sectors, the accumulated XOR of the
*good* sectors of each group is what later allows reconstruction.

**Repair.** For each damaged sector: if in its rotation group (`i` fixed) there is
a **single** damaged sector, it is rebuilt as the XOR of the recovery sector
with all the good sectors of that group. That is, **each group tolerates one damaged
sector**, and the total tolerates up to `rec_sectors` damages as long as two do not fall
in the same group. The result is written to `fixed.<name>` (the original is never
touched).

**Control info.** The CRCs block starts with the info:

- a **version as a NUL-terminated string**: `"0.36"` when there are recovery
  sectors and `"0.39"` when `rec_sectors = 0` (that is, CRCs only, `-rr0%`);
  the reader accepts both;
- `arcsize` of the protected area;
- the offset that allows recomputing `init_pos = block_pos − offset`;
- the list of `(sector_size, rec_sectors)` pairs.

**Serialisation, confirmed against a real `.arc`.** The types are those of §2
(`String` = `CStr`, `Integer` = `VInt`, tuple = concatenated fields, list = `VInt n`
+ elements), so the block starts exactly with:

    CStr "0.36" · VInt arcsize · VInt offset · VInt 1 · VInt sector_size · VInt rec_sectors

and behind it come the CRCs. **Watch out for a difference that is easy to overlook**: the
CRCs (both those of each data sector and those of each recovery sector) are
4-byte **LE** `U32`, not `VInt`; in Haskell they are `Storable` (lines 159 and 166),
they are not serialised like the `Integer`s of the addinfo.

Decoded from a real `.arc` from `arc.exe` with `-rr4%` (272,000 bytes of test data):
`"0.36"`, `arcsize = 104535`, `offset = 108144`, a list of 1 pair, and the pair
`(sector_size = 512, rec_sectors = 7)`. Those two values are exactly what the formulas
above predict for 4%, so the parameter derivation and the layout are confirmed
together: the `offset` gives `init_pos = block_pos − offset =
0`, that is, the protection starts at the beginning of the archive.

## 8. `.parc` — out of scope

`.parc` is the native format of **PArc**, a separate from-scratch Pascal rewrite of
FreeArc 0.67. It is a *derived* format: same block/descriptor/VInt/footer structure as
this document, with its own signature, version and extensions (64-bit timestamps, file
attributes, optional directory fields).

FreeArc-ex does **not** read or write `.parc`: its contract is the `.arc` format of
FreeArc 0.67.1 and the archives produced by `arc.exe`. PArc, in turn, dropped `.arc`
support, so the two projects do not overlap.

## 9. Still to be specified
- [ ] RECOVERY_BLOCK (`ArcRecover.hs`): format of the recovery sectors.
- [ ] Encryption of the data and of the headers (`Encryption.hs`, `-hp`).
- [ ] Multivolume archives, if any.
- [ ] Scanning of broken archives (`findBlocksInBrokenArchive`): backwards descriptor search in 8 MB windows.
- [ ] Order and grouping into solid blocks when **writing** (`ArhiveFileList.hs`).
- [ ] Verify with generated archives: empty, UTF-8, `-m0`, empty directories, SFX.

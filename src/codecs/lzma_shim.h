/* Compatibility shim for the FreeArc-patched LZMA SDK sources.
 *
 * The LZMA encoder and match finder in src/codecs/LZMA2/C were modified by
 * FreeArc to use a few helpers from its Common.h (size units, match-finder IDs
 * and rounddown_to_power_of). They are C files, so Common.h (C++) cannot be
 * included. This header provides exactly those definitions, with the same
 * semantics, and is force-included when compiling the codecs.
 *
 * It also keeps the fake Windows headers of the SDK out of the way on MinGW by
 * not touching the include path: see src/codecs/CMakeLists.txt.
 */
#ifndef FA_LZMA_SHIM_H
#define FA_LZMA_SHIM_H

#include <stddef.h>

typedef size_t MemSize;

#ifndef kb
#define kb (1024)
#endif
#ifndef mb
#define mb (1024 * kb)
#endif
#ifndef gb
#define gb (1024 * mb)
#endif

#ifndef mymin
#define mymin(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef mymax
#define mymax(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef BUFFER_SIZE
#define BUFFER_SIZE (256 * kb)
#endif
#if defined(__GNUC__)
#define prefetch(var) __builtin_prefetch(&(var))
#else
#define prefetch(var) ((void)0)
#endif

/* Match finder identifiers (values are FreeArc's C_LZMA.cpp enum). */
#define MF_HashChain 0
#define MF_BinaryTree 1
#define MF_HashTable 2

static inline int lb(MemSize n)
{
    int result = 0;
    while (n >>= 1) ++result;
    return result;
}

/* Largest power of `base` not greater than n; rounddown_to_power_of(13, 2) = 8. */
static inline MemSize rounddown_to_power_of(MemSize n, MemSize base)
{
    MemSize result = 1;
    if (!n) return 1;
    if (base == 2) {
        result <<= lb(n);
    } else {
        while (n /= base) result *= base;
    }
    return result;
}

#endif /* FA_LZMA_SHIM_H */

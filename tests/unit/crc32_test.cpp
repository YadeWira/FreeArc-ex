#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fa/crc32.h"

namespace {

int failures = 0;

void check_eq(std::uint32_t got, std::uint32_t want, const char* what)
{
    if (got != want) {
        std::printf("FAIL: %s: got 0x%08X, expected 0x%08X\n", what, got, want);
        ++failures;
    } else {
        std::printf("ok: %s (0x%08X)\n", what, got);
    }
}

std::uint32_t crc_of(const std::string& s)
{
    return fa::crc32(s.data(), s.size());
}

}  // namespace

int main()
{
    // Reference vectors for the standard CRC-32 (IEEE 802.3).
    check_eq(crc_of(""), 0x00000000u, "empty string");
    check_eq(crc_of("123456789"), 0xCBF43926u, "check vector \"123456789\"");
    check_eq(crc_of("The quick brown fox jumps over the lazy dog"), 0x414FA339u,
             "reference phrase");

    // Chunked computation must match the one-shot value, and it must exercise
    // all three paths: 8-byte blocks, tail, and empty input.
    const std::string a = "FreeArc-ex: ";
    const std::string b = "byte-for-byte compatibility with 0.67.1";
    const std::uint32_t whole = crc_of(a + b);
    std::uint32_t pieces = fa::crc32_update(0, a.data(), a.size());
    pieces = fa::crc32_update(pieces, b.data(), b.size());
    check_eq(pieces, whole, "chunked computation");

    // A 1 MiB buffer: exercises the slice-by-8 path over many blocks.
    std::vector<std::uint8_t> big(1024 * 1024);
    for (std::size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<std::uint8_t>(i * 31 + 7);
    }
    const std::uint32_t big_whole = fa::crc32(big.data(), big.size());
    std::uint32_t big_pieces = 0;
    const std::size_t chunk = 8191;  // prime chunk so it rarely stays aligned
    for (std::size_t off = 0; off < big.size(); off += chunk) {
        const std::size_t n = (big.size() - off < chunk) ? (big.size() - off) : chunk;
        big_pieces = fa::crc32_update(big_pieces, big.data() + off, n);
    }
    check_eq(big_pieces, big_whole, "1 MiB chunked");

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("crc32: all ok\n");
    return 0;
}

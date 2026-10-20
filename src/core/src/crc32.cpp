#include "fa/crc32.h"

#include <array>
#include <cstring>

namespace fa {
namespace {

// Eight tables of 256 entries: the slice-by-8 variant, which consumes 8 bytes
// per iteration using only two loads. It is the same portable path used by
// 7-Zip; on x86-64/arm64 it can be replaced with CRC32 instructions once CPU
// dispatch lands (milestone M4).
constexpr std::array<std::array<std::uint32_t, 256>, 8> make_tables() noexcept
{
    std::array<std::array<std::uint32_t, 256>, 8> tables{};

    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int bit = 0; bit < 8; ++bit) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        tables[0][i] = c;
    }

    for (std::size_t i = 0; i < 256; ++i) {
        for (std::size_t slice = 1; slice < 8; ++slice) {
            const std::uint32_t prev = tables[slice - 1][i];
            tables[slice][i] = (prev >> 8) ^ tables[0][prev & 0xFF];
        }
    }

    return tables;
}

constexpr auto kTables = make_tables();

inline std::uint32_t load_le32(const std::uint8_t* p) noexcept
{
    std::uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

}  // namespace

std::uint32_t crc32_update(std::uint32_t crc, const void* data, std::size_t size) noexcept
{
    const auto* p = static_cast<const std::uint8_t*>(data);
    crc = ~crc;

    while (size >= 8) {
        const std::uint32_t lo = load_le32(p);
        const std::uint32_t hi = load_le32(p + 4);
        crc ^= lo;
        crc = kTables[7][crc & 0xFF] ^ kTables[6][(crc >> 8) & 0xFF] ^
              kTables[5][(crc >> 16) & 0xFF] ^ kTables[4][(crc >> 24) & 0xFF] ^
              kTables[3][hi & 0xFF] ^ kTables[2][(hi >> 8) & 0xFF] ^
              kTables[1][(hi >> 16) & 0xFF] ^ kTables[0][(hi >> 24) & 0xFF];
        p += 8;
        size -= 8;
    }

    while (size-- > 0) {
        crc = kTables[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    }

    return ~crc;
}

}  // namespace fa

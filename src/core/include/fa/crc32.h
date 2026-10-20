#pragma once

#include <cstddef>
#include <cstdint>

namespace fa {

// CRC-32 (polynomial 0xEDB88320) with the same semantics as 7-Zip's CrcUpdate:
// the accumulator is passed in and returned without manual complementing, so
// crc32() == crc32_update(0, ...) and chaining crc32_update() is equivalent to a
// single computation over the concatenation.
std::uint32_t crc32_update(std::uint32_t crc, const void* data, std::size_t size) noexcept;

inline std::uint32_t crc32(const void* data, std::size_t size) noexcept
{
    return crc32_update(0, data, size);
}

}  // namespace fa

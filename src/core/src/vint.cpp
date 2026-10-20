#include "fa/vint.h"

namespace fa {
namespace {

constexpr std::size_t kMaxVintBytes = 9;

int trailing_ones(std::uint8_t first) noexcept
{
    int n = 0;
    while (n < 8 && ((first >> n) & 1u) != 0) {
        ++n;
    }
    return n;
}

}  // namespace

bool read_vint(const std::uint8_t* data, std::size_t size, std::size_t& pos,
               std::uint64_t& value) noexcept
{
    if (pos >= size) {
        return false;
    }

    const int n = trailing_ones(data[pos]);

    if (n == 8) {
        // 9-byte form: 0xFF followed by a plain 64-bit little-endian value.
        if (size - pos < kMaxVintBytes) {
            return false;
        }
        std::uint64_t v = 0;
        for (int i = 8; i >= 1; --i) {
            v = (v << 8) | data[pos + static_cast<std::size_t>(i)];
        }
        value = v;
        pos += kMaxVintBytes;
        return true;
    }

    const std::size_t width = static_cast<std::size_t>(n) + 1;
    if (size - pos < width) {
        return false;
    }

    std::uint64_t v = 0;
    for (std::size_t i = width; i-- > 0;) {
        v = (v << 8) | data[pos + i];
    }

    value = v >> (static_cast<unsigned>(n) + 1);
    pos += width;
    return true;
}

std::size_t vint_size(std::uint64_t value) noexcept
{
    int n = 0;
    while (n < 8 && value >= (std::uint64_t{1} << (7 * (n + 1)))) {
        ++n;
    }
    return static_cast<std::size_t>(n) + 1;
}

void write_vint(std::vector<std::uint8_t>& out, std::uint64_t value)
{
    int n = 0;
    while (n < 8 && value >= (std::uint64_t{1} << (7 * (n + 1)))) {
        ++n;
    }

    if (n == 8) {
        out.push_back(0xFF);
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
        return;
    }

    const std::uint64_t packed =
        (value << (static_cast<unsigned>(n) + 1)) | ((std::uint64_t{1} << n) - 1);
    const std::size_t width = static_cast<std::size_t>(n) + 1;
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(static_cast<std::uint8_t>(packed >> (8 * i)));
    }
}

}  // namespace fa

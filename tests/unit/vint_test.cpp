#include <cstdint>
#include <cstdio>
#include <vector>

#include "fa/vint.h"

namespace {

int failures = 0;

void check(bool cond, const char* what, std::uint64_t value)
{
    if (!cond) {
        std::printf("FAIL: %s (value %llu)\n", what, static_cast<unsigned long long>(value));
        ++failures;
    }
}

std::vector<std::uint8_t> encode(std::uint64_t value)
{
    std::vector<std::uint8_t> out;
    fa::write_vint(out, value);
    return out;
}

}  // namespace

int main()
{
    // Every boundary of the format's table: the first value that needs the next
    // width, and the last value that fits in the current one.
    const std::vector<std::uint64_t> values = {
        0,          1,          2,          127,        128,
        16383,      16384,      2097151,    2097152,    268435455,
        268435456,  34359738367ULL, 34359738368ULL,
        4398046511103ULL, 4398046511104ULL,
        562949953421311ULL, 562949953421312ULL,
        72057594037927935ULL, 72057594037927936ULL,
        18446744073709551615ULL};

    for (const std::uint64_t v : values) {
        const auto bytes = encode(v);
        check(bytes.size() == fa::vint_size(v), "vint_size matches encoding", v);

        std::size_t pos = 0;
        std::uint64_t decoded = 0;
        const bool ok = fa::read_vint(bytes.data(), bytes.size(), pos, decoded);
        check(ok, "decode succeeds", v);
        check(decoded == v, "round trip", v);
        check(pos == bytes.size(), "decode consumes exactly the integer", v);

        // The last byte must not be a valid start followed by garbage: reading
        // one byte short has to fail, never read out of bounds.
        if (bytes.size() > 1) {
            std::size_t p2 = 0;
            std::uint64_t v2 = 0;
            const bool truncated = !fa::read_vint(bytes.data(), bytes.size() - 1, p2, v2);
            check(truncated, "truncated input is rejected", v);
        }
    }

    // Known encodings from the format specification.
    const auto eight = encode(8);
    check(eight.size() == 1 && eight[0] == 0x10, "8 encodes as 0x10", 8);
    const auto one = encode(1);
    check(one.size() == 1 && one[0] == 0x02, "1 encodes as 0x02", 1);
    const auto max9 = encode(18446744073709551615ULL);
    check(max9.size() == 9 && max9[0] == 0xFF, "2^64-1 uses the 9-byte form", 0);

    // The example from the spec: 2d 1f -> 0x1f2d >> 2 = 1995.
    const std::uint8_t sample[] = {0x2d, 0x1f};
    std::size_t pos = 0;
    std::uint64_t v = 0;
    check(fa::read_vint(sample, sizeof(sample), pos, v) && v == 1995 && pos == 2,
          "spec example 2d 1f -> 1995", 1995);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("vint: all ok\n");
    return 0;
}

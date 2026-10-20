#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// FreeArc variable-length integers (VInt): 1 to 9 bytes, little-endian.
// The number of consecutive low 1 bits in the first byte says how many extra
// bytes follow; the value is stored shifted left by (n+1) bits in an (n+1)-byte
// word, except for the 9-byte form (first byte 0xFF), where the value is the
// 8 bytes that follow. See docs/format/ARC-FORMAT.md, section 2.1.
namespace fa {

// Decodes one VInt starting at data[pos] and advances pos on success.
// Returns false (leaving pos untouched) if the input ends in the middle of an
// integer, so callers can report a corrupt archive instead of reading past the
// buffer.
bool read_vint(const std::uint8_t* data, std::size_t size, std::size_t& pos,
               std::uint64_t& value) noexcept;

// Appends the shortest encoding of value, exactly like the original encoder.
void write_vint(std::vector<std::uint8_t>& out, std::uint64_t value);

// Number of bytes the shortest encoding of value occupies (1..9).
std::size_t vint_size(std::uint64_t value) noexcept;

}  // namespace fa

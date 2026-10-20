#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fa {

// Decompresses one block (`storing` or a single `lzma` stage) into exactly
// out_size bytes. Chain methods (rep+exe+lzma, ...) are rejected until M2.
bool decompress(const std::string& method, const std::uint8_t* input, std::size_t input_size,
                std::size_t output_size, std::vector<std::uint8_t>& output, std::string& error);

}  // namespace fa

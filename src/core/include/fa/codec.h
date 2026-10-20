#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fa {

// Wraps the legacy FreeArc compression library (src/codecs). It understands the
// full method grammar, including chains such as "rep:96m+exe+delta+lzma:16mb".
// `storing` is handled by the callers as a fast path.

// Decompresses exactly output_size bytes. Returns false and fills `error` on any
// failure (bad method, corrupt data, output size mismatch).
bool codec_decompress(const std::string& method, const std::uint8_t* input, std::size_t input_size,
                      std::size_t output_size, std::vector<std::uint8_t>& output,
                      std::string& error);

// Compresses with the given method text. The output buffer is grown until the
// library accepts it.
bool codec_compress(const std::string& method, const std::uint8_t* input, std::size_t input_size,
                    std::vector<std::uint8_t>& output, std::string& error);

// Shrinks the dictionaries in a method chain to `dict` bytes, the way the
// original limits them to the block size ("rep:96m+..." -> "rep:10kb+...").
bool codec_limit_dictionary(const std::string& method, std::uint64_t dict, std::string& output,
                            std::string& error);

}  // namespace fa

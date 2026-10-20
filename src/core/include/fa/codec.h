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

// True when the legacy engine can handle this method on the current architecture.
// PPMd works on 32-bit after the strict-aliasing fix, but its 32-bit heap layout
// has not been ported to 64-bit yet (docs/FINDINGS.md); LZ4's 2012 codec rejects
// real blocks on both.
bool codec_method_supported(const std::string& method);

// Shrinks the dictionaries in a method chain to `dict` bytes, the way the
// original limits them to the block size ("rep:96m+..." -> "rep:10kb+...").
bool codec_limit_dictionary(const std::string& method, std::uint64_t dict, std::string& output,
                            std::string& error);

}  // namespace fa

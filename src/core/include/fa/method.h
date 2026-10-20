#pragma once

#include <cstdint>
#include <string>

namespace fa {

// Decoding parameters of the `lzma` method. FreeArc stores them in the method
// text (e.g. "lzma:1mb:normal:bt4:32") instead of a header in the stream, so the
// decoder has to rebuild the LZMA properties from the method string.
struct LzmaParams {
    std::uint32_t dict_size = 64u * 1024u * 1024u;
    unsigned lc = 3;  // literal context bits
    unsigned lp = 0;  // literal position bits
    unsigned pb = 2;  // position state bits
};

// Splits a method chain stage ("lzma:16mb:max:bt4:64") into its name ("lzma")
// and, for lzma, its decoding parameters. Only the first stage is handled:
// chains (rep+exe+lzma) are rejected by the caller for now.
bool parse_method(const std::string& method, std::string& name, LzmaParams& lzma);

// Parses a FreeArc size ("1mb", "64m", "4096", "2g") into bytes; 0 on error.
std::uint64_t parse_mem(const std::string& text);

}  // namespace fa

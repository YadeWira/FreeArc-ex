#pragma once

#include <cstddef>
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

// Method text for an lzma stream encoded with the given dictionary, e.g.
// "lzma:32kb:normal:32".
std::string lzma_method_text(std::uint32_t dict_size, unsigned fast_bytes = 32);

// Smallest power of two that covers `size`, between 32 KiB and 64 MiB: the same
// dictionary choice the original uses.
std::uint32_t lzma_dictionary_for(std::size_t size);

}  // namespace fa

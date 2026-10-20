#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fa {

// Encodes a raw LZMA1 stream with an end marker, exactly as the original
// encoder does: the properties are NOT stored in the stream, they travel in the
// method text (see docs/format/ARC-FORMAT.md, section 7.1).
struct LzmaEncodeParams {
    std::uint32_t dict_size = 32u * 1024u;
    unsigned lc = 3;
    unsigned lp = 0;
    unsigned pb = 2;
    unsigned fb = 32;
};

bool lzma_compress(const std::uint8_t* data, std::size_t size, const LzmaEncodeParams& params,
                   std::vector<std::uint8_t>& out, std::string& error);

// Method text for a stream encoded with `params`, e.g. "lzma:32kb:normal:32".
std::string lzma_method_text(const LzmaEncodeParams& params);

// Picks a dictionary the way FreeArc does: the smallest power of two that covers
// the data, between 32 KiB and 64 MiB.
std::uint32_t lzma_dictionary_for(std::size_t size);

}  // namespace fa

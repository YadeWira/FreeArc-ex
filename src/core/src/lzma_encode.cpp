#include "fa/lzma_encode.h"

#include <cstdlib>

extern "C" {
#include "LZMA2/C/LzmaEnc.h"
}

namespace fa {
namespace {

void* sz_alloc(void* /*p*/, std::size_t size)
{
    return std::malloc(size);
}

void sz_free(void* /*p*/, void* address)
{
    std::free(address);
}

ISzAlloc g_alloc = {sz_alloc, sz_free};

std::string human_size(std::uint32_t bytes)
{
    if (bytes % (1024u * 1024u) == 0) return std::to_string(bytes / (1024u * 1024u)) + "mb";
    if (bytes % 1024u == 0) return std::to_string(bytes / 1024u) + "kb";
    return std::to_string(bytes);
}

}  // namespace

bool lzma_compress(const std::uint8_t* data, std::size_t size, const LzmaEncodeParams& params,
                   std::vector<std::uint8_t>& out, std::string& error)
{
    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.dictSize = params.dict_size;
    props.lc = params.lc;
    props.lp = params.lp;
    props.pb = params.pb;
    props.fb = params.fb;
    props.algo = 1;
    props.writeEndMark = 1;

    CLzmaEncHandle enc = LzmaEnc_Create(&g_alloc);
    if (enc == nullptr) {
        error = "LZMA: cannot create encoder";
        return false;
    }

    SRes res = LzmaEnc_SetProps(enc, &props);
    if (res == SZ_OK) {
        // Worst case for LZMA is small; the SDK's own bound is size + size/3 + 128.
        out.resize(size + size / 3 + 1024);
        SizeT out_len = out.size();
        res = LzmaEnc_MemEncode(enc, out.data(), &out_len, data, size, 1, nullptr, &g_alloc,
                                &g_alloc);
        if (res == SZ_OK) {
            out.resize(out_len);
        }
    }

    LzmaEnc_Destroy(enc, &g_alloc, &g_alloc);

    if (res != SZ_OK) {
        error = "LZMA encoder error " + std::to_string(static_cast<int>(res));
        out.clear();
        return false;
    }
    return true;
}

std::string lzma_method_text(const LzmaEncodeParams& params)
{
    return "lzma:" + human_size(params.dict_size) + ":normal:" + std::to_string(params.fb);
}

std::uint32_t lzma_dictionary_for(std::size_t size)
{
    std::uint32_t dict = 32u * 1024u;
    while (dict < size && dict < 64u * 1024u * 1024u) {
        dict <<= 1;
    }
    return dict;
}

}  // namespace fa

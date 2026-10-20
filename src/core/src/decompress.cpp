#include "fa/decompress.h"

#include <cstdlib>
#include <cstring>

#include "fa/method.h"

extern "C" {
#include "LZMA2/C/LzmaDec.h"
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

}  // namespace

bool decompress(const std::string& method, const std::uint8_t* input, std::size_t input_size,
                std::size_t output_size, std::vector<std::uint8_t>& output, std::string& error)
{
    std::string name;
    LzmaParams lzma;
    if (!parse_method(method, name, lzma)) {
        error = "unsupported method '" + method + "' (method chains arrive in M2)";
        return false;
    }

    if (name == "storing") {
        if (input_size != output_size) {
            error = "storing block with mismatched sizes";
            return false;
        }
        output.assign(input, input + input_size);
        return true;
    }

    if (name != "lzma") {
        error = "unsupported method '" + method + "'";
        return false;
    }

    if (output_size == 0) {
        output.clear();
        return true;
    }

    // FreeArc keeps the LZMA properties in the method text, not in the stream:
    // rebuild the standard 5-byte properties block from them.
    std::uint8_t props[LZMA_PROPS_SIZE];
    props[0] = static_cast<std::uint8_t>((lzma.pb * 5 + lzma.lp) * 9 + lzma.lc);
    props[1] = static_cast<std::uint8_t>(lzma.dict_size);
    props[2] = static_cast<std::uint8_t>(lzma.dict_size >> 8);
    props[3] = static_cast<std::uint8_t>(lzma.dict_size >> 16);
    props[4] = static_cast<std::uint8_t>(lzma.dict_size >> 24);

    output.resize(output_size);

    // Same path as the reference decoder: properties come from the method text,
    // the history window is the output buffer itself (memory-to-memory), and the
    // stream is decoded with LZMA_FINISH_ANY until the output is complete.
    CLzmaDec state;
    LzmaDec_Construct(&state);
    if (LzmaDec_AllocateProbs(&state, props, LZMA_PROPS_SIZE, &g_alloc) != SZ_OK) {
        error = "LZMA: cannot allocate probabilities";
        output.clear();
        return false;
    }
    state.dic = output.data();
    state.dicBufSize = output_size;
    LzmaDec_Init(&state);

    SizeT in_len = input_size;
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    const SRes res =
        LzmaDec_DecodeToDic(&state, output_size, input, &in_len, LZMA_FINISH_ANY, &status);
    const SizeT produced = state.dicPos;
    LzmaDec_FreeProbs(&state, &g_alloc);

    if (res != SZ_OK) {
        error = "LZMA error " + std::to_string(static_cast<int>(res)) + " in '" + method + "'";
        output.clear();
        return false;
    }
    if (status == LZMA_STATUS_NEEDS_MORE_INPUT) {
        error = "LZMA stream truncated in '" + method + "'";
        output.clear();
        return false;
    }
    if (produced != output_size) {
        error = "LZMA produced " + std::to_string(produced) + " of " +
                std::to_string(output_size) + " expected bytes";
        output.clear();
        return false;
    }
    return true;
}

}  // namespace fa

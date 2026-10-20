#include "fa/decompress.h"

#include "fa/codec.h"

namespace fa {

bool decompress(const std::string& method, const std::uint8_t* input, std::size_t input_size,
                std::size_t output_size, std::vector<std::uint8_t>& output, std::string& error)
{
    // Empty blocks (all entries are directories, for instance) carry orig=0 and
    // comp=0; no codec call is needed and some of the legacy ones reject it.
    if (output_size == 0) {
        output.clear();
        return true;
    }

    if (method == "storing") {
        if (input_size != output_size) {
            error = "storing block with mismatched sizes";
            return false;
        }
        output.assign(input, input + input_size);
        return true;
    }
    return codec_decompress(method, input, input_size, output_size, output, error);
}

}  // namespace fa

#include "fa/codec.h"

#include <climits>
#include <string>

// The legacy library exposes these two entry points with C linkage.
extern "C" {
int DecompressMem(char* method, void* input, int inputSize, void* output, int outputSize);
int CompressMem(char* method, void* input, int inputSize, void* output, int outputSize);
int LimitDictionary(char* in_method, std::size_t dict, char* out_method);
}

namespace fa {
namespace {

// FREEARC_ERRCODE_OUTBLOCK_TOO_SMALL, from src/codecs/Compression.h. Any negative
// return value is an error; this one only means "retry with a bigger buffer".
constexpr int kOutBlockTooSmall = -7;

// Methods that the legacy engine cannot handle reliably in this build. They are
// rejected with a clear error instead of producing garbage or crashing:
//  - ppmd: FreeArc's patched PPMd (variant H, modified) returns truncated output
//    on both 32- and 64-bit Linux builds; needs its own fix (PArc's approach is
//    to emulate the original 32-bit layout).
//  - lz4: the 2012 codec rejects real blocks written by arc.exe 0.67.1 (error -7)
//    on both architectures; the 1.10 decoder arrives with the library update.
bool method_is_unsupported(const std::string& method)
{
    return method.find("ppmd") != std::string::npos || method.find("lz4") != std::string::npos;
}

std::string describe(int code)
{
    return "codec error " + std::to_string(code);
}

}  // namespace

bool codec_decompress(const std::string& method, const std::uint8_t* input, std::size_t input_size,
                      std::size_t output_size, std::vector<std::uint8_t>& output,
                      std::string& error)
{
    if (method_is_unsupported(method)) {
        error = "method '" + method + "' is not supported yet (known legacy codec bug)";
        return false;
    }
    if (input_size > static_cast<std::size_t>(INT_MAX) ||
        output_size > static_cast<std::size_t>(INT_MAX)) {
        error = "block too large for the legacy codec API";
        return false;
    }

    // The legacy parser may write into the method string while splitting it, so
    // it gets a private mutable copy (passing c_str() corrupts the source).
    std::vector<char> method_copy(method.begin(), method.end());
    method_copy.push_back('\0');

    output.resize(output_size);
    char* name = method_copy.data();
    const int written = DecompressMem(name, const_cast<std::uint8_t*>(input),
                                      static_cast<int>(input_size), output.data(),
                                      static_cast<int>(output_size));
    if (written < 0) {
        error = "decompression failed (" + describe(written) + "): in=" +
                std::to_string(input_size) + " out=" + std::to_string(output_size) +
                " method='" + method + "'";
        output.clear();
        return false;
    }
    if (static_cast<std::size_t>(written) != output_size) {
        error = "decompressed " + std::to_string(written) + " of " +
                std::to_string(output_size) + " expected bytes";
        output.clear();
        return false;
    }
    return true;
}

bool codec_compress(const std::string& method, const std::uint8_t* input, std::size_t input_size,
                    std::vector<std::uint8_t>& output, std::string& error)
{
    if (method_is_unsupported(method)) {
        error = "method '" + method + "' is not supported yet (known legacy codec bug)";
        return false;
    }
    if (input_size > static_cast<std::size_t>(INT_MAX)) {
        error = "block too large for the legacy codec API";
        return false;
    }

    std::size_t capacity = input_size + input_size / 3 + 4096;
    for (;;) {
        if (capacity > static_cast<std::size_t>(INT_MAX)) {
            error = "compressed block would be too large";
            return false;
        }
        std::vector<char> method_copy(method.begin(), method.end());
        method_copy.push_back('\0');

        output.resize(capacity);
        char* name = method_copy.data();
        const int written = CompressMem(name, const_cast<std::uint8_t*>(input),
                                        static_cast<int>(input_size), output.data(),
                                        static_cast<int>(capacity));
        if (written == kOutBlockTooSmall) {
            capacity *= 2;
            continue;
        }
        if (written < 0) {
            error = "compression failed (" + describe(written) + ") for method '" + method + "'";
            output.clear();
            return false;
        }
        output.resize(static_cast<std::size_t>(written));
        return true;
    }
}


bool codec_limit_dictionary(const std::string& method, std::uint64_t dict, std::string& output,
                            std::string& error)
{
    // The parser behind LimitDictionary only understands a single stage, so the
    // chain is limited stage by stage and reassembled.
    output.clear();
    std::size_t start = 0;
    while (true) {
        const std::size_t end = method.find('+', start);
        const std::string stage =
            method.substr(start, end == std::string::npos ? std::string::npos : end - start);

        std::vector<char> in(stage.begin(), stage.end());
        in.push_back('\0');
        std::vector<char> out(4096);
        const int rc = LimitDictionary(in.data(), dict, out.data());
        if (rc < 0) {
            error = "cannot limit the dictionary of '" + stage + "' (" + describe(rc) + ")";
            return false;
        }
        if (!output.empty()) output += '+';
        output += out.data();

        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

}  // namespace fa

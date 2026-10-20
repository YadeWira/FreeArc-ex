#include "fa/method.h"

#include <cctype>
#include <vector>

namespace fa {
namespace {

std::string lower(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = s.find(sep, start);
        parts.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return parts;
}

bool parse_uint(const std::string& text, unsigned max_value, unsigned& out)
{
    if (text.empty()) return false;
    unsigned value = 0;
    for (const char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
        if (value > max_value) return false;
    }
    out = value;
    return true;
}

}  // namespace

std::uint64_t parse_mem(const std::string& text)
{
    if (text.empty()) return 0;
    std::size_t i = 0;
    std::uint64_t value = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        value = value * 10 + static_cast<std::uint64_t>(text[i] - '0');
        if (value > (std::uint64_t{1} << 48)) return 0;
        ++i;
    }
    if (i == 0) return 0;

    std::uint64_t multiplier = 1;
    const std::string suffix = lower(text.substr(i));
    if (suffix.empty() || suffix == "b") {
        multiplier = 1;
    } else if (suffix == "k" || suffix == "kb") {
        multiplier = 1024ULL;
    } else if (suffix == "m" || suffix == "mb") {
        multiplier = 1024ULL * 1024;
    } else if (suffix == "g" || suffix == "gb") {
        multiplier = 1024ULL * 1024 * 1024;
    } else {
        return 0;
    }
    return value * multiplier;
}

bool parse_method(const std::string& method, std::string& name, LzmaParams& lzma)
{
    const auto stages = split(method, '+');
    if (stages.empty() || stages.size() != 1) return false;

    const auto params = split(stages[0], ':');
    name = lower(params[0]);

    if (name != "lzma") {
        // The caller may still handle other methods (storing); parameters are
        // only meaningful for lzma.
        return true;
    }

    for (std::size_t i = 1; i < params.size(); ++i) {
        std::string p = lower(params[i]);
        if (p.empty()) continue;
        if (p[0] == '*') p.erase(0, 1);  // optional parameter marker

        unsigned number = 0;
        if (p.rfind("lc", 0) == 0 && parse_uint(p.substr(2), 4, number)) {
            lzma.lc = number;
        } else if (p.rfind("lp", 0) == 0 && parse_uint(p.substr(2), 4, number)) {
            lzma.lp = number;
        } else if (p.rfind("pb", 0) == 0 && parse_uint(p.substr(2), 4, number)) {
            lzma.pb = number;
        } else if ((p.rfind("d", 0) == 0) && !p.empty()) {
            const std::uint64_t size = parse_mem(p.substr(1));
            if (size != 0) lzma.dict_size = static_cast<std::uint32_t>(size);
        } else if (p.rfind("fb", 0) == 0 || p.rfind("mc", 0) == 0 ||
                   p.rfind("h", 0) == 0 || p.rfind("a", 0) == 0) {
            // Encoder-only parameters: they do not affect decoding.
            continue;
        } else if (p == "fastest" || p == "fast" || p == "normal" || p == "max" ||
                   p == "ultra" || p == "bt2" || p == "bt3" || p == "bt4" || p == "hc4" ||
                   p == "ht4" || p.rfind("mf", 0) == 0) {
            continue;  // presets / match finder: encoder only
        } else if (!std::isdigit(static_cast<unsigned char>(p[0]))) {
            // Bare dictionary size with a unit, e.g. "16mb". A bare number is the
            // encoder's numFastBytes (parse_LZMA tries parseInt first), not a size.
            const std::uint64_t size = parse_mem(p);
            if (size != 0) lzma.dict_size = static_cast<std::uint32_t>(size);
        }
    }
    return true;
}

}  // namespace fa

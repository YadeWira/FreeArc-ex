#include "fa/preset.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fa {
namespace {

std::string trim(const std::string& s)
{
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
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

// Glob match for the arc.groups patterns: '*' and '?' only, case-insensitive.
bool glob_match(std::string_view pattern, std::string_view text)
{
    std::size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' ||
                                   std::tolower(static_cast<unsigned char>(pattern[p])) ==
                                       std::tolower(static_cast<unsigned char>(text[t])))) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

std::string basename_of(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return lower(slash == std::string::npos ? path : path.substr(slash + 1));
}

}  // namespace

bool FileClasses::load(const std::filesystem::path& path, std::string& error)
{
    std::ifstream in(path);
    if (!in) {
        error = "cannot open groups file '" + path.string() + "'";
        return false;
    }
    bool have_section = false;
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t comment = line.find(';');
        if (comment != std::string::npos) line.erase(comment);
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '$') {
            sections_.push_back({line, {}});
            have_section = true;
        } else if (have_section) {
            sections_.back().patterns.push_back(lower(line));
        }
    }
    return true;
}

std::string FileClasses::classify(const std::string& name) const
{
    const std::string base = basename_of(name);
    for (const auto& section : sections_) {
        if (section.name == "$default") continue;  // used for ordering, not matching
        for (const auto& pattern : section.patterns) {
            if (glob_match(pattern, base)) return section.name;
        }
    }
    return "$obj";
}

MethodTable::MethodTable()
{
    // Built-in definitions from FreeArc's Compression.hs (builtinMethodSubsts),
    // reduced to the internal codecs. '#' is expanded to each preset digit.
    static const char* kBuiltin[] = {
        "x  = 9",
        "0  = storing",
        "1  = 1b",
        "#  = #rep+exe+#xb / $obj=#b / $text=#t",
        "#x = #xb/#xt",
        "1t  = 1b",
        "2t  = grzip:m4:8m:32:h15",
        "3t  = dict:p:64m:85% + lzp:64m:24:h20:92% + grzip:m3:8m:l",
        "4t  = dict:p:64m:80% + lzp:64m:65:d1m:s16:h20:90% + ppmd:8:96m",
        "5t  = dict:p:64m:80% + lzp:80m:105:d1m:s32:h22:92% + ppmd:12:192m",
        "#t  = dict:p:128m:80% + lzp:160m:145:d1m:s32:h23:92% + ppmd:16:384m",
        "#b  = #rep+#bx",
        "1rep = rep:96m:256:c256",
        "2rep = rep:96m:256:c256",
        "3rep = rep:96m",
        "4rep = rep:96m",
        "5rep = rep:96m",
        "6rep = rep:256m",
        "7rep = rep:512m",
        "8rep = rep:1024m",
        "9rep = rep:2040m",
        "#xt = dict:128m:75% + #binary",
        "#xb = delta + #binary",
        "1xb = 1binary",
        "2xb = 2binary",
        "1binary = tor:4",
        "2binary = tor:6",
        "3binary = lzma:96m:fast:32:mc4",
        "4binary = lzma:96m:normal:16:mc8",
        "5binary = lzma:96m:normal:32:mc32",
        "6binary = lzma:32m:max",
        "7binary = lzma:64m:max",
        "8binary = lzma:128m:max",
        "9binary = lzma:254m:max",
        "#bx = #xb",
        "#tx = #xt",
        "bcj = exe",
        "copy = storing",
        "exe2 = dispack",
        "dispack = dispack070",
    };

    for (const char* line : kBuiltin) add_line(line);
}

void MethodTable::add_line(const std::string& raw)
{
    std::string line = raw;
    const std::size_t comment = line.find(';');
    if (comment != std::string::npos) line.erase(comment);
    line = trim(line);
    if (line.empty()) return;

    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) return;
    const std::string name = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (name.empty() || value.empty()) return;

    // Expand '#' into each preset digit, in both the name and the value, as the
    // original does before parsing the definitions.
    std::vector<std::pair<std::string, std::string>> entries_to_add;
    if (name.find('#') != std::string::npos || value.find('#') != std::string::npos) {
        for (char d = '1'; d <= '9'; ++d) {
            std::string n = name;
            std::string v = value;
            std::replace(n.begin(), n.end(), '#', d);
            std::replace(v.begin(), v.end(), '#', d);
            entries_to_add.emplace_back(std::move(n), std::move(v));
        }
    } else {
        entries_to_add.emplace_back(name, value);
    }

    // Alternatives: "default / $obj=... / $text=..." or "exe+X / b / t".
    for (const auto& [n, v] : entries_to_add) {
        const auto alts = split(v, '/');
        auto& entry = defs_[n];
        for (std::size_t i = 0; i < alts.size(); ++i) {
            std::string alt = trim(alts[i]);
            if (alt.empty()) continue;
            if (alt[0] == '$' && alt.find('=') != std::string::npos) {
                const std::size_t pos = alt.find('=');
                entry[trim(alt.substr(0, pos))] = trim(alt.substr(pos + 1));
            } else if (i == 1 && alts.size() >= 3) {
                entry["$obj"] = alt;  // positional form: default / binary / text
            } else if (i == 2 && alts.size() >= 3) {
                entry["$text"] = alt;
            } else {
                entry[""] = alt;
            }
        }
        // A definition without a default uses its object form.
        if (entry.find("") == entry.end() && entry.count("$obj") != 0) entry[""] = entry["$obj"];
    }
}

std::string MethodTable::resolve_name(const std::string& name, const std::string& cls,
                                      int depth) const
{
    if (depth > 12) return name;  // cycle guard

    // Keep parameters attached to the name ("rep:96m" -> "rep" + ":96m").
    std::string base = name;
    std::string params;
    const std::size_t colon = name.find(':');
    if (colon != std::string::npos) {
        base = name.substr(0, colon);
        params = name.substr(colon);
    }

    const auto it = defs_.find(base);
    if (it == defs_.end()) return name;  // already a codec

    const auto& alts = it->second;
    std::string text;
    const auto exact = alts.find(cls);
    if (exact != alts.end()) {
        text = exact->second;
    } else {
        const auto def = alts.find("");
        if (def == alts.end()) return name;
        text = def->second;
    }

    // The '#' placeholder was already expanded to digits in the name; a literal
    // '#' left in the text means "the preset digit", which is the first digit of
    // the name being resolved (or the resolved definition's own digit).
    std::string digit = "4";
    if (!base.empty() && std::isdigit(static_cast<unsigned char>(base[0]))) digit = base.substr(0, 1);

    std::string expanded;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '#') {
            expanded += digit;
        } else if (c == ' ' && i + 1 < text.size() && text[i + 1] == '+') {
            // whitespace around '+' is cosmetic
        } else {
            expanded += c;
        }
    }

    // Resolve every stage of the chain.
    std::string result;
    for (const auto& stage : split(expanded, '+')) {
        if (!result.empty()) result += '+';
        result += resolve_name(trim(stage), cls, depth + 1);
    }
    if (!params.empty() && result.find(':') == std::string::npos) result += params;
    return result;
}

std::string MethodTable::resolve(const std::string& method, const std::string& file_class) const
{
    return resolve_name(trim(method), file_class.empty() ? "$obj" : file_class, 0);
}

bool MethodTable::load_ini(const std::filesystem::path& path, std::string& error)
{
    std::ifstream in(path);
    if (!in) {
        error = "cannot open '" + path.string() + "'";
        return false;
    }
    bool methods_section = false;
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == ';' || trimmed[0] == '#') continue;
        if (trimmed[0] == '[') {
            methods_section = lower(trimmed) == "[compression methods]";
            continue;
        }
        if (methods_section) add_line(trimmed);
    }
    return true;
}

}  // namespace fa

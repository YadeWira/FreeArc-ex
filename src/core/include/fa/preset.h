#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fa {

// FreeArc's file classes (arc.groups): a list of sections, each a set of
// wildcards. The first section that matches a file name wins; unmatched files
// are "$obj".
class FileClasses {
public:
    bool load(const std::filesystem::path& path, std::string& error);
    std::string classify(const std::string& name) const;  // "$text", "$obj", ...

private:
    struct Section {
        std::string name;
        std::vector<std::string> patterns;
    };
    std::vector<Section> sections_;
};

// FreeArc's method definitions ("Compression methods"): names that expand into
// method chains, with per-class overrides and the `#` placeholder replaced by
// the preset digit. Built-in defaults come from Compression.hs; an arc.ini file
// can extend or override them.
class MethodTable {
public:
    MethodTable();

    // Loads an arc.ini; unknown sections are ignored, the ones named
    // "Compression methods" are merged over the built-in table.
    bool load_ini(const std::filesystem::path& path, std::string& error);

    // Expands `method` (a preset like "4" or a name like "#b") for `file_class`.
    // Names that are already codecs ("lzma:96m") are returned unchanged.
    std::string resolve(const std::string& method, const std::string& file_class) const;

private:
    using Alternatives = std::map<std::string, std::string>;  // class -> text, "" = default
    std::map<std::string, Alternatives> defs_;

    void add_line(const std::string& line);
    std::string resolve_name(const std::string& name, const std::string& cls, int depth) const;
};

}  // namespace fa

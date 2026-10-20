#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "fa/preset.h"

namespace fa {

// One entry to write: the bytes are already available (from disk or from an
// existing archive when updating). base/dir/size/crc are derived by the writer.
struct NewEntry {
    std::string name;  // full path inside the archive, '/'-separated
    bool is_dir = false;
    std::int32_t mtime = 0;
    std::vector<std::uint8_t> data;

    // Derived fields, filled by write_archive().
    std::string base;
    std::string dir;
    std::uint64_t size = 0;
    std::uint32_t crc = 0;
};

// Solid-block grouping (-s...), parsed from FreeArc's grammar:
//   (empty) all together, "-" one file per block, "10m" by size, "100f" by
//   count, "e" by extension. Several criteria can be combined ("e100f10m").
struct SolidRule {
    enum class Kind { All, None, Files, Size, Ext } kind = Kind::All;
    std::uint64_t value = 0;  // files or bytes
};

struct AddOptions {
    std::string method = "lzma";   // preset ("4"), name ("#b") or raw chain
    const MethodTable* table = nullptr;     // required for presets/names
    const FileClasses* classes = nullptr;   // file classification (arc.groups)
    std::vector<SolidRule> solid{SolidRule{}};  // default: one block per method
};

// Creates a new archive containing `inputs` (files and/or directories, walked
// recursively). Files are grouped into solid blocks by the method the preset
// resolves to for their class, as the original does.
// Writes an archive from entries whose contents are already known.
bool write_archive(const std::filesystem::path& archive, std::vector<NewEntry> entries,
                   const AddOptions& options, std::string& error);

bool create_archive(const std::filesystem::path& archive,
                    const std::vector<std::filesystem::path>& inputs, const AddOptions& options,
                    std::string& error);

}  // namespace fa

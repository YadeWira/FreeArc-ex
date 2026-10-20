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

struct AddOptions {
    std::string method = "lzma";   // preset ("4"), name ("#b") or raw chain
    const MethodTable* table = nullptr;     // required for presets/names
    const FileClasses* classes = nullptr;   // file classification (arc.groups)
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

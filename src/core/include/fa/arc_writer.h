#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "fa/preset.h"

namespace fa {

struct AddOptions {
    std::string method = "lzma";   // preset ("4"), name ("#b") or raw chain
    const MethodTable* table = nullptr;     // required for presets/names
    const FileClasses* classes = nullptr;   // file classification (arc.groups)
};

// Creates a new archive containing `inputs` (files and/or directories, walked
// recursively). Files are grouped into solid blocks by the method the preset
// resolves to for their class, as the original does.
bool create_archive(const std::filesystem::path& archive,
                    const std::vector<std::filesystem::path>& inputs, const AddOptions& options,
                    std::string& error);

}  // namespace fa

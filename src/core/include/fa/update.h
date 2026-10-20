#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "fa/arc_writer.h"

namespace fa {

enum class UpdateMode {
    Update,   // u: add new files and replace changed ones
    Freshen,  // f: replace changed files, do not add new ones
    Delete,   // d: remove the matched entries
};

// Applies an update to an existing archive. The archive is rewritten in place
// (old blocks are decompressed and the data recompressed), which is correct but
// less efficient than FreeArc's block reuse; that optimisation comes later.
bool update_archive(const std::filesystem::path& archive,
                    const std::vector<std::filesystem::path>& inputs, UpdateMode mode,
                    const AddOptions& options, std::string& error);

}  // namespace fa

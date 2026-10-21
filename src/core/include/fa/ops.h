#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "fa/progress.h"

namespace fa {

struct ExtractOptions {
    std::filesystem::path output_dir = ".";
    std::vector<std::string> masks;     // empty = everything
    std::vector<std::string> excludes;
    bool overwrite = true;              // -o- keeps existing files
};

// Extracts an archive and verifies every file's CRC. Returns false on error or
// when the user cancelled (with `error` empty on cancellation).
bool extract_archive(const std::filesystem::path& archive, const ExtractOptions& options,
                     const ProgressFn& progress, bool& cancelled, std::string& error);

// Verifies every file's CRC without writing anything.
bool test_archive(const std::filesystem::path& archive, const ProgressFn& progress, bool& cancelled,
                  std::string& error);

}  // namespace fa

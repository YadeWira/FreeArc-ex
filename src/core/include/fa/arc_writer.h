#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fa {

struct AddOptions {
    std::string method = "lzma";  // "storing", "lzma" or "lzma:<dict>:..."
    bool store_directory = true;  // archive the directory itself (-ed disables it)
};

// Creates a new archive containing `inputs` (files and/or directories, walked
// recursively). Uses a single solid data block, which is what the format allows
// for M1; solid-block splitting arrives with M2.
bool create_archive(const std::filesystem::path& archive,
                    const std::vector<std::filesystem::path>& inputs, const AddOptions& options,
                    std::string& error);

}  // namespace fa

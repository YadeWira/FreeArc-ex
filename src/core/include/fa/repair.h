#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace fa {

struct RepairResult {
    std::size_t damaged = 0;   // sectors whose CRC did not match
    std::size_t repaired = 0;  // sectors reconstructed from the parity record
    std::size_t groups_lost = 0;  // groups with more than one damaged sector
};

// Uses the recovery record (-rr) to repair a damaged archive. The original is
// never touched: the result goes to `output`.
bool repair_archive(const std::filesystem::path& archive, const std::filesystem::path& output,
                    RepairResult& result, std::string& error);

}  // namespace fa

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace fa {

// Progress callback: done/total bytes and the current file. Returning false
// cancels the operation.
using ProgressFn = std::function<bool(std::uint64_t done, std::uint64_t total,
                                      const std::string& current)>;

}  // namespace fa

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Read side of the .arc format (docs/format/ARC-FORMAT.md). Milestone M1:
// control blocks in `storing` only; compressed control blocks (the usual case,
// directory compressed with lzma) arrive with the codec integration.
namespace fa {

struct BlockRef {
    std::uint32_t type = 0;
    std::string method;
    std::uint64_t pos = 0;  // absolute offset of the block data
    std::uint64_t orig_size = 0;
    std::uint64_t comp_size = 0;
    std::uint32_t crc = 0;
};

struct FileEntry {
    std::string name;   // full path inside the archive, '/'-separated
    std::uint64_t size = 0;
    std::int32_t mtime = 0;  // Unix seconds (0.67 stores a signed 32-bit time)
    bool is_dir = false;
    std::uint32_t crc = 0;
};

struct ArchiveInfo {
    std::vector<BlockRef> blocks;  // control blocks, in archive order (no footer)
    bool locked = false;
    std::string comment;
    std::vector<FileEntry> files;
    std::uint64_t sfx_size = 0;  // offset of the first block = length of any SFX prefix
};

// Block type values (fixed by the format, only appended at the end).
enum BlockType : std::uint32_t {
    kDescrBlock = 0,
    kHeaderBlock = 1,
    kDataBlock = 2,
    kDirBlock = 3,
    kFooterBlock = 4,
    kRecoveryBlock = 5,
};

// Parses an archive's metadata: footer, control blocks and file list.
// Returns false and fills `error` on any inconsistency (bad signature, CRC
// mismatch, truncated input, unsupported compression method).
bool open_archive(const std::filesystem::path& path, ArchiveInfo& info, std::string& error);

}  // namespace fa

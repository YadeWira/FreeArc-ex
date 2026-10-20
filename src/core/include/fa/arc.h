#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "fa/mapped_file.h"

// Read side of the .arc format (docs/format/ARC-FORMAT.md). Milestone M1: control
// blocks and data blocks with `storing` and a single `lzma` stage.
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
    std::string name;  // full path inside the archive, '/'-separated
    std::uint64_t size = 0;
    std::int32_t mtime = 0;  // Unix seconds (0.67 stores a signed 32-bit time)
    bool is_dir = false;
    std::uint32_t crc = 0;
    std::size_t data_block = 0;  // index into ArchiveInfo::data_blocks
    std::uint64_t offset = 0;    // offset of the file data inside its solid block
};

struct DataBlock {
    std::string method;
    std::uint64_t pos = 0;
    std::uint64_t comp_size = 0;
    std::uint64_t orig_size = 0;  // sum of the sizes of its files
    std::size_t first_file = 0;
    std::size_t file_count = 0;
};

struct ArchiveInfo {
    std::vector<BlockRef> blocks;  // control blocks, in archive order (no footer)
    std::vector<DataBlock> data_blocks;
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

// An opened archive: keeps the file contents in memory and exposes the parsed
// metadata plus on-demand block decompression.
class Archive {
public:
    static bool open(const std::filesystem::path& path, Archive& archive, std::string& error);

    const ArchiveInfo& info() const { return info_; }

    // Releases the mapping. Needed before rewriting an archive in place: on
    // Windows a mapped file cannot be opened for writing (sharing violation).
    void close() { file_ = MappedFile{}; }

    // Decompresses data block `index` into exactly its original size.
    bool read_data_block(std::size_t index, std::vector<std::uint8_t>& out,
                         std::string& error) const;

    // Raw (still compressed) bytes of a data block, for block reuse on update.
    bool raw_data_block(std::size_t index, std::vector<std::uint8_t>& out,
                        std::string& error) const;

private:
    MappedFile file_;
    ArchiveInfo info_;
};

}  // namespace fa

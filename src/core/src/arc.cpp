#include "fa/arc.h"

#include <cstring>
#include <string_view>

#include "fa/crc32.h"
#include "fa/decompress.h"
#include "fa/format.h"
#include "fa/mapped_file.h"
#include "fa/vint.h"

namespace fa {
namespace {

class Cursor {
public:
    Cursor(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool u8(std::uint8_t& out)
    {
        if (pos_ >= size_) return false;
        out = data_[pos_++];
        return true;
    }

    bool u32(std::uint32_t& out)
    {
        if (size_ - pos_ < 4) return false;
        out = static_cast<std::uint32_t>(data_[pos_]) |
              (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8) |
              (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16) |
              (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return true;
    }

    bool vint(std::uint64_t& out) { return read_vint(data_, size_, pos_, out); }

    bool cstr(std::string& out)
    {
        const std::size_t start = pos_;
        while (pos_ < size_ && data_[pos_] != 0) ++pos_;
        if (pos_ >= size_) return false;
        out.assign(reinterpret_cast<const char*>(data_ + start), pos_ - start);
        ++pos_;
        return true;
    }

    bool skip(std::size_t n)
    {
        if (size_ - pos_ < n) return false;
        pos_ += n;
        return true;
    }

    std::size_t pos() const { return pos_; }
    std::size_t remaining() const { return size_ - pos_; }
    bool eof() const { return pos_ >= size_; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

bool parse_descriptor(const std::uint8_t* buf, std::size_t size, std::size_t pos,
                      BlockRef& block)
{
    if (size - pos < format::kSignature.size()) return false;
    if (std::memcmp(buf + pos, format::kSignature.data(), format::kSignature.size()) != 0) {
        return false;
    }

    Cursor cur(buf + pos, size - pos);
    if (!cur.skip(format::kSignature.size())) return false;

    std::uint64_t type = 0;
    std::string method;
    std::uint64_t orig = 0;
    std::uint64_t comp = 0;
    std::uint32_t crc = 0;
    if (!cur.vint(type) || !cur.cstr(method) || !cur.vint(orig) || !cur.vint(comp) ||
        !cur.u32(crc)) {
        return false;
    }

    const std::size_t covered = cur.pos();
    std::uint32_t descr_crc = 0;
    if (!cur.u32(descr_crc)) return false;
    if (crc32(buf + pos, covered) != descr_crc) return false;

    if (comp == 0 || orig == 0 || comp > pos) return false;

    block.type = static_cast<std::uint32_t>(type);
    block.method = std::move(method);
    block.orig_size = orig;
    block.comp_size = comp;
    block.crc = crc;
    block.pos = pos - comp;
    return true;
}

bool read_block(const std::uint8_t* buf, std::size_t size, const BlockRef& block,
                std::vector<std::uint8_t>& out, std::string& error)
{
    if (block.comp_size > SIZE_MAX || block.orig_size > SIZE_MAX) {
        error = "block too large";
        return false;
    }
    const std::size_t comp = static_cast<std::size_t>(block.comp_size);
    const std::size_t orig = static_cast<std::size_t>(block.orig_size);
    if (block.pos > size || size - static_cast<std::size_t>(block.pos) < comp) {
        error = "block data out of range";
        return false;
    }

    if (!decompress(block.method, buf + block.pos, comp, orig, out, error)) return false;
    if (crc32(out.data(), out.size()) != block.crc) {
        error = "block CRC mismatch";
        return false;
    }
    return true;
}

bool find_footer(const std::uint8_t* buf, std::size_t size, BlockRef& footer)
{
    if (size < 4) return false;
    const std::size_t window = std::min(format::kDescriptorSearchWindow, size);
    const std::size_t base = size - window;

    // Search backwards for the last valid descriptor, as arc.exe does, so a
    // false signature inside data cannot hijack the footer.
    for (std::size_t i = window - 4 + 1; i-- > 0;) {
        const std::size_t pos = base + i;
        if (buf[pos + 3] != format::kSignature[3]) continue;
        if (std::memcmp(buf + pos, format::kSignature.data(), format::kSignature.size()) !=
            0) {
            continue;
        }
        BlockRef candidate;
        if (parse_descriptor(buf, size, pos, candidate) && candidate.type == kFooterBlock) {
            footer = candidate;
            return true;
        }
    }
    return false;
}

bool parse_footer(const std::vector<std::uint8_t>& data, ArchiveInfo& info, std::string& error)
{
    Cursor cur(data.data(), data.size());

    std::uint64_t nblocks = 0;
    if (!cur.vint(nblocks) || nblocks > cur.remaining()) {
        error = "invalid control block count";
        return false;
    }

    info.blocks.resize(static_cast<std::size_t>(nblocks));
    for (auto& block : info.blocks) {
        std::uint64_t type = 0;
        std::uint64_t rel_pos = 0;
        if (!cur.vint(type) || !cur.cstr(block.method) || !cur.vint(rel_pos) ||
            !cur.vint(block.orig_size) || !cur.vint(block.comp_size) || !cur.u32(block.crc)) {
            error = "truncated footer block list";
            return false;
        }
        block.type = static_cast<std::uint32_t>(type);
        block.pos = rel_pos;  // relative to the footer; resolved by the caller
    }

    std::uint8_t locked = 0;
    if (!cur.u8(locked)) {
        error = "truncated footer (locked flag)";
        return false;
    }
    info.locked = locked != 0;

    std::uint64_t old_comment_len = 0;
    if (!cur.vint(old_comment_len) || old_comment_len > cur.remaining() / 4) {
        error = "invalid old comment length";
        return false;
    }
    if (!cur.skip(static_cast<std::size_t>(old_comment_len) * 4)) {
        error = "truncated old comment";
        return false;
    }

    if (!cur.eof()) {
        std::string recovery;
        if (!cur.cstr(recovery)) {
            error = "truncated recovery string";
            return false;
        }
    }
    if (!cur.eof()) {
        std::uint64_t comment_len = 0;
        if (!cur.vint(comment_len) || comment_len > cur.remaining()) {
            error = "invalid comment length";
            return false;
        }
        const std::size_t start = cur.pos();
        if (!cur.skip(static_cast<std::size_t>(comment_len))) {
            error = "truncated comment";
            return false;
        }
        info.comment.assign(reinterpret_cast<const char*>(data.data() + start),
                            static_cast<std::size_t>(comment_len));
    }
    return true;
}

std::string sanitize_path(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    std::size_t i = 0;
    while (i <= raw.size()) {
        const std::size_t end = raw.find_first_of("/\\", i);
        const std::string_view part =
            raw.substr(i, end == std::string_view::npos ? raw.size() - i : end - i);
        if (!part.empty() && part != "." && part != "..") {
            if (!out.empty()) out.push_back('/');
            out.append(part);
        }
        if (end == std::string_view::npos) break;
        i = end + 1;
    }
    return out;
}

bool parse_directory(const std::vector<std::uint8_t>& data, std::uint64_t dir_block_pos,
                     ArchiveInfo& info, std::string& error)
{
    Cursor cur(data.data(), data.size());

    std::uint64_t nblocks = 0;
    if (!cur.vint(nblocks) || nblocks > cur.remaining()) {
        error = "invalid data block count";
        return false;
    }

    auto& blocks = info.data_blocks;
    const std::size_t first_block = blocks.size();
    blocks.resize(first_block + static_cast<std::size_t>(nblocks));

    std::vector<std::uint64_t> counts(static_cast<std::size_t>(nblocks));
    std::uint64_t total = 0;
    for (auto& count : counts) {
        if (!cur.vint(count) || count > cur.remaining()) {
            error = "invalid file count";
            return false;
        }
        total += count;
        if (total > data.size()) {
            error = "invalid file count";
            return false;
        }
    }
    const std::size_t first_file = info.files.size();

    // Methods, relative offsets and compressed sizes, each in its own column.
    std::size_t file_cursor = 0;
    for (std::size_t b = 0; b < counts.size(); ++b) {
        auto& block = blocks[first_block + b];
        if (!cur.cstr(block.method)) {
            error = "truncated block method";
            return false;
        }
        block.file_count = static_cast<std::size_t>(counts[b]);
        block.first_file = first_file + file_cursor;
        file_cursor += static_cast<std::size_t>(counts[b]);
    }
    for (std::size_t b = 0; b < counts.size(); ++b) {
        auto& block = blocks[first_block + b];
        std::uint64_t rel = 0;
        if (!cur.vint(rel) || rel > dir_block_pos) {
            error = "invalid data block offset";
            return false;
        }
        block.pos = dir_block_pos - rel;
    }
    for (std::size_t b = 0; b < counts.size(); ++b) {
        auto& block = blocks[first_block + b];
        if (!cur.vint(block.comp_size)) {
            error = "truncated data block size";
            return false;
        }
    }

    std::uint64_t ndirs = 0;
    if (!cur.vint(ndirs) || ndirs > cur.remaining()) {
        error = "invalid directory count";
        return false;
    }
    std::vector<std::string> dirs(static_cast<std::size_t>(ndirs));
    for (auto& dir : dirs) {
        std::string raw;
        if (!cur.cstr(raw)) {
            error = "truncated directory name";
            return false;
        }
        dir = sanitize_path(raw);
    }

    if (total > cur.remaining()) {
        error = "invalid file count";
        return false;
    }
    const auto count = static_cast<std::size_t>(total);
    std::vector<FileEntry> files(count);

    for (auto& file : files) {
        std::string name;
        if (!cur.cstr(name)) {
            error = "truncated file name";
            return false;
        }
        file.name = sanitize_path(name);
    }

    for (auto& file : files) {
        std::uint64_t dir_index = 0;
        if (!cur.vint(dir_index) || dir_index >= dirs.size()) {
            error = "invalid directory index";
            return false;
        }
        if (!dirs[static_cast<std::size_t>(dir_index)].empty()) {
            file.name = dirs[static_cast<std::size_t>(dir_index)] + "/" + file.name;
        }
    }

    for (auto& file : files) {
        if (!cur.vint(file.size)) {
            error = "truncated file size";
            return false;
        }
    }
    for (auto& file : files) {
        std::uint32_t t = 0;
        if (!cur.u32(t)) {
            error = "truncated file time";
            return false;
        }
        file.mtime = static_cast<std::int32_t>(t);
    }
    for (auto& file : files) {
        std::uint8_t is_dir = 0;
        if (!cur.u8(is_dir)) {
            error = "truncated directory flag";
            return false;
        }
        file.is_dir = is_dir != 0;
    }
    for (auto& file : files) {
        if (!cur.u32(file.crc)) {
            error = "truncated file CRC";
            return false;
        }
    }

    // Optional fields: tag 0 ends the list; unknown optional tags are skipped,
    // unknown mandatory ones are an error (0.67 writes only the end tag).
    while (!cur.eof()) {
        std::uint64_t tag = 0;
        if (!cur.vint(tag)) {
            error = "truncated optional tag";
            return false;
        }
        if (tag == 0) break;
        std::uint8_t mandatory = 0;
        std::uint64_t len = 0;
        if (!cur.u8(mandatory) || !cur.vint(len) || len > cur.remaining()) {
            error = "truncated optional field";
            return false;
        }
        if (mandatory != 0) {
            error = "unknown mandatory directory field";
            return false;
        }
        if (!cur.skip(static_cast<std::size_t>(len))) {
            error = "truncated optional field";
            return false;
        }
    }

    // Attach each file to its block and compute its offset inside the solid block.
    info.files.insert(info.files.end(), files.begin(), files.end());
    for (std::size_t b = 0; b < counts.size(); ++b) {
        auto& block = blocks[first_block + b];
        std::uint64_t offset = 0;
        block.orig_size = 0;
        for (std::size_t i = 0; i < block.file_count; ++i) {
            FileEntry& file = info.files[block.first_file + i];
            file.data_block = first_block + b;
            file.offset = offset;
            offset += file.size;
        }
        block.orig_size = offset;
    }
    return true;
}

}  // namespace

bool Archive::open(const std::filesystem::path& path, Archive& archive, std::string& error)
{
    Archive result;
    if (!result.file_.open(path, error)) return false;

    BlockRef footer;
    if (!find_footer(result.file_.data(), result.file_.size(), footer)) {
        error = "'" + path.string() + "' is not a FreeArc archive (no valid footer)";
        return false;
    }

    std::vector<std::uint8_t> footer_data;
    if (!read_block(result.file_.data(), result.file_.size(), footer, footer_data, error)) return false;
    if (!parse_footer(footer_data, result.info_, error)) return false;

    for (auto& block : result.info_.blocks) {
        if (block.pos > footer.pos) {
            error = "invalid block position in footer";
            return false;
        }
        block.pos = footer.pos - block.pos;
    }

    result.info_.sfx_size = footer.pos;
    for (const auto& block : result.info_.blocks) {
        result.info_.sfx_size = std::min(result.info_.sfx_size, block.pos);
    }

    for (const auto& block : result.info_.blocks) {
        if (block.type != kHeaderBlock) continue;
        std::vector<std::uint8_t> header;
        if (!read_block(result.file_.data(), result.file_.size(), block, header, error)) return false;
        if (header.size() != format::kHeaderBlockSize ||
            std::memcmp(header.data(), format::kSignature.data(), format::kSignature.size()) != 0) {
            error = "invalid HEADER_BLOCK";
            return false;
        }
    }

    for (const auto& block : result.info_.blocks) {
        if (block.type != kDirBlock) continue;
        std::vector<std::uint8_t> dir;
        if (!read_block(result.file_.data(), result.file_.size(), block, dir, error)) return false;
        if (!parse_directory(dir, block.pos, result.info_, error)) return false;
    }

    archive = std::move(result);
    return true;
}

bool Archive::raw_data_block(std::size_t index, std::vector<std::uint8_t>& out,
                             std::string& error) const
{
    if (index >= info_.data_blocks.size()) {
        error = "data block index out of range";
        return false;
    }
    const DataBlock& block = info_.data_blocks[index];
    if (block.pos > file_.size() || file_.size() - static_cast<std::size_t>(block.pos) <
                                        static_cast<std::size_t>(block.comp_size)) {
        error = "data block out of range";
        return false;
    }
    out.assign(file_.data() + block.pos,
               file_.data() + block.pos + static_cast<std::size_t>(block.comp_size));
    return true;
}

bool Archive::read_data_block(std::size_t index, std::vector<std::uint8_t>& out,
                              std::string& error) const
{
    if (index >= info_.data_blocks.size()) {
        error = "data block index out of range";
        return false;
    }
    const DataBlock& block = info_.data_blocks[index];
    if (block.pos > file_.size() || file_.size() - static_cast<std::size_t>(block.pos) <
                                        static_cast<std::size_t>(block.comp_size)) {
        error = "data block out of range";
        return false;
    }
    return decompress(block.method, file_.data() + block.pos,
                      static_cast<std::size_t>(block.comp_size),
                      static_cast<std::size_t>(block.orig_size), out, error);
}

}  // namespace fa

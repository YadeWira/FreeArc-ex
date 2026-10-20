#include "fa/arc.h"

#include <cstring>
#include <fstream>
#include <optional>
#include <string_view>

#include "fa/crc32.h"
#include "fa/format.h"
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

    bool bytes(std::size_t n)
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

bool load_file(const std::filesystem::path& path, std::vector<std::uint8_t>& data,
               std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open '" + path.string() + "'";
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        error = "cannot determine size of '" + path.string() + "'";
        return false;
    }
    in.seekg(0, std::ios::beg);
    data.resize(static_cast<std::size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size)) {
        error = "cannot read '" + path.string() + "'";
        return false;
    }
    return true;
}

// Parses the local descriptor that starts at `pos`. On success `block.pos` is
// the absolute offset of the described data.
bool parse_descriptor(const std::vector<std::uint8_t>& buf, std::size_t pos, BlockRef& block)
{
    if (buf.size() - pos < format::kSignature.size()) return false;
    if (std::memcmp(buf.data() + pos, format::kSignature.data(), format::kSignature.size()) != 0) {
        return false;
    }

    Cursor cur(buf.data() + pos, buf.size() - pos);
    if (!cur.bytes(format::kSignature.size())) return false;

    std::uint64_t type = 0;
    std::string method;
    std::uint64_t orig = 0;
    std::uint64_t comp = 0;
    std::uint32_t crc = 0;
    if (!cur.vint(type) || !cur.cstr(method) || !cur.vint(orig) || !cur.vint(comp) ||
        !cur.u32(crc)) {
        return false;
    }

    // descr_crc covers signature..crc, i.e. everything consumed so far.
    const std::size_t covered = cur.pos();
    std::uint32_t descr_crc = 0;
    if (!cur.u32(descr_crc)) return false;
    if (crc32(buf.data() + pos, covered) != descr_crc) return false;

    if (comp == 0 || orig == 0) return false;
    if (comp > pos) return false;

    block.type = static_cast<std::uint32_t>(type);
    block.method = std::move(method);
    block.orig_size = orig;
    block.comp_size = comp;
    block.crc = crc;
    block.pos = pos - comp;
    return true;
}

// Returns the decompressed contents of a control block. Only `storing` is
// supported in M1; anything else is reported as unsupported, never guessed.
bool read_block(const std::vector<std::uint8_t>& buf, const BlockRef& block,
                std::vector<std::uint8_t>& out, std::string& error)
{
    if (block.orig_size > SIZE_MAX || block.comp_size > SIZE_MAX) {
        error = "block too large";
        return false;
    }
    const std::size_t comp = static_cast<std::size_t>(block.comp_size);
    const std::size_t orig = static_cast<std::size_t>(block.orig_size);
    if (block.pos > buf.size() || buf.size() - block.pos < comp) {
        error = "block data out of range";
        return false;
    }

    if (block.method != "storing") {
        error = "unsupported control-block method '" + block.method +
                "' (only storing in M1)";
        return false;
    }
    if (orig != comp) {
        error = "storing block with orig_size != comp_size";
        return false;
    }

    out.assign(buf.begin() + static_cast<std::ptrdiff_t>(block.pos),
               buf.begin() + static_cast<std::ptrdiff_t>(block.pos + comp));
    if (crc32(out.data(), out.size()) != block.crc) {
        error = "block CRC mismatch";
        return false;
    }
    return true;
}

bool find_footer(const std::vector<std::uint8_t>& buf, BlockRef& footer)
{
    if (buf.size() < 4) return false;
    const std::size_t window = std::min(format::kDescriptorSearchWindow, buf.size());
    const std::size_t base = buf.size() - window;

    // Search backwards for the last valid descriptor, as arc.exe does, so a
    // false signature inside data cannot hijack the footer.
    for (std::size_t i = window - 4 + 1; i-- > 0;) {
        const std::size_t pos = base + i;
        if (buf[pos + 3] != format::kSignature[3]) continue;
        if (std::memcmp(buf.data() + pos, format::kSignature.data(), format::kSignature.size()) !=
            0) {
            continue;
        }
        BlockRef candidate;
        if (parse_descriptor(buf, pos, candidate) && candidate.type == kFooterBlock) {
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
        block.pos = rel_pos;  // resolved by the caller, relative to the footer position
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
    if (!cur.bytes(static_cast<std::size_t>(old_comment_len) * 4)) {
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
        if (!cur.bytes(static_cast<std::size_t>(comment_len))) {
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

bool parse_directory(const std::vector<std::uint8_t>& data, ArchiveInfo& info, std::string& error)
{
    Cursor cur(data.data(), data.size());

    std::uint64_t nblocks = 0;
    if (!cur.vint(nblocks) || nblocks > cur.remaining()) {
        error = "invalid data block count";
        return false;
    }

    std::vector<std::uint64_t> counts(static_cast<std::size_t>(nblocks));
    std::uint64_t total = 0;
    for (auto& count : counts) {
        if (!cur.vint(count) || count > cur.remaining()) {
            error = "invalid file count";
            return false;
        }
        total += count;
        if (total > data.size()) {  // sanity: never allocate more than the block
            error = "invalid file count";
            return false;
        }
    }

    for (std::uint64_t i = 0; i < nblocks; ++i) {
        std::string method;
        if (!cur.cstr(method)) {
            error = "truncated block method";
            return false;
        }
    }
    for (std::uint64_t i = 0; i < nblocks; ++i) {
        std::uint64_t rel = 0;
        if (!cur.vint(rel)) {
            error = "truncated data block offset";
            return false;
        }
    }
    for (std::uint64_t i = 0; i < nblocks; ++i) {
        std::uint64_t comp = 0;
        if (!cur.vint(comp)) {
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

    // Optional fields: a tag 0 ends the list; unknown optional tags are skipped,
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
        if (!cur.bytes(static_cast<std::size_t>(len))) {
            error = "truncated optional field";
            return false;
        }
    }

    info.files.insert(info.files.end(), files.begin(), files.end());
    return true;
}

}  // namespace

bool open_archive(const std::filesystem::path& path, ArchiveInfo& info, std::string& error)
{
    std::vector<std::uint8_t> buf;
    if (!load_file(path, buf, error)) return false;

    BlockRef footer;
    if (!find_footer(buf, footer)) {
        error = "'" + path.string() + "' is not a FreeArc archive (no valid footer)";
        return false;
    }

    std::vector<std::uint8_t> footer_data;
    if (!read_block(buf, footer, footer_data, error)) return false;
    if (!parse_footer(footer_data, info, error)) return false;

    // Block positions in the footer are relative to the footer descriptor.
    for (auto& block : info.blocks) {
        if (block.pos > footer.pos + format::kSignature.size()) {
            error = "invalid block position in footer";
            return false;
        }
        block.pos = footer.pos - block.pos;
    }

    info.sfx_size = footer.pos;
    for (const auto& block : info.blocks) {
        info.sfx_size = std::min(info.sfx_size, block.pos);
    }

    for (const auto& block : info.blocks) {
        if (block.type == kHeaderBlock) {
            std::vector<std::uint8_t> header;
            if (!read_block(buf, block, header, error)) return false;
            if (header.size() != format::kHeaderBlockSize ||
                std::memcmp(header.data(), format::kSignature.data(), format::kSignature.size()) !=
                    0) {
                error = "invalid HEADER_BLOCK";
                return false;
            }
        }
    }

    for (const auto& block : info.blocks) {
        if (block.type != kDirBlock) continue;
        std::vector<std::uint8_t> dir;
        if (!read_block(buf, block, dir, error)) return false;
        if (!parse_directory(dir, info, error)) return false;
    }

    return true;
}

}  // namespace fa

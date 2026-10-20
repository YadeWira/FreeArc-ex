#include "fa/arc_writer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>

#include "fa/arc.h"
#include "fa/codec.h"
#include "fa/crc32.h"
#include "fa/format.h"

#include "fa/method.h"
#include "fa/vint.h"

namespace fa {
namespace {

struct Entry {
    std::string name;  // full path inside the archive, '/'-separated
    std::string base;  // last component
    std::string dir;   // parent directory, "" for the root
    bool is_dir = false;
    std::uint64_t size = 0;
    std::int32_t mtime = 0;
    std::uint32_t crc = 0;
    std::vector<std::uint8_t> data;
};

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void put_cstr(std::vector<std::uint8_t>& out, const std::string& text)
{
    out.insert(out.end(), text.begin(), text.end());
    out.push_back(0);
}

std::int32_t to_unix_time(std::filesystem::file_time_type ft)
{
    using namespace std::chrono;
    const auto system = time_point_cast<system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + system_clock::now());
    return static_cast<std::int32_t>(system_clock::to_time_t(system));
}

std::string normalize_name(const std::filesystem::path& path)
{
    std::string text = path.generic_string();
    while (text.rfind("./", 0) == 0) text.erase(0, 2);
    while (!text.empty() && text.back() == '/') text.pop_back();
    while (!text.empty() && text.front() == '/') text.erase(0, 1);
    return text;
}

bool read_file(const std::filesystem::path& path, std::vector<std::uint8_t>& data, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot read '" + path.string() + "'";
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

bool add_path(const std::filesystem::path& disk_path, const std::string& name,
              std::vector<Entry>& entries, std::string& error)
{
    std::error_code ec;
    const auto status = std::filesystem::status(disk_path, ec);
    if (ec) {
        error = "cannot stat '" + disk_path.string() + "': " + ec.message();
        return false;
    }

    Entry entry;
    entry.name = name;
    const std::size_t slash = name.rfind('/');
    entry.base = (slash == std::string::npos) ? name : name.substr(slash + 1);
    entry.dir = (slash == std::string::npos) ? "" : name.substr(0, slash);
    entry.mtime = to_unix_time(std::filesystem::last_write_time(disk_path, ec));

    if (std::filesystem::is_directory(status)) {
        entry.is_dir = true;
        entries.push_back(std::move(entry));

        std::vector<std::filesystem::path> children;
        for (const auto& child : std::filesystem::directory_iterator(disk_path, ec)) {
            children.push_back(child.path());
        }
        if (ec) {
            error = "cannot list '" + disk_path.string() + "': " + ec.message();
            return false;
        }
        std::sort(children.begin(), children.end());
        for (const auto& child : children) {
            if (!add_path(child, name + "/" + child.filename().generic_string(), entries, error)) {
                return false;
            }
        }
        return true;
    }

    if (!std::filesystem::is_regular_file(status)) {
        // Symlinks, sockets and devices are skipped for now.
        return true;
    }

    if (!read_file(disk_path, entry.data, error)) return false;
    entry.size = entry.data.size();
    entry.crc = crc32(entry.data.data(), entry.data.size());
    entries.push_back(std::move(entry));
    return true;
}

std::vector<std::uint8_t> make_descriptor(std::uint32_t type, const std::string& method,
                                          std::uint64_t orig, std::uint64_t comp,
                                          std::uint32_t crc)
{
    std::vector<std::uint8_t> out(format::kSignature.begin(), format::kSignature.end());
    write_vint(out, type);
    put_cstr(out, method);
    write_vint(out, orig);
    write_vint(out, comp);
    put_u32(out, crc);
    put_u32(out, crc32(out.data(), out.size()));
    return out;
}

}  // namespace

bool create_archive(const std::filesystem::path& archive,
                    const std::vector<std::filesystem::path>& inputs, const AddOptions& options,
                    std::string& error)
{
    std::vector<Entry> entries;
    for (const auto& input : inputs) {
        const std::string name = normalize_name(input);
        if (name.empty()) {
            error = "invalid input path '" + input.string() + "'";
            return false;
        }
        if (!add_path(input, name, entries, error)) return false;
    }
    if (entries.empty()) {
        error = "nothing to add";
        return false;
    }

    // Directory table: index 0 is the root, and every parent directory must be
    // registered before the names are written.
    std::vector<std::string> dirs{""};
    auto dir_index = [&dirs](const std::string& dir) {
        const auto it = std::find(dirs.begin(), dirs.end(), dir);
        if (it != dirs.end()) return static_cast<std::uint64_t>(it - dirs.begin());
        dirs.push_back(dir);
        return static_cast<std::uint64_t>(dirs.size() - 1);
    };
    for (const auto& entry : entries) {
        if (!entry.dir.empty()) dir_index(entry.dir);
    }

    // Resolve the method for each entry: directories carry no data and go to a
    // storing block; files are classified (arc.groups) and the preset or name is
    // expanded for that class.
    std::vector<std::string> method_of(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].is_dir) {
            method_of[i] = "storing";
            continue;
        }
        const std::string cls =
            options.classes != nullptr ? options.classes->classify(entries[i].name) : "$obj";
        if (options.table != nullptr) {
            method_of[i] = options.table->resolve(options.method, cls);
        } else {
            method_of[i] = options.method;
        }
        if (method_of[i] == "0") method_of[i] = "storing";
        if (!codec_method_supported(method_of[i])) {
            error = "the preset resolves to '" + method_of[i] + "' for '" + entries[i].name +
                    "', but that method is not supported on this architecture yet "
                    "(see docs/COMPAT.md)";
            return false;
        }
    }

    // One solid block per distinct method, in first-seen order.
    struct Block {
        std::string method;
        std::vector<std::size_t> entries;
        std::vector<std::uint8_t> plain;
        std::vector<std::uint8_t> packed;
        std::uint64_t pos = 0;
    };
    std::vector<Block> blocks;
    std::map<std::string, std::size_t> block_of_method;

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::string& method = method_of[i];
        auto it = block_of_method.find(method);
        if (it == block_of_method.end()) {
            block_of_method[method] = blocks.size();
            it = block_of_method.find(method);
            blocks.push_back(Block{method, {}, {}, {}, 0});
        }
        const std::size_t b = it->second;
        blocks[b].entries.push_back(i);
        if (!entries[i].is_dir) {
            blocks[b].plain.insert(blocks[b].plain.end(), entries[i].data.begin(),
                                   entries[i].data.end());
        }
    }

    // The directory stores the files grouped by block: the entries of a block
    // must be contiguous in the file list, so the order is rebuilt from the
    // blocks (which is also what makes the per-block offsets correct).
    std::vector<std::size_t> order;
    order.reserve(entries.size());
    for (const auto& block : blocks) {
        order.insert(order.end(), block.entries.begin(), block.entries.end());
    }

    // Compress each block. Dictionaries are limited to the block size, like the
    // original ("rep:96m" becomes "rep:10kb" on a small block).
    for (auto& block : blocks) {
        if (block.plain.empty() || block.method == "storing" || block.method == "0") {
            block.method = "storing";
            block.packed = block.plain;
            continue;
        }
        std::string limited;
        const std::uint64_t dict = block.plain.size() + block.plain.size() / 100 + 512;
        if (!codec_limit_dictionary(block.method, dict, limited, error)) return false;
        block.method = limited;
        if (!codec_compress(block.method, block.plain.data(), block.plain.size(), block.packed,
                            error)) {
            return false;
        }
    }

    std::vector<std::uint8_t> header(format::kSignature.begin(), format::kSignature.end());
    header.insert(header.end(), format::kVersionBytes.begin(), format::kVersionBytes.end());

    const auto header_descr = make_descriptor(kHeaderBlock, "storing", header.size(),
                                              header.size(),
                                              crc32(header.data(), header.size()));
    const std::uint64_t header_total = header.size() + header_descr.size();

    // Block positions: right after the header, in block order.
    std::uint64_t pos = header_total;
    for (auto& block : blocks) {
        block.pos = pos;
        pos += block.packed.size();
    }
    const std::uint64_t dir_pos = pos;

    // Directory data: one entry per block, then the column-wise file fields. The
    // per-block offsets are relative to the directory block's data position,
    // which is known once every block is compressed.
    auto build_dir_data_full = [&]() {
        std::vector<std::uint8_t> out;
        write_vint(out, blocks.size());
        for (const auto& block : blocks) write_vint(out, block.entries.size());
        for (const auto& block : blocks) put_cstr(out, block.method);
        for (const auto& block : blocks) write_vint(out, dir_pos - block.pos);
        for (const auto& block : blocks) write_vint(out, block.packed.size());

        write_vint(out, dirs.size());
        for (const auto& dir : dirs) put_cstr(out, dir);
        for (const auto index : order) put_cstr(out, entries[index].base);
        for (const auto index : order) write_vint(out, dir_index(entries[index].dir));
        for (const auto index : order) write_vint(out, entries[index].size);
        for (const auto index : order)
            put_u32(out, static_cast<std::uint32_t>(entries[index].mtime));
        for (const auto index : order) out.push_back(entries[index].is_dir ? 1 : 0);
        for (const auto index : order) put_u32(out, entries[index].crc);
        write_vint(out, 0);
        return out;
    };
    std::vector<std::uint8_t> dir_data = build_dir_data_full();

    const auto dir_descr =
        make_descriptor(kDirBlock, "storing", dir_data.size(), dir_data.size(),
                        crc32(dir_data.data(), dir_data.size()));
    const std::uint64_t before_footer = dir_pos + dir_data.size() + dir_descr.size();

    std::vector<std::uint8_t> footer_data;
    write_vint(footer_data, 2);  // HEADER and DIR
    write_vint(footer_data, kHeaderBlock);
    put_cstr(footer_data, "storing");
    write_vint(footer_data, before_footer);
    write_vint(footer_data, header.size());
    write_vint(footer_data, header.size());
    put_u32(footer_data, crc32(header.data(), header.size()));

    write_vint(footer_data, kDirBlock);
    put_cstr(footer_data, "storing");
    write_vint(footer_data, before_footer - dir_pos);
    write_vint(footer_data, dir_data.size());
    write_vint(footer_data, dir_data.size());
    put_u32(footer_data, crc32(dir_data.data(), dir_data.size()));

    footer_data.push_back(0);    // not locked
    write_vint(footer_data, 0);  // old comment length

    const auto footer_descr =
        make_descriptor(kFooterBlock, "storing", footer_data.size(), footer_data.size(),
                        crc32(footer_data.data(), footer_data.size()));

    std::ofstream out(archive, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot create '" + archive.string() + "'";
        return false;
    }
    auto write_bytes = [&out](const std::vector<std::uint8_t>& bytes) {
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
        return static_cast<bool>(out);
    };

    bool ok = write_bytes(header) && write_bytes(header_descr);
    for (const auto& block : blocks) {
        ok = ok && write_bytes(block.packed);
    }
    if (!ok || !write_bytes(dir_data) || !write_bytes(dir_descr) || !write_bytes(footer_data) ||
        !write_bytes(footer_descr)) {
        error = "write error on '" + archive.string() + "'";
        return false;
    }
    out.close();
    if (!out) {
        error = "write error on '" + archive.string() + "'";
        return false;
    }
    return true;
}

}  // namespace fa

#include "fa/arc_writer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>

#include "fa/arc.h"
#include "fa/crc32.h"
#include "fa/format.h"
#include "fa/lzma_encode.h"
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

std::uint32_t dictionary_for(std::size_t size)
{
    return lzma_dictionary_for(size);
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

    // Solid data: all file contents concatenated, in directory order.
    std::vector<std::uint8_t> plain;
    for (const auto& entry : entries) {
        if (!entry.is_dir) plain.insert(plain.end(), entry.data.begin(), entry.data.end());
    }

    std::string data_method = "storing";
    std::vector<std::uint8_t> packed;
    if (options.method == "storing" || options.method == "0" || plain.empty()) {
        packed = plain;
    } else {
        std::string name;
        LzmaParams parsed;
        if (!parse_method(options.method, name, parsed) || name != "lzma") {
            error = "unsupported compression method '" + options.method +
                    "' (M1 supports storing and lzma)";
            return false;
        }
        LzmaEncodeParams params;
        params.dict_size = parsed.dict_size == 64u * 1024u * 1024u
                               ? dictionary_for(plain.size())
                               : parsed.dict_size;
        params.lc = static_cast<unsigned>(parsed.lc);
        params.lp = static_cast<unsigned>(parsed.lp);
        params.pb = static_cast<unsigned>(parsed.pb);
        if (!lzma_compress(plain.data(), plain.size(), params, packed, error)) return false;
        data_method = lzma_method_text(params);
    }

    // Directory data: one data block holding every entry, then column-wise fields.
    std::vector<std::string> dirs{""};
    auto dir_index = [&dirs](const std::string& dir) {
        const auto it = std::find(dirs.begin(), dirs.end(), dir);
        if (it != dirs.end()) return static_cast<std::uint64_t>(it - dirs.begin());
        dirs.push_back(dir);
        return static_cast<std::uint64_t>(dirs.size() - 1);
    };

    auto build_dir_data = [&](std::uint64_t data_rel) {
        std::vector<std::uint8_t> out;
        write_vint(out, 1);  // one data block
        write_vint(out, entries.size());
        put_cstr(out, data_method);
        write_vint(out, data_rel);
        write_vint(out, packed.size());

        write_vint(out, dirs.size());
        for (const auto& dir : dirs) put_cstr(out, dir);

        for (const auto& entry : entries) put_cstr(out, entry.base);
        for (const auto& entry : entries) write_vint(out, dir_index(entry.dir));
        for (const auto& entry : entries) write_vint(out, entry.size);
        for (const auto& entry : entries) put_u32(out, static_cast<std::uint32_t>(entry.mtime));
        for (const auto& entry : entries) out.push_back(entry.is_dir ? 1 : 0);
        for (const auto& entry : entries) put_u32(out, entry.crc);
        write_vint(out, 0);  // no optional fields
        return out;
    };

    // Register every parent directory first: the count and the names must be
    // written after the list is final.
    for (const auto& entry : entries) {
        if (!entry.dir.empty()) dir_index(entry.dir);
    }

    // HEADER: 8 bytes.
    std::vector<std::uint8_t> header(format::kSignature.begin(), format::kSignature.end());
    header.insert(header.end(), format::kVersionBytes.begin(), format::kVersionBytes.end());

    const auto header_descr = make_descriptor(kHeaderBlock, "storing", header.size(),
                                              header.size(),
                                              crc32(header.data(), header.size()));
    const std::uint64_t header_total = header.size() + header_descr.size();
    const std::uint64_t data_pos = header_total;
    const std::uint64_t dir_pos = data_pos + packed.size();

    // Both offsets are relative to the *data* position of the referring block
    // (that is what the reference reader does), and both are known before
    // writing: no fixed point is needed.
    const std::uint64_t data_rel = dir_pos - data_pos;
    const std::vector<std::uint8_t> dir_data = build_dir_data(data_rel);
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

    if (!write_bytes(header) || !write_bytes(header_descr) || !write_bytes(packed) ||
        !write_bytes(dir_data) || !write_bytes(dir_descr) || !write_bytes(footer_data) ||
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

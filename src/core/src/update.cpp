#include "fa/update.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>

#include "fa/arc.h"
#include "fa/preset.h"

namespace fa {
namespace {

std::int32_t to_unix_time(std::filesystem::file_time_type ft)
{
    using namespace std::chrono;
    const auto system = time_point_cast<system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + system_clock::now());
    return static_cast<std::int32_t>(system_clock::to_time_t(system));
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

std::string apply_path_mode(const std::string& name, int mode)
{
    if (mode <= 0) return name;
    if (mode >= 9) {
        const std::size_t slash = name.find_last_of('/');
        return slash == std::string::npos ? name : name.substr(slash + 1);
    }
    std::string out = name;
    for (int i = 0; i < mode; ++i) {
        const std::size_t slash = out.find('/');
        if (slash == std::string::npos) break;
        out.erase(0, slash + 1);
    }
    return out.empty() ? name : out;
}

std::string normalize_name(const std::filesystem::path& path)
{
    std::string text = path.generic_string();
    while (text.rfind("./", 0) == 0) text.erase(0, 2);
    while (!text.empty() && text.back() == '/') text.pop_back();
    while (!text.empty() && text.front() == '/') text.erase(0, 1);
    return text;
}

std::string basename_of(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool glob_match(const std::string& pattern, const std::string& text)
{
    std::size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

struct DiskEntry {
    std::string name;
    bool is_dir = false;
    std::int32_t mtime = 0;
    std::vector<std::uint8_t> data;
};

bool walk_path(const std::filesystem::path& disk_path, const std::string& name,
               std::vector<DiskEntry>& out, std::string& error, int path_mode)
{
    std::error_code ec;
    const auto status = std::filesystem::status(disk_path, ec);
    if (ec) {
        error = "cannot stat '" + disk_path.string() + "': " + ec.message();
        return false;
    }

    DiskEntry entry;
    entry.name = apply_path_mode(name, path_mode);
    entry.mtime = to_unix_time(std::filesystem::last_write_time(disk_path, ec));

    if (std::filesystem::is_directory(status)) {
        entry.is_dir = true;
        out.push_back(std::move(entry));
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
            if (!walk_path(child, name + "/" + child.filename().generic_string(), out, error,
                           path_mode)) {
                return false;
            }
        }
        return true;
    }
    if (!std::filesystem::is_regular_file(status)) return true;
    if (!read_file(disk_path, entry.data, error)) return false;
    out.push_back(std::move(entry));
    return true;
}

}  // namespace

bool update_archive(const std::filesystem::path& archive,
                    const std::vector<std::filesystem::path>& inputs, UpdateMode mode,
                    const AddOptions& options, std::string& error)
{
    Archive old;
    if (!Archive::open(archive, old, error)) return false;
    const ArchiveInfo& info = old.info();

    // Old entries, in archive order, with their data resolved from the blocks.
    struct OldEntry {
        std::string name;
        bool is_dir = false;
        std::int32_t mtime = 0;
        std::size_t block = 0;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };
    std::vector<OldEntry> olds;
    olds.reserve(info.files.size());
    for (const auto& file : info.files) {
        olds.push_back({file.name, file.is_dir, file.mtime, file.data_block, file.offset,
                        file.size});
    }

    std::vector<std::vector<std::uint8_t>> block_cache(info.data_blocks.size());
    std::vector<bool> block_loaded(info.data_blocks.size(), false);
    auto old_file_data = [&](const OldEntry& entry, std::vector<std::uint8_t>& out,
                             std::string& err) -> bool {
        if (entry.is_dir) {
            out.clear();
            return true;
        }
        if (entry.block >= info.data_blocks.size()) {
            err = "invalid block for '" + entry.name + "'";
            return false;
        }
        if (!block_loaded[entry.block]) {
            if (!old.read_data_block(entry.block, block_cache[entry.block], err)) return false;
            block_loaded[entry.block] = true;
        }
        const auto& block = block_cache[entry.block];
        if (entry.offset + entry.size > block.size()) {
            err = "'" + entry.name + "' outside its block";
            return false;
        }
        out.assign(block.begin() + static_cast<std::ptrdiff_t>(entry.offset),
                   block.begin() + static_cast<std::ptrdiff_t>(entry.offset + entry.size));
        return true;
    };

    // Deletion: drop the entries that match any input (name, basename or glob).
    if (mode == UpdateMode::Delete) {
        std::vector<NewEntry> keep;
        for (const auto& entry : olds) {
            bool matched = false;
            for (const auto& input : inputs) {
                const std::string pattern = normalize_name(input);
                if (entry.name == pattern || basename_of(entry.name) == pattern ||
                    glob_match(pattern, entry.name)) {
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            NewEntry out;
            out.name = entry.name;
            out.is_dir = entry.is_dir;
            out.mtime = entry.mtime;
            if (!old_file_data(entry, out.data, error)) return false;
            keep.push_back(std::move(out));
        }
        if (keep.empty()) {
            error = "nothing left in the archive";
            return false;
        }
        old.close();
        return write_archive(archive, std::move(keep), options, error);
    }

    // Update/freshen: merge the disk tree with the archive contents.
    std::vector<DiskEntry> disk;
    for (const auto& input : inputs) {
        const std::string name = normalize_name(input);
        if (name.empty()) {
            error = "invalid input path '" + input.string() + "'";
            return false;
        }
        if (!walk_path(input, name, disk, error, options.path_mode)) return false;
    }
    std::map<std::string, const DiskEntry*> disk_by_name;
    for (const auto& entry : disk) disk_by_name[entry.name] = &entry;

    std::vector<NewEntry> result;
    std::map<std::string, bool> from_disk;
    std::vector<bool> unchanged(olds.size(), false);

    for (const auto& entry : olds) {
        const auto it = disk_by_name.find(entry.name);
        if (it == disk_by_name.end()) {
            // Not on disk: keep it (both u and f keep existing entries).
            NewEntry out;
            out.name = entry.name;
            out.is_dir = entry.is_dir;
            out.mtime = entry.mtime;
            if (!old_file_data(entry, out.data, error)) return false;
            result.push_back(std::move(out));
            continue;
        }
        const DiskEntry& current = *it->second;
        const bool changed =
            current.is_dir != entry.is_dir || (!entry.is_dir && current.data.size() != entry.size) ||
            current.mtime != entry.mtime;
        if (changed) {
            NewEntry out;
            out.name = current.name;
            out.is_dir = current.is_dir;
            out.mtime = current.mtime;
            out.data = current.data;
            result.push_back(std::move(out));
        } else {
            NewEntry out;
            out.name = entry.name;
            out.is_dir = entry.is_dir;
            out.mtime = entry.mtime;
            if (!old_file_data(entry, out.data, error)) return false;
            result.push_back(std::move(out));
            unchanged[&entry - olds.data()] = true;
        }
        from_disk[entry.name] = true;
    }

    if (mode == UpdateMode::Update) {
        for (const auto& entry : disk) {
            if (from_disk.count(entry.name) != 0) continue;
            NewEntry out;
            out.name = entry.name;
            out.is_dir = entry.is_dir;
            out.mtime = entry.mtime;
            out.data = entry.data;
            result.push_back(std::move(out));
        }
    }

    if (result.empty()) {
        error = "nothing to update";
        return false;
    }
    // Blocks whose files are all unchanged are copied verbatim instead of being
    // recompressed; that is what makes an update cheap on big archives.
    std::map<std::string, bool> is_unchanged;
    for (std::size_t i = 0; i < olds.size(); ++i) {
        if (unchanged[i]) is_unchanged[olds[i].name] = true;
    }

    std::vector<PreservedBlock> preserved;
    for (std::size_t b = 0; b < info.data_blocks.size(); ++b) {
        const DataBlock& block = info.data_blocks[b];
        if (block.file_count == 0) continue;
        PreservedBlock candidate;
        candidate.method = block.method;
        bool reusable = true;
        for (std::size_t i = 0; i < block.file_count; ++i) {
            const FileEntry& file = info.files[block.first_file + i];
            if (file.is_dir || is_unchanged.count(file.name) == 0) {
                reusable = false;
                break;
            }
            candidate.files.push_back(file.name);
        }
        if (!reusable) continue;
        if (!old.raw_data_block(b, candidate.packed, error)) return false;
        candidate.orig_size = block.orig_size;
        preserved.push_back(std::move(candidate));
    }

    old.close();
    return write_archive(archive, std::move(result), preserved, options, error);
}

}  // namespace fa

#include "fa/ops.h"

#include <algorithm>
#include <fstream>

#include "fa/arc.h"
#include "fa/crc32.h"

namespace fa {
namespace {

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

std::string basename_of(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool matches_any(const std::vector<std::string>& masks, const std::string& name)
{
    for (const auto& mask : masks) {
        if (glob_match(mask, name) || glob_match(mask, basename_of(name))) return true;
    }
    return false;
}

bool selected(const ExtractOptions& options, const std::string& name)
{
    if (!options.masks.empty() && !matches_any(options.masks, name)) return false;
    if (!options.excludes.empty() && matches_any(options.excludes, name)) return false;
    return true;
}

}  // namespace

bool test_archive(const std::filesystem::path& path, const ProgressFn& progress, bool& cancelled,
                  std::string& error)
{
    Archive archive;
    if (!Archive::open(path, archive, error)) return false;

    const ArchiveInfo& info = archive.info();
    for (std::size_t b = 0; b < info.data_blocks.size(); ++b) {
        std::vector<std::uint8_t> data;
        if (!archive.read_data_block(b, data, error)) return false;
        const DataBlock& block = info.data_blocks[b];
        for (std::size_t i = 0; i < block.file_count; ++i) {
            const FileEntry& file = info.files[block.first_file + i];
            if (file.is_dir) continue;
            if (file.offset + file.size > data.size()) {
                error = "'" + file.name + "' outside its block";
                return false;
            }
            if (crc32(data.data() + file.offset, static_cast<std::size_t>(file.size)) != file.crc) {
                error = "'" + file.name + "': CRC mismatch";
                return false;
            }
        }
    }
    if (progress && !progress(1, 1, "")) {
        cancelled = true;
        return false;
    }
    return true;
}

bool extract_archive(const std::filesystem::path& path, const ExtractOptions& options,
                     const ProgressFn& progress, bool& cancelled, std::string& error)
{
    Archive archive;
    if (!Archive::open(path, archive, error)) return false;

    const ArchiveInfo& info = archive.info();
    std::uint64_t total = 0;
    for (const auto& file : info.files) {
        if (!file.is_dir && selected(options, file.name)) total += file.size;
    }
    std::uint64_t done = 0;

    // Directories first, so empty ones are created too.
    for (const auto& file : info.files) {
        if (!file.is_dir || !selected(options, file.name)) continue;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(options.output_dir) / file.name,
                                            ec);
        if (ec) {
            error = "cannot create '" + file.name + "': " + ec.message();
            return false;
        }
    }

    for (std::size_t b = 0; b < info.data_blocks.size(); ++b) {
        std::vector<std::uint8_t> data;
        if (!archive.read_data_block(b, data, error)) return false;
        const DataBlock& block = info.data_blocks[b];
        for (std::size_t i = 0; i < block.file_count; ++i) {
            const FileEntry& file = info.files[block.first_file + i];
            if (file.is_dir || !selected(options, file.name)) continue;
            if (file.offset + file.size > data.size()) {
                error = "'" + file.name + "' outside its block";
                return false;
            }
            if (progress && !progress(done, total, file.name)) {
                cancelled = true;
                return false;
            }
            const auto* payload = data.data() + file.offset;
            const auto size = static_cast<std::size_t>(file.size);
            if (crc32(payload, size) != file.crc) {
                error = "'" + file.name + "': CRC mismatch";
                return false;
            }

            const std::filesystem::path target =
                std::filesystem::path(options.output_dir) / file.name;
            std::error_code ec;
            if (target.has_parent_path()) {
                std::filesystem::create_directories(target.parent_path(), ec);
                if (ec) {
                    error = "cannot create '" + target.parent_path().string() + "'";
                    return false;
                }
            }
            if (!options.overwrite && std::filesystem::exists(target, ec)) {
                done += file.size;
                continue;
            }
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) {
                error = "cannot write '" + target.string() + "'";
                return false;
            }
            out.write(reinterpret_cast<const char*>(payload), static_cast<std::streamsize>(size));
            if (!out) {
                error = "write error on '" + target.string() + "'";
                return false;
            }
            done += file.size;
        }
    }
    if (progress) progress(total, total, "");
    return true;
}

}  // namespace fa

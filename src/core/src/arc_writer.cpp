#include "fa/arc_writer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>

#include "fa/arc.h"
#include "fa/codec.h"
#include "fa/crc32.h"
#include "fa/crypto.h"
#include "fa/format.h"

#include "fa/method.h"
#include "fa/vint.h"

namespace fa {
namespace {

using Entry = NewEntry;

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

// -ep: 0 keeps the path, 9 (or "--") keeps only the base name, N strips N
// leading components.
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
              std::vector<Entry>& entries, std::string& error, int path_mode)
{
    std::error_code ec;
    const auto status = std::filesystem::status(disk_path, ec);
    if (ec) {
        error = "cannot stat '" + disk_path.string() + "': " + ec.message();
        return false;
    }

    Entry entry;
    entry.name = apply_path_mode(name, path_mode);
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
            if (!add_path(child, name + "/" + child.filename().generic_string(), entries, error,
                          path_mode)) {
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
                                          std::uint32_t crc);

struct Span {
    const std::uint8_t* data;
    std::size_t size;
};

// Builds the recovery record: rotating XOR of the protected sectors plus a CRC
// per data sector and per recovery sector (docs/format/ARC-FORMAT.md, 7.4).
bool build_recovery(const std::string& text, const std::vector<Span>& spans, std::uint64_t arcsize,
                    std::vector<std::uint8_t>& sectors_out, std::vector<std::uint8_t>& crcs_out,
                    std::string& error)
{
    // Parse the amount requested by -rr.
    std::uint64_t wanted = 0;
    std::uint64_t forced_sector = 0;
    const std::size_t semi = text.find(';');
    const std::size_t star = text.find('*');
    if (semi != std::string::npos) {
        wanted = parse_mem(text.substr(0, semi));
        forced_sector = parse_mem(text.substr(semi + 1));
    } else if (star != std::string::npos) {
        const std::uint64_t count = parse_mem(text.substr(0, star));
        forced_sector = parse_mem(text.substr(star + 1));
        wanted = count * forced_sector;
    } else if (!text.empty() && (text.back() == '%' || text.back() == 'p')) {
        const std::uint64_t percent = parse_mem(text.substr(0, text.size() - 1));
        wanted = (arcsize * percent + 99) / 100;  // divRoundUp
    } else {
        const std::uint64_t n = parse_mem(text);
        wanted = n * 512;  // no suffix or "s" means sectors of 512 bytes
    }

    std::uint64_t sector_size = forced_sector;
    if (sector_size == 0) {
        if (wanted == 0) {
            sector_size = 4096;
        } else {
            const std::uint64_t ratio = (40 * arcsize) / wanted;
            if (ratio <= 1) {
                sector_size = 512;
            } else {
                sector_size = 1;
                while ((sector_size << 1) <= ratio) sector_size <<= 1;
            }
            if (sector_size < 512) sector_size = 512;
        }
    }

    const std::uint64_t arc_sectors = (arcsize + sector_size - 1) / sector_size;
    const std::uint64_t crcs_size0 = arc_sectors * 4;
    const std::uint64_t rec_size = std::max(wanted, crcs_size0);
    const std::uint64_t rec_sectors =
        (rec_size <= crcs_size0) ? 0 : (rec_size - crcs_size0 + sector_size - 1) / sector_size;
    const std::uint64_t rec_sectors_size = rec_sectors * sector_size;

    // Rotating XOR over the protected bytes.
    sectors_out.assign(static_cast<std::size_t>(rec_sectors_size), 0);
    std::vector<std::uint32_t> data_crcs;
    data_crcs.reserve(static_cast<std::size_t>(arc_sectors));

    std::size_t span_index = 0;
    std::size_t span_offset = 0;
    std::uint64_t consumed = 0;
    std::uint64_t group = (rec_sectors == 0)
                              ? 0
                              : (rec_sectors - (arc_sectors % rec_sectors)) % rec_sectors;

    std::vector<std::uint8_t> chunk(static_cast<std::size_t>(sector_size));
    while (consumed < arcsize) {
        const std::size_t len =
            static_cast<std::size_t>(std::min<std::uint64_t>(sector_size, arcsize - consumed));
        std::size_t filled = 0;
        while (filled < len) {
            while (span_index < spans.size() && span_offset == spans[span_index].size) {
                ++span_index;
                span_offset = 0;
            }
            if (span_index >= spans.size()) {
                error = "recovery: protected region is shorter than expected";
                return false;
            }
            const auto& span = spans[span_index];
            const std::size_t n = std::min(len - filled, span.size - span_offset);
            std::memcpy(chunk.data() + filled, span.data + span_offset, n);
            filled += n;
            span_offset += n;
        }

        data_crcs.push_back(crc32(chunk.data(), len));
        if (rec_sectors != 0) {
            std::uint8_t* target = sectors_out.data() + group * sector_size;
            for (std::size_t i = 0; i < len; ++i) target[i] ^= chunk[i];
            group = (group + 1) % rec_sectors;
        }
        consumed += len;
    }
    // The last sector is XOR-ed only up to its length, but its CRC covers the
    // whole sector buffer (zero padding), as the reference does.
    if (rec_sectors != 0 && arc_sectors % rec_sectors != 0) {
        // nothing to fix: the padding bytes of the recovery sector are already 0
    }

    // Control info + CRCs of the data sectors + CRCs of the recovery sectors.
    std::vector<std::uint8_t> out;
    put_cstr(out, "0.36");
    write_vint(out, arcsize);
    const std::uint64_t rec1_descr_size = make_descriptor(kRecoveryBlock, "storing",
                                                          rec_sectors_size, rec_sectors_size, 0)
                                              .size();
    const std::uint64_t rec2_pos = arcsize + rec_sectors_size + rec1_descr_size;
    write_vint(out, rec2_pos);  // offset: init_pos = block_pos - offset = 0
    write_vint(out, 1);         // one (sector_size, rec_sectors) pair
    write_vint(out, sector_size);
    write_vint(out, rec_sectors);

    for (const auto crc : data_crcs) put_u32(out, crc);
    for (std::uint64_t r = 0; r < rec_sectors; ++r) {
        put_u32(out, crc32(sectors_out.data() + r * sector_size,
                           static_cast<std::size_t>(sector_size)));
    }
    crcs_out = std::move(out);
    return true;
}

// Prepares a control block for writing: with -hp it is encrypted with its own
// salt and IV, and the stored method keeps everything except the key.
bool prepare_control_block(const std::vector<std::uint8_t>& plain, bool encrypt,
                           const std::string& algorithm,
                           std::vector<std::uint8_t>& stored_data, std::string& stored_method,
                           std::string& error)
{
    if (!encrypt) {
        stored_data = plain;
        stored_method = "storing";
        return true;
    }
    std::string with_key;
    std::string stored_stage;
    if (!make_encryption_stage(algorithm, password(), with_key, stored_stage, error)) return false;
    if (!codec_compress("storing+" + with_key, plain.data(), plain.size(), stored_data, error)) {
        return false;
    }
    stored_method = "storing+" + stored_stage;
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

static bool write_entries(const std::filesystem::path& archive, std::vector<Entry> entries,
                          const std::vector<PreservedBlock>& preserved, const AddOptions& options,
                          std::string& error);

bool write_archive(const std::filesystem::path& archive, std::vector<NewEntry> entries,
                   const std::vector<PreservedBlock>& preserved, const AddOptions& options,
                   std::string& error)
{
    return write_entries(archive, std::move(entries), preserved, options, error);
}

bool create_archive(const std::filesystem::path& archive,
                    const std::vector<std::filesystem::path>& inputs, const AddOptions& options,
                    std::string& error)
{
    std::vector<Entry> entries;
    for (const auto& input : inputs) {
        std::string name = normalize_name(input);
        if (!options.archive_prefix.empty()) name = options.archive_prefix + "/" + name;
        if (name.empty()) {
            error = "invalid input path '" + input.string() + "'";
            return false;
        }
        if (!add_path(input, name, entries, error, options.path_mode)) return false;
    }
    if (entries.empty()) {
        error = "nothing to add";
        return false;
    }
    return write_entries(archive, std::move(entries), {}, options, error);
}

static bool write_entries(const std::filesystem::path& archive, std::vector<Entry> entries,
                          const std::vector<PreservedBlock>& preserved, const AddOptions& options,
                          std::string& error)
{
    if (entries.empty()) {
        error = "nothing to add";
        return false;
    }

    // Derived fields: the caller may have built entries directly (update).
    for (auto& entry : entries) {
        const std::size_t slash = entry.name.rfind('/');
        entry.base = (slash == std::string::npos) ? entry.name : entry.name.substr(slash + 1);
        entry.dir = (slash == std::string::npos) ? "" : entry.name.substr(0, slash);
        entry.size = entry.data.size();
        if (!entry.is_dir) entry.crc = crc32(entry.data.data(), entry.data.size());
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
        std::string cls =
            options.classes != nullptr ? options.classes->classify(entries[i].name) : "$obj";
        if (cls == "$obj" && options.detect_level > 0 && !entries[i].data.empty()) {
            // -ma: without a name match, look at the first block of the file;
            // printable bytes with no NUL are treated as text.
            const std::size_t sample = std::min<std::size_t>(entries[i].data.size(), 8192);
            std::size_t printable = 0;
            bool has_nul = false;
            for (std::size_t k = 0; k < sample; ++k) {
                const unsigned char c = entries[i].data[k];
                if (c == 0) has_nul = true;
                if (c == 9 || c == 10 || c == 13 || (c >= 32 && c < 127) || c >= 128) ++printable;
            }
            if (!has_nul && printable * 100 >= sample * 95) cls = "$text";
        }
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

    // Directories always go to their own storing block, as the original does.
    // Files are grouped by resolved method and then split into solid blocks
    // according to the -s criteria.
    struct Block {
        std::string method;
        std::vector<std::size_t> entries;
        std::vector<std::uint8_t> plain;
        std::vector<std::uint8_t> packed;
        std::uint64_t pos = 0;
        bool preserved = false;
    };
    std::vector<Block> blocks;

    // Blocks reused from the previous archive: copied verbatim. They are placed
    // where their files are in the new order, so the archive keeps the file
    // ordering the original had.
    std::map<std::string, std::size_t> index_of_name;
    for (std::size_t i = 0; i < entries.size(); ++i) index_of_name[entries[i].name] = i;

    struct Preserved {
        std::size_t first = 0;           // first entry index in the new order
        std::vector<std::size_t> files;  // contiguous entries
        std::string method;
        std::vector<std::uint8_t> packed;
    };
    std::vector<Preserved> preserved_blocks;
    std::vector<bool> consumed(entries.size(), false);
    std::map<std::size_t, std::size_t> preserved_at;  // first entry index -> block

    for (const auto& old : preserved) {
        std::vector<std::size_t> files;
        bool contiguous = true;
        for (std::size_t k = 0; k < old.files.size(); ++k) {
            const auto it = index_of_name.find(old.files[k]);
            if (it == index_of_name.end()) {
                contiguous = false;
                break;
            }
            if (k > 0 && it->second != files.back() + 1) contiguous = false;
            if (consumed[it->second]) contiguous = false;
            files.push_back(it->second);
        }
        if (!contiguous || files.empty()) continue;
        for (const auto index : files) consumed[index] = true;
        Preserved block;
        block.first = files.front();
        block.files = std::move(files);
        block.method = old.method;
        block.packed = old.packed;
        preserved_at[block.first] = preserved_blocks.size();
        preserved_blocks.push_back(std::move(block));
    }

    auto add_block = [&](const std::string& method, std::vector<std::size_t> indices) {
        Block block;
        block.method = method;
        block.entries = std::move(indices);
        for (const auto index : block.entries) {
            if (!entries[index].is_dir) {
                block.plain.insert(block.plain.end(), entries[index].data.begin(),
                                   entries[index].data.end());
            }
        }
        blocks.push_back(std::move(block));
    };

    std::vector<std::size_t> dirs_only;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].is_dir && !consumed[i]) dirs_only.push_back(i);
    }
    if (!dirs_only.empty()) add_block("storing", std::move(dirs_only));

    // Walk the files in order: a preserved block is emitted where it belongs,
    // everything else is grouped by method and split by the -s criteria.
    auto extension_of = [](const std::string& name) {
        const std::size_t dot = name.find_last_of('.');
        const std::size_t slash = name.find_last_of('/');
        return (dot == std::string::npos || (slash != std::string::npos && dot < slash))
                   ? std::string()
                   : name.substr(dot);
    };

    auto chunk_len = [&](const std::vector<SolidRule>& rules,
                         const std::vector<std::size_t>& indices, std::size_t start) {
        std::size_t best = indices.size() - start;
        for (const auto& rule : rules) {
            std::size_t n = best;
            switch (rule.kind) {
                case SolidRule::Kind::All: n = best; break;
                case SolidRule::Kind::None: n = 1; break;
                case SolidRule::Kind::Files: n = static_cast<std::size_t>(rule.value); break;
                case SolidRule::Kind::Size: {
                    std::uint64_t sum = 0;
                    n = 0;
                    for (std::size_t i = start; i < indices.size(); ++i) {
                        if (sum >= rule.value) break;
                        sum += entries[indices[i]].size;
                        ++n;
                    }
                    break;
                }
                case SolidRule::Kind::Ext: {
                    const std::string ext = extension_of(entries[indices[start]].name);
                    n = 0;
                    for (std::size_t i = start; i < indices.size(); ++i) {
                        if (extension_of(entries[indices[i]].name) != ext) break;
                        ++n;
                    }
                    break;
                }
            }
            if (n == 0) n = 1;
            if (n < best) best = n;
        }
        return best;
    };

    const std::vector<SolidRule> rules =
        options.solid.empty() ? std::vector<SolidRule>{SolidRule{}} : options.solid;

    std::size_t i = 0;
    while (i < entries.size()) {
        if (entries[i].is_dir) { ++i; continue; }

        const auto preserved_it = preserved_at.find(i);
        if (preserved_it != preserved_at.end()) {
            const Preserved& old = preserved_blocks[preserved_it->second];
            Block block;
            block.method = old.method;
            block.packed = old.packed;
            block.entries = old.files;
            block.preserved = true;
            blocks.push_back(std::move(block));
            i = old.files.back() + 1;
            continue;
        }

        // New block: same method, as many files as the criteria allow, but stop
        // before a preserved block so it can be emitted intact.
        const std::string method = method_of[i];
        std::vector<std::size_t> indices;
        std::size_t j = i;
        while (j < entries.size() && !entries[j].is_dir && method_of[j] == method &&
               preserved_at.find(j) == preserved_at.end()) {
            indices.push_back(j);
            ++j;
        }
        const std::size_t len = chunk_len(rules, indices, 0);
        std::vector<std::size_t> chunk(indices.begin(),
                                       indices.begin() + static_cast<std::ptrdiff_t>(len));
        i += len;
        add_block(method, std::move(chunk));
    }

    // The directory block always goes first, as in the original.
    for (std::size_t i = 1; i < blocks.size(); ++i) {
        const auto& block = blocks[i];
        bool all_dirs = !block.entries.empty();
        for (const auto index : block.entries) all_dirs = all_dirs && entries[index].is_dir;
        if (all_dirs) {
            Block dir_block = std::move(blocks[i]);
            blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(i));
            blocks.insert(blocks.begin(), std::move(dir_block));
            break;
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
    std::uint64_t progress_done = 0;
    std::uint64_t progress_total = 0;
    for (const auto& block : blocks) progress_total += block.plain.size();
    for (auto& block : blocks) {
        if (block.preserved) continue;  // already compressed
        if (block.plain.empty() || block.method == "storing" || block.method == "0") {
            block.method = "storing";
            block.packed = block.plain;
            continue;
        }
        std::string limited;
        const std::uint64_t dict = block.plain.size() + block.plain.size() / 100 + 512;
        if (!codec_limit_dictionary(block.method, dict, limited, error)) return false;
        block.method = limited;
        std::string to_compress = block.method;
        std::string to_store = block.method;
        if (password_is_set()) {
            std::string with_key;
            std::string stored;
            std::string crypto_error;
            if (!make_encryption_stage(options.encryption_algorithm, password(), with_key, stored,
                                       crypto_error)) {
                error = crypto_error;
                return false;
            }
            to_compress += "+" + with_key;
            to_store += "+" + stored;
        }
        if (!codec_compress(to_compress, block.plain.data(), block.plain.size(), block.packed,
                            error)) {
            return false;
        }
        block.method = to_store;
        progress_done += block.plain.size();
        if (options.progress && !options.progress(progress_done, progress_total, block.method)) {
            error = "cancelled";
            return false;
        }
    }

    std::vector<std::uint8_t> header(format::kSignature.begin(), format::kSignature.end());
    header.insert(header.end(), format::kVersionBytes.begin(), format::kVersionBytes.end());

    std::vector<std::uint8_t> header_stored;
    std::string header_method;
    if (!prepare_control_block(header, options.header_encryption, options.encryption_algorithm,
                               header_stored, header_method, error)) {
        return false;
    }
    const auto header_descr =
        make_descriptor(kHeaderBlock, header_method, header.size(), header_stored.size(),
                        crc32(header.data(), header.size()));
    const std::uint64_t header_total = header_stored.size() + header_descr.size();

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

    // Directory: stored by default, compressed when -dm asks for it and then
    // optionally encrypted (-hp).
    std::vector<std::uint8_t> dir_encoded = dir_data;
    std::string dir_method = "storing";
    if (!options.directory_method.empty() && options.directory_method != "0" &&
        options.directory_method != "storing") {
        std::string limited;
        if (!codec_limit_dictionary(options.directory_method,
                                    dir_data.size() + dir_data.size() / 100 + 512, limited,
                                    error)) {
            return false;
        }
        if (!codec_compress(limited, dir_data.data(), dir_data.size(), dir_encoded, error)) {
            return false;
        }
        dir_method = limited;
    }
    std::vector<std::uint8_t> dir_stored;
    std::string dir_stored_method;
    if (options.header_encryption) {
        std::string with_key;
        std::string stored_stage;
        const std::string& algorithm = options.encryption_algorithm;
        if (!make_encryption_stage(algorithm, password(), with_key, stored_stage, error)) return false;
        if (!codec_compress(dir_method + "+" + with_key, dir_data.data(), dir_data.size(),
                            dir_stored, error)) {
            return false;
        }
        dir_stored_method = dir_method + "+" + stored_stage;
    } else {
        dir_stored = dir_encoded;
        dir_stored_method = dir_method;
    }
    dir_method = dir_stored_method;
    const auto dir_descr =
        make_descriptor(kDirBlock, dir_method, dir_data.size(), dir_stored.size(),
                        crc32(dir_data.data(), dir_data.size()));
    const std::uint64_t before_footer = dir_pos + dir_stored.size() + dir_descr.size();

    struct FooterEntry {
        std::uint32_t type;
        std::string method;
        std::uint64_t pos, orig, comp;
        std::uint32_t crc;
    };

    const std::vector<FooterEntry> base_blocks = {
        {kHeaderBlock, header_method, 0, header.size(), header_stored.size(),
         crc32(header.data(), header.size())},
        {kDirBlock, dir_method, dir_pos, dir_data.size(), dir_stored.size(),
         crc32(dir_data.data(), dir_data.size())},
    };

    auto build_footer = [&](const std::vector<FooterEntry>& list, const std::string& recovery,
                            std::uint64_t footer_pos) {
        std::vector<std::uint8_t> out;
        write_vint(out, list.size());
        for (const auto& b : list) {
            write_vint(out, b.type);
            put_cstr(out, b.method);
            write_vint(out, footer_pos - b.pos);
            write_vint(out, b.orig);
            write_vint(out, b.comp);
            put_u32(out, b.crc);
        }
        out.push_back(0);    // not locked
        write_vint(out, 0);  // old comment length
        if (!recovery.empty() || options.recovery_present) put_cstr(out, recovery);
        return out;
    };

    // Provisional footer (only when a recovery record is requested): the record
    // protects everything up to and including it.
    // Footer offsets are relative to the footer block's *data* position, which
    // for the provisional footer is right after the directory.
    std::vector<std::uint8_t> provisional_data;
    std::vector<std::uint8_t> provisional_descr;
    std::uint64_t rr_pos = before_footer;
    if (!options.recovery.empty()) {
        provisional_data = build_footer(base_blocks, "", before_footer);
        provisional_descr = make_descriptor(kFooterBlock, "storing", provisional_data.size(),
                                            provisional_data.size(),
                                            crc32(provisional_data.data(), provisional_data.size()));
        rr_pos = before_footer + provisional_data.size() + provisional_descr.size();
    }

    // Recovery record: rotating XOR of the protected sectors plus their CRCs.
    std::vector<std::uint8_t> rec_sectors;
    std::vector<std::uint8_t> rec_crcs;
    if (!options.recovery.empty()) {
        std::vector<Span> spans;
        spans.push_back({header_stored.data(), header_stored.size()});
        spans.push_back({header_descr.data(), header_descr.size()});
        for (const auto& block : blocks) spans.push_back({block.packed.data(), block.packed.size()});
        spans.push_back({dir_stored.data(), dir_stored.size()});
        spans.push_back({dir_descr.data(), dir_descr.size()});
        spans.push_back({provisional_data.data(), provisional_data.size()});
        spans.push_back({provisional_descr.data(), provisional_descr.size()});
        if (!build_recovery(options.recovery, spans, rr_pos, rec_sectors, rec_crcs, error)) {
            return false;
        }
    }

    std::vector<FooterEntry> final_blocks = base_blocks;
    std::uint64_t final_pos = rr_pos;
    if (!options.recovery.empty()) {
        // Positions of the two recovery blocks, known from the sizes above.
        const std::uint64_t rec1_pos = rr_pos;
        const std::uint64_t rec1_descr_size =
            make_descriptor(kRecoveryBlock, "storing", rec_sectors.size(), rec_sectors.size(),
                            crc32(rec_sectors.data(), rec_sectors.size()))
                .size();
        const std::uint64_t rec2_pos = rec1_pos + rec_sectors.size() + rec1_descr_size;
        final_blocks.push_back({kRecoveryBlock, "storing", rec1_pos, rec_sectors.size(),
                                rec_sectors.size(), crc32(rec_sectors.data(), rec_sectors.size())});
        final_blocks.push_back({kRecoveryBlock, "storing", rec2_pos, rec_crcs.size(),
                                rec_crcs.size(), crc32(rec_crcs.data(), rec_crcs.size())});
        final_pos = rec2_pos + rec_crcs.size() +
                    make_descriptor(kRecoveryBlock, "storing", rec_crcs.size(), rec_crcs.size(),
                                    crc32(rec_crcs.data(), rec_crcs.size()))
                        .size();
    }

    std::vector<std::uint8_t> footer_data = build_footer(final_blocks, options.recovery, final_pos);
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

    bool ok = write_bytes(header_stored) && write_bytes(header_descr);
    for (const auto& block : blocks) {
        ok = ok && write_bytes(block.packed);
    }
    // The directory comes first; the recovery record protects everything up to
    // and including the provisional footer.
    ok = ok && write_bytes(dir_stored) && write_bytes(dir_descr);
    if (!options.recovery.empty()) {
        ok = ok && write_bytes(provisional_data) && write_bytes(provisional_descr);
        ok = ok && write_bytes(rec_sectors);
        ok = ok && write_bytes(make_descriptor(kRecoveryBlock, "storing", rec_sectors.size(),
                                               rec_sectors.size(),
                                               crc32(rec_sectors.data(), rec_sectors.size())));
        ok = ok && write_bytes(rec_crcs);
        ok = ok && write_bytes(make_descriptor(kRecoveryBlock, "storing", rec_crcs.size(),
                                               rec_crcs.size(),
                                               crc32(rec_crcs.data(), rec_crcs.size())));
    }
    if (!ok || !write_bytes(footer_data) || !write_bytes(footer_descr)) {
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

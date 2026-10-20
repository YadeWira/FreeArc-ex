#include "fa/repair.h"

#include <cstring>
#include <fstream>
#include <map>
#include <vector>

#include "fa/arc.h"
#include "fa/crc32.h"
#include "fa/vint.h"

namespace fa {
namespace {

class Reader {
public:
    Reader(const std::vector<std::uint8_t>& data) : data_(data) {}

    bool u32(std::uint32_t& out)
    {
        if (data_.size() - pos_ < 4) return false;
        out = static_cast<std::uint32_t>(data_[pos_]) |
              (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8) |
              (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16) |
              (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return true;
    }

    bool vint(std::uint64_t& out) { return read_vint(data_.data(), data_.size(), pos_, out); }

    bool cstr(std::string& out)
    {
        const std::size_t start = pos_;
        while (pos_ < data_.size() && data_[pos_] != 0) ++pos_;
        if (pos_ >= data_.size()) return false;
        out.assign(reinterpret_cast<const char*>(data_.data() + start), pos_ - start);
        ++pos_;
        return true;
    }

    std::size_t remaining() const { return data_.size() - pos_; }

private:
    const std::vector<std::uint8_t>& data_;
    std::size_t pos_ = 0;
};

}  // namespace

bool repair_archive(const std::filesystem::path& archive, const std::filesystem::path& output,
                    RepairResult& result, std::string& error)
{
    Archive input;
    if (!Archive::open(archive, input, error)) return false;
    const ArchiveInfo& info = input.info();

    // Two recovery blocks: parity sectors, then info + CRCs.
    std::vector<std::size_t> recovery;
    for (std::size_t i = 0; i < info.blocks.size(); ++i) {
        if (info.blocks[i].type == kRecoveryBlock) recovery.push_back(i);
    }
    if (recovery.size() < 2) {
        error = "this archive has no recovery record";
        return false;
    }

    std::vector<std::uint8_t> parity;
    std::vector<std::uint8_t> info_block;
    if (!input.raw_control_block(recovery[0], parity, error)) return false;
    if (!input.raw_control_block(recovery[1], info_block, error)) return false;

    Reader reader(info_block);
    std::string version;
    std::uint64_t arcsize = 0;
    std::uint64_t offset = 0;
    std::uint64_t pairs = 0;
    if (!reader.cstr(version) || !reader.vint(arcsize) || !reader.vint(offset) ||
        !reader.vint(pairs)) {
        error = "malformed recovery information";
        return false;
    }

    std::uint64_t sector_size = 0;
    std::uint64_t rec_sectors = 0;
    for (std::uint64_t i = 0; i < pairs; ++i) {
        std::uint64_t ss = 0;
        std::uint64_t rs = 0;
        if (!reader.vint(ss) || !reader.vint(rs)) {
            error = "malformed recovery information";
            return false;
        }
        if (i == 0) {
            sector_size = ss;
            rec_sectors = rs;
        }
    }
    if (sector_size == 0) {
        error = "invalid recovery sector size";
        return false;
    }

    const std::uint64_t arc_sectors = (arcsize + sector_size - 1) / sector_size;
    std::vector<std::uint32_t> data_crcs;
    data_crcs.reserve(static_cast<std::size_t>(arc_sectors));
    for (std::uint64_t i = 0; i < arc_sectors; ++i) {
        std::uint32_t crc = 0;
        if (!reader.u32(crc)) {
            error = "truncated recovery CRCs";
            return false;
        }
        data_crcs.push_back(crc);
    }

    const std::uint64_t init_pos = info.blocks[recovery[1]].pos - offset;

    // Load the archive contents and copy them: the original is never modified.
    std::ifstream in(archive, std::ios::binary);
    if (!in) {
        error = "cannot read '" + archive.string() + "'";
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff file_size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(file_size));
    if (file_size > 0 && !in.read(reinterpret_cast<char*>(data.data()), file_size)) {
        error = "cannot read '" + archive.string() + "'";
        return false;
    }
    in.close();

    if (init_pos + arcsize > data.size()) {
        error = "recovery record points outside the archive";
        return false;
    }

    const std::uint64_t group0 =
        rec_sectors == 0 ? 0 : (rec_sectors - (arc_sectors % rec_sectors)) % rec_sectors;

    std::vector<std::vector<std::uint64_t>> damaged(static_cast<std::size_t>(rec_sectors));
    std::vector<std::uint8_t> good_xor(static_cast<std::size_t>(rec_sectors * sector_size), 0);

    for (std::uint64_t s = 0; s < arc_sectors; ++s) {
        const std::uint64_t pos = init_pos + s * sector_size;
        const std::size_t len = static_cast<std::size_t>(
            std::min<std::uint64_t>(sector_size, arcsize - s * sector_size));
        const std::uint64_t group = rec_sectors == 0 ? 0 : (group0 + s) % rec_sectors;
        if (crc32(data.data() + pos, len) != data_crcs[static_cast<std::size_t>(s)]) {
            ++result.damaged;
            damaged[static_cast<std::size_t>(group)].push_back(s);
        } else if (rec_sectors != 0) {
            std::uint8_t* target = good_xor.data() + group * sector_size;
            for (std::size_t i = 0; i < len; ++i) target[i] ^= data[pos + i];
        }
    }

    if (result.damaged == 0) {
        error = "no damaged sectors found";
        return false;
    }

    // Rebuild every sector that is alone in its group.
    for (std::uint64_t g = 0; g < rec_sectors; ++g) {
        auto& sectors = damaged[static_cast<std::size_t>(g)];
        if (sectors.size() != 1) {
            if (!sectors.empty()) ++result.groups_lost;
            continue;
        }
        const std::uint64_t s = sectors[0];
        const std::uint64_t pos = init_pos + s * sector_size;
        const std::size_t len = static_cast<std::size_t>(
            std::min<std::uint64_t>(sector_size, arcsize - s * sector_size));

        std::vector<std::uint8_t> rebuilt(static_cast<std::size_t>(sector_size));
        const std::uint8_t* parity_group = parity.data() + g * sector_size;
        for (std::size_t i = 0; i < static_cast<std::size_t>(sector_size); ++i) {
            rebuilt[i] = parity_group[i] ^ good_xor[static_cast<std::size_t>(g * sector_size + i)];
        }
        if (crc32(rebuilt.data(), len) != data_crcs[static_cast<std::size_t>(s)]) {
            continue;  // could not reconstruct: leave it damaged
        }
        std::memcpy(data.data() + pos, rebuilt.data(), len);
        ++result.repaired;
    }

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write '" + output.string() + "'";
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    if (!out) {
        error = "write error on '" + output.string() + "'";
        return false;
    }
    return true;
}

}  // namespace fa

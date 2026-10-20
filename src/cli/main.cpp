#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "fa/arc.h"
#include "fa/arc_writer.h"
#include "fa/crc32.h"
#include "fa/format.h"
#include "fa/preset.h"
#include "fa/method.h"
#include "fa/update.h"

namespace {

// FreeArc's -s grammar: "" = all together, "-" = one file per block, "e" = by
// extension, "100f" = 100 files, "10m"/"1mb" = by size. Tokens concatenate.
bool parse_solid(const std::string& text, std::vector<fa::SolidRule>& rules, std::string& error)
{
    if (text.empty()) {
        rules.push_back(fa::SolidRule{});
        return true;
    }
    if (text == "-") {
        rules.push_back({fa::SolidRule::Kind::None, 0});
        return true;
    }
    rules.clear();
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == 'e') {
            rules.push_back({fa::SolidRule::Kind::Ext, 0});
            ++i;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            error = "bad solid option '-s" + text + "'";
            return false;
        }
        std::size_t start = i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        const std::string number = text.substr(start, i - start);
        std::string suffix;
        while (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i]))) {
            suffix.push_back(static_cast<char>(std::tolower(text[i])));
            ++i;
        }
        if (suffix.empty() || suffix == "f") {
            rules.push_back({fa::SolidRule::Kind::Files,
                             static_cast<std::uint64_t>(std::strtoull(number.c_str(), nullptr, 10))});
        } else {
            const std::uint64_t size = fa::parse_mem(number + suffix);
            if (size == 0) {
                error = "bad solid option '-s" + text + "'";
                return false;
            }
            rules.push_back({fa::SolidRule::Kind::Size, size});
        }
    }
    return true;
}

constexpr const char* kProgram = "FreeArc-ex";
constexpr const char* kVersion = "0.1.0";
constexpr const char* kCompatibleWith = "FreeArc 0.67.1";

void print_version()
{
    std::printf("%s %s (compatible with %s)\n", kProgram, kVersion, kCompatibleWith);
}

void print_usage(std::FILE* out)
{
    std::fprintf(out,
                 "%s %s - extended support for %s archives\n"
                 "\n"
                 "Usage: arc-ex command [options...] archive [files...]\n"
                 "\n"
                 "Commands (M1):\n"
                 "  a        add files to a new archive\n"
                 "  u        add new files and replace changed ones\n"
                 "  f        replace changed files (no new entries)\n"
                 "  d        delete entries (name or wildcard)\n"
                 "             -mMETHOD   compression: 0/storing or lzma[:DICT]\n"
                 "  l        list archive contents\n"
                 "  lb       bare list of file names\n"
                 "  t        test archive integrity (per-file CRCs)\n"
                 "  x        extract archive\n"
                 "             -dpDIR   extract into DIR (default: current directory)\n"
                 "\n"
                 "General options:\n"
                 "  -h, --help       show this help\n"
                 "  -V, --version    show the version\n"
                 "\n"
                 "Limits: `storing` and a single `lzma` stage only, so far.\n",
                 kProgram, kVersion, kCompatibleWith);
}

std::string format_time(std::int32_t seconds)
{
    if (seconds == 0) return "-";
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#if defined(_WIN32)
    // MinGW has no localtime_r; the CLI is single-threaded.
    if (localtime_s(&tm, &t) != 0) return "-";
#else
    if (localtime_r(&t, &tm) == nullptr) return "-";
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

std::string format_size(std::uint64_t size)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(size));
    return buf;
}

int list_archive(const fa::ArchiveInfo& info)
{
    std::printf("%12s  %-16s  %s\n", "size", "date", "name");
    std::printf("%s\n", "------------------------------------------------------------");
    std::uint64_t total = 0;
    std::size_t entries = 0;
    for (const auto& file : info.files) {
        std::printf("%12s  %-16s  %s%s\n", format_size(file.size).c_str(),
                    format_time(file.mtime).c_str(), file.name.c_str(), file.is_dir ? "/" : "");
        if (!file.is_dir) {
            total += file.size;
            ++entries;
        }
    }
    std::printf("%s\n", "------------------------------------------------------------");
    std::printf("%zu file(s), %llu byte(s)\n", entries,
                static_cast<unsigned long long>(total));
    if (info.locked) std::printf("archive is locked\n");
    return 0;
}

// Walks the data blocks, checking (and optionally writing) every file.
int process_archive(const fa::Archive& archive, const std::string& output_dir, bool extract)
{
    const fa::ArchiveInfo& info = archive.info();
    std::size_t failures = 0;
    std::uint64_t written = 0;

    for (std::size_t b = 0; b < info.data_blocks.size(); ++b) {
        std::vector<std::uint8_t> data;
        std::string error;
        if (!archive.read_data_block(b, data, error)) {
            std::fprintf(stderr, "arc-ex: block %zu: %s\n", b, error.c_str());
            ++failures;
            continue;
        }

        const fa::DataBlock& block = info.data_blocks[b];
        for (std::size_t i = 0; i < block.file_count; ++i) {
            const fa::FileEntry& file = info.files[block.first_file + i];
            if (file.is_dir) {
                if (extract) {
                    std::error_code ec;
                    std::filesystem::create_directories(std::filesystem::path(output_dir) / file.name,
                                                        ec);
                    if (ec) {
                        std::fprintf(stderr, "arc-ex: cannot create '%s': %s\n", file.name.c_str(),
                                     ec.message().c_str());
                        ++failures;
                    }
                }
                continue;
            }

            if (file.offset + file.size > data.size()) {
                std::fprintf(stderr, "arc-ex: '%s' outside its block\n", file.name.c_str());
                ++failures;
                continue;
            }
            const std::uint8_t* payload = data.data() + file.offset;
            const auto size = static_cast<std::size_t>(file.size);

            if (fa::crc32(payload, size) != file.crc) {
                std::fprintf(stderr, "arc-ex: '%s': CRC mismatch\n", file.name.c_str());
                ++failures;
                continue;
            }

            if (!extract) continue;

            const std::filesystem::path target = std::filesystem::path(output_dir) / file.name;
            std::error_code ec;
            if (target.has_parent_path()) {
                std::filesystem::create_directories(target.parent_path(), ec);
                if (ec) {
                    std::fprintf(stderr, "arc-ex: cannot create '%s': %s\n",
                                 target.parent_path().string().c_str(), ec.message().c_str());
                    ++failures;
                    continue;
                }
            }
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) {
                std::fprintf(stderr, "arc-ex: cannot write '%s'\n", target.string().c_str());
                ++failures;
                continue;
            }
            out.write(reinterpret_cast<const char*>(payload), static_cast<std::streamsize>(size));
            if (!out) {
                std::fprintf(stderr, "arc-ex: write error on '%s'\n", target.string().c_str());
                ++failures;
                continue;
            }
            out.close();

            written += size;
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "arc-ex: %zu problem(s)\n", failures);
        return 1;
    }
    if (extract) {
        std::printf("All OK: %llu byte(s) extracted into '%s'\n",
                    static_cast<unsigned long long>(written), output_dir.c_str());
    } else {
        std::printf("All OK: %zu file(s) verified\n", info.files.size());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }

    const std::string_view command(argv[1]);

    if (command == "-h" || command == "--help") {
        print_usage(stdout);
        return 0;
    }
    if (command == "-V" || command == "--version") {
        print_version();
        return 0;
    }

    if (command == "lt") {
        if (argc < 3) {
            std::fprintf(stderr, "arc-ex: 'lt' needs an archive name\n");
            return 2;
        }
        fa::Archive archive;
        std::string error;
        if (!fa::Archive::open(argv[2], archive, error)) {
            std::fprintf(stderr, "arc-ex: %s\n", error.c_str());
            return 1;
        }
        const fa::ArchiveInfo& info = archive.info();
        std::printf("control blocks:\n");
        for (const auto& b : info.blocks) {
            std::printf("  type=%-2u pos=%-10llu orig=%-10llu comp=%-10llu %s\n", b.type,
                        static_cast<unsigned long long>(b.pos),
                        static_cast<unsigned long long>(b.orig_size),
                        static_cast<unsigned long long>(b.comp_size), b.method.c_str());
        }
        std::printf("data blocks:\n");
        for (const auto& b : info.data_blocks) {
            std::printf("  pos=%-10llu orig=%-10llu comp=%-10llu files=%-3zu %s\n",
                        static_cast<unsigned long long>(b.pos),
                        static_cast<unsigned long long>(b.orig_size),
                        static_cast<unsigned long long>(b.comp_size), b.file_count,
                        b.method.c_str());
        }
        std::printf("sfx_size=%llu locked=%d\n",
                    static_cast<unsigned long long>(info.sfx_size), info.locked ? 1 : 0);
        return 0;
    }

    if (command == "a" || command == "u" || command == "f" || command == "d") {
        // a/u/f/d archive[.arc] path... [-mMETHOD]
        std::string archive;
        std::vector<std::filesystem::path> inputs;
        fa::AddOptions options;
        bool method_given = false;
        for (int i = 2; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg.rfind("-m", 0) == 0 && arg.size() > 2) {
                options.method = std::string(arg.substr(2));
                method_given = true;
            } else if (arg.rfind("-dm", 0) == 0) {
                // Directory method: stored for now, accepted for CLI compatibility.
            } else if (arg.rfind("-s", 0) == 0) {
                std::string solid_error;
                if (!parse_solid(std::string(arg.substr(2)), options.solid, solid_error)) {
                    std::fprintf(stderr, "arc-ex: %s\n", solid_error.c_str());
                    return 2;
                }
            } else if (arg == "--no-progress" || arg.rfind("-di", 0) == 0) {
                // Accepted, no effect yet.
            } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
                std::fprintf(stderr, "arc-ex: unknown option '%.*s'\n",
                             static_cast<int>(arg.size()), arg.data());
                return 2;
            } else if (archive.empty()) {
                archive = std::string(arg);
            } else {
                inputs.emplace_back(arg);
            }
        }
        if (archive.empty() || inputs.empty()) {
            std::fprintf(stderr,
                         "arc-ex: usage: arc-ex %.1s archive.arc path... [-mMETHOD]\n",
                         command.data());
            return 2;
        }
        if (!method_given) options.method = "lzma";

        // File classes and method definitions: arc.groups orders/classifies the
        // input files, arc.ini (optional) overrides the built-in presets.
        static fa::FileClasses classes;
        static fa::MethodTable table;
        std::string error;
        for (const char* candidate : {"arc.groups", "data/arc.groups"}) {
            if (std::filesystem::exists(candidate)) {
                if (!classes.load(candidate, error)) {
                    std::fprintf(stderr, "arc-ex: warning: %s\n", error.c_str());
                }
                break;
            }
        }
        for (const char* candidate : {"arc.ini"}) {
            if (std::filesystem::exists(candidate)) {
                if (!table.load_ini(candidate, error)) {
                    std::fprintf(stderr, "arc-ex: warning: %s\n", error.c_str());
                }
                break;
            }
        }
        options.table = &table;
        options.classes = &classes;

        const bool created =
            command == "a"
                ? fa::create_archive(archive, inputs, options, error)
                : fa::update_archive(archive, inputs,
                                     command == "u"   ? fa::UpdateMode::Update
                                     : command == "f" ? fa::UpdateMode::Freshen
                                                      : fa::UpdateMode::Delete,
                                     options, error);
        if (!created) {
            std::fprintf(stderr, "arc-ex: %s\n", error.c_str());
            return 1;
        }
        std::printf(command == "a" ? "Archive created: %s\n" : "Archive updated: %s\n",
                    archive.c_str());
        return 0;
    }

    if (command != "l" && command != "lb" && command != "t" && command != "x") {
        std::fprintf(stderr, "arc-ex: command '%.*s' is not implemented yet\n",
                     static_cast<int>(command.size()), command.data());
        return 2;
    }

    std::string archive_path;
    std::string output_dir = ".";
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg.rfind("-dp", 0) == 0) {
            output_dir = std::string(arg.substr(3));
        } else if (arg == "-o+" || arg == "-o-") {
            // Overwrite policy: accepted for CLI compatibility, extraction always overwrites.
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "arc-ex: unknown option '%.*s'\n", static_cast<int>(arg.size()),
                         arg.data());
            return 2;
        } else if (archive_path.empty()) {
            archive_path = std::string(arg);
        } else {
            std::fprintf(stderr, "arc-ex: selecting individual files is not implemented yet\n");
            return 2;
        }
    }

    if (archive_path.empty()) {
        std::fprintf(stderr, "arc-ex: '%.*s' needs an archive name\n",
                     static_cast<int>(command.size()), command.data());
        return 2;
    }

    // Format consistency guard: if someone changes the header constants, the
    // binary says so instead of writing invalid archives.
    static_assert(fa::format::kSignature.size() == 4);
    static_assert(fa::format::kHeaderBlockSize == 8);

    fa::Archive archive;
    std::string error;
    if (!fa::Archive::open(archive_path, archive, error)) {
        std::fprintf(stderr, "arc-ex: %s\n", error.c_str());
        return 1;
    }

    if (command == "l") return list_archive(archive.info());
    if (command == "lb") {
        for (const auto& file : archive.info().files) {
            std::printf("%s\n", file.name.c_str());
        }
        return 0;
    }
    return process_archive(archive, output_dir, command == "x");
}

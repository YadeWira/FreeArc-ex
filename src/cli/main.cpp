#include <cstdio>
#include <cstring>
#include <ctime>
#include <string_view>

#include "fa/arc.h"
#include "fa/format.h"

namespace {

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
                 "Commands (M1, read side):\n"
                 "  l        list archive contents\n"
                 "\n"
                 "General options:\n"
                 "  -h, --help       show this help\n"
                 "  -V, --version    show the version\n"
                 "\n"
                 "Limit: only control blocks stored uncompressed ('storing') are\n"
                 "supported so far, i.e. archives created with -dm0.\n",
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

int list_archive(const char* path)
{
    fa::ArchiveInfo info;
    std::string error;
    if (!fa::open_archive(path, info, error)) {
        std::fprintf(stderr, "arc-ex: %s\n", error.c_str());
        return 1;
    }

    std::printf("%12s  %-16s  %s\n", "size", "date", "name");
    std::printf("%s\n", "------------------------------------------------------------");
    std::uint64_t total = 0;
    std::size_t entries = 0;
    for (const auto& file : info.files) {
        std::printf("%12s  %-16s  %s%s\n", format_size(file.size).c_str(),
                    format_time(file.mtime).c_str(), file.name.c_str(),
                    file.is_dir ? "/" : "");
        if (!file.is_dir) {
            total += file.size;
            ++entries;
        }
    }
    std::printf("%s\n", "------------------------------------------------------------");
    std::printf("%zu file(s), %llu byte(s)\n", entries,
                static_cast<unsigned long long>(total));
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
    if (command == "l") {
        if (argc < 3) {
            std::fprintf(stderr, "arc-ex: 'l' needs an archive name\n");
            return 2;
        }
        return list_archive(argv[2]);
    }

    // Format consistency guard: if someone changes the header constants, the
    // binary says so instead of writing invalid archives.
    static_assert(fa::format::kSignature.size() == 4);
    static_assert(fa::format::kHeaderBlockSize == 8);

    std::fprintf(stderr, "arc-ex: command '%s' is not implemented yet\n", argv[1]);
    return 2;
}

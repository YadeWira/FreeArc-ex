#include <cstdio>
#include <cstring>
#include <string_view>

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
                 "Status: M0 (scaffolding). The a/l/x/t commands are not implemented\n"
                 "yet; see docs/COMPAT.md and the milestone plan.\n"
                 "\n"
                 "General options:\n"
                 "  -h, --help       show this help\n"
                 "  -V, --version    show the version\n",
                 kProgram, kVersion, kCompatibleWith);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "-V" || arg == "--version") {
            print_version();
            return 0;
        }
        if (arg == "-h" || arg == "--help") {
            print_usage(stdout);
            return 0;
        }
    }

    // Format consistency guard: if someone changes the header constants, the
    // binary says so instead of writing invalid archives.
    static_assert(fa::format::kSignature.size() == 4);
    static_assert(fa::format::kHeaderBlockSize == 8);

    std::fprintf(stderr,
                 "arc-ex: command '%s' is not implemented yet (milestone M0 of the plan)\n",
                 argv[1]);
    return 2;
}

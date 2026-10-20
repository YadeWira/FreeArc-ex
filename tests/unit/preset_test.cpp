#include <cstdio>
#include <string>

#include "fa/preset.h"

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    } else {
        std::printf("ok: %s\n", what);
    }
}

}  // namespace

int main()
{
    fa::MethodTable table;

    // Binary presets expand through the built-in definitions.
    const std::string m0 = table.resolve("0", "$obj");
    check(m0 == "storing", "preset 0 is storing");

    const std::string m4 = table.resolve("4", "$obj");
    check(m4.find("lzma:96m:normal:16:mc8") != std::string::npos, "preset 4 uses the 4binary chain");
    check(m4.find("rep:96m") != std::string::npos, "preset 4 starts with rep");
    check(m4.find("delta") != std::string::npos, "preset 4 includes delta");

    const std::string m1 = table.resolve("1", "$obj");
    check(m1.find("tor:4") != std::string::npos, "preset 1 uses tornado");

    // Text presets resolve to the text chain.
    const std::string t4 = table.resolve("4", "$text");
    check(t4.find("ppmd") != std::string::npos, "preset 4 for text uses ppmd");

    // A codec name is left untouched.
    check(table.resolve("lzma:16mb", "$obj") == "lzma:16mb", "raw method passes through");

    // File classification follows arc.groups' first match.
    fa::FileClasses classes;
    std::string error;
    if (!classes.load("data/arc.groups", error)) {
        std::printf("FAIL: cannot load data/arc.groups: %s\n", error.c_str());
        return 1;
    }
    check(classes.classify("src/a.txt") == "$text", ".txt is text");
    check(classes.classify("src/sub/c.bin") != "$text", ".bin is not text");

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("preset: all ok\n");
    return 0;
}

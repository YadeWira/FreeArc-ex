#include <cstdint>
#include <cstdio>

#include "fa/format.h"

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
    using namespace fa::format;

    // .arc format 0.67 (docs/format/ARC-FORMAT.md, sections 1 and 2):
    // the HEADER_BLOCK is 8 bytes: "ArC\x01" + 00 00 06 07.
    check(kSignature[0] == 'A' && kSignature[1] == 'r' && kSignature[2] == 'C',
          "ASCII signature \"ArC\"");
    check(kSignature[3] == 0x01, "signature version byte = 0x01");
    check(kVersionBytes[0] == 0x00 && kVersionBytes[1] == 0x00 && kVersionBytes[2] == 0x06 &&
              kVersionBytes[3] == 0x07,
          "format version 0.67 = 00 00 06 07");
    check(kHeaderBlockSize == 8, "8-byte HEADER_BLOCK");
    check(kDescriptorSearchWindow == 4096, "descriptor search window = 4096 bytes");

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("format: all ok\n");
    return 0;
}

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Constants of the FreeArc 0.67 .arc format, taken from
// docs/format/ARC-FORMAT.md (a spec derived from ArhiveStructure.hs,
// ArhiveDirectory.hs, ByteStream.hs and Unarc/ArcStructure.h, and validated
// byte by byte against real archives). Every multi-byte number is little-endian.
namespace fa::format {

// Signature of the HEADER_BLOCK and of the local descriptors: "ArC\x01".
inline constexpr std::array<std::uint8_t, 4> kSignature{'A', 'r', 'C', 0x01};

// Format version that follows the signature inside the HEADER_BLOCK:
// 00 00 06 07 (FreeArc 0.67). Stored as bytes so it does not depend on read order.
inline constexpr std::array<std::uint8_t, 4> kVersionBytes{0x00, 0x00, 0x06, 0x07};

// HEADER_BLOCK: signature (4) + version (4), always stored uncompressed.
inline constexpr std::size_t kHeaderBlockSize = kSignature.size() + kVersionBytes.size();

// The FOOTER_BLOCK descriptor must start within the last 4096 bytes of the file;
// that is the window scanned when reading the archive backwards.
inline constexpr std::size_t kDescriptorSearchWindow = 4096;

}  // namespace fa::format

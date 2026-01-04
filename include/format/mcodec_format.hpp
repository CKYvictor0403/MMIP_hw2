#pragma once

#include <cstdint>

namespace mcodec {

// IMPORTANT:
// Do NOT write/read this struct by dumping raw memory or using sizeof(MCodecHeader).
// Struct padding/alignment is compiler-dependent. Always serialize field-by-field.
inline constexpr uint16_t kMCodecHeaderBytes = 32; // fixed on-disk header size for v1

// .mcodec file layout:
// [Header][payload...]
//
// Header fields are little-endian.
struct MCodecHeader {
    char     magic[4];        // "MCDC"
    uint16_t version;         // codec version
    uint16_t header_bytes;    // fixed header size

    uint32_t width;
    uint32_t height;
    uint16_t channels;        // 1 for grayscale
    uint16_t bits_allocated;  // 8 / 16
    uint16_t bits_stored;     // e.g. 12
    uint8_t  is_signed;       // 0/1
    uint8_t  flags;           // reserved for future

    uint16_t block_size;      // 8 or 16
    uint16_t quality;         // quantization quality

    uint32_t payload_bytes;   // bytes after header
};


} // namespace mcodec



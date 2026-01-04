#include "preprocess/level_shift.hpp"

#include <cstdint>
#include <stdexcept>
#include <algorithm>

// ------------------------------------------------------------
// Helper
// ------------------------------------------------------------
static inline int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
    return std::min(std::max(v, lo), hi);
}


namespace mcodec {

// ------------------------------------------------------------
// Encode-side: apply level shift
// ------------------------------------------------------------
// Purpose:
//   Move pixel values to a zero-centered signed domain
//   for better transform and quantization efficiency.
//
// Rule:
//   If original image is unsigned:
//       x' = x - 2^(B-1)
//   If original image is signed:
//       do nothing
//
void apply_level_shift(Image& img) {
    if (img.empty()) return;

    // Basic sanity checks
    if (img.bits_stored <= 0 || img.bits_stored > 16) {
        throw std::runtime_error("apply_level_shift: invalid bits_stored");
    }

    // If already signed, nothing to do
    if (img.is_signed) {
        return;
    }

    const int32_t offset = 1 << (img.bits_stored - 1);

    for (auto& v : img.pixels) {
        v -= offset;
    }

    // After level shift, the internal representation is signed
    img.is_signed = true;
}

// ------------------------------------------------------------
// Decode-side: inverse level shift
// ------------------------------------------------------------
// Purpose:
//   Restore pixel values back to the original unsigned domain
//   after inverse transform and dequantization.
//
// Rule:
//   x = clamp(x' + 2^(B-1), 0, 2^B - 1)
//
void inverse_level_shift(Image& img) {
    if (img.empty()) return;

    // Sanity checks
    if (img.bits_stored <= 0 || img.bits_stored > 16) {
        throw std::runtime_error("inverse_level_shift: invalid bits_stored");
    }

    const int32_t offset = 1 << (img.bits_stored - 1);
    const int32_t minv   = 0;
    const int32_t maxv   = (1 << img.bits_stored) - 1;

    for (auto& v : img.pixels) {
        v = clamp_i32(v + offset, minv, maxv);
    }

    // Restored to unsigned semantic domain
    img.is_signed = false;
}

#ifndef NDEBUG
namespace {
// Debug self-test: unsigned 8-bit image should round-trip with level shift.
struct LevelShiftSelfTest {
    LevelShiftSelfTest() {
        mcodec::Image img;
        img.width = 2;
        img.height = 2;
        img.channels = 1;
        img.bits_stored = 8;
        img.bits_allocated = 8;
        img.is_signed = false;
        img.pixels = {0, 10, 200, 255};

        auto orig = img.pixels;

        mcodec::apply_level_shift(img);
        if (!img.is_signed) throw std::runtime_error("level_shift self-test: expected signed");

        mcodec::inverse_level_shift(img);
        if (img.is_signed) throw std::runtime_error("level_shift self-test: expected unsigned after inverse");
        if (img.pixels != orig) throw std::runtime_error("level_shift self-test: round-trip mismatch");
    }
};
static LevelShiftSelfTest _level_shift_self_test{};
} // namespace
#endif

} // namespace mcodec
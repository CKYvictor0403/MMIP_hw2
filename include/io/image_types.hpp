#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace mcodec {

enum class PixelType : uint8_t {
    U8  = 1,
    U16 = 2,
    S16 = 3,
};

struct Image {
    int width = 0;
    int height = 0;
    int channels = 1;         // grayscale only for now
    int bits_stored = 0;      // 8/12/16
    int bits_allocated = 0;   // 8/16
    bool is_signed = false;
    PixelType type = PixelType::U16;
    std::vector<int32_t> pixels; // unified buffer

    size_t size() const { return pixels.size(); }
    bool empty() const { return pixels.empty(); }
};

} // namespace mcodec



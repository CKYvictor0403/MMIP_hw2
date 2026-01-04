#pragma once

#include <vector>
#include <cstdint>
#include "io/image_types.hpp"

namespace mcodec {

// Encode image to .mcodec bytes (minimal baseline: optional RLE on int32 stream).
std::vector<uint8_t> encode_to_mcodec(const Image& im, int quality);

} // namespace mcodec



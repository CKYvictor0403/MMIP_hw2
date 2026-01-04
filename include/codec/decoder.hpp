#pragma once

#include <vector>
#include <cstdint>
#include "io/image_types.hpp"

namespace mcodec {

// Decode .mcodec bytes to image.
Image decode_from_mcodec(const std::vector<uint8_t>& bytes);

} // namespace mcodec



#pragma once

#include "io/image_types.hpp"

namespace mcodec {

// Level shift:
// - encode side: move unsigned pixels to zero-centered signed domain
// - decode side: restore back to unsigned domain
void apply_level_shift(Image& im);
void inverse_level_shift(Image& im);

} // namespace mcodec



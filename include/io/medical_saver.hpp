#pragma once

#include <string>
#include "io/image_types.hpp"

namespace mcodec {

// Minimal saver:
// - PGM (P5) 8/16-bit
void save_pgm(const std::string& path, const Image& im);

} // namespace mcodec



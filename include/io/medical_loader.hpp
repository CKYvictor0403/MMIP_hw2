#pragma once

#include <string>
#include "io/image_types.hpp"

namespace mcodec {

// Minimal loader:
// - PGM (P5) 8/16-bit supported
// - DICOM (DCMTK) supported for uncompressed grayscale 16-bit (common CT/MR)
// - Others (PNG/TIFF) are placeholders for now
Image load_medical(const std::string& path);

} // namespace mcodec



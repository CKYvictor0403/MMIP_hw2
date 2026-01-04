#pragma once

#include <cstdint>
#include <vector>

namespace mcodec {

// Map quality [1..100] to scalar step (clamped), baseline: step = 101 - quality.
int quant_step_from_quality(int quality);

// Uniform scalar quantization.
// block_size is validated (8 or 16), but not otherwise used in scalar quant.
void quantize(const std::vector<float>& coeff_in,
              int block_size,
              int quality,
              std::vector<int16_t>& qcoeff_out);

// Dequantization (inverse of above).
void dequantize(const std::vector<int16_t>& qcoeff_in,
                int block_size,
                int quality,
                std::vector<float>& coeff_out);

} // namespace mcodec



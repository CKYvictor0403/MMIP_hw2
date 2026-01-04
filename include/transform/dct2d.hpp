#pragma once

#include <vector>
#include <cstdint>

namespace mcodec {

// Forward DCT (block-wise, DCT-II, orthonormal scaling)
// blocks_in: int32 values, length = k * (N*N)
// coeff_out: float output, resized inside
void dct2d_blocks(const std::vector<int32_t>& blocks_in,
                  int block_size,
                  std::vector<float>& coeff_out);

void idct2d_blocks(const std::vector<float>& coeff_in,
                   int block_size,
                   std::vector<int32_t>& blocks_out);

} // namespace mcodec



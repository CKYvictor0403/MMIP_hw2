#pragma once

#include <vector>
#include <cstdint>

namespace mcodec {

// Generate zigzag order indices for NxN.
std::vector<int> make_zigzag_order(int N);

// Scan blocks (int16) using zigzag order, concat all blocks.
void zigzag_scan_blocks(const std::vector<int16_t>& qcoeff_in,
                        int block_size,
                        std::vector<int16_t>& seq_out);

// Inverse zigzag: fill blocks from concatenated sequence.
void inverse_zigzag_blocks(const std::vector<int16_t>& seq_in,
                           int block_size,
                           std::vector<int16_t>& qcoeff_out);

} // namespace mcodec



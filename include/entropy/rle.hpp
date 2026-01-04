#pragma once

#include <cstdint>
#include <vector>

namespace mcodec {

struct RlePair {
    int16_t value;
    uint16_t run; // number of following zeros
};

// Encode a sequence of int16_t (single block or concatenated blocks) with zero RLE.
// Assumes AC coefficients dominate zeros; DC is stored as (value, run=0).
void rle_encode_zeros(const std::vector<int16_t>& seq_in,
                      int block_size,
                      std::vector<RlePair>& rle_out);

// Decode RLE back to int16_t sequence (length must be block_size*block_size*k).
void rle_decode_zeros(const std::vector<RlePair>& rle_in,
                      int block_size,
                      size_t total_coeffs,
                      std::vector<int16_t>& seq_out);

// Pack RLE pairs into 32-bit symbols: (run << 16) | uint16_t(value)
void pack_rle_symbols(const std::vector<RlePair>& pairs,
    std::vector<uint32_t>& symbols);
// Unpack 32-bit symbols back to RLE pairs.
void unpack_rle_symbols(const std::vector<uint32_t>& symbols,
      std::vector<RlePair>& pairs);

} // namespace mcodec



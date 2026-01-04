#include "entropy/rle.hpp"

#include <stdexcept>
#include <limits>

namespace mcodec {

void rle_encode_zeros(const std::vector<int16_t>& seq_in,
                      int block_size,
                      std::vector<RlePair>& rle_out) {
    if (block_size != 8 && block_size != 16) {
        throw std::runtime_error("rle_encode_zeros: block_size must be 8 or 16");
    }
    const size_t block_elems = static_cast<size_t>(block_size * block_size);
    if (seq_in.size() % block_elems != 0) {
        throw std::runtime_error("rle_encode_zeros: input size not multiple of block");
    }

    rle_out.clear();
    rle_out.reserve(seq_in.size());

    for (size_t i = 0; i < seq_in.size(); ) {
        // DC
        int16_t dc = seq_in[i++];
        rle_out.push_back({dc, 0});

        uint16_t run = 0;
        const size_t block_end = i + block_elems - 1; // consumed DC already
        for (; i < block_end; ++i) {
            int16_t v = seq_in[i];
            if (v == 0) {
                if (run == std::numeric_limits<uint16_t>::max()) {
                    rle_out.push_back({0, run});
                    run = 0;
                }
                ++run;
            } else {
                rle_out.push_back({v, run});
                run = 0;
            }
        }
        // push the last run of zeros
        if (run > 0) {
            // trailing zeros: encode as zeros with run-1 so decode yields exactly 'run' zeros
            uint16_t store_run = static_cast<uint16_t>(run - 1);
            rle_out.push_back({0, store_run});
        }
    }
}

void rle_decode_zeros(const std::vector<RlePair>& rle_in,
                      int block_size,
                      size_t total_coeffs,
                      std::vector<int16_t>& seq_out) {
    if (block_size != 8 && block_size != 16) {
        throw std::runtime_error("rle_decode_zeros: block_size must be 8 or 16");
    }
    seq_out.clear();
    seq_out.reserve(total_coeffs);

    for (const auto& p : rle_in) {
        // run zeros first, then the value
        if (p.run > 0) {
            seq_out.insert(seq_out.end(), static_cast<size_t>(p.run), static_cast<int16_t>(0));
        }
        seq_out.push_back(p.value);
        if (seq_out.size() > total_coeffs) {
            throw std::runtime_error("rle_decode_zeros: output exceeds expected size");
        }
    }
    if (seq_out.size() != total_coeffs) {
        throw std::runtime_error("rle_decode_zeros: output size mismatch");
    }
}

// Pack RLE pairs into 32-bit symbols: (run << 16) | uint16_t(value)
void pack_rle_symbols(const std::vector<RlePair>& pairs,
                      std::vector<uint32_t>& symbols) {
    symbols.clear();
    symbols.reserve(pairs.size());
    for (const auto& p : pairs) {
        uint32_t sym = (static_cast<uint32_t>(p.run) << 16) |
                       static_cast<uint16_t>(p.value);
        symbols.push_back(sym);
    }
}

// Unpack 32-bit symbols back to RLE pairs.
void unpack_rle_symbols(const std::vector<uint32_t>& symbols,
                        std::vector<RlePair>& pairs) {
    pairs.clear();
    pairs.reserve(symbols.size());
    for (uint32_t sym : symbols) {
        uint16_t run = static_cast<uint16_t>(sym >> 16);
        uint16_t val_bits = static_cast<uint16_t>(sym & 0xFFFFu);
        int16_t value = static_cast<int16_t>(val_bits);
        pairs.push_back(RlePair{value, run});
    }
}



#ifndef NDEBUG
namespace {
// Self-test: one block of 8x8 with many zeros should round-trip.
struct RleSelfTest {
    RleSelfTest() {
        const int N = 8;
        const size_t block_elems = static_cast<size_t>(N * N);
        std::vector<int16_t> src(block_elems, 0);
        // set a few nonzeros
        src[0] = 5;      // DC
        src[5] = -3;
        src[12] = 7;
        src[63] = -1;

        std::vector<RlePair> rle;
        rle_encode_zeros(src, N, rle);

        std::vector<int16_t> recon;
        rle_decode_zeros(rle, N, block_elems, recon);

        if (recon != src) {
            throw std::runtime_error("rle self-test: round-trip mismatch");
        }
    }
};
static RleSelfTest _rle_self_test{};
} // namespace
#endif

} // namespace mcodec



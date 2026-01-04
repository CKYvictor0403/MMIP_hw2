#include "block/zigzag.hpp"

#include <stdexcept>

namespace mcodec {

std::vector<int> make_zigzag_order(int N) {
    if (N <= 0) throw std::runtime_error("make_zigzag_order: N must be positive");
    std::vector<int> order(static_cast<size_t>(N * N));
    int idx = 0;
    for (int s = 0; s <= 2 * (N - 1); ++s) {
        if (s % 2 == 0) {
            // even sum: traverse from bottom to top
            for (int x = 0; x <= s; ++x) {
                int y = s - x;
                if (x < N && y < N) order[idx++] = y * N + x;
            }
        } else {
            // odd sum: traverse from top to bottom
            for (int y = 0; y <= s; ++y) {
                int x = s - y;
                if (x < N && y < N) order[idx++] = y * N + x;
            }
        }
    }
    return order;
}

void zigzag_scan_blocks(const std::vector<int16_t>& qcoeff_in,
                        int block_size,
                        std::vector<int16_t>& seq_out) {
    if (block_size != 8 && block_size != 16) {
        throw std::runtime_error("zigzag_scan_blocks: block_size must be 8 or 16");
    }
    const size_t block_elems = static_cast<size_t>(block_size * block_size);
    if (qcoeff_in.size() % block_elems != 0) {
        throw std::runtime_error("zigzag_scan_blocks: input size not multiple of block");
    }

    auto order = make_zigzag_order(block_size);
    const size_t blocks = qcoeff_in.size() / block_elems;
    seq_out.resize(qcoeff_in.size());

    for (size_t b = 0; b < blocks; ++b) {
        const int16_t* src = qcoeff_in.data() + b * block_elems;
        int16_t* dst = seq_out.data() + b * block_elems;
        for (size_t i = 0; i < block_elems; ++i) {
            dst[i] = src[static_cast<size_t>(order[i])];
        }
    }
}

void inverse_zigzag_blocks(const std::vector<int16_t>& seq_in,
                           int block_size,
                           std::vector<int16_t>& qcoeff_out) {
    if (block_size != 8 && block_size != 16) {
        throw std::runtime_error("inverse_zigzag_blocks: block_size must be 8 or 16");
    }
    const size_t block_elems = static_cast<size_t>(block_size * block_size);
    if (seq_in.size() % block_elems != 0) {
        throw std::runtime_error("inverse_zigzag_blocks: input size not multiple of block");
    }

    auto order = make_zigzag_order(block_size);
    const size_t blocks = seq_in.size() / block_elems;
    qcoeff_out.resize(seq_in.size());

    for (size_t b = 0; b < blocks; ++b) {
        const int16_t* src = seq_in.data() + b * block_elems;
        int16_t* dst = qcoeff_out.data() + b * block_elems;
        for (size_t i = 0; i < block_elems; ++i) {
            dst[static_cast<size_t>(order[i])] = src[i];
        }
    }
}

#ifndef NDEBUG
namespace {
// Simple self-test: zigzag scan + inverse on a small block should round-trip.
struct ZigzagSelfTest {
    ZigzagSelfTest() {
        const int N = 8;
        const size_t block_elems = static_cast<size_t>(N * N);
        std::vector<int16_t> src(block_elems);
        for (size_t i = 0; i < block_elems; ++i) {
            src[i] = static_cast<int16_t>(i);
        }
        std::vector<int16_t> seq;
        zigzag_scan_blocks(src, N, seq);
        std::vector<int16_t> recon;
        inverse_zigzag_blocks(seq, N, recon);
        if (recon != src) {
            throw std::runtime_error("zigzag self-test: round-trip mismatch");
        }
    }
};
static ZigzagSelfTest _zigzag_self_test{};
} // namespace
#endif

} // namespace mcodec
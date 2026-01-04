#include "block/tiling.hpp"

#include <stdexcept>

namespace mcodec {

BlockGrid make_grid(int width, int height, int block_size) {
    if (block_size != 8 && block_size != 16) {
        throw std::runtime_error("make_grid: block_size must be 8 or 16");
    }
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("make_grid: invalid image size");
    }
    BlockGrid g;
    g.block_size = block_size;
    g.blocks_x = (width + block_size - 1) / block_size;
    g.blocks_y = (height + block_size - 1) / block_size;
    g.padded_w = g.blocks_x * block_size;
    g.padded_h = g.blocks_y * block_size;
    return g;
}

std::vector<int32_t> tile_to_blocks(const Image& img, const BlockGrid& g) {
    if (img.channels != 1) throw std::runtime_error("tile_to_blocks: only grayscale supported");
    if (img.width <= 0 || img.height <= 0) throw std::runtime_error("tile_to_blocks: invalid image size");
    if (static_cast<int>(img.pixels.size()) != img.width * img.height) {
        throw std::runtime_error("tile_to_blocks: pixel buffer mismatch");
    }
    if (g.padded_w <= 0 || g.padded_h <= 0) {
        throw std::runtime_error("tile_to_blocks: invalid grid");
    }

    std::vector<int32_t> padded(static_cast<size_t>(g.padded_w) * g.padded_h, 0);

    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const size_t src_idx = static_cast<size_t>(y) * img.width + x;
            const size_t dst_idx = static_cast<size_t>(y) * g.padded_w + x;
            padded[dst_idx] = img.pixels[src_idx];
        }
    }
    return padded;
}

void untile_from_blocks(Image& img, const BlockGrid& g, const std::vector<int32_t>& padded) {
    if (img.channels != 1) throw std::runtime_error("untile_from_blocks: only grayscale supported");
    if (img.width <= 0 || img.height <= 0) throw std::runtime_error("untile_from_blocks: invalid image size");
    if (static_cast<int>(padded.size()) != g.padded_w * g.padded_h) {
        throw std::runtime_error("untile_from_blocks: padded buffer mismatch");
    }

    img.pixels.resize(static_cast<size_t>(img.width) * img.height);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const size_t src_idx = static_cast<size_t>(y) * g.padded_w + x;
            const size_t dst_idx = static_cast<size_t>(y) * img.width + x;
            img.pixels[dst_idx] = padded[src_idx];
        }
    }
}

#ifndef NDEBUG
namespace {
// Simple self-test to validate tiling/untile round-trip on a tiny image.
struct TilingSelfTest {
    TilingSelfTest() {
        Image img;
        img.width = 16;
        img.height = 6;
        img.channels = 1;
        img.bits_stored = 8;
        img.bits_allocated = 8;
        img.is_signed = false;
        img.pixels = {
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
            17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
            33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
            49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
            65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
            81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96
        };

        BlockGrid g = make_grid(img.width, img.height, 8); // use supported block size
        auto padded = tile_to_blocks(img, g);
        if (static_cast<int>(padded.size()) != g.padded_w * g.padded_h) {
            throw std::runtime_error("tiling self-test: padded size mismatch");
        }

        Image out;
        out.width = img.width;
        out.height = img.height;
        out.channels = 1;
        out.bits_allocated = img.bits_allocated;
        out.bits_stored = img.bits_stored;
        out.is_signed = img.is_signed;
        untile_from_blocks(out, g, padded);
        if (out.pixels != img.pixels) {
            throw std::runtime_error("tiling self-test: round-trip mismatch");
        }
    }
};
static TilingSelfTest _tiling_self_test{};
} // namespace
#endif

} // namespace mcodec

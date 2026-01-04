#pragma once

#include <vector>
#include "io/image_types.hpp"

namespace mcodec {

// Block tiling metadata
struct BlockGrid {
    int block_size = 8;
    int blocks_x = 0;
    int blocks_y = 0;
    int padded_w = 0;
    int padded_h = 0;
};

// Create grid info from image dimensions and block size (8 or 16).
BlockGrid make_grid(int width, int height, int block_size);

// Tile image into padded blocks (raster order), output size = padded_w * padded_h.
std::vector<int32_t> tile_to_blocks(const Image& img, const BlockGrid& g);

// Reconstruct image from padded blocks (crop back to width x height in img).
void untile_from_blocks(Image& img, const BlockGrid& g, const std::vector<int32_t>& padded);

} // namespace mcodec


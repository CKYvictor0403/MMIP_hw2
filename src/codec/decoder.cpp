#include "codec/decoder.hpp"

#include "format/mcodec_format.hpp"

#include "preprocess/level_shift.hpp"

#include "entropy/bitstream.hpp"
#include "entropy/rle.hpp"
#include "entropy/huffman.hpp"

#include "block/tiling.hpp"
#include "block/zigzag.hpp"

#include "transform/dct2d.hpp"
#include "quant/quantizer.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace mcodec {

Image decode_from_mcodec(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < kMCodecHeaderBytes) {
        throw std::runtime_error("decode: buffer too small for header");
    }
    MCodecHeader hdr = read_bitstream_header(bytes);
    if (bytes.size() < hdr.header_bytes + hdr.payload_bytes) {
        throw std::runtime_error("decode: buffer smaller than declared payload_bytes");
    }

    const size_t payload_start = hdr.header_bytes;
    const size_t payload_end = hdr.header_bytes + hdr.payload_bytes;
    ByteReader r(std::vector<uint8_t>(bytes.begin() + payload_start, bytes.begin() + payload_end));

    // Huffman table section
    uint32_t symbol_count = r.read_u32_le();
    uint32_t used_symbol_count = r.read_u32_le();
    if (used_symbol_count == 0) {
        throw std::runtime_error("decode: used_symbol_count is zero");
    }
    if (r.remaining() < static_cast<size_t>(used_symbol_count) * (4u + 1u)) {
        throw std::runtime_error("decode: table section truncated");
    }
    std::vector<std::pair<uint32_t, uint8_t>> entries;
    entries.reserve(used_symbol_count);
    for (uint32_t i = 0; i < used_symbol_count; ++i) {
        uint32_t sym = r.read_u32_le();
        uint8_t len = r.read_u8();
        if (len == 0 || len > 32) {
            throw std::runtime_error("decode: invalid code length in table section");
        }
        entries.push_back({sym, len});
    }

    // Remaining are Huffman payload bits
    std::vector<uint8_t> huff_bits(r.remaining());
    if (!huff_bits.empty()) {
        r.read_bytes(huff_bits.data(), huff_bits.size());
    }

    // Rebuild Huffman table and decode symbols
    HuffTable table = build_table_from_code_lengths(entries);
    std::vector<uint32_t> symbols;
    symbols.reserve(symbol_count);
    huff_decode(huff_bits, table, symbol_count, symbols);

    // Unpack RLE pairs
    std::vector<RlePair> rle;
    unpack_rle_symbols(symbols, rle);

    // Recompute grid
    const int block_size = static_cast<int>(hdr.block_size);
    BlockGrid grid = make_grid(static_cast<int>(hdr.width), static_cast<int>(hdr.height), block_size);
    const size_t coeffs_per_block = static_cast<size_t>(block_size * block_size);
    const size_t total_coeffs = static_cast<size_t>(grid.blocks_x * grid.blocks_y) * coeffs_per_block;

    std::vector<int16_t> seq;
    rle_decode_zeros(rle, block_size, total_coeffs, seq);

    // Inverse zigzag -> qcoeff
    std::vector<int16_t> qcoeff;
    inverse_zigzag_blocks(seq, block_size, qcoeff);

    // Dequantize
    std::vector<float> coeffs;
    dequantize(qcoeff, block_size, static_cast<int>(hdr.quality), coeffs);

    // IDCT
    std::vector<int32_t> blocks;
    idct2d_blocks(coeffs, block_size, blocks);

    // Untile
    Image im;
    im.width = static_cast<int>(hdr.width);
    im.height = static_cast<int>(hdr.height);
    im.channels = static_cast<int>(hdr.channels);
    im.bits_allocated = static_cast<int>(hdr.bits_allocated);
    im.bits_stored = static_cast<int>(hdr.bits_stored);
    im.is_signed = (hdr.is_signed != 0);
    const bool level_shift_applied = (hdr.flags & 0x01u) != 0;
    im.type = im.is_signed ? PixelType::S16 : (im.bits_allocated <= 8 ? PixelType::U8 : PixelType::U16);

    untile_from_blocks(im, grid, blocks);

    // Inverse level shift and clip
    if (level_shift_applied) {
        inverse_level_shift(im);
    }

    if (static_cast<int>(im.pixels.size()) != im.width * im.height * im.channels) {
        throw std::runtime_error("decode: decoded pixel count mismatch");
    }
    return im;
}

} // namespace mcodec



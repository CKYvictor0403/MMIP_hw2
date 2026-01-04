#include "codec/encoder.hpp"

#include "preprocess/level_shift.hpp"

#include "entropy/bitstream.hpp"
#include "entropy/rle.hpp"
#include "entropy/huffman.hpp"

#include "block/tiling.hpp"
#include "block/zigzag.hpp"

#include "transform/dct2d.hpp"
#include "quant/quantizer.hpp"

#include <stdexcept>

#include <iostream>
#include <algorithm>

namespace mcodec {

std::vector<uint8_t> encode_to_mcodec(const Image& im, int quality) {
    Image img = im; // make a mutable working copy
    if (img.channels != 1) throw std::runtime_error("encode: only grayscale is supported");
    if (img.width <= 0 || img.height <= 0) throw std::runtime_error("encode: invalid image size");
    if (static_cast<int>(img.pixels.size()) != img.width * img.height) throw std::runtime_error("encode: buffer size mismatch");

    int block_size = 8;
    const bool level_shift_applied = !img.is_signed;
    //===Preprocess image===//
#ifndef NDEBUG
    std::cerr << "B(stored)=" << img.bits_stored
              << " allocated=" << img.bits_allocated
              << " is_signed=" << img.is_signed << "\n";
#endif

    // Debug: print 8x8 pixels before and after level shift
    apply_level_shift(img);
#ifndef NDEBUG
    if (!img.empty()) {
        const int N = block_size;
        std::fprintf(stderr, "First block of pixels AFTER level shift (%dx%d):\n", N, N);
        for (int v = 0; v < N; ++v) {
            for (int u = 0; u < N; ++u) {
                std::fprintf(stderr, "%5d ", img.pixels[v * N + u]);
            }
            std::fprintf(stderr, "\n");
        }
    }
#endif
    //===Tiling image===//
    BlockGrid grid = make_grid(img.width, img.height, block_size);
    std::vector<int32_t> blocks = tile_to_blocks(img, grid);
    
    //===Decorrelate===//
    std::vector<float> coeffs;
    dct2d_blocks(blocks, block_size, coeffs);

    // Debug: print first coefficient block
#ifndef NDEBUG
    if (!coeffs.empty()) {
        const int N = block_size;
        std::fprintf(stderr, "First DCT coefficient block (%dx%d):\n", N, N);
        for (int v = 0; v < N; ++v) {
            for (int u = 0; u < N; ++u) {
                std::fprintf(stderr, "%8.2f ", coeffs[v * N + u]);
            }
            std::fprintf(stderr, "\n");
        }
    }
#endif
    //===Quantizer===//
    std::vector<int16_t> qcoeff;
#ifndef NDEBUG
    std::fprintf(stderr, "Quantizing with quality %d\n", quality);
#endif
    quantize(coeffs, block_size, quality, qcoeff);

    // Debug: print first quantized coefficients
#ifndef NDEBUG
    if (!qcoeff.empty()) {
        const int N = block_size;
        std::fprintf(stderr, "First block of quantized coefficients (%dx%d):\n", N, N);
        for (int v = 0; v < N; ++v) {
            for (int u = 0; u < N; ++u) {
                std::fprintf(stderr, "%6d ", static_cast<int>(qcoeff[ v * N + u]));
            }
            std::fprintf(stderr, "\n");
        }
    }
#endif

    //===Scan===//
    std::vector<int16_t> zigzag_seq;
    zigzag_scan_blocks(qcoeff, block_size, zigzag_seq);

    // Debug: print first block of zigzag sequence
#ifndef NDEBUG
    std::fprintf(stderr, "First block of zigzag sequence (%dx%d):\n", block_size, block_size);
    for (int i = 0; i < block_size * block_size; ++i) {
        std::fprintf(stderr, "%6d ", static_cast<int>(zigzag_seq[i]));
    
        if ((i + 1) % block_size == 0) {
            std::fprintf(stderr, "\n");
        }
    }
    std::fprintf(stderr, "\n");
#endif

    //===Symbolization (RLE)===//
    std::vector<RlePair> rle;
    rle_encode_zeros(zigzag_seq, block_size, rle);

    // Debug: print first block of RLE pairs
#ifndef NDEBUG
    std::fprintf(stderr, "First block of RLE pairs (%dx%d):\n", block_size, block_size);
    for (int i = 0; i < block_size * block_size; ++i) {
        std::fprintf(stderr, "%6d %6d ", static_cast<int>(rle[i].value), static_cast<int>(rle[i].run));
        if ((i + 1) % block_size == 0) {
            std::fprintf(stderr, "\n");
        } else {
            std::fprintf(stderr, ", ");
        }
    }
    std::fprintf(stderr, "\n");
#endif

    //===Entropy Coding===//
    std::vector<uint32_t> symbols;
    pack_rle_symbols(rle, symbols);

    // Debug: print first block of symbols
#ifndef NDEBUG
    std::fprintf(stderr, "First 10 symbols (binary):\n");
    for (int i = 0; i < 10 && i < static_cast<int>(symbols.size()); ++i) {
        uint32_t sym = symbols[i];
        std::fprintf(stderr, "0b");
        for (int bit = 31; bit >= 0; --bit) {
            std::fprintf(stderr, "%d", (sym >> bit) & 1);
        }
        std::fprintf(stderr, " ");
    }
    std::fprintf(stderr, "\n");
#endif

    auto encoded = huff_encode(symbols);
    HuffTable table = std::move(encoded.first);
    std::vector<uint8_t> huff_encode_bits = std::move(encoded.second);

    // Debug: print Huffman table (code lengths)
#ifndef NDEBUG
    std::fprintf(stderr, "Huffman table (first 10 used symbols):\n");
    int count = 0;
    for (size_t i = 0; i < table.enc.size() && count < 10; ++i) {
        if (table.enc[i].valid) {
            std::fprintf(stderr,
                    "sym=%zu len=%u code=0x%x\n",
                    i,
                    table.enc[i].len,
                    table.enc[i].code);
            ++count;
        }
    }

    // Debug: print first 10 Huffman encoded bits (binary)
    std::fprintf(stderr, "First 2 Huffman encoded bytes (binary):\n");
    for (int i = 0; i < 2 && i < static_cast<int>(huff_encode_bits.size()); ++i) {
        std::fprintf(stderr, "0b");
        for (int bit = 7; bit >= 0; --bit) {
            std::fprintf(stderr, "%d", (huff_encode_bits[i] >> bit) & 1);
        }
        std::fprintf(stderr, " ");
    }
    std::fprintf(stderr, "\n");
#endif

    //===Bitstream Writer===//
    const uint8_t flags = level_shift_applied ? 0x01 : 0x00;
    const uint32_t symbol_count = static_cast<uint32_t>(symbols.size());

    // Collect used symbols (freq>0) with their code lengths
    std::vector<std::pair<uint32_t, uint8_t>> table_entries;
    table_entries.reserve(table.enc.size());
    for (uint32_t i = 0; i < table.enc.size(); ++i) {
        if (table.enc[i].valid && table.enc[i].len > 0) {
            table_entries.push_back({i, table.enc[i].len});
        }
    }
    // canonical rebuild要求 (len asc, symbol asc)
    std::sort(table_entries.begin(), table_entries.end(),
              [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second < b.second;
                  return a.first < b.first;
              });
    if (table_entries.empty()) {
        throw std::runtime_error("encode: no used symbols for Huffman table");
    }
    const uint32_t used_symbol_count = static_cast<uint32_t>(table_entries.size());

    // Huffman table section bytes: 
    // 4 bytes for symbol_count, 4 bytes for used_symbol_count, used_symbol_count * (4 bytes for symbol + 1 byte for code length)
    const uint32_t huff_table_section_bytes = 4u + 4u + used_symbol_count * (4u + 1u); 
    const uint32_t huff_payload_bytes = static_cast<uint32_t>(huff_encode_bits.size());
    const uint32_t payload_bytes = huff_table_section_bytes + huff_payload_bytes;

    ByteWriter w;
    // header (payload_bytes will be patched after table/payload are written)
    write_bitstream_header(w, img, flags, /*block_size=*/block_size, /*quality=*/quality);

    // Huffman table section
    w.write_u32_le(symbol_count);
    w.write_u32_le(used_symbol_count);
    for (const auto& [sym, len] : table_entries) {
        w.write_u32_le(sym);
        w.write_u8(len);
    }

    // Huffman payload bits
    w.write_bytes(huff_encode_bits.data(), huff_encode_bits.size());

    // patch payload_bytes (at fixed offset in header)
    std::vector<uint8_t> bytes = w.bytes();
    if (bytes.size() < kMCodecHeaderBytes) {
        throw std::runtime_error("encode: header size too small when patching payload_bytes");
    }
    bytes[28] = static_cast<uint8_t>(payload_bytes & 0xFF);
    bytes[29] = static_cast<uint8_t>((payload_bytes >> 8) & 0xFF);
    bytes[30] = static_cast<uint8_t>((payload_bytes >> 16) & 0xFF);
    bytes[31] = static_cast<uint8_t>((payload_bytes >> 24) & 0xFF);
    const uint32_t expected_size = kMCodecHeaderBytes + payload_bytes;
    if (bytes.size() != expected_size) {
        throw std::runtime_error("encode: buffer size mismatch after patching payload_bytes");
    }
    return std::move(bytes);
}


// Debug self-test: one 8x8 block round-trip DCT -> IDCT should equal original ints.
#ifndef NDEBUG
namespace {
struct DctSelfTest {
    DctSelfTest() {
        const int N = 8;
        std::vector<int32_t> src(static_cast<size_t>(N * N));
        for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<int32_t>(i);

        std::vector<float> coeffs;
        dct2d_blocks(src, N, coeffs);

        std::vector<int32_t> recon;
        idct2d_blocks(coeffs, N, recon);

        if (recon.size() != src.size()) {
            throw std::runtime_error("DCT self-test: size mismatch");
        }
        for (size_t i = 0; i < src.size(); ++i) {
            if (recon[i] != src[i]) {
                throw std::runtime_error("DCT self-test: round-trip mismatch");
            }
        }
    }
};
static DctSelfTest _dct_self_test{};
} // namespace
#endif

} // namespace mcodec



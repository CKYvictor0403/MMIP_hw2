#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace mcodec {

struct HuffTable {
    struct EncEntry {
        uint32_t code{0};
        uint8_t len{0};
        bool valid{false};
    };
    struct Node {
        int left{-1};
        int right{-1};
        int symbol{-1};
    };
    std::vector<EncEntry> enc;
    std::vector<Node> decode_nodes;
};
class BitWriter;
class BitReader;

// Build sparse (symbol,freq) list from a symbol stream.
void build_symbol_frequencies(const std::vector<uint32_t>& symbols,
                              std::vector<std::pair<uint32_t, uint32_t>>& sym_freq);

// Build canonical Huffman table (sparse symbol,freq pairs)
HuffTable build_canonical_table(const std::vector<std::pair<uint32_t, uint32_t>>& sym_freq);
// Build Huffman table from (symbol_id, code_len) entries (canonical assignment).
HuffTable build_table_from_code_lengths(const std::vector<std::pair<uint32_t, uint8_t>>& entries);

// Huffman encode: full pipeline from symbols -> (table, bitstream)
std::pair<HuffTable, std::vector<uint8_t>> huff_encode(const std::vector<uint32_t>& symbols);
// Huffman decode
void huff_decode(const std::vector<uint8_t>& bits,
                 const HuffTable& t,
                 size_t symbol_count,
                 std::vector<uint32_t>& out);

} // namespace mcodec
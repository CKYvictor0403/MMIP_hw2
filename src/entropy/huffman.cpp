#include "entropy/huffman.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mcodec {

// Build sparse (symbol,freq) list from symbols
void build_symbol_frequencies(const std::vector<uint32_t>& symbols,
                              std::vector<std::pair<uint32_t, uint32_t>>& sym_freq) {
    std::unordered_map<uint32_t, uint32_t> freq_map;
    freq_map.reserve(symbols.size());
    for (uint32_t s : symbols) {
        auto it = freq_map.find(s);
        if (it == freq_map.end()) {
            freq_map.emplace(s, 1u);
        } else {
            if (it->second == std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("huffman: frequency overflow");
            }
            ++it->second;
        }
    }
    sym_freq.clear();
    sym_freq.reserve(freq_map.size());
    for (const auto& kv : freq_map) {
        sym_freq.push_back(kv);
    }
    std::sort(sym_freq.begin(), sym_freq.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
}

// ---------------- BitWriter ---------------- //
class BitWriter {
public:
    void write_bits(uint32_t code, uint8_t bit_len) {
        if (bit_len == 0 || bit_len > 32) {
            throw std::runtime_error("BitWriter: invalid bit length");
        }
        // write MSB-first
        for (int i = bit_len - 1; i >= 0; --i) {
            uint8_t bit = static_cast<uint8_t>((code >> i) & 1u);
            cur_ = static_cast<uint8_t>((cur_ << 1) | bit);
            ++bit_pos_;
            if (bit_pos_ == 8) {
                data_.push_back(cur_);
                cur_ = 0;
                bit_pos_ = 0;
            }
        }
    }

    void flush() {
        if (bit_pos_ > 0) {
            cur_ <<= static_cast<uint8_t>(8 - bit_pos_);
            data_.push_back(cur_);
            cur_ = 0;
            bit_pos_ = 0;
        }
    }

    const std::vector<uint8_t>& data() const { return data_; }

private:
    std::vector<uint8_t> data_;
    uint8_t cur_{0};
    uint8_t bit_pos_{0}; // bits filled in cur_ (0..8)
};

// ---------------- BitReader ---------------- //
class BitReader {
public:
    explicit BitReader(const std::vector<uint8_t>& buf)
        : data_(buf) {}

    bool read_bit() {
        if (byte_idx_ >= data_.size()) {
            throw std::runtime_error("BitReader: out of data");
        }
        uint8_t byte = data_[byte_idx_];
        uint8_t bit = static_cast<uint8_t>((byte >> (7 - bit_pos_)) & 1u);
        ++bit_pos_;
        if (bit_pos_ == 8) {
            bit_pos_ = 0;
            ++byte_idx_;
        }
        return bit != 0;
    }

private:
    const std::vector<uint8_t>& data_;
    size_t byte_idx_{0};
    uint8_t bit_pos_{0};
};

// Internal node used during build
struct HeapNode {
    uint32_t freq;
    uint32_t symbol; // for tie-break; if internal, smallest symbol in subtree
    int left;  // index into temp nodes, -1 if leaf
    int right; // index into temp nodes
};

struct HeapComp {
    bool operator()(const HeapNode& a, const HeapNode& b) const {
        if (a.freq != b.freq) return a.freq > b.freq; // min-heap
        return a.symbol > b.symbol; // tie-break by smallest symbol
    }
};

// Build canonical Huffman table from frequencies
HuffTable build_canonical_table(const std::vector<std::pair<uint32_t, uint32_t>>& sym_freq) {
    if (sym_freq.empty()) {
        throw std::runtime_error("huffman: empty symbol-frequency list");
    }
    uint32_t max_sym = 0;
    bool has_nonzero = false;
    for (const auto& sf : sym_freq) {
        uint32_t sym = sf.first;
        uint32_t f = sf.second;
        if (f == 0) continue;
        has_nonzero = true;
        if (sym > max_sym) max_sym = sym;
    }
    if (!has_nonzero) {
        throw std::runtime_error("huffman: all frequencies are zero");
    }
    std::vector<uint32_t> freqs(static_cast<size_t>(max_sym) + 1, 0);
    for (const auto& sf : sym_freq) {
        uint32_t sym = sf.first;
        uint32_t f = sf.second;
        if (f == 0) continue;
        uint64_t sum = static_cast<uint64_t>(freqs[sym]) + static_cast<uint64_t>(f);
        if (sum > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("huffman: frequency overflow");
        }
        freqs[sym] = static_cast<uint32_t>(sum);
    }

    // Collect leaves
    std::priority_queue<HeapNode, std::vector<HeapNode>, HeapComp> pq;
    std::vector<HeapNode> nodes; // store merged nodes for traversal of lengths
    nodes.reserve(freqs.size() * 2);

    for (uint32_t i = 0; i < freqs.size(); ++i) {
        if (freqs[i] == 0) continue;
        HeapNode leaf{freqs[i], i, -1, -1};
        pq.push(leaf);
    }

    // Edge case: only one symbol
    if (pq.size() == 1) {
        HuffTable t;
        t.enc.resize(freqs.size());
        uint32_t sym = pq.top().symbol;
        t.enc[sym] = {0u, 1u, true};
        t.decode_nodes.push_back({-1, -1, -1}); // root
        t.decode_nodes.push_back({-1, -1, static_cast<int>(sym)}); // leaf
        t.decode_nodes[0].left = 1;
        return t;
    }

    // Build Huffman tree (non-canonical) to get lengths
    while (pq.size() > 1) {
        HeapNode a = pq.top(); pq.pop();
        HeapNode b = pq.top(); pq.pop();
        HeapNode parent;
        parent.freq = a.freq + b.freq;
        parent.symbol = std::min(a.symbol, b.symbol);
        parent.left = static_cast<int>(nodes.size());
        nodes.push_back(a);
        parent.right = static_cast<int>(nodes.size());
        nodes.push_back(b);
        pq.push(parent);
    }
    HeapNode root = pq.top();

    // Compute code lengths by DFS
    struct LenEntry { uint32_t symbol; uint8_t len; };
    std::vector<LenEntry> lens;
    std::vector<std::pair<int, uint8_t>> stack; // (node index, depth)
    stack.push_back({-1, 0}); // -1 represents root

    auto get_child = [&](int idx, bool left)->HeapNode {
        const HeapNode& n = (idx == -1) ? root : nodes[idx];
        int child_idx = left ? n.left : n.right;
        if (child_idx == -1) {
            throw std::runtime_error("huffman: invalid tree structure");
        }
        return nodes[child_idx];
    };

    while (!stack.empty()) {
        auto [idx, depth] = stack.back();
        stack.pop_back();
        HeapNode cur = (idx == -1) ? root : nodes[idx];
        if (cur.left == -1 && cur.right == -1) {
            // leaf
            lens.push_back({cur.symbol, static_cast<uint8_t>(depth)});
            continue;
        }
        if (depth >= 32) {
            throw std::runtime_error("huffman: code length exceeds 32");
        }
        // push children; right then left so left processed first
        if (cur.right != -1) stack.push_back({cur.right, static_cast<uint8_t>(depth + 1)});
        if (cur.left != -1) stack.push_back({cur.left, static_cast<uint8_t>(depth + 1)});
    }

    // Sort for canonical assignment
    std::sort(lens.begin(), lens.end(), [](const LenEntry& a, const LenEntry& b) {
        if (a.len != b.len) return a.len < b.len;
        return a.symbol < b.symbol;
    });

    // Assign canonical codes
    struct CanonEntry { uint32_t symbol; uint32_t code; uint8_t len; };
    std::vector<CanonEntry> canon;
    canon.reserve(lens.size());

    uint32_t code = 0;
    uint8_t prev_len = lens.front().len;
    code <<= (prev_len);
    for (size_t i = 0; i < lens.size(); ++i) {
        const auto& le = lens[i];
        if (le.len != prev_len) {
            code <<= (le.len - prev_len);
            prev_len = le.len;
        }
        canon.push_back({le.symbol, code, le.len});
        ++code;
    }

    // Build table
    HuffTable t;
    t.enc.resize(freqs.size());

    // decode tree root
    t.decode_nodes.push_back({-1, -1, -1});

    for (const auto& ce : canon) {
        // encode map
        if (ce.symbol >= t.enc.size()) {
            t.enc.resize(ce.symbol + 1);
        }
        t.enc[ce.symbol] = {ce.code, ce.len, true};

        // decode tree insert
        int node_idx = 0;
        for (int i = ce.len - 1; i >= 0; --i) {
            uint32_t bit = (ce.code >> i) & 1u;
            int next = (bit == 0) ? t.decode_nodes[node_idx].left : t.decode_nodes[node_idx].right;
            if (next == -1) {
                next = static_cast<int>(t.decode_nodes.size());
                t.decode_nodes.push_back({-1, -1, -1});
                if (bit == 0) t.decode_nodes[node_idx].left = next;
                else t.decode_nodes[node_idx].right = next;
            }
            node_idx = next;
        }
        if (t.decode_nodes[node_idx].symbol != -1) {
            throw std::runtime_error("huffman: duplicate code assignment");
        }
        t.decode_nodes[node_idx].symbol = static_cast<int>(ce.symbol);
    }

    return t;
}

HuffTable build_table_from_code_lengths(const std::vector<std::pair<uint32_t, uint8_t>>& entries) {
    if (entries.empty()) {
        throw std::runtime_error("decode: empty Huffman table entries");
    }
    for (const auto& e : entries) {
        if (e.second == 0 || e.second > 32) {
            throw std::runtime_error("decode: invalid code length");
        }
    }
    // sort by (len asc, symbol asc)
    std::vector<std::pair<uint32_t, uint8_t>> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second < b.second;
        return a.first < b.first;
    });

    // assign canonical codes
    struct CanonEntry { uint32_t symbol; uint32_t code; uint8_t len; };
    std::vector<CanonEntry> canon;
    canon.reserve(sorted.size());

    uint32_t code = 0;
    uint8_t prev_len = sorted.front().second;
    code <<= prev_len;
    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& le = sorted[i];
        if (le.second != prev_len) {
            code <<= (le.second - prev_len);
            prev_len = le.second;
        }
        canon.push_back({le.first, code, le.second});
        ++code;
    }

    // build HuffTable
    HuffTable t;
    uint32_t max_sym = 0;
    for (const auto& c : canon) max_sym = std::max(max_sym, c.symbol);
    t.enc.assign(static_cast<size_t>(max_sym + 1), {});

    t.decode_nodes.push_back({-1, -1, -1}); // root
    for (const auto& ce : canon) {
        t.enc[ce.symbol] = {ce.code, ce.len, true};

        int node_idx = 0;
        for (int i = ce.len - 1; i >= 0; --i) {
            uint32_t bit = (ce.code >> i) & 1u;
            int next = (bit == 0) ? t.decode_nodes[node_idx].left : t.decode_nodes[node_idx].right;
            if (next == -1) {
                next = static_cast<int>(t.decode_nodes.size());
                t.decode_nodes.push_back({-1, -1, -1});
                if (bit == 0) t.decode_nodes[node_idx].left = next;
                else t.decode_nodes[node_idx].right = next;
            }
            node_idx = next;
        }
        if (t.decode_nodes[node_idx].symbol != -1) {
            throw std::runtime_error("decode: duplicate code assignment");
        }
        t.decode_nodes[node_idx].symbol = static_cast<int>(ce.symbol);
    }
    return t;
}

std::pair<HuffTable, std::vector<uint8_t>> huff_encode(const std::vector<uint32_t>& symbols) {
    if (symbols.empty()) {
        throw std::runtime_error("huffman encode: empty symbols");
    }
    std::vector<std::pair<uint32_t, uint32_t>> freqs;
    build_symbol_frequencies(symbols, freqs);
    HuffTable t = build_canonical_table(freqs);

    BitWriter bw;
    for (uint32_t s : symbols) {
        if (s >= t.enc.size() || !t.enc[s].valid) {
            throw std::runtime_error("huffman encode: symbol not in table");
        }
        const auto& e = t.enc[s];
        bw.write_bits(e.code, e.len);
    }
    bw.flush();
    std::vector<uint8_t> bits = bw.data();
    return {std::move(t), std::move(bits)};
}

void huff_decode(const std::vector<uint8_t>& bits,
                 const HuffTable& t,
                 size_t symbol_count,
                 std::vector<uint32_t>& out) {
    BitReader br(bits);
    out.clear();
    out.reserve(symbol_count);
    for (size_t n = 0; n < symbol_count; ++n) {
        int node = 0;
        while (true) {
            if (node < 0 || static_cast<size_t>(node) >= t.decode_nodes.size()) {
                throw std::runtime_error("huffman decode: invalid node");
            }
            const auto& nd = t.decode_nodes[node];
            if (nd.symbol != -1) {
                out.push_back(static_cast<uint32_t>(nd.symbol));
                break;
            }
            bool bit = br.read_bit();
            node = bit ? nd.right : nd.left;
            if (node == -1) {
                throw std::runtime_error("huffman decode: reached null child");
            }
        }
    }
}

#ifndef NDEBUG
namespace {
// Minimal self-test: build table, encode, decode.
struct HuffmanSelfTest {
    HuffmanSelfTest() {
        std::vector<uint32_t> symbols = {3, 0, 1, 3, 2, 2, 3};
        //encode
        auto encoded = huff_encode(symbols);
        auto table = std::move(encoded.first);
        
        //decode 
        std::vector<uint32_t> decoded;
        huff_decode(encoded.second, table, symbols.size(), decoded);
        if (decoded != symbols) {
            throw std::runtime_error("huffman self-test: round-trip mismatch");
        }
    }
};
static HuffmanSelfTest _huff_self_test{};
} // namespace
#endif

} // namespace mcodec
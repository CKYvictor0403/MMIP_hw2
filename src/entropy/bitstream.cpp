#include "entropy/bitstream.hpp"

#include <ostream>
#include <istream>

namespace mcodec {

namespace {
static uint16_t read_u16_le_at(const std::vector<uint8_t>& b, size_t off) {
    if (off + 2 > b.size()) throw std::runtime_error("bitstream: premature EOF (u16)");
    return static_cast<uint16_t>(b[off] | (static_cast<uint16_t>(b[off + 1]) << 8));
}
static uint32_t read_u32_le_at(const std::vector<uint8_t>& b, size_t off) {
    if (off + 4 > b.size()) throw std::runtime_error("bitstream: premature EOF (u32)");
    return static_cast<uint32_t>(b[off] |
                                 (static_cast<uint32_t>(b[off + 1]) << 8) |
                                 (static_cast<uint32_t>(b[off + 2]) << 16) |
                                 (static_cast<uint32_t>(b[off + 3]) << 24));
}
} // namespace

void write_bitstream_header(ByteWriter& w,
                            const Image& im,
                            uint8_t flags,
                            uint16_t block_size,
                            uint16_t quality) {
    MCodecHeader hdr{};
    // "MCDC"
    hdr.magic[0] = 'M';
    hdr.magic[1] = 'C';
    hdr.magic[2] = 'D';
    hdr.magic[3] = 'C';
    hdr.version = 1;
    hdr.header_bytes = kMCodecHeaderBytes;

    hdr.width = static_cast<uint32_t>(im.width);
    hdr.height = static_cast<uint32_t>(im.height);
    hdr.channels = static_cast<uint16_t>(im.channels);
    hdr.bits_allocated = static_cast<uint16_t>(im.bits_allocated);
    hdr.bits_stored = static_cast<uint16_t>(im.bits_stored);
    hdr.is_signed = static_cast<uint8_t>(im.is_signed ? 1 : 0);
    hdr.flags = flags; // bit0 = RLE (payload encoding)
    hdr.block_size = block_size;
    hdr.quality = quality;
    hdr.payload_bytes = 0; // patch later by caller

    w.write_bytes(hdr.magic, 4);
    w.write_u16_le(hdr.version);
    w.write_u16_le(hdr.header_bytes);
    w.write_u32_le(hdr.width);
    w.write_u32_le(hdr.height);
    w.write_u16_le(hdr.channels);
    w.write_u16_le(hdr.bits_allocated);
    w.write_u16_le(hdr.bits_stored);
    w.write_u8(hdr.is_signed);
    w.write_u8(hdr.flags);
    w.write_u16_le(hdr.block_size);
    w.write_u16_le(hdr.quality);
    w.write_u32_le(hdr.payload_bytes);
}

MCodecHeader read_bitstream_header(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < kMCodecHeaderBytes) throw std::runtime_error("decode: file too small");

    MCodecHeader hdr{};
    hdr.magic[0] = static_cast<char>(bytes[0]);
    hdr.magic[1] = static_cast<char>(bytes[1]);
    hdr.magic[2] = static_cast<char>(bytes[2]);
    hdr.magic[3] = static_cast<char>(bytes[3]);
    hdr.version = read_u16_le_at(bytes, 4);
    hdr.header_bytes = read_u16_le_at(bytes, 6);
    hdr.width = read_u32_le_at(bytes, 8);
    hdr.height = read_u32_le_at(bytes, 12);
    hdr.channels = read_u16_le_at(bytes, 16);
    hdr.bits_allocated = read_u16_le_at(bytes, 18);
    hdr.bits_stored = read_u16_le_at(bytes, 20);
    hdr.is_signed = bytes[22];
    hdr.flags = bytes[23];
    hdr.block_size = read_u16_le_at(bytes, 24);
    hdr.quality = read_u16_le_at(bytes, 26);
    hdr.payload_bytes = read_u32_le_at(bytes, 28);

    if (!(hdr.magic[0] == 'M' && hdr.magic[1] == 'C' && hdr.magic[2] == 'D' && hdr.magic[3] == 'C')) {
        throw std::runtime_error("decode: bad magic");
    }
    if (hdr.version != 1) throw std::runtime_error("decode: unsupported version");
    if (hdr.header_bytes < kMCodecHeaderBytes) throw std::runtime_error("decode: invalid header_bytes");
    if (bytes.size() < hdr.header_bytes) throw std::runtime_error("decode: truncated header");
    return hdr;
}

void write_payload(ByteWriter& w, const uint8_t* data, size_t bytes) {
    if (!data && bytes != 0) throw std::runtime_error("bitstream: write_payload null data");
    w.write_bytes(data, bytes);
}

void read_payload(ByteReader& r, uint8_t* data, size_t bytes) {
    if (!data && bytes != 0) throw std::runtime_error("bitstream: read_payload null data");
    r.read_bytes(data, bytes);
}

} // namespace mcodec

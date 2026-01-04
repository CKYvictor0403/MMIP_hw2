#pragma once

#include <iosfwd>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <cstring>

#include "format/mcodec_format.hpp"
#include "io/image_types.hpp"

namespace mcodec {

class ByteWriter {
public:
    void write_u8(uint8_t v) { buf_.push_back(v); }
    void write_u16_le(uint16_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    void write_u32_le(uint32_t v) {
        write_u16_le(static_cast<uint16_t>(v & 0xFFFF));
        write_u16_le(static_cast<uint16_t>((v >> 16) & 0xFFFF));
    }
    void write_bytes(const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    const std::vector<uint8_t>& bytes() const { return buf_; }
private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    explicit ByteReader(std::vector<uint8_t> data) : buf_(std::move(data)) {}

    uint8_t read_u8() {
        need(1);
        return buf_[pos_++];
    }
    uint16_t read_u16_le() {
        uint16_t lo = read_u8();
        uint16_t hi = read_u8();
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    uint32_t read_u32_le() {
        uint32_t a = read_u16_le();
        uint32_t b = read_u16_le();
        return a | (b << 16);
    }
    void read_bytes(void* out, size_t n) {
        need(n);
        std::memcpy(out, buf_.data() + pos_, n);
        pos_ += n;
    }
    bool eof() const { return pos_ >= buf_.size(); }
    size_t remaining() const { return buf_.size() - pos_; }
private:
    void need(size_t n) {
        if (pos_ + n > buf_.size()) throw std::runtime_error("bitstream: premature EOF");
    }
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
};

// In-memory API (used by encoder/decoder)

// Helper: build header from Image + flags and write it.
void write_bitstream_header(ByteWriter& w,
                            const Image& im,
                            uint8_t flags,
                            uint16_t block_size = 8,
                            uint16_t quality = 50);
MCodecHeader read_bitstream_header(const std::vector<uint8_t>& bytes);

void write_payload(ByteWriter& w, const uint8_t* data, size_t bytes);
void read_payload(ByteReader& r, uint8_t* data, size_t bytes);

// void write_payload(std::ostream& os, const uint8_t* data, size_t bytes);
// void read_payload(std::istream& is, uint8_t* data, size_t bytes);

} // namespace mcodec



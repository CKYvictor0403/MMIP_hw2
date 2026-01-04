#include "io/medical_saver.hpp"

#include <fstream>
#include <stdexcept>

namespace mcodec {

void save_pgm(const std::string& path, const Image& im) {
    if (im.channels != 1) throw std::runtime_error("Only grayscale is supported for PGM output");
    if (im.width <= 0 || im.height <= 0) throw std::runtime_error("Invalid image size");
    if (static_cast<int>(im.pixels.size()) != im.width * im.height) throw std::runtime_error("pixel buffer size mismatch");

    const int maxv = (im.bits_stored <= 8) ? 255 : ((1 << im.bits_stored) - 1);
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.good()) throw std::runtime_error("Cannot write file: " + path);

    ofs << "P5\n" << im.width << " " << im.height << "\n" << maxv << "\n";

    if (maxv == 255) {
        for (int i = 0; i < im.width * im.height; ++i) {
            int32_t v = im.pixels[static_cast<size_t>(i)];
            if (v < 0) v = 0;
            if (v > maxv) v = maxv;
            uint8_t b = static_cast<uint8_t>(v);
            ofs.put(static_cast<char>(b));
        }
    } else {
        // PGM 16-bit expects big-endian; maxv may be < 65535 (e.g., 12-bit => 4095)
        for (int i = 0; i < im.width * im.height; ++i) {
            int32_t v = im.pixels[static_cast<size_t>(i)];
            if (v < 0) v = 0;
            if (v > maxv) v = maxv;
            uint16_t u = static_cast<uint16_t>(v);
            uint8_t hi = static_cast<uint8_t>((u >> 8) & 0xFF);
            uint8_t lo = static_cast<uint8_t>(u & 0xFF);
            ofs.put(static_cast<char>(hi));
            ofs.put(static_cast<char>(lo));
        }
    }
}

} // namespace mcodec



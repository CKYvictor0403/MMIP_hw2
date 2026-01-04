#include "cli/cli_parser.hpp"
#include "io/medical_loader.hpp"
#include "codec/encoder.hpp"

#include <fstream>
#include <iostream>

static void write_all(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.good()) throw std::runtime_error("Cannot write file: " + path);
    ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

int main(int argc, char** argv) {
    try {
        mcodec::CliParser cli;
        cli.parse(argc, argv);
        const std::string in = cli.get("in");
        const std::string out = cli.get("out");
        const std::string quality_str = cli.get("quality");
        if (in.empty() || out.empty() || quality_str.empty()) {
            std::cout << "Usage: encode --in <input.dicom> --out <output.mcodec> --quality <1..100>\n";
            return 1;
        }
        int quality = 0;
        try {
            quality = std::stoi(quality_str);
        } catch (...) {
            std::cout << "Usage: encode --in <input.dicom> --out <output.mcodec> --quality <1..100>\n";
            return 1;
        }
        if (quality < 1 || quality > 100) {
            std::cout << "Usage: encode --in <input.dicom> --out <output.mcodec> --quality <1..100>\n";
            return 1;
        }
        auto im = mcodec::load_medical(in);
        auto bytes = mcodec::encode_to_mcodec(im, quality);
        write_all(out, bytes);
        
        const size_t raw_size = static_cast<size_t>(im.width) * static_cast<size_t>(im.height) * (static_cast<size_t>(im.bits_allocated) / 8);
        std::cout << "input file size: " << raw_size << " bytes\n";
        std::cout << "Wrote: " << out << " (" << bytes.size() << " bytes)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 2;
    }
}



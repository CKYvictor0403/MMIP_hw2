#include "cli/cli_parser.hpp"
#include "codec/decoder.hpp"
#include "io/medical_saver.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>

static std::vector<uint8_t> read_all(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.good()) throw std::runtime_error("Cannot open file: " + path);
    ifs.seekg(0, std::ios::end);
    std::streamsize n = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    ifs.read(reinterpret_cast<char*>(buf.data()), n);
    return buf;
}

int main(int argc, char** argv) {
    try {
        mcodec::CliParser cli;
        cli.parse(argc, argv);
        const std::string in = cli.get("in");
        const std::string out = cli.get("out");
        if (in.empty() || out.empty()) {
            std::cerr << "Usage: decode --in <input.mcodec> --out <output.pgm>\n";
            return 1;
        }

        auto bytes = read_all(in);
        auto im = mcodec::decode_from_mcodec(bytes);
        mcodec::save_pgm(out, im);
        std::cout << "Wrote: " << out << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 2;
    }
}



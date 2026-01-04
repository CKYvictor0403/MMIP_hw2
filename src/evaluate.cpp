// Mode B evaluator: encode -> decode -> metrics (RMSE/PSNR) and figures.
#include "io/medical_loader.hpp"
#include "io/medical_saver.hpp"
#include "codec/encoder.hpp"
#include "codec/decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Cli {
    std::string ref;
    std::vector<int> qualities;
    std::string tmp_dir;
    std::string out_csv;
    std::string fig_dir;
};

std::vector<uint8_t> read_all(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.good()) throw std::runtime_error("Cannot open file: " + path);
    ifs.seekg(0, std::ios::end);
    auto n = static_cast<std::streamsize>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    ifs.read(reinterpret_cast<char*>(buf.data()), n);
    return buf;
}

void write_all(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.good()) throw std::runtime_error("Cannot write file: " + path);
    ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

Cli parse_cli(int argc, char** argv) {
    Cli c;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i] ? argv[i] : "";
        if (a == "--ref" && i + 1 < argc) {
            c.ref = argv[++i];
        } else if (a == "--tmp_dir" && i + 1 < argc) {
            c.tmp_dir = argv[++i];
        } else if (a == "--out" && i + 1 < argc) {
            c.out_csv = argv[++i];
        } else if (a == "--fig_dir" && i + 1 < argc) {
            c.fig_dir = argv[++i];
        } else if (a == "--quality") {
            while (i + 1 < argc) {
                std::string next = argv[i + 1] ? argv[i + 1] : "";
                if (next.rfind("--", 0) == 0) break;
                try {
                    c.qualities.push_back(std::stoi(next));
                } catch (...) {
                    throw std::runtime_error("quality must be integer");
                }
                ++i;
            }
        }
    }
    if (c.ref.empty() || c.tmp_dir.empty() || c.out_csv.empty() || c.fig_dir.empty()) {
        throw std::runtime_error("Usage: evaluate --ref <dicom> --quality q1 q2 q3 --tmp_dir <dir> --out <metrics.csv> --fig_dir <dir>");
    }
    if (c.qualities.size() < 3) {
        throw std::runtime_error("Need at least 3 quality values");
    }
    // take first three
    c.qualities.resize(3);
    return c;
}

template <typename T>
T clamp_val(T v, T lo, T hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void map_to_unsigned(const mcodec::Image& im, std::vector<uint32_t>& out_u, uint32_t maxv) {
    out_u.resize(im.pixels.size());
    if (im.is_signed) {
        const uint32_t offset = 1u << (im.bits_stored - 1);
        for (size_t i = 0; i < im.pixels.size(); ++i) {
            int32_t s = im.pixels[i];
            uint32_t u = static_cast<uint32_t>(s + static_cast<int32_t>(offset));
            out_u[i] = clamp_val<uint32_t>(u, 0u, maxv);
        }
    } else {
        for (size_t i = 0; i < im.pixels.size(); ++i) {
            int32_t s = im.pixels[i];
            uint32_t u = s < 0 ? 0u : static_cast<uint32_t>(s);
            out_u[i] = clamp_val<uint32_t>(u, 0u, maxv);
        }
    }
}

double compute_rmse_psnr(const std::vector<uint32_t>& ref_u,
                         const std::vector<uint32_t>& rec_u,
                         uint32_t maxv,
                         double& out_psnr) {
    if (ref_u.size() != rec_u.size()) {
        throw std::runtime_error("compute_rmse_psnr: size mismatch");
    }
    double mse = 0.0;
    for (size_t i = 0; i < ref_u.size(); ++i) {
        double d = static_cast<double>(rec_u[i]) - static_cast<double>(ref_u[i]);
        mse += d * d;
    }
    mse /= static_cast<double>(ref_u.size());
    double rmse = std::sqrt(mse);
    if (mse == 0.0) {
        out_psnr = std::numeric_limits<double>::infinity();
    } else {
        out_psnr = 20.0 * std::log10(static_cast<double>(maxv)) - 10.0 * std::log10(mse);
    }
    return rmse;
}

uint32_t percentile_p99(std::vector<uint32_t> v) {
    if (v.empty()) return 0;
    size_t idx = static_cast<size_t>(std::floor(0.99 * static_cast<double>(v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + idx, v.end());
    return v[idx];
}

} // namespace

int main(int argc, char** argv) {
    try {
        Cli cli = parse_cli(argc, argv);

        fs::create_directories(cli.tmp_dir);
        fs::create_directories(cli.fig_dir);

        mcodec::Image ref = mcodec::load_medical(cli.ref);
        if (ref.bits_stored <= 0 || ref.bits_stored > 16) {
            throw std::runtime_error("ref bits_stored out of range");
        }
        const uint32_t MAX = (1u << ref.bits_stored) - 1u;
        const uint64_t raw_bytes = static_cast<uint64_t>(ref.width) *
                                   static_cast<uint64_t>(ref.height) *
                                   static_cast<uint64_t>(ref.channels) *
                                   static_cast<uint64_t>(ref.bits_allocated / 8);
        const std::string stem = fs::path(cli.ref).stem().string();

        // Save reference image (original bit depth)
        {
            std::string ref_path = (fs::path(cli.fig_dir) / (stem + "_ref.pgm")).string();
            mcodec::save_pgm(ref_path, ref);
        }

        // prepare CSV
        {
            std::ofstream ofs(cli.out_csv, std::ios::trunc);
            if (!ofs.good()) throw std::runtime_error("Cannot write csv: " + cli.out_csv);
            ofs << "quality,block_size,compressed_bytes,bpp,raw_bytes,compression_ratio,rmse,psnr\n";
        }

        for (int q : cli.qualities) {
            if (q < 1 || q > 100) {
                throw std::runtime_error("quality out of range 1..100");
            }

            std::string mcodec_path = (fs::path(cli.tmp_dir) / (stem + "_q" + std::to_string(q) + ".mcodec")).string();

            // encode
            auto bytes = mcodec::encode_to_mcodec(ref, q);
            write_all(mcodec_path, bytes);
            uint64_t compressed_bytes = fs::file_size(mcodec_path);

            // rate metrics
            double bpp = (8.0 * static_cast<double>(compressed_bytes)) /
                         (static_cast<double>(ref.width) * static_cast<double>(ref.height));
            double cr = raw_bytes > 0 ? static_cast<double>(raw_bytes) / static_cast<double>(compressed_bytes) : 0.0;

            // decode
            mcodec::Image rec = mcodec::decode_from_mcodec(bytes);

            // metadata checks
            if (rec.width != ref.width || rec.height != ref.height || rec.channels != ref.channels) {
                throw std::runtime_error("decoded dimensions mismatch");
            }
            if (rec.bits_stored != ref.bits_stored) {
                throw std::runtime_error("decoded bits_stored mismatch");
            }
            if (rec.is_signed != ref.is_signed) {
                throw std::runtime_error("decoded is_signed mismatch");
            }

            // map to unsigned domain
            std::vector<uint32_t> ref_u, rec_u;
            map_to_unsigned(ref, ref_u, MAX);
            map_to_unsigned(rec, rec_u, MAX);

            // metrics
            double psnr = 0.0;
            double rmse = compute_rmse_psnr(ref_u, rec_u, MAX, psnr);

            // recon (original bit depth)
            std::string recon_path = (fs::path(cli.fig_dir) / (stem + "_q" + std::to_string(q) + "_recon.pgm")).string();
            mcodec::save_pgm(recon_path, rec);

            // error map 8-bit (p99 scaling)
            std::vector<uint32_t> err(rec_u.size());
            for (size_t i = 0; i < rec_u.size(); ++i) {
                uint32_t e = (rec_u[i] > ref_u[i]) ? (rec_u[i] - ref_u[i]) : (ref_u[i] - rec_u[i]);
                err[i] = e;
            }
            uint32_t scale = percentile_p99(err);
            if (scale == 0) scale = 1;
            std::vector<uint8_t> err8(err.size());
            for (size_t i = 0; i < err.size(); ++i) {
                uint32_t c = std::min(err[i], scale);
                double v = (255.0 * static_cast<double>(c)) / static_cast<double>(scale);
                if (v < 0.0) v = 0.0;
                if (v > 255.0) v = 255.0;
                err8[i] = static_cast<uint8_t>(std::lround(v));
            }
            std::string err_path = (fs::path(cli.fig_dir) / (stem + "_q" + std::to_string(q) + "_err.pgm")).string();
            mcodec::Image err_img;
            err_img.width = ref.width;
            err_img.height = ref.height;
            err_img.channels = 1;
            err_img.bits_allocated = 8;
            err_img.bits_stored = 8;
            err_img.is_signed = false;
            err_img.type = mcodec::PixelType::U8;
            err_img.pixels.assign(err8.begin(), err8.end());
            mcodec::save_pgm(err_path, err_img);

            // append CSV
            {
                std::ofstream ofs(cli.out_csv, std::ios::app);
                if (!ofs.good()) throw std::runtime_error("Cannot append csv: " + cli.out_csv);
                ofs << q << ","
                    << 8 << "," // block_size fixed 8
                    << compressed_bytes << ","
                    << bpp << ","
                    << raw_bytes << ","
                    << cr << ","
                    << rmse << ","
                    << psnr << "\n";
            }
        }

        std::cout << "Evaluation completed -> " << cli.out_csv << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        std::cerr << "Usage: evaluate --ref <dicom> --quality q1 q2 q3 --tmp_dir <dir> --out <metrics.csv> --fig_dir <dir>\n";
        return 1;
    }
}


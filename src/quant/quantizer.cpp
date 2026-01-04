#include "quant/quantizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mcodec {

int quant_step_from_quality(int quality) {
    int q = 101 - quality;
    if (q < 1) q = 1;
    if (q > 100) q = 100;
    return q;
}

void quantize(const std::vector<float>& coeff_in,
              int block_size,
              int quality,
              std::vector<int16_t>& qcoeff_out) {
    if (block_size != 8 && block_size != 16) {
        throw std::runtime_error("quantize: block_size must be 8 or 16");
    }
    const size_t block_elems = static_cast<size_t>(block_size * block_size);
    if (coeff_in.size() % block_elems != 0) {
        throw std::runtime_error("quantize: coeff size not multiple of block");
    }

    const int step = quant_step_from_quality(quality);
    qcoeff_out.resize(coeff_in.size());
    const float inv_step = 1.0f / static_cast<float>(step);
    const int16_t hi = std::numeric_limits<int16_t>::max();
    const int16_t lo = std::numeric_limits<int16_t>::min();

    for (size_t i = 0; i < coeff_in.size(); ++i) {
        float v = coeff_in[i] * inv_step;
        int q = static_cast<int>(std::round(v));
        if (q > hi) q = hi;
        if (q < lo) q = lo;
        qcoeff_out[i] = static_cast<int16_t>(q);
    }
}

void dequantize(const std::vector<int16_t>& qcoeff_in,
                int block_size,
                int quality,
                std::vector<float>& coeff_out) {
    if (block_size != 8 && block_size != 16) {
        throw std::runtime_error("dequantize: block_size must be 8 or 16");
    }
    const size_t block_elems = static_cast<size_t>(block_size * block_size);
    if (qcoeff_in.size() % block_elems != 0) {
        throw std::runtime_error("dequantize: qcoeff size not multiple of block");
    }

    const int step = quant_step_from_quality(quality);
    coeff_out.resize(qcoeff_in.size());
    for (size_t i = 0; i < qcoeff_in.size(); ++i) {
        coeff_out[i] = static_cast<float>(qcoeff_in[i]) * static_cast<float>(step);
    }
}

#ifndef NDEBUG
namespace {
// Simple self-test: quant/dequant round-trip for a small block.
struct QuantSelfTest {
    QuantSelfTest() {
        const int N = 8;
        const size_t block_elems = static_cast<size_t>(N * N);
        std::vector<float> coeff(block_elems);
        for (size_t i = 0; i < block_elems; ++i) {
            coeff[i] = static_cast<float>((int)i - 32); // mix of positive/negative
        }
        const int quality = 50;

        std::vector<int16_t> q;
        quantize(coeff, N, quality, q);

        std::vector<float> recon;
        dequantize(q, N, quality, recon);

        if (recon.size() != coeff.size()) {
            throw std::runtime_error("quant self-test: size mismatch");
        }
        // For uniform scalar quant, recon should be q * step exactly.
        const int step = quant_step_from_quality(quality);
        for (size_t i = 0; i < coeff.size(); ++i) {
            if (std::fabs(recon[i] - static_cast<float>(q[i] * step)) > 1e-6f) {
                throw std::runtime_error("quant self-test: dequant mismatch");
            }
        }
    }
};
static QuantSelfTest _quant_self_test{};
} // namespace
#endif

} // namespace mcodec
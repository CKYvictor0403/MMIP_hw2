#include "transform/dct2d.hpp"

#include <vector>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace mcodec {

namespace {
static constexpr double kPi = 3.14159265358979323846;

struct DctCache {
    int N = 0;
    std::vector<double> cos_table; // size N*N, indexed [u*N + x]
    std::vector<double> alpha;     // size N
};

static const DctCache& get_cache(int N) {
    static DctCache cache8, cache16;
    DctCache* cache = (N == 8) ? &cache8 : &cache16;
    if (cache->N == N && !cache->cos_table.empty()) return *cache;

    cache->N = N;
    cache->cos_table.resize(static_cast<size_t>(N * N));
    cache->alpha.resize(static_cast<size_t>(N));
    const double factor = kPi / (2.0 * N);
    for (int u = 0; u < N; ++u) {
        cache->alpha[static_cast<size_t>(u)] = (u == 0) ? std::sqrt(1.0 / N) : std::sqrt(2.0 / N);
        for (int x = 0; x < N; ++x) {
            cache->cos_table[static_cast<size_t>(u) * N + x] = std::cos((2 * x + 1) * u * factor);
        }
    }
    return *cache;
}
} // namespace

void dct2d_blocks(const std::vector<int32_t>& blocks_in,
                  int block_size,
                  std::vector<float>& coeff_out) {
    const int N = block_size;
    if (N != 8 && N != 16) throw std::runtime_error("dct2d_blocks: block_size must be 8 or 16");
    if (blocks_in.size() % static_cast<size_t>(N * N) != 0) {
        throw std::runtime_error("dct2d_blocks: input size not multiple of block");
    }
    const size_t block_elems = static_cast<size_t>(N * N);
    const size_t blocks = blocks_in.size() / block_elems;
    coeff_out.resize(blocks_in.size());

    const auto& cache = get_cache(N);
    const double* cos_tbl = cache.cos_table.data();
    const double* a = cache.alpha.data();

    // Temporary buffer for row-pass
    std::vector<double> tmp(block_elems);

    for (size_t b = 0; b < blocks; ++b) {
        const int32_t* src = blocks_in.data() + b * block_elems;
        float* dst = coeff_out.data() + b * block_elems;

        // Row DCT: tmp[y,u]
        for (int y = 0; y < N; ++y) {
            for (int u = 0; u < N; ++u) {
                double sum = 0.0;
                const double cu = cos_tbl[static_cast<size_t>(u) * N]; // base pointer for u
                for (int x = 0; x < N; ++x) {
                    const double cx = cos_tbl[static_cast<size_t>(u) * N + x];
                    sum += static_cast<double>(src[y * N + x]) * cx;
                }
                tmp[static_cast<size_t>(y) * N + u] = sum * a[u];
            }
        }

        // Column DCT: dst[v,u]
        for (int v = 0; v < N; ++v) {
            for (int u = 0; u < N; ++u) {
                double sum = 0.0;
                for (int y = 0; y < N; ++y) {
                    const double cy = cos_tbl[static_cast<size_t>(v) * N + y];
                    sum += tmp[static_cast<size_t>(y) * N + u] * cy;
                }
                sum *= a[v];
                dst[v * N + u] = static_cast<float>(sum);
            }
        }
    }
}

void idct2d_blocks(const std::vector<float>& coeff_in,
                   int block_size,
                   std::vector<int32_t>& blocks_out) {
    const int N = block_size;
    if (N != 8 && N != 16) throw std::runtime_error("idct2d_blocks: block_size must be 8 or 16");
    if (coeff_in.size() % static_cast<size_t>(N * N) != 0) {
        throw std::runtime_error("idct2d_blocks: input size not multiple of block");
    }
    const size_t block_elems = static_cast<size_t>(N * N);
    const size_t blocks = coeff_in.size() / block_elems;
    blocks_out.resize(coeff_in.size());

    const auto& cache = get_cache(N);
    const double* cos_tbl = cache.cos_table.data();
    const double* a = cache.alpha.data();

    // Temporary buffer for column-pass
    std::vector<double> tmp(block_elems);

    for (size_t b = 0; b < blocks; ++b) {
        const float* src = coeff_in.data() + b * block_elems;
        int32_t* dst = blocks_out.data() + b * block_elems;

        // Column iDCT: tmp[y,u] = sum_v alpha(v)*C[v,y]*src[v,u]
        for (int u = 0; u < N; ++u) {
            for (int y = 0; y < N; ++y) {
                double sum = 0.0;
                for (int v = 0; v < N; ++v) {
                    const double cy = cos_tbl[static_cast<size_t>(v) * N + y];
                    sum += a[v] * static_cast<double>(src[v * N + u]) * cy;
                }
                tmp[static_cast<size_t>(y) * N + u] = sum;
            }
        }

        // Row iDCT: dst[y,x] = sum_u alpha(u)*C[u,x]*tmp[y,u]
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                double sum = 0.0;
                for (int u = 0; u < N; ++u) {
                    const double cx = cos_tbl[static_cast<size_t>(u) * N + x];
                    sum += a[u] * tmp[static_cast<size_t>(y) * N + u] * cx;
                }
                sum = std::round(sum);
                if (sum > static_cast<double>(std::numeric_limits<int32_t>::max())) {
                    sum = static_cast<double>(std::numeric_limits<int32_t>::max());
                }
                if (sum < static_cast<double>(std::numeric_limits<int32_t>::min())) {
                    sum = static_cast<double>(std::numeric_limits<int32_t>::min());
                }
                dst[y * N + x] = static_cast<int32_t>(sum);
            }
        }
    }
}

} // namespace mcodec
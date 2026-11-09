// opt_probe14.cpp - 垂直方向也做「倍增」：把行间 H-1 次 AND 降到 ~log2(H) 次
//   原理：G[k][z] = AND(rows z..z+2^k-1)，由 G[k-1][z] & G[k-1][z+2^(k-1)] 得到；
//         窗口高度 H 按二进制拆成若干 2^k 块，查询 = 每个置位 k 一次 AND。
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <immintrin.h>

namespace {

template <typename Fn>
double timeMs(Fn&& fn, int reps = 3)
{
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms < best) best = ms;
    }
    return best;
}

void makeBits(std::vector<uint64_t>& bits, int W, int H, int& wpr)
{
    wpr = (W + 63) / 64;
    bits.assign(static_cast<size_t>(wpr) * H, 0);
    for (int z = 0; z < H; ++z) {
        for (int x = 0; x < W; ++x) {
            const uint64_t h = (static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL) ^
                               (static_cast<uint64_t>(z) * 0xC2B2AE3D27D4EB4FULL);
            const uint64_t v = h * 6364136223846793005ULL + 1442695040888963407ULL;
            if (((v >> 33) % 10u) == 0u) bits[static_cast<size_t>(z) * wpr + (x >> 6)] |= 1ULL << (x & 63);
        }
    }
}

inline uint64_t rowDoubling(uint64_t v, int gx)
{
    int k = 1;
    while (k < gx) {
        const int s = (k < 64) ? k : 63;
        v &= v >> s;
        k += s;
    }
    return v;
}

void passRowAvx2(const std::vector<uint64_t>& bits, int wpr, int H, int gx, std::vector<uint64_t>& R)
{
    const int n4 = wpr & ~3;
    for (int z = 0; z < H; ++z) {
        const uint64_t* src = &bits[static_cast<size_t>(z) * wpr];
        uint64_t* dst = &R[static_cast<size_t>(z) * wpr];
        for (int w = 0; w < n4; w += 4) {
            __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + w));
            int k = 1;
            while (k < gx) {
                const int s = (k < 64) ? k : 63;
                v = _mm256_and_si256(v, _mm256_srli_epi64(v, s));
                k += s;
            }
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + w), v);
        }
        for (int w = n4; w < wpr; ++w) dst[w] = rowDoubling(src[w], gx);
    }
}

inline __m256i andAt(const uint64_t* a, const uint64_t* b, int w)
{
    return _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + w)),
                            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + w)));
}

void run(int gx, int gz, int W, int H, int wpr, const std::vector<uint64_t>& bits)
{
    const int interior = 64 - (gx - 1);
    const double anchors = static_cast<double>(interior) * static_cast<double>(wpr) * H;
    std::vector<uint64_t> R(static_cast<size_t>(wpr) * H, 0);
    const int n4 = wpr & ~3;

    // 基准：行内倍增 + 行间逐行（AVX2）
    uint64_t cA = 0;
    const double msA = timeMs([&] {
        passRowAvx2(bits, wpr, H, gx, R);
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            const uint64_t* r0 = &R[static_cast<size_t>(z) * wpr];
            for (int w = 0; w < n4; w += 4) {
                __m256i res = _mm256_set1_epi64x(-1);
                for (int dz = 0; dz < gz; ++dz) {
                    res = _mm256_and_si256(
                        res, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
                                 r0 + static_cast<size_t>(dz) * wpr + w)));
                }
                alignas(32) uint64_t tmp[4];
                _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), res);
                total += std::popcount(tmp[0]) + std::popcount(tmp[1]) + std::popcount(tmp[2]) +
                         std::popcount(tmp[3]);
            }
        }
        cA = static_cast<uint64_t>(total);
    });

    // 新：行内倍增 + 行间倍增（按 H 的二进制分解查询）
    uint64_t cB = 0;
    const double msB = timeMs([&] {
        passRowAvx2(bits, wpr, H, gx, R);
        int kMax = 0;
        while ((1 << (kMax + 1)) <= gz) ++kMax;
        std::vector<std::vector<uint64_t>> G(static_cast<size_t>(kMax + 1));
        G[0] = R;
        for (int lvl = 1; lvl <= kMax; ++lvl) {
            const int half = 1 << (lvl - 1);
            const int rows = H - (1 << lvl) + 1;
            G[static_cast<size_t>(lvl)].assign(static_cast<size_t>(wpr) * rows, ~0ULL);
            for (int z = 0; z < rows; ++z) {
                const uint64_t* a = &G[static_cast<size_t>(lvl - 1)][static_cast<size_t>(z) * wpr];
                const uint64_t* b = &G[static_cast<size_t>(lvl - 1)][static_cast<size_t>(z + half) * wpr];
                uint64_t* o = &G[static_cast<size_t>(lvl)][static_cast<size_t>(z) * wpr];
                for (int w = 0; w < n4; w += 4) {
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(o + w), andAt(a, b, w));
                }
                for (int w = n4; w < wpr; ++w) o[w] = a[w] & b[w];
            }
        }
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            int off = 0;
            for (int lvl = 0; lvl <= kMax; ++lvl) {
                if ((gz >> lvl) & 1) {
                    const uint64_t* p = &G[static_cast<size_t>(lvl)][static_cast<size_t>(z + off) * wpr];
                    if (off == 0) {
                        for (int w = 0; w < n4; w += 4) {
                            const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + w));
                            alignas(32) uint64_t tmp[4];
                            _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), v);
                            total += std::popcount(tmp[0]) + std::popcount(tmp[1]) + std::popcount(tmp[2]) +
                                     std::popcount(tmp[3]);
                        }
                        for (int w = n4; w < wpr; ++w) total += std::popcount(p[w]);
                    }
                    off += 1 << lvl;
                }
            }
        }
        cB = static_cast<uint64_t>(total);
    });

    std::printf("  %2dx%-2d 行间逐行 %7.3f | 行间倍增 %7.3f (×%.2f) ns/锚点 | 一致=%s\n", gx, gz,
                msA * 1e6 / anchors, msB * 1e6 / anchors, msA / (msB > 1e-12 ? msB : 1e-12),
                (cA == cB) ? "是" : "否");
}

} // namespace

int main()
{
    const int W = 4096, H = 2048;
    int wpr = 0;
    std::vector<uint64_t> bits;
    makeBits(bits, W, H, wpr);
    std::printf("数据 %dx%d，密度 10%%\n\n", W, H);
    run(3, 3, W, H, wpr, bits);
    run(5, 5, W, H, wpr, bits);
    run(8, 8, W, H, wpr, bits);
    run(12, 12, W, H, wpr, bits);
    run(32, 32, W, H, wpr, bits);
    run(64, 64, W, H, wpr, bits);
    return 0;
}

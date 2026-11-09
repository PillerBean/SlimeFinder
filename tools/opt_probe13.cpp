// opt_probe13.cpp - 把可分离 / 倍增 / 稀疏表三条路径 AVX2 化，量收益
//   约定：只统计「窗口完整落在一个 64 位 lane 内」的锚点，因此行内移位不需要跨 lane 进位，
//         可以直接用 _mm256_srli_epi64 逐 lane 处理（这也是引擎里 64 锚点并行的同款假设）。
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

// 行内倍增（标量）
void passRowScalar(const std::vector<uint64_t>& bits, int wpr, int H, int gx, std::vector<uint64_t>& R)
{
    for (int z = 0; z < H; ++z) {
        const uint64_t* src = &bits[static_cast<size_t>(z) * wpr];
        uint64_t* dst = &R[static_cast<size_t>(z) * wpr];
        for (int w = 0; w < wpr; ++w) dst[w] = rowDoubling(src[w], gx);
    }
}

// 行内倍增（AVX2）
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

void run(int gx, int gz, int W, int H, int wpr, const std::vector<uint64_t>& bits)
{
    const int interior = 64 - (gx - 1);
    const double anchors = static_cast<double>(interior) * static_cast<double>(wpr) * H;
    std::vector<uint64_t> R(static_cast<size_t>(wpr) * H, 0);
    const int n4 = wpr & ~3;

    // 标量：行内倍增 + 行间逐行
    uint64_t cS = 0;
    const double msS = timeMs([&] {
        passRowScalar(bits, wpr, H, gx, R);
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            for (int w = 0; w < wpr; ++w) {
                uint64_t res = ~0ULL;
                for (int dz = 0; dz < gz; ++dz) res &= R[static_cast<size_t>(z + dz) * wpr + w];
                total += std::popcount(res);
            }
        }
        cS = static_cast<uint64_t>(total);
    });

    // AVX2：行内倍增 + 行间逐行
    uint64_t cV = 0;
    const double msV = timeMs([&] {
        passRowAvx2(bits, wpr, H, gx, R);
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            const uint64_t* r0 = &R[static_cast<size_t>(z) * wpr];
            for (int w = 0; w < n4; w += 4) {
                __m256i res = _mm256_set1_epi64x(-1);
                for (int dz = 0; dz < gz; ++dz) {
                    res = _mm256_and_si256(
                        res, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(r0 + static_cast<size_t>(dz) * wpr + w)));
                }
                alignas(32) uint64_t tmp[4];
                _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), res);
                total += std::popcount(tmp[0]) + std::popcount(tmp[1]) + std::popcount(tmp[2]) +
                         std::popcount(tmp[3]);
            }
            for (int w = n4; w < wpr; ++w) {
                uint64_t res = ~0ULL;
                for (int dz = 0; dz < gz; ++dz) res &= r0[static_cast<size_t>(dz) * wpr + w];
                total += std::popcount(res);
            }
        }
        cV = static_cast<uint64_t>(total);
    });

    // AVX2：行内倍增 + 稀疏表
    uint64_t cT = 0;
    const double msT = timeMs([&] {
        passRowAvx2(bits, wpr, H, gx, R);
        int k = 0;
        while ((1 << (k + 1)) <= gz) ++k;
        const int span = 1 << k;
        std::vector<std::vector<uint64_t>> st(static_cast<size_t>(k + 1));
        st[0] = R;
        for (int lvl = 1; lvl <= k; ++lvl) {
            st[static_cast<size_t>(lvl)].assign(static_cast<size_t>(wpr) * H, ~0ULL);
            const int half = 1 << (lvl - 1);
            for (int z = 0; z + (1 << lvl) <= H; ++z) {
                const uint64_t* a = &st[static_cast<size_t>(lvl - 1)][static_cast<size_t>(z) * wpr];
                const uint64_t* b = &st[static_cast<size_t>(lvl - 1)][static_cast<size_t>(z + half) * wpr];
                uint64_t* o = &st[static_cast<size_t>(lvl)][static_cast<size_t>(z) * wpr];
                for (int w = 0; w < n4; w += 4) {
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(o + w),
                                        _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + w)),
                                                         _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + w))));
                }
                for (int w = n4; w < wpr; ++w) o[w] = a[w] & b[w];
            }
        }
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            const uint64_t* lo = &st[static_cast<size_t>(k)][static_cast<size_t>(z) * wpr];
            const uint64_t* hi = &st[static_cast<size_t>(k)][static_cast<size_t>(z + gz - span) * wpr];
            for (int w = 0; w < n4; w += 4) {
                const __m256i v = _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(lo + w)),
                                                   _mm256_loadu_si256(reinterpret_cast<const __m256i*>(hi + w)));
                alignas(32) uint64_t tmp[4];
                _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), v);
                total += std::popcount(tmp[0]) + std::popcount(tmp[1]) + std::popcount(tmp[2]) +
                         std::popcount(tmp[3]);
            }
            for (int w = n4; w < wpr; ++w) total += std::popcount(lo[w] & hi[w]);
        }
        cT = static_cast<uint64_t>(total);
    });

    const bool ok = (cS == cV && cV == cT);
    std::printf("  %2dx%-2d 标量(倍增+逐行) %7.3f | AVX2(倍增+逐行) %7.3f (×%.2f) | AVX2(倍增+稀疏) %7.3f (×%.2f) ns/锚点 | 一致=%s\n",
                gx, gz, msS * 1e6 / anchors, msV * 1e6 / anchors, msS / (msV > 1e-12 ? msV : 1e-12),
                msT * 1e6 / anchors, msS / (msT > 1e-12 ? msT : 1e-12), ok ? "是" : "否");
}

} // namespace

int main()
{
    const int W = 4096, H = 2048;
    int wpr = 0;
    std::vector<uint64_t> bits;
    makeBits(bits, W, H, wpr);
    std::printf("数据 %dx%d，密度 10%%（只统计窗口落在 64 位 lane 内的锚点）\n\n", W, H);
    run(3, 3, W, H, wpr, bits);
    run(5, 5, W, H, wpr, bits);
    run(12, 12, W, H, wpr, bits);
    run(32, 32, W, H, wpr, bits);
    run(64, 64, W, H, wpr, bits);
    return 0;
}

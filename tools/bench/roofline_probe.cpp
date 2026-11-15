// 四类上限审计用的微基准：CPU 向量乘法峰值 + 内存拷贝带宽。
// 仅测量，不参与产品路径。
#include <immintrin.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
volatile uint32_t g_sink = 0;

double mulPeak()
{
    constexpr int kAcc = 8;
    constexpr int kIters = 20'000'000;
    __m256i acc[kAcc];
    const __m256i mul = _mm256_set1_epi32(1812433253);
    for (int i = 0; i < kAcc; ++i) acc[i] = _mm256_set1_epi32(i + 1);
    const auto t0 = Clock::now();
    for (int it = 0; it < kIters; ++it) {
        for (int i = 0; i < kAcc; ++i) {
            acc[i] = _mm256_mullo_epi32(acc[i], mul);
        }
    }
    const auto t1 = Clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    alignas(32) uint32_t tmp[8];
    uint32_t s = 0;
    for (int i = 0; i < kAcc; ++i) {
        _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), acc[i]);
        for (int j = 0; j < 8; ++j) s ^= tmp[j];
    }
    g_sink ^= s;
    return static_cast<double>(kIters) * kAcc / sec / 1e9; // G 次 256 位乘法/秒
}

double memBandwidth()
{
    constexpr size_t kBytes = size_t{ 256 } << 20;
    std::vector<uint8_t> a(kBytes), b(kBytes);
    std::memset(a.data(), 1, kBytes);
    std::memset(b.data(), 0, kBytes);
    double best = 1e100;
    for (int rep = 0; rep < 3; ++rep) {
        const auto t0 = Clock::now();
        std::memcpy(b.data(), a.data(), kBytes);
        const auto t1 = Clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        g_sink ^= b[0] ^ b[kBytes - 1];
        best = (std::min)(best, sec);
    }
    return (2.0 * static_cast<double>(kBytes) / best) / 1e9; // GB/s
}

// 与 Bedrock draw64x8 内层完全同构的 8 链循环（无 seed 准备/无末尾输出），
// 用来测“同一指令混合”的峰值链步时间。
double chainStepPeakNs()
{
    constexpr int kIters = 20'000'000;
    constexpr int kChains = 8;
    __m256i s[kChains];
    const __m256i mul = _mm256_set1_epi32(1812433253);
    const __m256i one = _mm256_set1_epi32(1);
    for (int i = 0; i < kChains; ++i) s[i] = _mm256_set1_epi32(i + 1);
    __m256i iv = one;
    const auto t0 = Clock::now();
    for (int it = 0; it < kIters; ++it) {
        iv = _mm256_add_epi32(iv, one);
        for (int i = 0; i < kChains; ++i) {
            const __m256i t = _mm256_xor_si256(s[i], _mm256_srli_epi32(s[i], 30));
            s[i] = _mm256_add_epi32(_mm256_mullo_epi32(t, mul), iv);
        }
    }
    const auto t1 = Clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    alignas(32) uint32_t tmp[8];
    uint32_t sink = 0;
    for (int i = 0; i < kChains; ++i) {
        _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), s[i]);
        for (int j = 0; j < 8; ++j) sink ^= tmp[j];
    }
    g_sink ^= sink;
    const double chainSteps = static_cast<double>(kIters) * kChains;
    return sec * 1e9 / chainSteps;
}
} // namespace

int main()
{
    const double mul = mulPeak();
    const double bw = memBandwidth();
    const double step = chainStepPeakNs();
    std::printf("vpmulld peak: %.2f G 次 256 位乘法/s\n", mul);
    std::printf("memcpy bandwidth: %.2f GB/s (读写合计)\n", bw);
    std::printf("8-chain inner step: %.4f ns/链步\n", step);
    std::printf("implied 64-chunk batch: %.2f ns/chunk (49.625 steps/chunk)\n",
                step * 49.625);
    return 0;
}

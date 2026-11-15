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
} // namespace

int main()
{
    const double mul = mulPeak();
    const double bw = memBandwidth();
    std::printf("vpmulld peak: %.2f G 次 256 位乘法/s\n", mul);
    std::printf("memcpy bandwidth: %.2f GB/s (读写合计)\n", bw);
    return 0;
}

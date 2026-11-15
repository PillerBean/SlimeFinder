// 基岩版 CPU 内核探针：8 lane/链，扫描不同链数。
// 目的：验证“减少 m0/m1 常驻寄存器、改为末尾重算”能否让 5~8 链超过当前 4 链。
// 仅实验工具，不接入产品路径。
#include "core/FastMt.h"

#include <immintrin.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

inline __m256i bc(uint32_t v) noexcept
{
    return _mm256_set1_epi32(static_cast<int32_t>(v));
}

constexpr uint32_t kSeedMul = 0x1F1F1F1FU;
constexpr uint32_t kInitMul = 1812433253U;
constexpr int kInit397 = 397;
constexpr uint32_t kMagic = 0x9908B0DFU;

inline void store8(__m256i v, uint8_t* out8) noexcept
{
    alignas(32) uint32_t t[8];
    _mm256_store_si256(reinterpret_cast<__m256i*>(t), v);
    for (int j = 0; j < 8; ++j) out8[j] = (t[j] % 10U) == 0U ? 1 : 0;
}

template <int C>
inline void drawNx8(uint32_t cx0, uint32_t czu, uint8_t* out) noexcept
{
    const __m256i k = bc(kSeedMul);
    const __m256i c = bc(kInitMul);
    const __m256i one = bc(1U);
    const __m256i z = bc(czu);
    const __m256i stepK = bc(8U * kSeedMul);
    const __m256i lane = _mm256_setr_epi32(
        static_cast<int>(cx0), static_cast<int>(cx0 + 1), static_cast<int>(cx0 + 2),
        static_cast<int>(cx0 + 3), static_cast<int>(cx0 + 4), static_cast<int>(cx0 + 5),
        static_cast<int>(cx0 + 6), static_cast<int>(cx0 + 7));
    const __m256i seedBase = _mm256_mullo_epi32(lane, k);

    __m256i s[C];
    {
        __m256i base = seedBase;
        for (int g = 0; g < C; ++g) {
            s[g] = _mm256_xor_si256(base, z);
            base = _mm256_add_epi32(base, stepK);
        }
    }
    for (int g = 0; g < C; ++g) {
        const __m256i t = _mm256_xor_si256(s[g], _mm256_srli_epi32(s[g], 30));
        s[g] = _mm256_add_epi32(_mm256_mullo_epi32(t, c), one);
    }

    __m256i iv = one;
    for (int i = 2; i <= kInit397; ++i) {
        iv = _mm256_add_epi32(iv, one);
        for (int g = 0; g < C; ++g) {
            const __m256i t = _mm256_xor_si256(s[g], _mm256_srli_epi32(s[g], 30));
            s[g] = _mm256_add_epi32(_mm256_mullo_epi32(t, c), iv);
        }
    }

    __m256i base = seedBase;
    for (int g = 0; g < C; ++g) {
        const __m256i m0 = _mm256_xor_si256(base, z);
        const __m256i t = _mm256_xor_si256(m0, _mm256_srli_epi32(m0, 30));
        const __m256i m1 = _mm256_add_epi32(_mm256_mullo_epi32(t, c), one);
        const __m256i y = _mm256_or_si256(_mm256_and_si256(m0, bc(0x80000000U)),
                                         _mm256_and_si256(m1, bc(0x7FFFFFFFU)));
        const __m256i parity = _mm256_and_si256(y, one);
        const __m256i neg = _mm256_sub_epi32(_mm256_setzero_si256(), parity);
        __m256i v = _mm256_xor_si256(s[g], _mm256_srli_epi32(y, 1));
        v = _mm256_xor_si256(v, _mm256_and_si256(neg, bc(kMagic)));
        v = _mm256_xor_si256(v, _mm256_srli_epi32(v, 11));
        v = _mm256_xor_si256(v, _mm256_and_si256(_mm256_slli_epi32(v, 7), bc(0x9D2C5680U)));
        v = _mm256_xor_si256(v, _mm256_and_si256(_mm256_slli_epi32(v, 15), bc(0xEFC60000U)));
        v = _mm256_xor_si256(v, _mm256_srli_epi32(v, 18));
        store8(v, out + g * 8);
        base = _mm256_add_epi32(base, stepK);
    }
}

template <int C>
void scanVariant(uint32_t cx0, uint32_t czu, int64_t count, uint8_t* out) noexcept
{
    constexpr int64_t kBlock = int64_t{ 8 } * C;
    int64_t done = 0;
    while (done + kBlock <= count) {
        drawNx8<C>(cx0 + static_cast<uint32_t>(done), czu, out + done);
        done += kBlock;
    }
    while (done < count) {
        out[done] = slime::fastmt::bedrockFirstDrawScalarShortcut(
                        slime::fastmt::bedrockChunkSeedScalar(static_cast<int32_t>(cx0 + done), czu)) %
                            10U ==
                        0U
                    ? 1
                    : 0;
        ++done;
    }
}

template <int C>
double timeOne(uint32_t cx0, uint32_t czu, int64_t count, uint8_t* out)
{
    const auto t0 = Clock::now();
    scanVariant<C>(cx0, czu, count, out);
    const auto t1 = Clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(count);
}

} // namespace

int main()
{
    constexpr int64_t kCount = 16LL << 20; // 16M，64 的整数倍
    constexpr uint32_t kCz = 12345U;
    std::vector<uint8_t> ref(static_cast<size_t>(kCount));
    std::vector<uint8_t> got(static_cast<size_t>(kCount));

    slime::fastmt::bedrockSlimeScanScalar(0, static_cast<int32_t>(kCz), static_cast<int32_t>(kCount),
                                          ref.data(), true);

    auto verify = [&](auto scanFn, const char* name) {
        std::fill(got.begin(), got.end(), 0);
        scanFn();
        const bool ok = std::memcmp(ref.data(), got.data(), ref.size()) == 0;
        std::printf("%-12s verify=%s\n", name, ok ? "OK" : "FAIL");
        return ok;
    };

    verify([&] { slime::fastmt::bedrockSlimeScan(0, static_cast<int32_t>(kCz),
                                                static_cast<int32_t>(kCount), got.data(), true); },
           "reference");

    auto run = [&](auto scanFn, const char* name) {
        const bool ok = verify(scanFn, name);
        if (!ok) return;
        double best = 1e100;
        for (int rep = 0; rep < 3; ++rep) best = (std::min)(best, [&] {
            const auto t0 = Clock::now();
            scanFn();
            return std::chrono::duration<double, std::nano>(Clock::now() - t0).count() /
                   static_cast<double>(kCount);
        }());
        std::printf("%-12s %8.3f ns/chunk  %8.2f M/s\n", name, best, 1000.0 / best);
    };

    run([&] { slime::fastmt::bedrockSlimeScan(0, static_cast<int32_t>(kCz),
                                             static_cast<int32_t>(kCount), got.data(), true); },
        "current");

    run([&] { scanVariant<2>(0, kCz, kCount, got.data()); }, "chains=2");
    run([&] { scanVariant<3>(0, kCz, kCount, got.data()); }, "chains=3");
    run([&] { scanVariant<4>(0, kCz, kCount, got.data()); }, "chains=4");
    run([&] { scanVariant<5>(0, kCz, kCount, got.data()); }, "chains=5");
    run([&] { scanVariant<6>(0, kCz, kCount, got.data()); }, "chains=6");
    run([&] { scanVariant<7>(0, kCz, kCount, got.data()); }, "chains=7");
    run([&] { scanVariant<8>(0, kCz, kCount, got.data()); }, "chains=8");
    run([&] { scanVariant<9>(0, kCz, kCount, got.data()); }, "chains=9");
    run([&] { scanVariant<10>(0, kCz, kCount, got.data()); }, "chains=10");
    run([&] { scanVariant<11>(0, kCz, kCount, got.data()); }, "chains=11");
    run([&] { scanVariant<12>(0, kCz, kCount, got.data()); }, "chains=12");
    return 0;
}

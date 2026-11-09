// opt_probe3.cpp - v3.6.16 第三支探针：整条判定链路 AVX2 化（哈希 + 48 位 LCG + mod10）
//   目标：给出 P2 的真实上限（4-lane 与 8-lane 两档），并与标量基线逐位核对。
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <immintrin.h>

namespace {

constexpr uint64_t kMask = (1ULL << 48) - 1;
constexpr uint64_t kMult = 0x5DEECE66DULL;
constexpr uint64_t kAdd = 0xBULL;
constexpr uint64_t kMlo = kMult & 0xFFFFFFFFULL;
constexpr uint64_t kMhi = (kMult >> 32) & 0xFFFFFFFFULL;
constexpr uint64_t kMlo16 = kMult & 0xFFFFULL;
constexpr uint64_t kXor = 987234911ULL;

inline int32_t imul32(int32_t a, int32_t b) noexcept
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

inline uint64_t javaRowBase(int64_t seed, int32_t cz) noexcept
{
    const int32_t t3 = imul32(cz, cz);
    const int32_t t4 = imul32(cz, 389711);
    uint64_t b = static_cast<uint64_t>(seed);
    b += static_cast<uint64_t>(static_cast<int64_t>(t3) * 4392871LL);
    b += static_cast<uint64_t>(static_cast<int64_t>(t4));
    return b;
}

inline uint64_t javaXTail(int32_t x) noexcept
{
    const int32_t t1 = imul32(imul32(x, x), 4987142);
    const int32_t t2 = imul32(x, 5947611);
    uint64_t s = static_cast<uint64_t>(static_cast<int64_t>(t1));
    s += static_cast<uint64_t>(static_cast<int64_t>(t2));
    return s;
}

inline bool slimeV0(uint64_t base, uint64_t xtail) noexcept
{
    uint64_t s = base + xtail;
    s ^= kXor;
    s = (s ^ kMult) & kMask;
    for (;;) {
        s = (s * kMult + kAdd) & kMask;
        const int bits = static_cast<int>(s >> 17);
        const int val = bits % 10;
        if (bits - val + 9 >= 0) return val == 0;
    }
}

// 4 lane：给定初始状态（已 mask + xor + 乘法前），返回命中掩码低 4 位
inline int lcg4(__m256i s)
{
    const __m256i mask48 = _mm256_set1_epi64x(static_cast<long long>(kMask));
    const __m256i lo32 = _mm256_set1_epi64x(0xFFFFFFFFLL);
    const __m256i mlo = _mm256_set1_epi64x(static_cast<long long>(kMlo));
    const __m256i mhi = _mm256_set1_epi64x(static_cast<long long>(kMhi));
    const __m256i lo16 = _mm256_set1_epi64x(0xFFFF);
    const __m256i mlo16 = _mm256_set1_epi64x(static_cast<long long>(kMlo16));
    const __m256i magic = _mm256_set1_epi64x(0xCCCCCCCDULL);
    const __m256i ten = _mm256_set1_epi64x(10);

    const __m256i a = _mm256_and_si256(s, lo32);
    const __m256i b = _mm256_srli_epi64(s, 32);
    const __m256i p1 = _mm256_mul_epu32(a, mlo);
    const __m256i q1 = _mm256_and_si256(_mm256_mul_epu32(a, mhi), lo16);
    const __m256i q2 = _mm256_and_si256(_mm256_mul_epu32(b, mlo16), lo16);
    const __m256i hi = _mm256_and_si256(_mm256_add_epi64(q1, q2), lo16);
    __m256i prod = _mm256_add_epi64(p1, _mm256_slli_epi64(hi, 32));
    prod = _mm256_and_si256(prod, mask48);
    const __m256i sn = _mm256_and_si256(_mm256_add_epi64(prod, _mm256_set1_epi64x(11)), mask48);
    const __m256i bits = _mm256_srli_epi64(sn, 17);
    const __m256i q = _mm256_srli_epi64(_mm256_mul_epu32(bits, magic), 35);
    const __m256i r = _mm256_sub_epi64(bits, _mm256_mul_epu32(q, ten));
    const __m256i eq = _mm256_cmpeq_epi64(r, _mm256_setzero_si256());
    return _mm256_movemask_pd(_mm256_castsi256_pd(eq));
}

// 8 lane：从 x（8 个连续 int32）与行基座算出两路 4-lane 初始状态
inline void states8(__m256i xv, uint64_t rowBase, __m256i& sA, __m256i& sB)
{
    const __m256i c1 = _mm256_set1_epi32(4987142);
    const __m256i c2 = _mm256_set1_epi32(5947611);
    const __m256i xx = _mm256_mullo_epi32(xv, xv);
    const __m256i t1 = _mm256_mullo_epi32(xx, c1);
    const __m256i t2 = _mm256_mullo_epi32(xv, c2);

    // 低 4 lane → 64 位（sext(t1)+sext(t2)+rowBase），再 xor + mask + 乘前状态
    __m128i t1lo = _mm256_castsi256_si128(t1);
    __m128i t2lo = _mm256_castsi256_si128(t2);
    __m128i t1hi = _mm256_extracti128_si256(t1, 1);
    __m128i t2hi = _mm256_extracti128_si256(t2, 1);

    const __m256i rb = _mm256_set1_epi64x(static_cast<long long>(rowBase));
    const __m256i xorv = _mm256_set1_epi64x(static_cast<long long>(kXor));
    const __m256i mask48 = _mm256_set1_epi64x(static_cast<long long>(kMask));
    const __m256i multv = _mm256_set1_epi64x(static_cast<long long>(kMult));

    __m256i a = _mm256_add_epi64(_mm256_add_epi64(_mm256_cvtepi32_epi64(t1lo), _mm256_cvtepi32_epi64(t2lo)), rb);
    a = _mm256_and_si256(_mm256_xor_si256(_mm256_xor_si256(a, xorv), multv), mask48);
    __m256i b = _mm256_add_epi64(_mm256_add_epi64(_mm256_cvtepi32_epi64(t1hi), _mm256_cvtepi32_epi64(t2hi)), rb);
    b = _mm256_and_si256(_mm256_xor_si256(_mm256_xor_si256(b, xorv), multv), mask48);
    sA = a;
    sB = b;
}

struct Cfg {
    int w = 4096;
    int h = 4096;
    int64_t seed = 12345;
};

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

void run(const Cfg& c)
{
    const int64_t chunks = static_cast<int64_t>(c.w) * c.h;
    std::printf("== %d x %d = %.1f M 区块 ==\n", c.w, c.h, static_cast<double>(chunks) / 1e6);

    std::vector<uint64_t> rowBase(static_cast<size_t>(c.h)), xTail(static_cast<size_t>(c.w));
    std::vector<int32_t> xv(static_cast<size_t>(c.w));
    for (int z = 0; z < c.h; ++z) rowBase[static_cast<size_t>(z)] = javaRowBase(c.seed, -c.h / 2 + z);
    for (int x = 0; x < c.w; ++x) {
        const int32_t cx = -c.w / 2 + x;
        xTail[static_cast<size_t>(x)] = javaXTail(cx);
        xv[static_cast<size_t>(x)] = cx;
    }

    uint64_t cnt0 = 0, cnt8 = 0;
    const double ms0 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x < c.w; ++x) acc += slimeV0(rb, xTail[static_cast<size_t>(x)]) ? 1 : 0;
        }
        cnt0 = acc;
    });

    const double ms8 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            int x = 0;
            for (; x + 8 <= c.w; x += 8) {
                const __m256i xvi =
                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&xv[static_cast<size_t>(x)]));
                __m256i sA, sB;
                states8(xvi, rb, sA, sB);
                const int ma = lcg4(sA);
                const int mb = lcg4(sB);
                acc += static_cast<uint64_t>(std::popcount(static_cast<unsigned>(ma)));
                acc += static_cast<uint64_t>(std::popcount(static_cast<unsigned>(mb)));
            }
            for (; x < c.w; ++x) acc += slimeV0(rb, xTail[static_cast<size_t>(x)]) ? 1 : 0;  // 尾巴
        }
        cnt8 = acc;
    });

    const double per = static_cast<double>(chunks);
    const auto rep = [per](const char* n, double ms, uint64_t ck) {
        std::printf("%-30s %8.2f ms | %6.3f ns/区块 | %7.1f M/s | %llu\n", n, ms, ms * 1e6 / per,
                    per / (ms * 1000.0), static_cast<unsigned long long>(ck));
    };
    rep("V0 标量基线", ms0, cnt0);
    rep("V7 AVX2 整链（8 lane）", ms8, cnt8);
    if (cnt0 != cnt8) std::printf("!! 结果不一致（%llu vs %llu）\n", (unsigned long long)cnt0,
                                  (unsigned long long)cnt8);
    else std::printf("结果逐位一致 ✔\n");
    std::printf("\n");
}

} // namespace

int main()
{
    Cfg c;
    c.w = 4096; c.h = 4096;
    run(c);
    c.w = 16384; c.h = 16384;
    run(c);
    return 0;
}

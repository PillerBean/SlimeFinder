// opt_probe2.cpp - v3.6.16 追加探针：LCG 段的可向量化性
//   V5：标量但去掉「==0」的可预测性惩罚（branchless 累加）
//   V6：AVX2 4-lane 48 位 LCG + mod10（P2 的核心，只测吞吐与正确性）
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <bit>
#include <vector>

#include <immintrin.h>

namespace {

constexpr uint64_t kMask = (1ULL << 48) - 1;
constexpr uint64_t kMult = 0x5DEECE66DULL;
constexpr uint64_t kAdd = 0xBULL;
constexpr uint64_t kMlo = kMult & 0xFFFFFFFFULL;     // 0xDEECE66D
constexpr uint64_t kMhi = (kMult >> 32) & 0xFFFFFFFFULL; // 5
constexpr uint64_t kMlo16 = kMult & 0xFFFFULL;       // 0xE66D

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
    s ^= 987234911ULL;
    s = (s ^ kMult) & kMask;
    for (;;) {
        s = (s * kMult + kAdd) & kMask;
        const int bits = static_cast<int>(s >> 17);
        const int val = bits % 10;
        if (bits - val + 9 >= 0) return val == 0;
    }
}

inline bool slimeBranchless(uint64_t base, uint64_t xtail) noexcept
{
    uint64_t s = base + xtail;
    s ^= 987234911ULL;
    s = (s ^ kMult) & kMask;
    s = (s * kMult + kAdd) & kMask;
    const uint32_t bits = static_cast<uint32_t>(s >> 17);
    if (bits < 2147483640U) return (bits % 10U) == 0U;   // 常态：单次判定
    return slimeV0(base, xtail);                          // 极稀有：退回完整语义
}

// AVX2：4 lane 48 位 LCG（输入已是 mask 后的 48 位初始状态），输出命中掩码低 4 位
inline int lcg4(__m256i s, uint32_t* outBits)
{
    const __m256i mask48 = _mm256_set1_epi64x(static_cast<long long>(kMask));
    const __m256i one = _mm256_set1_epi64x(1);
    const __m256i mlo = _mm256_set1_epi64x(static_cast<long long>(kMlo));
    const __m256i mhi = _mm256_set1_epi64x(static_cast<long long>(kMhi));
    const __m256i lo16 = _mm256_set1_epi64x(0xFFFF);
    const __m256i mlo16 = _mm256_set1_epi64x(static_cast<long long>(kMlo16));
    const __m256i magic = _mm256_set1_epi64x(0xCCCCCCCDULL);
    const __m256i sh35 = _mm256_set1_epi64x(35);
    const __m256i ten = _mm256_set1_epi64x(10);
    (void)one;

    // a = s & 0xFFFFFFFF, b = s >> 32
    const __m256i a = _mm256_and_si256(s, _mm256_set1_epi64x(0xFFFFFFFFLL));
    const __m256i b = _mm256_srli_epi64(s, 32);

    // p1 = a*Mlo（64 位）
    const __m256i p1 = _mm256_mul_epu32(a, mlo);
    // q1 = (a*Mhi) & 0xFFFF, q2 = (b*Mlo16) & 0xFFFF
    const __m256i q1 = _mm256_and_si256(_mm256_mul_epu32(a, mhi), lo16);
    const __m256i q2 = _mm256_and_si256(_mm256_mul_epu32(b, mlo16), lo16);
    const __m256i hi = _mm256_and_si256(_mm256_add_epi64(q1, q2), lo16);

    __m256i prod = _mm256_add_epi64(p1, _mm256_slli_epi64(hi, 32));
    prod = _mm256_and_si256(prod, mask48);
    const __m256i sn = _mm256_and_si256(_mm256_add_epi64(prod, _mm256_set1_epi64x(11)), mask48);

    // bits = sn >> 17（< 2^31）
    const __m256i bits = _mm256_srli_epi64(sn, 17);
    // q = (bits * 0xCCCCCCCD) >> 35 ; r = bits - 10q
    const __m256i q = _mm256_srli_epi64(_mm256_mul_epu32(bits, magic), 35);
    const __m256i r = _mm256_sub_epi64(bits, _mm256_mul_epu32(q, ten));
    const __m256i eq = _mm256_cmpeq_epi64(r, _mm256_setzero_si256());
    if (outBits) {
        // 低 32 位供核对
        const __m128i lo = _mm256_castsi256_si128(bits);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(outBits), lo);
    }
    return _mm256_movemask_pd(_mm256_castsi256_pd(eq));
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
    for (int z = 0; z < c.h; ++z) rowBase[static_cast<size_t>(z)] = javaRowBase(c.seed, -c.h / 2 + z);
    for (int x = 0; x < c.w; ++x) xTail[static_cast<size_t>(x)] = javaXTail(-c.w / 2 + x);

    uint64_t cnt0 = 0, cnt5 = 0, cnt6 = 0;
    const double ms0 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x < c.w; ++x) acc += slimeV0(rb, xTail[static_cast<size_t>(x)]) ? 1 : 0;
        }
        cnt0 = acc;
    });
    const double ms5 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x < c.w; ++x) acc += slimeBranchless(rb, xTail[static_cast<size_t>(x)]) ? 1 : 0;
        }
        cnt5 = acc;
    });
    const double ms6 = timeMs([&] {
        uint64_t acc = 0;
        const __m256i mask48 = _mm256_set1_epi64x(static_cast<long long>(kMask));
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x + 4 <= c.w; x += 4) {
                // 用标量算 4 个初始状态（哈希本来就便宜），再交给 AVX2 跑 LCG
                uint64_t s0 = (rb + xTail[static_cast<size_t>(x)] ^ 987234911ULL ^ kMult) & kMask;
                uint64_t s1 = (rb + xTail[static_cast<size_t>(x + 1)] ^ 987234911ULL ^ kMult) & kMask;
                uint64_t s2 = (rb + xTail[static_cast<size_t>(x + 2)] ^ 987234911ULL ^ kMult) & kMask;
                uint64_t s3 = (rb + xTail[static_cast<size_t>(x + 3)] ^ 987234911ULL ^ kMult) & kMask;
                const __m256i st = _mm256_set_epi64x(static_cast<long long>(s3), static_cast<long long>(s2),
                                                     static_cast<long long>(s1), static_cast<long long>(s0));
                const int m = lcg4(st, nullptr);
                acc += static_cast<uint64_t>(std::popcount(static_cast<unsigned>(m)));
            }
        }
        cnt6 = acc;
    });

    const double per = static_cast<double>(chunks);
    const auto rep = [per](const char* n, double ms, uint64_t ck) {
        std::printf("%-30s %8.2f ms | %6.3f ns/区块 | %7.1f M/s | %llu\n", n, ms, ms * 1e6 / per,
                    per / (ms * 1000.0), static_cast<unsigned long long>(ck));
    };
    rep("V0 基线（完整）", ms0, cnt0);
    rep("V5 分支消除（标量）", ms5, cnt5);
    rep("V6 AVX2 4-lane LCG", ms6, cnt6);
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

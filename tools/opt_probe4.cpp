// opt_probe4.cpp - v3.6.16 第四支探针
//   V9 ：16-lane 批次（4 组独立 LCG 链交织）—— 看 AVX2 整链能否从 1.9× 走到 2.3×+
//   V10：匹配阶段读取 —— 字节数组逐格早退 vs 位集移位 AND（5×5 全绿图案）
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

inline void states8(__m256i xv, uint64_t rb, __m256i& sA, __m256i& sB)
{
    const __m256i c1 = _mm256_set1_epi32(4987142);
    const __m256i c2 = _mm256_set1_epi32(5947611);
    const __m256i xx = _mm256_mullo_epi32(xv, xv);
    const __m256i t1 = _mm256_mullo_epi32(xx, c1);
    const __m256i t2 = _mm256_mullo_epi32(xv, c2);
    const __m256i rbv = _mm256_set1_epi64x(static_cast<long long>(rb));
    const __m256i xorv = _mm256_set1_epi64x(static_cast<long long>(kXor));
    const __m256i mask48 = _mm256_set1_epi64x(static_cast<long long>(kMask));
    const __m256i multv = _mm256_set1_epi64x(static_cast<long long>(kMult));
    __m256i a = _mm256_add_epi64(
        _mm256_add_epi64(_mm256_cvtepi32_epi64(_mm256_castsi256_si128(t1)),
                         _mm256_cvtepi32_epi64(_mm256_castsi256_si128(t2))),
        rbv);
    a = _mm256_and_si256(_mm256_xor_si256(_mm256_xor_si256(a, xorv), multv), mask48);
    __m256i b = _mm256_add_epi64(
        _mm256_add_epi64(_mm256_cvtepi32_epi64(_mm256_extracti128_si256(t1, 1)),
                         _mm256_cvtepi32_epi64(_mm256_extracti128_si256(t2, 1))),
        rbv);
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

void runScan(const Cfg& c)
{
    const int64_t chunks = static_cast<int64_t>(c.w) * c.h;
    std::printf("== 扫描 %d x %d = %.1f M 区块 ==\n", c.w, c.h, static_cast<double>(chunks) / 1e6);
    std::vector<uint64_t> rowBase(static_cast<size_t>(c.h)), xTail(static_cast<size_t>(c.w));
    std::vector<int32_t> xv(static_cast<size_t>(c.w));
    for (int z = 0; z < c.h; ++z) rowBase[static_cast<size_t>(z)] = javaRowBase(c.seed, -c.h / 2 + z);
    for (int x = 0; x < c.w; ++x) {
        const int32_t cx = -c.w / 2 + x;
        xTail[static_cast<size_t>(x)] = javaXTail(cx);
        xv[static_cast<size_t>(x)] = cx;
    }

    uint64_t c0 = 0, c8 = 0, c16 = 0;
    const double ms0 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x < c.w; ++x) acc += slimeV0(rb, xTail[static_cast<size_t>(x)]) ? 1 : 0;
        }
        c0 = acc;
    });
    const double ms8 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            int x = 0;
            for (; x + 8 <= c.w; x += 8) {
                __m256i sA, sB;
                states8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(&xv[static_cast<size_t>(x)])), rb, sA, sB);
                acc += static_cast<uint64_t>(std::popcount(static_cast<unsigned>(lcg4(sA))));
                acc += static_cast<uint64_t>(std::popcount(static_cast<unsigned>(lcg4(sB))));
            }
            for (; x < c.w; ++x) acc += slimeV0(rb, xTail[static_cast<size_t>(x)]) ? 1 : 0;
        }
        c8 = acc;
    });
    const double ms16 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            int x = 0;
            for (; x + 16 <= c.w; x += 16) {
                __m256i a0, b0, a1, b1;
                const __m256i x0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&xv[static_cast<size_t>(x)]));
                const __m256i x1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&xv[static_cast<size_t>(x + 8)]));
                states8(x0, rb, a0, b0);
                states8(x1, rb, a1, b1);
                const int m0 = lcg4(a0);
                const int m1 = lcg4(b0);
                const int m2 = lcg4(a1);
                const int m3 = lcg4(b1);
                acc += static_cast<uint64_t>(std::popcount(static_cast<unsigned>(m0)) +
                                             std::popcount(static_cast<unsigned>(m1)) +
                                             std::popcount(static_cast<unsigned>(m2)) +
                                             std::popcount(static_cast<unsigned>(m3)));
            }
            for (; x < c.w; ++x) acc += slimeV0(rb, xTail[static_cast<size_t>(x)]) ? 1 : 0;
        }
        c16 = acc;
    });

    const double per = static_cast<double>(chunks);
    const auto rep = [per](const char* n, double ms, uint64_t ck) {
        std::printf("%-28s %8.2f ms | %6.3f ns/区块 | %7.1f M/s | %llu\n", n, ms, ms * 1e6 / per,
                    per / (ms * 1000.0), static_cast<unsigned long long>(ck));
    };
    rep("V0 标量", ms0, c0);
    rep("V7 8-lane", ms8, c8);
    rep("V9 16-lane", ms16, c16);
    std::printf("一致：%s\n\n", (c0 == c8 && c0 == c16) ? "是 ✔" : "否 ✘");
}

// ---------- V10：匹配阶段读取对比 ----------
void runMatch()
{
    const int W = 8192, H = 4096;   // 32 M 锚点范围
    const int g = 5;                // 5x5 全绿
    std::vector<uint8_t> bytes(static_cast<size_t>(W) * H);
    const int wordsPerRow = (W + 63) / 64;
    std::vector<uint64_t> bits(static_cast<size_t>(wordsPerRow) * H, 0);

    // 用真实判定填充（保证密度 10%）
    for (int z = 0; z < H; ++z) {
        const uint64_t rb = javaRowBase(12345, z - H / 2);
        for (int x = 0; x < W; ++x) {
            const bool s = slimeV0(rb, javaXTail(x - W / 2));
            bytes[static_cast<size_t>(z) * W + x] = s ? 1 : 0;
            if (s) bits[static_cast<size_t>(z) * wordsPerRow + (x >> 6)] |= 1ULL << (x & 63);
        }
    }

    // (a) 字节数组：逐锚点、逐格早退
    uint64_t cntA = 0;
    const double msA = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z + g <= H; ++z) {
            const uint8_t* r0 = &bytes[static_cast<size_t>(z) * W];
            for (int x = 0; x + g <= W; ++x) {
                bool ok = true;
                for (int dz = 0; dz < g && ok; ++dz) {
                    const uint8_t* r = r0 + static_cast<size_t>(dz) * W + x;
                    for (int dx = 0; dx < g; ++dx)
                        if (!r[dx]) { ok = false; break; }
                }
                acc += ok ? 1 : 0;
            }
        }
        cntA = acc;
    });

    // (b) 位集：每行取 g 位窗口 → 5 行 AND → 逐锚点判定
    uint64_t cntB = 0;
    const double msB = timeMs([&] {
        uint64_t acc = 0;
        const uint64_t colMask = (1ULL << g) - 1;
        for (int z = 0; z + g <= H; ++z) {
            for (int x = 0; x + g <= W; ++x) {
                const int w = x >> 6;
                const int off = x & 63;
                bool ok = true;
                for (int dz = 0; dz < g && ok; ++dz) {
                    const uint64_t* row = &bits[static_cast<size_t>(z + dz) * wordsPerRow];
                    uint64_t v = row[w] >> off;
                    if (off > 63 - g) v |= row[w + 1] << (64 - off);
                    if ((v & colMask) != colMask) ok = false;
                }
                acc += ok ? 1 : 0;
            }
        }
        cntB = acc;
    });

    const double anchors = static_cast<double>(W - g + 1) * (H - g + 1);
    const auto rep = [anchors](const char* n, double ms, uint64_t ck) {
        std::printf("%-28s %8.2f ms | %6.3f ns/锚点 | %10.0f 锚点/秒 | %llu\n", n, ms, ms * 1e6 / anchors,
                    anchors / (ms * 1000.0), static_cast<unsigned long long>(ck));
    };
    std::printf("== 匹配读取（%dx%d 锚点，5x5 全绿）==\n", W - g + 1, H - g + 1);
    rep("V10a 字节逐格早退", msA, cntA);
    rep("V10b 位集 5 行 AND", msB, cntB);
    std::printf("命中一致：%s（%llu）\n", cntA == cntB ? "是 ✔" : "否 ✘", static_cast<unsigned long long>(cntA));
}

} // namespace

int main()
{
    Cfg c;
    c.w = 4096; c.h = 4096;
    runScan(c);
    c.w = 16384; c.h = 16384;
    runScan(c);
    runMatch();
    return 0;
}

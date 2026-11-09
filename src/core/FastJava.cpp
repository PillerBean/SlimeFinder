// FastJava.cpp - v3.6.16 Java 版判定 AVX2 内核（16 lane 一批）
//
// 数学与引擎标量路径完全一致（见 SlimeEngine.cpp 的 javaRowBase / javaXTail / javaSlimeFromTail）：
//   t1   = int32(x*x*4987142)      t2 = int32(x*5947611)
//   t    = (rowBase + sext(t1) + sext(t2)) & 2^48
//   s    = t ^ 987234911 ^ MULT                       // JavaRandom 构造
//   s'   = (s*MULT + 11) & 2^48 ; bits = s' >> 17 ; 命中 = bits % 10 == 0
// 48 位乘用 vpmuludq 拆成 32x32（MULT = 0x5_DEECE66D），mod10 用魔数乘；
// nextInt 的拒绝采样（bits ≥ 2147483640，概率 ~3.7e-9）在 SIMD 里检测，命中就整批退回标量。
//
// 本文件是项目里第三个启用 AVX2 的编译单元（另两个是 FastMatch.cpp / FastMt.cpp）。
#include "core/FastJava.h"

#include <immintrin.h>
#include <intrin.h>

namespace fastjava {
namespace {

constexpr uint64_t kMask = (1ULL << 48) - 1;
constexpr uint64_t kMult = 0x5DEECE66DULL;
constexpr uint64_t kMlo = kMult & 0xFFFFFFFFULL;          // 0xDEECE66D
constexpr uint64_t kMhi = (kMult >> 32) & 0xFFFFFFFFULL;  // 5
constexpr uint64_t kMlo16 = kMult & 0xFFFFULL;            // 0xE66D
constexpr uint64_t kXor = 987234911ULL;
constexpr int64_t kRejectFrom = 2147483640LL;             // bits 落在这里要重抽

inline int32_t imul32(int32_t a, int32_t b) noexcept
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

inline bool slimeScalar(uint64_t rowBase, int32_t x) noexcept
{
    const int32_t t1 = imul32(imul32(x, x), 4987142);
    const int32_t t2 = imul32(x, 5947611);
    uint64_t s = rowBase + static_cast<uint64_t>(static_cast<int64_t>(t1)) +
                 static_cast<uint64_t>(static_cast<int64_t>(t2));
    s ^= kXor;
    s = (s ^ kMult) & kMask;
    for (;;) {
        s = (s * kMult + 0xBULL) & kMask;
        const int bits = static_cast<int>(s >> 17);
        const int val = bits % 10;
        if (bits - val + 9 >= 0) return val == 0;
    }
}

// 4 lane：输入「已 mask 的 LCG 前状态」，返回低 4 位命中掩码；rare 置位表示有 lane 落在拒绝采样区
__forceinline int lcg4(__m256i s, bool& rare) noexcept
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

    // 拒绝采样区检测（bits ≥ 2147483640）—— 极稀有，命中就整批退回标量
    const __m256i lim = _mm256_set1_epi64x(kRejectFrom - 1);
    if (_mm256_movemask_pd(_mm256_castsi256_pd(_mm256_cmpgt_epi64(bits, lim))) != 0) rare = true;

    const __m256i q = _mm256_srli_epi64(_mm256_mul_epu32(bits, magic), 35);
    const __m256i r = _mm256_sub_epi64(bits, _mm256_mul_epu32(q, ten));
    const __m256i eq = _mm256_cmpeq_epi64(r, _mm256_setzero_si256());
    return _mm256_movemask_pd(_mm256_castsi256_pd(eq));
}

} // namespace

bool available() noexcept
{
    static const bool ok = []() -> bool {
        int regs[4] = { 0, 0, 0, 0 };
        __cpuid(regs, 0);
        if (regs[0] < 7) return false;
        __cpuid(regs, 1);
        const bool osxsave = (regs[2] & (1 << 27)) != 0;
        const bool avx = (regs[2] & (1 << 28)) != 0;
        if (!osxsave || !avx) return false;
        const unsigned long long xcr0 = _xgetbv(0);
        if ((xcr0 & 0x6) != 0x6) return false;   // XMM+YMM 状态已开启
        __cpuid(regs, 7);
        return (regs[1] & (1 << 5)) != 0;
    }();
    return ok;
}

uint32_t slimeMask16(uint64_t rowBase, int32_t cx0) noexcept
{
    if (!available()) {
        uint32_t m = 0;
        for (int i = 0; i < 16; ++i)
            if (slimeScalar(rowBase, cx0 + i)) m |= 1u << i;
        return m;
    }

    const __m256i c1 = _mm256_set1_epi32(4987142);
    const __m256i c2 = _mm256_set1_epi32(5947611);
    const __m256i rb = _mm256_set1_epi64x(static_cast<long long>(rowBase));
    const __m256i xorv = _mm256_set1_epi64x(static_cast<long long>(kXor));
    const __m256i mask48 = _mm256_set1_epi64x(static_cast<long long>(kMask));
    const __m256i multv = _mm256_set1_epi64x(static_cast<long long>(kMult));

    const __m256i laneLo = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    const __m256i laneHi = _mm256_setr_epi32(8, 9, 10, 11, 12, 13, 14, 15);
    const __m256i xlo = _mm256_add_epi32(_mm256_set1_epi32(cx0), laneLo);
    const __m256i xhi = _mm256_add_epi32(_mm256_set1_epi32(cx0), laneHi);
    const __m256i t1lo = _mm256_mullo_epi32(_mm256_mullo_epi32(xlo, xlo), c1);
    const __m256i t2lo = _mm256_mullo_epi32(xlo, c2);
    const __m256i t1hi = _mm256_mullo_epi32(_mm256_mullo_epi32(xhi, xhi), c1);
    const __m256i t2hi = _mm256_mullo_epi32(xhi, c2);

    auto buildState = [&](__m256i t1, __m256i t2, bool highHalf) {
        const __m128i a1 = highHalf ? _mm256_extracti128_si256(t1, 1) : _mm256_castsi256_si128(t1);
        const __m128i a2 = highHalf ? _mm256_extracti128_si256(t2, 1) : _mm256_castsi256_si128(t2);
        __m256i v =
            _mm256_add_epi64(_mm256_add_epi64(_mm256_cvtepi32_epi64(a1), _mm256_cvtepi32_epi64(a2)), rb);
        return _mm256_and_si256(_mm256_xor_si256(_mm256_xor_si256(v, xorv), multv), mask48);
    };

    bool rare = false;
    const int m0 = lcg4(buildState(t1lo, t2lo, false), rare);
    const int m1 = lcg4(buildState(t1lo, t2lo, true), rare);
    const int m2 = lcg4(buildState(t1hi, t2hi, false), rare);
    const int m3 = lcg4(buildState(t1hi, t2hi, true), rare);
    if (rare) {   // 退回标量（逐位一致由 nextInt 的完整语义保证）
        uint32_t m = 0;
        for (int i = 0; i < 16; ++i)
            if (slimeScalar(rowBase, cx0 + i)) m |= 1u << i;
        return m;
    }
    return (static_cast<uint32_t>(m0 & 0xF)) | (static_cast<uint32_t>(m1 & 0xF) << 4) |
           (static_cast<uint32_t>(m2 & 0xF) << 8) | (static_cast<uint32_t>(m3 & 0xF) << 12);
}


uint64_t slimeMask64(uint64_t rowBase, int32_t cx0) noexcept
{
    if (!available()) {
        uint64_t m = 0;
        for (int i = 0; i < 64; ++i)
            if (slimeScalar(rowBase, cx0 + i)) m |= 1ULL << i;
        return m;
    }

    const __m256i c1 = _mm256_set1_epi32(4987142);
    const __m256i c2 = _mm256_set1_epi32(5947611);
    const __m256i rb = _mm256_set1_epi64x(static_cast<long long>(rowBase));
    const __m256i xorv = _mm256_set1_epi64x(static_cast<long long>(kXor));
    const __m256i mask48 = _mm256_set1_epi64x(static_cast<long long>(kMask));
    const __m256i multv = _mm256_set1_epi64x(static_cast<long long>(kMult));
    const __m256i laneLo = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    const __m256i laneHi = _mm256_setr_epi32(8, 9, 10, 11, 12, 13, 14, 15);

    uint64_t out = 0;
    bool rare = false;
    for (int base = 0; base < 64; base += 16) {
        const __m256i bx = _mm256_set1_epi32(cx0 + base);
        const __m256i xlo = _mm256_add_epi32(bx, laneLo);
        const __m256i xhi = _mm256_add_epi32(bx, laneHi);
        const __m256i t1lo = _mm256_mullo_epi32(_mm256_mullo_epi32(xlo, xlo), c1);
        const __m256i t2lo = _mm256_mullo_epi32(xlo, c2);
        const __m256i t1hi = _mm256_mullo_epi32(_mm256_mullo_epi32(xhi, xhi), c1);
        const __m256i t2hi = _mm256_mullo_epi32(xhi, c2);
        const auto build = [&](__m256i t1, __m256i t2, bool hiHalf) {
            const __m128i a1 = hiHalf ? _mm256_extracti128_si256(t1, 1) : _mm256_castsi256_si128(t1);
            const __m128i a2 = hiHalf ? _mm256_extracti128_si256(t2, 1) : _mm256_castsi256_si128(t2);
            __m256i v = _mm256_add_epi64(_mm256_add_epi64(_mm256_cvtepi32_epi64(a1), _mm256_cvtepi32_epi64(a2)), rb);
            return _mm256_and_si256(_mm256_xor_si256(_mm256_xor_si256(v, xorv), multv), mask48);
        };
        const int m0 = lcg4(build(t1lo, t2lo, false), rare);
        const int m1 = lcg4(build(t1lo, t2lo, true), rare);
        const int m2 = lcg4(build(t1hi, t2hi, false), rare);
        const int m3 = lcg4(build(t1hi, t2hi, true), rare);
        const uint32_t m16 = (static_cast<uint32_t>(m0 & 0xF)) | (static_cast<uint32_t>(m1 & 0xF) << 4) |
                             (static_cast<uint32_t>(m2 & 0xF) << 8) | (static_cast<uint32_t>(m3 & 0xF) << 12);
        out |= static_cast<uint64_t>(m16) << base;
    }
    if (rare) {
        uint64_t m = 0;
        for (int i = 0; i < 64; ++i)
            if (slimeScalar(rowBase, cx0 + i)) m |= 1ULL << i;
        return m;
    }
    return out;
}


void scanRowJava(uint64_t rowBase, int32_t cx0, int32_t count, uint64_t* outWords) noexcept
{
    if (!outWords || count <= 0) return;
    const int64_t words = (count + 63) / 64;
    if (!available()) {
        for (int64_t w = 0; w < words; ++w) {
            uint64_t acc = 0;
            const int64_t base = w * 64;
            const int64_t n = (count - base < 64) ? (count - base) : 64;
            for (int64_t i = 0; i < n; ++i)
                if (slimeScalar(rowBase, cx0 + static_cast<int32_t>(base + i))) acc |= 1ULL << i;
            outWords[w] = acc;
        }
        return;
    }

    const __m256i c1 = _mm256_set1_epi32(4987142);
    const __m256i c2 = _mm256_set1_epi32(5947611);
    const __m256i rb = _mm256_set1_epi64x(static_cast<long long>(rowBase));
    const __m256i xorv = _mm256_set1_epi64x(static_cast<long long>(kXor));
    const __m256i mask48 = _mm256_set1_epi64x(static_cast<long long>(kMask));
    const __m256i multv = _mm256_set1_epi64x(static_cast<long long>(kMult));
    const __m256i laneLo = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    const __m256i laneHi = _mm256_setr_epi32(8, 9, 10, 11, 12, 13, 14, 15);

    bool rare = false;
    for (int64_t w = 0; w < words; ++w) {
        const int64_t base = w * 64;
        const int64_t n = (count - base < 64) ? (count - base) : 64;
        uint64_t out = 0;
        const int64_t full = n & ~15LL;   // 16 的整数倍部分走 AVX2
        for (int64_t off = 0; off < full; off += 16) {
            const __m256i bx = _mm256_set1_epi32(static_cast<int>(cx0 + base + off));
            const __m256i xlo = _mm256_add_epi32(bx, laneLo);
            const __m256i xhi = _mm256_add_epi32(bx, laneHi);
            const __m256i t1lo = _mm256_mullo_epi32(_mm256_mullo_epi32(xlo, xlo), c1);
            const __m256i t2lo = _mm256_mullo_epi32(xlo, c2);
            const __m256i t1hi = _mm256_mullo_epi32(_mm256_mullo_epi32(xhi, xhi), c1);
            const __m256i t2hi = _mm256_mullo_epi32(xhi, c2);
            const auto build = [&](__m256i t1, __m256i t2, bool hiHalf) {
                const __m128i a1 = hiHalf ? _mm256_extracti128_si256(t1, 1) : _mm256_castsi256_si128(t1);
                const __m128i a2 = hiHalf ? _mm256_extracti128_si256(t2, 1) : _mm256_castsi256_si128(t2);
                __m256i v =
                    _mm256_add_epi64(_mm256_add_epi64(_mm256_cvtepi32_epi64(a1), _mm256_cvtepi32_epi64(a2)), rb);
                return _mm256_and_si256(_mm256_xor_si256(_mm256_xor_si256(v, xorv), multv), mask48);
            };
            const int m0 = lcg4(build(t1lo, t2lo, false), rare);
            const int m1 = lcg4(build(t1lo, t2lo, true), rare);
            const int m2 = lcg4(build(t1hi, t2hi, false), rare);
            const int m3 = lcg4(build(t1hi, t2hi, true), rare);
            const uint32_t m16 = (static_cast<uint32_t>(m0 & 0xF)) | (static_cast<uint32_t>(m1 & 0xF) << 4) |
                                 (static_cast<uint32_t>(m2 & 0xF) << 8) | (static_cast<uint32_t>(m3 & 0xF) << 12);
            out |= static_cast<uint64_t>(m16) << off;
        }
        for (int64_t i = full; i < n; ++i)
            if (slimeScalar(rowBase, cx0 + static_cast<int32_t>(base + i))) out |= 1ULL << i;
        outWords[w] = out;
    }
    if (rare) {   // 极稀有：整行退回标量，保证逐位一致
        for (int64_t w = 0; w < words; ++w) {
            uint64_t acc = 0;
            const int64_t base = w * 64;
            const int64_t n = (count - base < 64) ? (count - base) : 64;
            for (int64_t i = 0; i < n; ++i)
                if (slimeScalar(rowBase, cx0 + static_cast<int32_t>(base + i))) acc |= 1ULL << i;
            outWords[w] = acc;
        }
    }
}


void scanWordsOr(uint64_t rowBase, int32_t cx0, int32_t wordCount, uint64_t* rowW) noexcept
{
    if (!rowW || wordCount <= 0) return;
    if (!available()) {
        for (int32_t w = 0; w < wordCount; ++w) {
            uint64_t acc = 0;
            const int32_t base = w * 64;
            for (int32_t i = 0; i < 64; ++i)
                if (slimeScalar(rowBase, cx0 + base + i)) acc |= 1ULL << i;
            rowW[w] |= acc;
        }
        return;
    }

    const __m256i c1 = _mm256_set1_epi32(4987142);
    const __m256i c2 = _mm256_set1_epi32(5947611);
    const __m256i rb = _mm256_set1_epi64x(static_cast<long long>(rowBase));
    const __m256i xorv = _mm256_set1_epi64x(static_cast<long long>(kXor));
    const __m256i mask48 = _mm256_set1_epi64x(static_cast<long long>(kMask));
    const __m256i multv = _mm256_set1_epi64x(static_cast<long long>(kMult));
    const __m256i laneLo = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    const __m256i laneHi = _mm256_setr_epi32(8, 9, 10, 11, 12, 13, 14, 15);

    bool rare = false;
    for (int32_t w = 0; w < wordCount; ++w) {
        const int32_t base = cx0 + w * 64;
        uint64_t out = 0;
        for (int32_t off = 0; off < 64; off += 16) {
            const __m256i bx = _mm256_set1_epi32(base + off);
            const __m256i xlo = _mm256_add_epi32(bx, laneLo);
            const __m256i xhi = _mm256_add_epi32(bx, laneHi);
            const __m256i t1lo = _mm256_mullo_epi32(_mm256_mullo_epi32(xlo, xlo), c1);
            const __m256i t2lo = _mm256_mullo_epi32(xlo, c2);
            const __m256i t1hi = _mm256_mullo_epi32(_mm256_mullo_epi32(xhi, xhi), c1);
            const __m256i t2hi = _mm256_mullo_epi32(xhi, c2);
            const auto build = [&](__m256i t1, __m256i t2, bool hiHalf) {
                const __m128i a1 = hiHalf ? _mm256_extracti128_si256(t1, 1) : _mm256_castsi256_si128(t1);
                const __m128i a2 = hiHalf ? _mm256_extracti128_si256(t2, 1) : _mm256_castsi256_si128(t2);
                __m256i v =
                    _mm256_add_epi64(_mm256_add_epi64(_mm256_cvtepi32_epi64(a1), _mm256_cvtepi32_epi64(a2)), rb);
                return _mm256_and_si256(_mm256_xor_si256(_mm256_xor_si256(v, xorv), multv), mask48);
            };
            const int m0 = lcg4(build(t1lo, t2lo, false), rare);
            const int m1 = lcg4(build(t1lo, t2lo, true), rare);
            const int m2 = lcg4(build(t1hi, t2hi, false), rare);
            const int m3 = lcg4(build(t1hi, t2hi, true), rare);
            const uint32_t m16 = (static_cast<uint32_t>(m0 & 0xF)) | (static_cast<uint32_t>(m1 & 0xF) << 4) |
                                 (static_cast<uint32_t>(m2 & 0xF) << 8) | (static_cast<uint32_t>(m3 & 0xF) << 12);
            out |= static_cast<uint64_t>(m16) << off;
        }
        rowW[w] |= out;
    }
    if (rare) {   // 极稀有：整批退回标量
        for (int32_t w = 0; w < wordCount; ++w) {
            uint64_t acc = 0;
            const int32_t base = cx0 + w * 64;
            for (int32_t i = 0; i < 64; ++i)
                if (slimeScalar(rowBase, base + i)) acc |= 1ULL << i;
            rowW[w] |= acc;
        }
    }
}

} // namespace fastjava

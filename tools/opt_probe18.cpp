// opt_probe18.cpp - 基岩版真正的加速点：把「每链 4 lane」改成「每链 8 lane」
//   现有 FastMt 每条链只用一个 256 位寄存器塞 4 个区块（低 4 lane），
//   而 xor / shift / mullo / add 全是 32 位运算，8 个 lane 可以同时跑 8 个区块。
//   这里对比：4 lane/链（现状） vs 8 lane/链，两种都在同样的「链数」下测吞吐。
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <immintrin.h>

namespace {

constexpr uint32_t kSeedMul = 0x1F1F1F1FU;
constexpr uint32_t kInitMul = 1812433253U;
constexpr uint32_t kMagic = 0x9908B0DFU;
constexpr int kIdx397 = 397;

inline uint32_t umul32Lo(uint32_t a, uint32_t b) noexcept
{
    return static_cast<uint32_t>((static_cast<uint64_t>(a) * b) & 0xFFFFFFFFULL);
}
inline uint32_t temper32(uint32_t v) noexcept
{
    v ^= (v >> 11);
    v ^= (v << 7) & 0x9D2C5680U;
    v ^= (v << 15) & 0xEFC60000U;
    v ^= (v >> 18);
    return v;
}
inline bool slimeDraw(uint32_t v) noexcept
{
    return (v % 10U) == 0U;
}
inline __m256i bc(uint32_t v) noexcept
{
    return _mm256_set1_epi32(static_cast<int>(v));
}

// 8 lane 链的一步：t = s^(s>>30); s = mullo(t,c)+i
inline void step8(__m256i& s, __m256i c, __m256i iv) noexcept
{
    const __m256i t = _mm256_xor_si256(s, _mm256_srli_epi32(s, 30));
    s = _mm256_add_epi32(_mm256_mullo_epi32(t, c), iv);
}

// 32 个区块：4 个向量 × 8 lane
inline void draw32x8(uint32_t cx0, uint32_t czu, uint8_t* out32) noexcept
{
    const __m256i c = bc(kInitMul);
    const __m256i stepK = bc(8U * kSeedMul);
    const __m256i czv = bc(czu);
    __m256i s[4];
    __m256i base = _mm256_setr_epi32(static_cast<int>(cx0), static_cast<int>(cx0 + 1),
                                     static_cast<int>(cx0 + 2), static_cast<int>(cx0 + 3),
                                     static_cast<int>(cx0 + 4), static_cast<int>(cx0 + 5),
                                     static_cast<int>(cx0 + 6), static_cast<int>(cx0 + 7));
    base = _mm256_mullo_epi32(base, bc(kSeedMul));
    for (int g = 0; g < 4; ++g) s[g] = _mm256_xor_si256(base, czv), base = _mm256_add_epi32(base, stepK);
    // 重新算（上面的逗号表达式顺序有歧义，重写一版）
    base = _mm256_mullo_epi32(_mm256_setr_epi32(static_cast<int>(cx0), static_cast<int>(cx0 + 1),
                                                static_cast<int>(cx0 + 2), static_cast<int>(cx0 + 3),
                                                static_cast<int>(cx0 + 4), static_cast<int>(cx0 + 5),
                                                static_cast<int>(cx0 + 6), static_cast<int>(cx0 + 7)),
                              bc(kSeedMul));
    for (int g = 0; g < 4; ++g) {
        s[g] = _mm256_xor_si256(base, czv);
        base = _mm256_add_epi32(base, stepK);
    }
    // m0 / m1
    __m256i m0[4], m1[4];
    for (int g = 0; g < 4; ++g) {
        m0[g] = s[g];
        const __m256i t = _mm256_xor_si256(m0[g], _mm256_srli_epi32(m0[g], 30));
        m1[g] = _mm256_add_epi32(_mm256_mullo_epi32(t, c), bc(1U));
        s[g] = m1[g];
    }
    __m256i iv = bc(1U);
    for (int i = 2; i <= kIdx397; ++i) {
        iv = _mm256_add_epi32(iv, bc(1U));
        step8(s[0], c, iv);
        step8(s[1], c, iv);
        step8(s[2], c, iv);
        step8(s[3], c, iv);
    }
    for (int g = 0; g < 4; ++g) {
        const __m256i y = _mm256_or_si256(_mm256_and_si256(m0[g], bc(0x80000000U)),
                                          _mm256_and_si256(m1[g], bc(0x7FFFFFFFU)));
        const __m256i neg = _mm256_sub_epi32(_mm256_setzero_si256(), _mm256_and_si256(y, bc(1U)));
        __m256i v = _mm256_xor_si256(s[g], _mm256_srli_epi32(y, 1));
        v = _mm256_xor_si256(v, _mm256_and_si256(neg, bc(kMagic)));
        v = _mm256_xor_si256(v, _mm256_srli_epi32(v, 11));
        v = _mm256_xor_si256(v, _mm256_and_si256(_mm256_slli_epi32(v, 7), bc(0x9D2C5680U)));
        v = _mm256_xor_si256(v, _mm256_and_si256(_mm256_slli_epi32(v, 15), bc(0xEFC60000U)));
        v = _mm256_xor_si256(v, _mm256_srli_epi32(v, 18));
        alignas(32) uint32_t t[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(t), v);
        for (int j = 0; j < 8; ++j) out32[g * 8 + j] = slimeDraw(t[j]) ? 1 : 0;
    }
}

// 4 lane 版（现状）：4 个向量 × 4 lane = 16 区块；重复两次凑 32 区块方便对比
inline void draw16x4(uint32_t cx0, uint32_t czu, uint8_t* out16) noexcept
{
    const __m256i c = bc(kInitMul);
    const __m256i stepK = bc(4U * kSeedMul);
    const __m256i czv = bc(czu);
    __m256i s[4];
    __m256i base = _mm256_mullo_epi32(_mm256_setr_epi32(static_cast<int>(cx0), static_cast<int>(cx0 + 1),
                                                        static_cast<int>(cx0 + 2), static_cast<int>(cx0 + 3),
                                                        0, 0, 0, 0),
                                      bc(kSeedMul));
    for (int g = 0; g < 4; ++g) {
        s[g] = _mm256_xor_si256(base, czv);
        base = _mm256_add_epi32(base, stepK);
    }
    __m256i m0[4], m1[4];
    for (int g = 0; g < 4; ++g) {
        m0[g] = s[g];
        const __m256i t = _mm256_xor_si256(m0[g], _mm256_srli_epi32(m0[g], 30));
        m1[g] = _mm256_add_epi32(_mm256_mullo_epi32(t, c), bc(1U));
        s[g] = m1[g];
    }
    __m256i iv = bc(1U);
    for (int i = 2; i <= kIdx397; ++i) {
        iv = _mm256_add_epi32(iv, bc(1U));
        step8(s[0], c, iv);
        step8(s[1], c, iv);
        step8(s[2], c, iv);
        step8(s[3], c, iv);
    }
    for (int g = 0; g < 4; ++g) {
        const __m256i y = _mm256_or_si256(_mm256_and_si256(m0[g], bc(0x80000000U)),
                                          _mm256_and_si256(m1[g], bc(0x7FFFFFFFU)));
        const __m256i neg = _mm256_sub_epi32(_mm256_setzero_si256(), _mm256_and_si256(y, bc(1U)));
        __m256i v = _mm256_xor_si256(s[g], _mm256_srli_epi32(y, 1));
        v = _mm256_xor_si256(v, _mm256_and_si256(neg, bc(kMagic)));
        v = _mm256_xor_si256(v, _mm256_srli_epi32(v, 11));
        v = _mm256_xor_si256(v, _mm256_and_si256(_mm256_slli_epi32(v, 7), bc(0x9D2C5680U)));
        v = _mm256_xor_si256(v, _mm256_and_si256(_mm256_slli_epi32(v, 15), bc(0xEFC60000U)));
        v = _mm256_xor_si256(v, _mm256_srli_epi32(v, 18));
        alignas(32) uint32_t t[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(t), v);
        for (int j = 0; j < 4; ++j) out16[g * 4 + j] = slimeDraw(t[j]) ? 1 : 0;
    }
}

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

} // namespace

int main()
{
    const int chunks = 1 << 20;
    std::vector<uint8_t> a(static_cast<size_t>(chunks)), b(static_cast<size_t>(chunks));

    const double msA = timeMs([&] {
        for (int i = 0; i + 32 <= chunks; i += 32) {
            draw16x4(static_cast<uint32_t>(i), 0x1234U, &a[static_cast<size_t>(i)]);
            draw16x4(static_cast<uint32_t>(i + 16), 0x1234U, &a[static_cast<size_t>(i) + 16]);
        }
    });
    const double msB = timeMs([&] {
        for (int i = 0; i + 32 <= chunks; i += 32) {
            draw32x8(static_cast<uint32_t>(i), 0x1234U, &b[static_cast<size_t>(i)]);
        }
    });
    const bool same = std::memcmp(a.data(), b.data(), static_cast<size_t>(chunks)) == 0;
    const double per = chunks;
    std::printf("基岩 MT 判定 %d 区块\n", chunks);
    std::printf("  4 lane/链（现状，4 链 ×2 次）: %8.2f ms | %6.2f ns/区块 | %5.2f M/s\n", msA, msA * 1e6 / per,
                per / (msA * 1000.0));
    std::printf("  8 lane/链（4 链）           : %8.2f ms | %6.2f ns/区块 | %5.2f M/s | 一致=%s\n", msB,
                msB * 1e6 / per, per / (msB * 1000.0), same ? "是" : "否");
    std::printf("  提升：%.2fx\n", msA / (msB > 1e-9 ? msB : 1e-9));
    return 0;
}

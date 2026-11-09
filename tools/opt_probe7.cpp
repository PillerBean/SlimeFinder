// opt_probe7.cpp - v3.6.16 第七支探针：基岩版 MT19937 判定的「链数 vs 吞吐」
//   现状：引擎 29.6 M/s（33.6 ns/区块 ≈ 148 cycles）。理论下限取决于 396 步串行递推
//   能被多少条独立链交织（每条链 4 个 lane）。这里测 4/8/16/24/32 lane 五档，并与标量对比。
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <immintrin.h>

namespace {

constexpr uint32_t kSeedMul = 0x1F1F1F1FU;
constexpr uint32_t kInitMul = 1812433253U;
constexpr uint32_t kMagic = 0x9908B0DFU;
constexpr int kIdx397 = 397;

inline uint32_t umul32Lo(uint32_t a, uint32_t b) noexcept
{
    return static_cast<uint32_t>((static_cast<uint64_t>(a) * static_cast<uint64_t>(b)) & 0xFFFFFFFFULL);
}
inline uint32_t chunkSeed(int32_t cx, uint32_t czu) noexcept
{
    return umul32Lo(static_cast<uint32_t>(cx), kSeedMul) ^ czu;
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

// ---------- 标量：单区块完整 396 步 ----------
bool bedrockScalar(uint32_t seed) noexcept
{
    uint32_t m0 = seed;
    uint32_t m1 = umul32Lo(kInitMul, m0 ^ (m0 >> 30)) + 1U;
    uint32_t c = m1;
    for (int i = 2; i <= kIdx397; ++i) c = umul32Lo(kInitMul, c ^ (c >> 30)) + static_cast<uint32_t>(i);
    const uint32_t y = (m0 & 0x80000000U) | (m1 & 0x7FFFFFFFU);
    const uint32_t mt0 = c ^ (y >> 1) ^ (kMagic & (0U - (y & 1U)));
    return slimeDraw(temper32(mt0));
}

// ---------- AVX2：N 条链 × 4 lane = 4N 个区块 ----------
inline __m256i bc(uint32_t v) noexcept
{
    return _mm256_set1_epi32(static_cast<int>(v));
}
inline __m256i step1(__m256i c, __m256i cA, __m256i iv) noexcept
{
    const __m256i t = _mm256_xor_si256(c, _mm256_srli_epi32(c, 30));
    return _mm256_add_epi32(_mm256_mullo_epi32(t, cA), iv);
}
inline __m256i finish(__m256i m0, __m256i m1, __m256i c397) noexcept
{
    const __m256i y = _mm256_or_si256(_mm256_and_si256(m0, bc(0x80000000U)),
                                      _mm256_and_si256(m1, bc(0x7FFFFFFFU)));
    const __m256i mag = _mm256_and_si256(bc(kMagic), _mm256_sub_epi32(_mm256_setzero_si256(),
                                                                     _mm256_and_si256(y, bc(1U))));
    return _mm256_xor_si256(_mm256_xor_si256(c397, _mm256_srli_epi32(y, 1)), mag);
}
inline void storeTemper(__m256i mt0, uint8_t* out, int n = 4) noexcept
{
    alignas(32) uint32_t t[8];
    _mm256_store_si256(reinterpret_cast<__m256i*>(t), mt0);
    for (int i = 0; i < n; ++i) out[i] = slimeDraw(temper32(t[i])) ? 1 : 0;
}

template <int kChains>
inline void drawN(const __m256i* seeds, uint8_t* out) noexcept
{
    const __m256i cA = bc(kInitMul);
    const __m256i cOne = bc(1U);
    __m256i m0[kChains], m1[kChains], c[kChains];
    for (int k = 0; k < kChains; ++k) {
        m0[k] = seeds[k];
        m1[k] = _mm256_add_epi32(_mm256_mullo_epi32(_mm256_xor_si256(m0[k], _mm256_srli_epi32(m0[k], 30)), cA),
                                 cOne);
        c[k] = m1[k];
    }
    __m256i iv = cOne;
    for (int i = 2; i <= kIdx397; ++i) {
        iv = _mm256_add_epi32(iv, cOne);
        for (int k = 0; k < kChains; ++k) c[k] = step1(c[k], cA, iv);
    }
    for (int k = 0; k < kChains; ++k) storeTemper(finish(m0[k], m1[k], c[k]), out + 4 * k);
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

void run(int chunks)
{
    std::printf("== 基岩 MT 判定 %d 个区块 ==\n", chunks);
    std::vector<uint8_t> ref(static_cast<size_t>(chunks));
    std::vector<uint8_t> got(static_cast<size_t>(chunks));

    uint64_t sum0 = 0;
    const double ms0 = timeMs([&] {
        uint64_t acc = 0;
        for (int i = 0; i < chunks; ++i) {
            const uint32_t seed = chunkSeed(i, 0x1234U);
            const bool s = bedrockScalar(seed);
            ref[static_cast<size_t>(i)] = s ? 1 : 0;
            acc += s ? 1 : 0;
        }
        sum0 = acc;
    });
    const double per = static_cast<double>(chunks);
    std::printf("%-24s %9.2f ms | %7.2f ns/区块 | %6.2f M/s | %llu\n", "标量（单链）", ms0, ms0 * 1e6 / per,
                per / (ms0 * 1000.0), static_cast<unsigned long long>(sum0));

    auto benchN = [&](const char* name, auto&& launch, int batch) {
        uint64_t sum = 0;
        const double ms = timeMs([&] {
            uint64_t acc = 0;
            int i = 0;
            for (; i + batch <= chunks; i += batch) {
                launch(i, &got[static_cast<size_t>(i)]);
                for (int k = 0; k < batch; ++k) acc += got[static_cast<size_t>(i + k)];
            }
            for (; i < chunks; ++i) {   // 尾巴走标量
                const bool s = bedrockScalar(chunkSeed(i, 0x1234U));
                got[static_cast<size_t>(i)] = s ? 1 : 0;
                acc += s ? 1 : 0;
            }
            sum = acc;
        });
        const bool same = std::memcmp(ref.data(), got.data(), static_cast<size_t>(chunks)) == 0;
        std::printf("%-24s %9.2f ms | %7.2f ns/区块 | %6.2f M/s | %llu | 一致=%s\n", name, ms, ms * 1e6 / per,
                    per / (ms * 1000.0), static_cast<unsigned long long>(sum), same ? "是" : "否");
    };

    benchN("AVX2 4 lane（1 链）", [&](int i, uint8_t* out) {
        __m256i s[1];
        alignas(32) uint32_t t[4];
        for (int k = 0; k < 4; ++k) t[k] = chunkSeed(i + k, 0x1234U);
        s[0] = _mm256_load_si256(reinterpret_cast<const __m256i*>(t));
        drawN<1>(s, out);
    }, 4);

    benchN("AVX2 8 lane（2 链）", [&](int i, uint8_t* out) {
        __m256i s[2];
        alignas(32) uint32_t t[8];
        for (int k = 0; k < 8; ++k) t[k] = chunkSeed(i + k, 0x1234U);
        s[0] = _mm256_load_si256(reinterpret_cast<const __m256i*>(t));
        s[1] = _mm256_load_si256(reinterpret_cast<const __m256i*>(t + 4));
        drawN<2>(s, out);
    }, 8);

    benchN("AVX2 16 lane（4 链）", [&](int i, uint8_t* out) {
        __m256i s[4];
        alignas(32) uint32_t t[16];
        for (int k = 0; k < 16; ++k) t[k] = chunkSeed(i + k, 0x1234U);
        for (int k = 0; k < 4; ++k) s[k] = _mm256_load_si256(reinterpret_cast<const __m256i*>(t + 4 * k));
        drawN<4>(s, out);
    }, 16);

    benchN("AVX2 24 lane（6 链）", [&](int i, uint8_t* out) {
        __m256i s[6];
        alignas(32) uint32_t t[24];
        for (int k = 0; k < 24; ++k) t[k] = chunkSeed(i + k, 0x1234U);
        for (int k = 0; k < 6; ++k) s[k] = _mm256_load_si256(reinterpret_cast<const __m256i*>(t + 4 * k));
        drawN<6>(s, out);
    }, 24);

    benchN("AVX2 32 lane（8 链）", [&](int i, uint8_t* out) {
        __m256i s[8];
        alignas(32) uint32_t t[32];
        for (int k = 0; k < 32; ++k) t[k] = chunkSeed(i + k, 0x1234U);
        for (int k = 0; k < 8; ++k) s[k] = _mm256_load_si256(reinterpret_cast<const __m256i*>(t + 4 * k));
        drawN<8>(s, out);
    }, 32);

    benchN("AVX2 48 lane（12 链）", [&](int i, uint8_t* out) {
        __m256i s[12];
        alignas(32) uint32_t t[48];
        for (int k = 0; k < 48; ++k) t[k] = chunkSeed(i + k, 0x1234U);
        for (int k = 0; k < 12; ++k) s[k] = _mm256_load_si256(reinterpret_cast<const __m256i*>(t + 4 * k));
        drawN<12>(s, out);
    }, 48);

    benchN("AVX2 64 lane（16 链）", [&](int i, uint8_t* out) {
        __m256i s[16];
        alignas(32) uint32_t t[64];
        for (int k = 0; k < 64; ++k) t[k] = chunkSeed(i + k, 0x1234U);
        for (int k = 0; k < 16; ++k) s[k] = _mm256_load_si256(reinterpret_cast<const __m256i*>(t + 4 * k));
        drawN<16>(s, out);
    }, 64);
    std::printf("\n");
}

} // namespace

int main()
{
    run(1 << 20);
    return 0;
}

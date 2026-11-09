// opt_probe.cpp - v3.6.16 独立探针：只做「加速小方案」的可行性/收益测量
//   不进主程序、不改引擎，只用相同的数学式做 A/B。
//
// 目的：把 Java 判定拆成「哈希」「LCG」「mod10」三段分别计时，并测 AVX2 哈希与位集写入，
//       从而判断 P1（差分/向量化）到底该打哪一段。
//
// 编译（vcvars 环境下，Release 语义）：
//   cl /nologo /O2 /GL /arch:AVX2 /EHsc /std:c++20 opt_probe.cpp /Fe:opt_probe.exe
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <immintrin.h>

namespace {

constexpr uint64_t kMask = (1ULL << 48) - 1;
constexpr uint64_t kMult = 0x5DEECE66DULL;
constexpr uint64_t kAdd = 0xBULL;

inline int32_t imul32(int32_t a, int32_t b) noexcept
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

// 与引擎 SlimeEngine.cpp 完全一致的分解（行基座 + x 项）
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

// 完整判定（V0 基线）
inline bool slimeV0(uint64_t base, uint64_t xtail) noexcept
{
    uint64_t s = base + xtail;
    s ^= 987234911ULL;
    s = (s ^ kMult) & kMask;       // JavaRandom ctor
    for (;;) {
        s = (s * kMult + kAdd) & kMask;
        const int bits = static_cast<int>(s >> 17);
        const int val = bits % 10;
        if (bits - val + 9 >= 0) return val == 0;
    }
}

// 只做哈希（不含 LCG）：用来量「哈希段」的成本
inline uint64_t hashOnly(uint64_t base, uint64_t xtail) noexcept
{
    uint64_t s = base + xtail;
    s ^= 987234911ULL;
    return s & kMask;
}

// 只做 LCG + mod10（哈希用常数替代）：用来量「LCG 段」的成本
inline bool lcgOnly(uint64_t s) noexcept
{
    s = (s ^ kMult) & kMask;
    for (;;) {
        s = (s * kMult + kAdd) & kMask;
        const int bits = static_cast<int>(s >> 17);
        const int val = bits % 10;
        if (bits - val + 9 >= 0) return val == 0;
    }
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

// V3：AVX2 8 lane 哈希（只算 h32，不接 LCG）—— 用于判断「哈希向量化」的收益
inline __m256i hash8(__m256i xv, __m256i rowHi, __m256i rowLo)
{
    const __m256i c1 = _mm256_set1_epi32(4987142);
    const __m256i c2 = _mm256_set1_epi32(5947611);
    const __m256i xx = _mm256_mullo_epi32(xv, xv);
    const __m256i t1 = _mm256_mullo_epi32(xx, c1);
    const __m256i t2 = _mm256_mullo_epi32(xv, c2);
    return _mm256_add_epi32(_mm256_add_epi32(t1, t2), rowLo);   // 低 32 位（等价于 int 溢出相加）
}

void run(const Cfg& c)
{
    const int64_t chunks = static_cast<int64_t>(c.w) * c.h;
    std::printf("== 配置：%d x %d = %.1f M 区块，seed=%lld ==\n", c.w, c.h,
                static_cast<double>(chunks) / 1e6, static_cast<long long>(c.seed));

    std::vector<uint64_t> rowBase(static_cast<size_t>(c.h));
    std::vector<uint64_t> xTail(static_cast<size_t>(c.w));
    std::vector<uint32_t> xv(static_cast<size_t>(c.w));
    for (int z = 0; z < c.h; ++z) rowBase[static_cast<size_t>(z)] = javaRowBase(c.seed, -c.h / 2 + z);
    for (int x = 0; x < c.w; ++x) {
        xTail[static_cast<size_t>(x)] = javaXTail(-c.w / 2 + x);
        xv[static_cast<size_t>(x)] = static_cast<uint32_t>(-c.w / 2 + x);
    }

    // ---- V0：完整判定 ----
    uint64_t sum0 = 0;
    const double ms0 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x < c.w; ++x)
                acc += slimeV0(rb, xTail[static_cast<size_t>(x)]) ? 1 : 0;
        }
        sum0 = acc;
    });

    // ---- V1：只算哈希 ----
    uint64_t sum1 = 0;
    const double ms1 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x < c.w; ++x)
                acc ^= hashOnly(rb, xTail[static_cast<size_t>(x)]);
        }
        sum1 = acc;
    });

    // ---- V2：只算 LCG+mod10 ----
    uint64_t sum2 = 0;
    const double ms2 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x < c.w; ++x)
                acc += lcgOnly(rb + xTail[static_cast<size_t>(x)]) ? 1 : 0;
        }
        sum2 = acc;
    });

    // ---- V3：AVX2 哈希 8 lane（校验哈希等价 + 计时） ----
    uint64_t sum3 = 0;
    const double ms3 = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            const __m256i rowLo = _mm256_set1_epi32(static_cast<int32_t>(rb));
            for (int x = 0; x + 8 <= c.w; x += 8) {
                const __m256i xvi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&xv[static_cast<size_t>(x)]));
                const __m256i h = hash8(xvi, _mm256_setzero_si256(), rowLo);
                acc += static_cast<uint64_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi32(h, h)));
            }
        }
        sum3 = acc;
    });

    // ---- V4：字节写入 vs 位集写入（同样的计算，不同 store） ----
    std::vector<uint8_t> bytes(static_cast<size_t>(chunks));
    std::vector<uint64_t> bits(static_cast<size_t>((chunks + 63) / 64), 0);
    const double msBytes = timeMs([&] {
        size_t i = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x < c.w; ++x, ++i)
                bytes[i] = slimeV0(rb, xTail[static_cast<size_t>(x)]) ? 1 : 0;
        }
    });
    const double msBits = timeMs([&] {
        size_t i = 0;
        for (int z = 0; z < c.h; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x < c.w; ++x, ++i) {
                if (slimeV0(rb, xTail[static_cast<size_t>(x)])) bits[i >> 6] |= (1ULL << (i & 63));
            }
        }
    });

    const double per = static_cast<double>(chunks);
    const auto report = [per](const char* name, double ms, uint64_t check) {
        const double ns = ms * 1e6 / per;
        const double mps = per / (ms * 1000.0);
        std::printf("%-26s %8.2f ms | %7.3f ns/区块 | %8.1f M/s | check=%llu\n", name, ms, ns, mps,
                    static_cast<unsigned long long>(check));
    };
    report("V0 完整判定（基线）", ms0, sum0);
    report("V1 只算哈希", ms1, sum1 & 0xFFFF);
    report("V2 只算 LCG+mod10", ms2, sum2);
    report("V3 AVX2 哈希(8 lane)", ms3, sum3 & 0xFFFF);
    report("V4a 完整判定 + 字节写入", msBytes, 0);
    report("V4b 完整判定 + 位集写入", msBits, 0);
    std::printf("分配：字节数组 %.1f MB vs 位集 %.1f MB\n\n", chunks / 1e6,
                static_cast<double>(bits.size() * 8) / 1e6);
}

} // namespace

int main()
{
    std::printf("CPU: AVX2=%d\n", [] {
        int regs[4] = { 0, 0, 0, 0 };
        __cpuid(regs, 7);
        return (regs[1] & (1 << 5)) != 0 ? 1 : 0;
    }());
    Cfg c;
    c.w = 4096; c.h = 4096;
    run(c);
    c.w = 16384; c.h = 16384;
    run(c);
    return 0;
}

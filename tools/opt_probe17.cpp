// opt_probe17.cpp - 基岩版：「用更简单的算法拟合 + 例外表」到底可不可行？
//   做法：拿真实 MT19937 判定当基准，试几种「简化算法」，看它们的命中率能到多少。
//   若简化算法只有 ~10%（等于随机密度），例外表就得覆盖 90% 的区块 ⇒ 等价于直接存位图。
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kSeedMul = 0x1F1F1F1FU;
constexpr uint32_t kInitMul = 1812433253U;
constexpr uint32_t kMagic = 0x9908B0DFU;

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

// 完整判定（基准）
inline bool truth(uint32_t seed) noexcept
{
    uint32_t m0 = seed;
    uint32_t m1 = umul32Lo(kInitMul, m0 ^ (m0 >> 30)) + 1U;
    uint32_t c = m1;
    for (int i = 2; i <= 397; ++i) c = umul32Lo(kInitMul, c ^ (c >> 30)) + static_cast<uint32_t>(i);
    const uint32_t y = (m0 & 0x80000000U) | (m1 & 0x7FFFFFFFU);
    const uint32_t mt0 = c ^ (y >> 1) ^ (kMagic & (0U - (y & 1U)));
    return (temper32(mt0) % 10U) == 0U;
}

// 简化 1：只迭代到第 N 步（近似 ML 的「提前截断」）
inline bool approxSteps(uint32_t seed, int steps) noexcept
{
    uint32_t m0 = seed;
    uint32_t m1 = umul32Lo(kInitMul, m0 ^ (m0 >> 30)) + 1U;
    uint32_t c = m1;
    for (int i = 2; i <= steps; ++i) c = umul32Lo(kInitMul, c ^ (c >> 30)) + static_cast<uint32_t>(i);
    const uint32_t y = (m0 & 0x80000000U) | (m1 & 0x7FFFFFFFU);
    const uint32_t mt0 = c ^ (y >> 1) ^ (kMagic & (0U - (y & 1U)));
    return (temper32(mt0) % 10U) == 0U;
}

// 简化 2：跳过 twist（直接用 init 的最后一个状态）
inline bool approxNoTwist(uint32_t seed) noexcept
{
    uint32_t m0 = seed;
    uint32_t c = umul32Lo(kInitMul, m0 ^ (m0 >> 30)) + 1U;
    for (int i = 2; i <= 397; ++i) c = umul32Lo(kInitMul, c ^ (c >> 30)) + static_cast<uint32_t>(i);
    return (temper32(c) % 10U) == 0U;
}

// 简化 3：跳过 temper
inline bool approxNoTemper(uint32_t seed) noexcept
{
    uint32_t m0 = seed;
    uint32_t m1 = umul32Lo(kInitMul, m0 ^ (m0 >> 30)) + 1U;
    uint32_t c = m1;
    for (int i = 2; i <= 397; ++i) c = umul32Lo(kInitMul, c ^ (c >> 30)) + static_cast<uint32_t>(i);
    const uint32_t y = (m0 & 0x80000000U) | (m1 & 0x7FFFFFFFU);
    const uint32_t mt0 = c ^ (y >> 1) ^ (kMagic & (0U - (y & 1U)));
    return (mt0 % 10U) == 0U;
}

// 简化 4：纯哈希（splitmix32），完全不看 MT
inline bool approxHash(uint32_t seed) noexcept
{
    uint32_t z = seed + 0x9E3779B9U;
    z = (z ^ (z >> 16)) * 0x85EBCA6BU;
    z = (z ^ (z >> 13)) * 0xC2B2AE35U;
    z ^= z >> 16;
    return (z % 10U) == 0U;
}

} // namespace

int main()
{
    const int N = 1 << 20;
    uint64_t truthCount = 0;
    struct Row { const char* name; uint64_t hit; };
    Row rows[] = {
        { "截断到 200 步", 0 },
        { "截断到 300 步", 0 },
        { "截断到 396 步（少 1 步）", 0 },
        { "跳过 twist", 0 },
        { "跳过 temper", 0 },
        { "纯哈希 splitmix32", 0 },
    };
    for (int i = 0; i < N; ++i) {
        const uint32_t seed = static_cast<uint32_t>(i * 2654435761U + 12345U);
        const bool t = truth(seed);
        truthCount += t ? 1 : 0;
        rows[0].hit += (approxSteps(seed, 200) == t) ? 1 : 0;
        rows[1].hit += (approxSteps(seed, 300) == t) ? 1 : 0;
        rows[2].hit += (approxSteps(seed, 396) == t) ? 1 : 0;
        rows[3].hit += (approxNoTwist(seed) == t) ? 1 : 0;
        rows[4].hit += (approxNoTemper(seed) == t) ? 1 : 0;
        rows[5].hit += (approxHash(seed) == t) ? 1 : 0;
    }
    std::printf("基岩版判定：%d 个种子，真实史莱姆率 %.2f%%\n\n", N, 100.0 * truthCount / N);
    std::printf("%-24s %10s %10s\n", "简化算法", "命中率", "需例外表覆盖");
    for (const Row& r : rows) {
        const double acc = static_cast<double>(r.hit) / N;
        std::printf("%-24s %9.2f%% %9.2f%%\n", r.name, acc * 100.0, (1.0 - acc) * 100.0);
    }
    std::printf("\n（若命中率 ≈ 90%%，例外表就要存 10%%；≈ 10%% 则要存 90%% —— 等于直接存位图）\n");
    return 0;
}

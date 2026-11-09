// RandomGenerators.h - 与《我的世界》算法逐位一致的随机数发生器
// Java 版: java.util.Random 的 48 位 LCG
// 基岩版: Mersenne Twister MT19937（社区逆向算法，与已验证的参考实现一致）
#pragma once

#include <cstdint>

namespace slime {

// ---------- 32 位溢出乘法（模拟 Java int 乘法溢出，参考任务书 6.1） ----------
// int32 * int32 -> 截断到低 32 位（二补码回绕，与 Java 一致）
inline int32_t int32Mul(int32_t a, int32_t b) noexcept
{
    return static_cast<int32_t>(static_cast<uint64_t>(static_cast<int64_t>(a) * static_cast<int64_t>(b)) & 0xFFFFFFFFULL);
}

// uint32 乘法取低 32 位
inline uint32_t umul32Lo(uint32_t a, uint32_t b) noexcept
{
    return static_cast<uint32_t>((static_cast<uint64_t>(a) * static_cast<uint64_t>(b)) & 0xFFFFFFFFULL);
}

// ---------- JavaRandom：模拟 java.util.Random ----------
// 参考实现（main.cpp / SlimeChecker.exe）：
//   seed = (seed ^ multiplier) & mask；next(n)：推进一次 LCG，取 bits=seed>>17
class JavaRandom {
public:
    explicit JavaRandom(uint64_t seed) noexcept : m_seed((seed ^ kMultiplier) & kMask) {}

    // 与 java.util.Random.nextInt(int) 行为完全一致（含拒绝采样）
    int nextInt(int n) noexcept
    {
        m_seed = (m_seed * kMultiplier + kAddend) & kMask;
        int bits = static_cast<int>(m_seed >> 17);
        if ((n & (n - 1)) == 0) { // 2 的幂
            return static_cast<int>((static_cast<int64_t>(n) * static_cast<int64_t>(bits)) >> 31);
        }
        int val = bits % n;
        while (bits - val + (n - 1) < 0) {
            m_seed = (m_seed * kMultiplier + kAddend) & kMask;
            bits = static_cast<int>(m_seed >> 17);
            val = bits % n;
        }
        return val;
    }

    uint64_t seed() const noexcept { return m_seed; }

private:
    static constexpr uint64_t kMultiplier = 0x5DEECE66DULL;
    static constexpr uint64_t kAddend     = 0xBULL;
    static constexpr uint64_t kMask       = (1ULL << 48) - 1;
    uint64_t m_seed;
};

// ---------- MersenneTwister：基岩版 MT19937 ----------
// 与参考实现逐位一致：以 seed 直接作为 mt[0]，仅做 624 步初始化；
// random_int() 提取一次加扰输出。
class MersenneTwister {
public:
    explicit MersenneTwister(uint32_t seed) noexcept;

    uint32_t nextUInt32() noexcept;

private:
    void twist() noexcept;

    uint32_t m_state[624];
    int m_index; // == 624 表示需要 twist
};

} // namespace slime

// RandomGenerators.cpp - 随机数发生器实现（与参考实现逐位一致）
#include "core/RandomGenerators.h"

namespace slime {

MersenneTwister::MersenneTwister(uint32_t seed) noexcept : m_index(624)
{
    m_state[0] = seed;
    for (int i = 1; i < 624; ++i) {
        // (1812433253 * (mt[i-1] ^ (mt[i-1] >> 30)) + i) mod 2^32
        m_state[i] = (1812433253UL * (m_state[i - 1] ^ (m_state[i - 1] >> 30)) + static_cast<uint32_t>(i)) & 0xFFFFFFFFUL;
    }
}

void MersenneTwister::twist() noexcept
{
    for (int i = 0; i < 624; ++i) {
        const uint32_t y = (m_state[i] & 0x80000000UL) + (m_state[(i + 1) % 624] & 0x7FFFFFFFUL);
        m_state[i] = m_state[(i + 397) % 624] ^ (y >> 1);
        if (y & 1U) {
            m_state[i] ^= 0x9908B0DFUL;
        }
    }
    m_index = 0;
}

uint32_t MersenneTwister::nextUInt32() noexcept
{
    if (m_index >= 624) {
        twist();
    }
    uint32_t y = m_state[m_index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9D2C5680UL;
    y ^= (y << 15) & 0xEFC60000UL;
    y ^= (y >> 18);
    return y;
}

} // namespace slime

// Compare Bedrock biome generation against upstream quick-test hashes.
#include "biomes.h"
#include "generator.h"

#include <cstdint>
#include <cstdio>

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 15;
    x *= 0xd168aaad;
    x ^= x >> 15;
    x *= 0xaf723597;
    x ^= x >> 15;
    return x;
}

static uint32_t refHash(int mc, int bits)
{
    Generator g;
    setupGenerator(&g, mc, 0);
    const int r = 1 << (bits - 1);
    uint32_t h = 0;
    for (int x = -r; x < r; ++x) {
        for (int z = -r; z < r; ++z) {
            const int64_t s = static_cast<int64_t>((z << bits) ^ x);
            applySeed(&g, DIM_OVERWORLD, static_cast<uint64_t>(s));
            const int y = static_cast<int>((hash32(static_cast<uint32_t>(s)) & 0x7fffffff) % 384 - 64) >> 2;
            const int id = getBiomeAt(&g, 4, x, y, z);
            h ^= hash32(static_cast<uint32_t>(s) ^ (static_cast<uint32_t>(id) << (2 * bits)));
        }
    }
    return h;
}

int main()
{
    struct Case { const char* name; int mc; uint32_t expected; };
    const Case cases[] = {
        { "1.20", MC_1_20, 0x0f8888ab }, { "1.19", MC_1_19, 0x391c36ec },
        { "1.18", MC_1_18, 0xade7f891 },





    };
    int fails = 0;
    for (const auto& c : cases) {
        const uint32_t got = refHash(c.mc, 6);
        const bool ok = got == c.expected;
        std::printf("%-5s got=%08x expected=%08x %s\n", c.name, got, c.expected, ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }
    std::printf("HASH_RESULT fails=%d/%zu\n", fails, sizeof(cases) / sizeof(cases[0]));
    return fails ? 1 : 0;
}



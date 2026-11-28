// Isolated Bedrock biome query. This target links ONLY cubiomes-bedrock,
// never the Java cubiomes library used by SlimeFinder's main executable.
#include "biomes.h"
#include "generator.h"
#include "util.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct VersionMap { const char* label; int mc; };

static const VersionMap kVersions[] = {
    { "1.18", MC_1_18 }, { "1.19", MC_1_19 }, { "1.20", MC_1_20 },
    { "1.21", MC_1_21 }, { "1.21.50", MC_1_21_50 }, { "1.21.60", MC_1_21_60 },
    { "26.20", MC_26_20 }, { "26.30", MC_26_30 },
};

static bool parseVersion(const char* text, int* mc)
{
    for (const auto& v : kVersions) {
        if (std::strcmp(text, v.label) == 0) {
            *mc = v.mc;
            return true;
        }
    }
    return false;
}

int main(int argc, char** argv)
{
    if (argc < 6) {
        std::fprintf(stderr, "usage: %s <seed> <version> <x> <z> <y>\n", argv[0]);
        std::fprintf(stderr, "versions:");
        for (const auto& v : kVersions) std::fprintf(stderr, " %s", v.label);
        std::fprintf(stderr, "\n");
        return 2;
    }
    const int64_t seed = std::strtoll(argv[1], nullptr, 10);
    int mc = MC_1_21_60;
    if (!parseVersion(argv[2], &mc)) {
        std::fprintf(stderr, "unsupported version: %s\n", argv[2]);
        return 2;
    }
    const int x = std::atoi(argv[3]);
    const int z = std::atoi(argv[4]);
    const int y = std::atoi(argv[5]);

    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, static_cast<uint64_t>(seed));
    const int id = getBiomeAt(&g, 1, x, y, z);
    std::printf("BEDROCK_BIOME seed=%lld version=%s x=%d z=%d y=%d id=%d name=%s\n",
                static_cast<long long>(seed), argv[2], x, z, y, id,
                (id >= 0) ? biome2str(mc, id) : "unknown");
    return 0;
}

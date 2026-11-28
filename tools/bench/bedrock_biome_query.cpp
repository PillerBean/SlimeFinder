// Isolated Bedrock biome query. This target links ONLY cubiomes-bedrock,
// never the Java cubiomes library used by SlimeFinder's main executable.
#include "biomes.h"
#include "generator.h"
#include "util.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

struct VersionMap { const char* label; int mc; };

static const VersionMap kVersions[] = {
    { "1.0", MC_1_0 }, { "1.1", MC_1_1 }, { "1.2", MC_1_2 },
    { "1.4", MC_1_4 }, { "1.5", MC_1_5 }, { "1.6", MC_1_6 }, { "1.7", MC_1_7 },
    { "1.8", MC_1_8 }, { "1.9", MC_1_9 }, { "1.10", MC_1_10 },
    { "1.11", MC_1_11 }, { "1.12", MC_1_12 }, { "1.13", MC_1_13 }, { "1.14", MC_1_14 },
    { "1.16", MC_1_16 }, { "1.17", MC_1_17 }, { "1.17.30", MC_1_17_30 },
    { "1.18", MC_1_18 }, { "1.19", MC_1_19 }, { "1.20", MC_1_20 },
    { "1.21", MC_1_21 }, { "1.21.50", MC_1_21_50 }, { "1.21.60", MC_1_21_60 },
    { "26.20", MC_26_20 }, { "26.30", MC_26_30 },
    { "26.40", MC_26_40 }, { "26.50", MC_26_50 }, { "26.60", MC_26_60 },
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
    if (argc >= 4 && std::strcmp(argv[1], "--batch") == 0) {
        const int64_t seed = std::strtoll(argv[2], nullptr, 10);
        int mc = MC_1_21_60;
        if (!parseVersion(argv[3], &mc)) {
            std::fprintf(stderr, "unsupported version: %s\n", argv[3]);
            return 2;
        }
        Generator g;
        setupGenerator(&g, mc, 0);
        applySeed(&g, DIM_OVERWORLD, static_cast<uint64_t>(seed));
        int x = 0, z = 0, y = 63;
        while (std::cin >> x >> z >> y) {
            const int id = getBiomeAt(&g, 1, x, y, z);
            std::printf("%d %d %d %d %s\n", x, z, y, id,
                        (id >= 0) ? biome2str(mc, id) : "unknown");
            std::fflush(stdout);
        }
        return 0;
    }
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





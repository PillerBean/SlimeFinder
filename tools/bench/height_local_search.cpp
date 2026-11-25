// Dense block-level local search around one center, heights 0..24.
#include "core/BiomeMap.h"
#include "core/SlimeEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace slime;

struct BlockW { int rx, rz; double w; };

int main(int argc, char** argv)
{
    if (argc < 6) {
        std::fprintf(stderr, "usage: %s seed bx bz radius step\n", argv[0]);
        return 2;
    }
    const int64_t seed = std::strtoll(argv[1], nullptr, 10);
    const int bx = std::atoi(argv[2]);
    const int bz = std::atoi(argv[3]);
    const int rad = std::atoi(argv[4]);
    const int step = std::max(1, std::atoi(argv[5]));
    BiomeMap biome;
    if (!biome.init(defaultJavaVersionId(), seed)) return 1;

    const int win = 128 + rad;
    std::vector<BlockW> blocks;
    for (int dz = -win; dz <= win; ++dz) {
        for (int dx = -win; dx <= win; ++dx) {
            if (dx * dx + dz * dz > 128 * 128) continue;
            const int wx = bx + dx, wz = bz + dz;
            const int cx = wx >= 0 ? wx / 16 : -((-wx + 15) / 16);
            const int cz = wz >= 0 ? wz / 16 : -((-wz + 15) / 16);
            if (!SlimeEngine::isSlimeChunk(Version::Java, seed, cx, cz)) continue;
            const double w = spawnWeight(biome.biomeAt(wx, wz), Platform::Java);
            if (w > 0.0) blocks.push_back(BlockW{dx, dz, w});
        }
    }
    std::fprintf(stderr, "slime blocks=%zu window=%d\n", blocks.size(), win);

    double bestArea = -1.0;
    int bestH = 0, bestOx = 0, bestOz = 0;
    for (int oz = -rad; oz <= rad; oz += step) {
        for (int ox = -rad; ox <= rad; ox += step) {
            double diff[32] = {};
            for (const auto& b : blocks) {
                const int rx = b.rx - ox;
                const int rz = b.rz - oz;
                const int rho2 = rx * rx + rz * rz;
                if (rho2 > 128 * 128) continue;
                int lo2 = 24 * 24 - rho2;
                if (lo2 < 0) lo2 = 0;
                const int hi2 = 128 * 128 - rho2;
                if (hi2 < lo2) continue;
                int lo = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(lo2))));
                int hi = static_cast<int>(std::floor(std::sqrt(static_cast<double>(hi2))));
                if (lo < 0) lo = 0;
                if (hi > 24) hi = 24;
                if (lo <= hi) {
                    diff[lo] += b.w;
                    diff[hi + 1] -= b.w;
                }
            }
            double cur = 0.0;
            for (int h = 0; h <= 24; ++h) {
                cur += diff[h];
                if (cur > bestArea) {
                    bestArea = cur;
                    bestH = h;
                    bestOx = ox;
                    bestOz = oz;
                }
            }
        }
    }
    std::printf("LOCAL seed=%lld base=(%d,%d) radius=%d step=%d best=(%d,%d) h=%d area=%.3f\n",
                static_cast<long long>(seed), bx, bz, rad, step,
                bx + bestOx, bz + bestOz, bestH, bestArea);
    return 0;
}

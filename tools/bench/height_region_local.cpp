// Local fixed-height effective-area region check.
#include "core/BiomeMap.h"
#include "core/SlimeEngine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace slime;

struct B { int rx, rz; double w; };

int main(int argc, char** argv)
{
    if (argc < 7) {
        std::fprintf(stderr, "usage: %s seed bx bz radius step h\n", argv[0]);
        return 2;
    }
    const int64_t seed = std::strtoll(argv[1], nullptr, 10);
    const int bx = std::atoi(argv[2]), bz = std::atoi(argv[3]);
    const int rad = std::atoi(argv[4]), step = std::max(1, std::atoi(argv[5]));
    const int H = std::atoi(argv[6]);
    BiomeMap biome;
    if (!biome.init(defaultJavaVersionId(), seed)) return 1;

    const int win = 128 + rad;
    std::vector<B> blocks;
    for (int dz = -win; dz <= win; ++dz)
        for (int dx = -win; dx <= win; ++dx) {
            const int wx = bx + dx, wz = bz + dz;
            const int cx = wx >= 0 ? wx / 16 : -((-wx + 15) / 16);
            const int cz = wz >= 0 ? wz / 16 : -((-wz + 15) / 16);
            if (!SlimeEngine::isSlimeChunk(Version::Java, seed, cx, cz)) continue;
            const double w = spawnWeight(biome.biomeAt(wx, wz), Platform::Java);
            if (w > 0.0) blocks.push_back(B{dx, dz, w});
        }
    const int n = 2 * rad / step + 1;
    std::vector<double> area(static_cast<size_t>(n) * n, 0.0);
    double best = -1.0;
    for (int iz = 0; iz < n; ++iz)
        for (int ix = 0; ix < n; ++ix) {
            const int ox = ix * step - rad, oz = iz * step - rad;
            double sum = 0.0;
            for (const auto& b : blocks) {
                const int rx = b.rx - ox, rz = b.rz - oz;
                const int d2 = rx * rx + rz * rz + H * H;
                if (d2 >= 24 * 24 && d2 <= 128 * 128) sum += b.w;
            }
            area[static_cast<size_t>(iz) * n + ix] = sum;
            if (sum > best) best = sum;
        }
    int cnt = 0, minx = n, maxx = -1, minz = n, maxz = -1;
    for (int iz = 0; iz < n; ++iz)
        for (int ix = 0; ix < n; ++ix)
            if (area[static_cast<size_t>(iz) * n + ix] >= best - 1e-9) {
                ++cnt;
                minx = std::min(minx, ix); maxx = std::max(maxx, ix);
                minz = std::min(minz, iz); maxz = std::max(maxz, iz);
            }
    std::printf("REGION seed=%lld base=(%d,%d) h=%d radius=%d step=%d best=%.3f cells=%d bbox=(%d..%d,%d..%d)\n",
                static_cast<long long>(seed), bx, bz, H, rad, step, best, cnt,
                bx + minx * step - rad, bx + maxx * step - rad,
                bz + minz * step - rad, bz + maxz * step - rad);
    return 0;
}

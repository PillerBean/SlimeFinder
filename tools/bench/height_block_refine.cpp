// Block-level 16x16 offset refinement for known full-map candidates.
#include "core/BiomeMap.h"
#include "core/SlimeEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace slime;

static std::vector<std::pair<int, int>> parseCenters(const char* text)
{
    std::vector<std::pair<int, int>> out;
    const char* p = text;
    while (p && *p) {
        while (*p == ';' || *p == ' ') ++p;
        if (!*p) break;
        char* end = nullptr;
        const long cx = std::strtol(p, &end, 10);
        if (end == p) break;
        p = end;
        while (*p == ',' || *p == ' ') ++p;
        const long cz = std::strtol(p, &end, 10);
        if (end == p) break;
        p = end;
        out.emplace_back(static_cast<int>(cx), static_cast<int>(cz));
    }
    return out;
}

int main(int argc, char** argv)
{
    const int64_t seed = argc > 1 ? std::strtoll(argv[1], nullptr, 10) : 0;
    const char* centersText = argc > 2 ? argv[2] : std::getenv("RING_CENTERS");
    if (!centersText) {
        std::fprintf(stderr, "usage: %s <seed> \"cx,cz;cx,cz;...\"\n", argv[0]);
        return 2;
    }
    const auto centers = parseCenters(centersText);
    if (centers.empty()) {
        std::fprintf(stderr, "no centers\n");
        return 2;
    }
    BiomeMap biome;
    if (!biome.init(defaultJavaVersionId(), seed)) {
        std::fprintf(stderr, "biome init failed\n");
        return 1;
    }

    struct Hit { int cx, cz, ox, oz, h; double area; };
    std::vector<Hit> hits;
    for (const auto& c : centers) {
        const int cx = c.first, cz = c.second;
        // Eligible blocks are within ±128 of any in-chunk center.
        const int win = 257;
        std::vector<double> w(static_cast<size_t>(win) * win, 0.0);
        for (int z = 0; z < win; ++z) {
            for (int x = 0; x < win; ++x) {
                const int bx = cx * 16 - 128 + x;
                const int bz = cz * 16 - 128 + z;
                w[static_cast<size_t>(z) * win + x] =
                    spawnWeight(biome.biomeAt(bx, bz), Platform::Java);
            }
        }
        double bestArea = -1.0;
        int bestH = 0, bestOx = 0, bestOz = 0;
        for (int oz = 0; oz < 16; ++oz) {
            for (int ox = 0; ox < 16; ++ox) {
                double diff[32] = {};
                for (int dz = -8; dz <= 8; ++dz) {
                    for (int dx = -8; dx <= 8; ++dx) {
                        if (!SlimeEngine::isSlimeChunk(Version::Java, seed, cx + dx, cz + dz))
                            continue;
                        for (int pz = 0; pz < 16; ++pz) {
                            for (int px = 0; px < 16; ++px) {
                                const int rx = dx * 16 + px - ox;
                                const int rz = dz * 16 + pz - oz;
                                if (rx < -128 || rx > 128 || rz < -128 || rz > 128) continue;
                                const double ww = w[static_cast<size_t>(rz + 128) * 257 + (rx + 128)];
                                if (ww <= 0.0) continue;
                                const int rho2 = rx * rx + rz * rz;
                                int lo2 = 24 * 24 - rho2;
                                if (lo2 < 0) lo2 = 0;
                                const int hi2 = 128 * 128 - rho2;
                                if (hi2 < lo2) continue;
                                int lo = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(lo2))));
                                int hi = static_cast<int>(std::floor(std::sqrt(static_cast<double>(hi2))));
                                if (lo < 0) lo = 0;
                                if (hi > 24) hi = 24;
                                if (lo > hi) continue;
                                diff[lo] += ww;
                                diff[hi + 1] -= ww;
                            }
                        }
                    }
                }
                double cur = 0.0;
                for (int h = 0; h <= 24; ++h) {
                    cur += diff[h];
                    if (cur > bestArea + 1e-9) {
                        bestArea = cur;
                        bestH = h;
                        bestOx = ox;
                        bestOz = oz;
                    }
                }
            }
        }
        Hit hit{ cx, cz, bestOx, bestOz, bestH, bestArea };
        hits.push_back(hit);
        std::printf("CENTER cx=%d cz=%d bx=%d bz=%d h=%d area=%.3f\n",
                    cx, cz, cx * 16 + bestOx, cz * 16 + bestOz, bestH, bestArea);
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.area > b.area; });
    std::printf("RANKED %zu\n", hits.size());
    for (size_t i = 0; i < hits.size(); ++i) {
        std::printf("RANK rank=%zu cx=%d cz=%d bx=%d bz=%d h=%d area=%.3f\n",
                    i + 1, hits[i].cx, hits[i].cz,
                    hits[i].cx * 16 + hits[i].ox, hits[i].cz * 16 + hits[i].oz,
                    hits[i].h, hits[i].area);
    }
    return 0;
}

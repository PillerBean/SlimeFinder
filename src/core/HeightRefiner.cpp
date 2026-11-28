#include "core/HeightRefiner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace slime {
namespace {
struct LocalBlock {
    int rx = 0;
    int rz = 0;
    double w = 1.0;
};

struct AreaAt {
    double area = 0.0;
    int64_t blocks = 0;
    int h = 0;
};
} // namespace

int afkBaseYForVersion(int javaVersionId, bool bedrock) noexcept
{
    // v3.5.8：洞穴更新前世界倒数第二层为 y=1；1.18+ 为 y=-63。
    // 基岩版当前按现代世界底部处理（与 AfkDialog 旧逻辑一致）。
    if (bedrock) return -63;
    for (const auto& v : javaVersionOptions()) {
        if (v.id != javaVersionId) continue;
        int major = 0, minor = 0;
        if (std::sscanf(v.label, "%d.%d", &major, &minor) == 2)
            return (major > 1 || (major == 1 && minor >= 18)) ? -63 : 1;
    }
    return -63;
}

HeightRefiner::HeightRefiner(Version version, int64_t seed, int javaVersionId,
                             Platform platform, bool useBiome)
    : m_version(version), m_seed(seed)
{
    m_useBiome = useBiome && platform == Platform::Java && version == Version::Java;
    m_valid = true;
    if (m_useBiome) m_valid = m_biome.init(javaVersionId, seed);
}

HeightResult HeightRefiner::refine(int blockX, int blockZ, int innerRadius,
                                   int outerRadius, int neighborhoodRadius)
{
    HeightResult out;
    if (!m_valid || innerRadius < 0 || outerRadius <= innerRadius || neighborhoodRadius < 0)
        return out;

    const int rad = neighborhoodRadius;
    const int win = outerRadius + rad;
    const int baseX = blockX, baseZ = blockZ;
    std::vector<LocalBlock> blocks;
    blocks.reserve(static_cast<size_t>(win) * 2 * 32);
    for (int dz = -win; dz <= win; ++dz) {
        for (int dx = -win; dx <= win; ++dx) {
            const int wx = baseX + dx, wz = baseZ + dz;
            const int cx = wx >= 0 ? wx / 16 : -((-wx + 15) / 16);
            const int cz = wz >= 0 ? wz / 16 : -((-wz + 15) / 16);
            if (!SlimeEngine::isSlimeChunk(m_version, m_seed, cx, cz)) continue;
            double w = 1.0;
            if (m_useBiome) {
                w = spawnWeight(m_biome.biomeAt(wx, wz), Platform::Java);
                if (w <= 0.0) continue;
            }
            blocks.push_back(LocalBlock{ dx, dz, w });
        }
    }

    auto eval = [&](int ox, int oz, int& outH) {
        double diff[32] = {};
        for (const auto& b : blocks) {
            const int rx = b.rx - ox, rz = b.rz - oz;
            const int rho2 = rx * rx + rz * rz;
            if (rho2 > outerRadius * outerRadius) continue;
            int lo2 = innerRadius * innerRadius - rho2;
            if (lo2 < 0) lo2 = 0;
            const int hi2 = outerRadius * outerRadius - rho2;
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
        double cur = 0.0, best = 0.0;
        int bh = 0;
        for (int h = 0; h <= 24; ++h) {
            cur += diff[h];
            if (cur > best) {
                best = cur;
                bh = h;
            }
        }
        outH = bh;
        return best;
    };

    int bestOx = 0, bestOz = 0, bestH = 0;
    double bestArea = -1.0;
    for (int oz = -rad; oz <= rad; oz += 4) {
        for (int ox = -rad; ox <= rad; ox += 4) {
            int h = 0;
            const double a = eval(ox, oz, h);
            if (a > bestArea) {
                bestArea = a;
                bestH = h;
                bestOx = ox;
                bestOz = oz;
            }
        }
    }
    for (int oz = std::max(-rad, bestOz - 4); oz <= std::min(rad, bestOz + 4); ++oz) {
        for (int ox = std::max(-rad, bestOx - 4); ox <= std::min(rad, bestOx + 4); ++ox) {
            int h = 0;
            const double a = eval(ox, oz, h);
            if (a > bestArea) {
                bestArea = a;
                bestH = h;
                bestOx = ox;
                bestOz = oz;
            }
        }
    }

    // Recompute the full H profile at the final center for the band/blocks.
    double diff[32] = {};
    int64_t count[32] = {};
    for (const auto& b : blocks) {
        const int rx = b.rx - bestOx, rz = b.rz - bestOz;
        const int rho2 = rx * rx + rz * rz;
        if (rho2 > outerRadius * outerRadius) continue;
        int lo2 = innerRadius * innerRadius - rho2;
        if (lo2 < 0) lo2 = 0;
        const int hi2 = outerRadius * outerRadius - rho2;
        if (hi2 < lo2) continue;
        int lo = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(lo2))));
        int hi = static_cast<int>(std::floor(std::sqrt(static_cast<double>(hi2))));
        if (lo < 0) lo = 0;
        if (hi > 24) hi = 24;
        if (lo <= hi) {
            diff[lo] += b.w;
            diff[hi + 1] -= b.w;
            count[lo]++;
            count[hi + 1]--;
        }
    }
    double cur = 0.0, finalBest = 0.0;
    int64_t curCount = 0, finalCount = 0;
    int finalH = 0;
    for (int h = 0; h <= 24; ++h) {
        cur += diff[h];
        curCount += count[h];
        if (cur > finalBest) {
            finalBest = cur;
            finalCount = curCount;
            finalH = h;
        }
    }
    int bandLo = finalH, bandHi = finalH;
    cur = 0.0;
    std::vector<double> prof(25, 0.0);
    for (int h = 0; h <= 24; ++h) {
        cur += diff[h];
        prof[h] = cur;
    }
    while (bandLo > 0 && std::abs(prof[bandLo - 1] - finalBest) <= 1e-9) --bandLo;
    while (bandHi < 24 && std::abs(prof[bandHi + 1] - finalBest) <= 1e-9) ++bandHi;

    out.valid = true;
    out.x = baseX + bestOx;
    out.z = baseZ + bestOz;
    out.bestHeight = finalH;
    out.bandLo = bandLo;
    out.bandHi = bandHi;
    out.effectiveArea = finalBest;
    out.coveredBlocks = finalCount;
    return out;
}

} // namespace slime

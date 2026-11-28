#pragma once

#include <cstdint>
#include <vector>

#include "core/BiomeMap.h"
#include "core/SlimeEngine.h"
#include "core/SpawnWeights.h"

namespace slime {

// v3.15.x: height-aware final scoring for an AFK candidate.
// Platform is the bedrock bottom layer; H is measured above it.
struct HeightResult {
    bool valid = false;
    int x = 0;                 // refined block center
    int z = 0;
    int bestHeight = 0;        // 0..24
    int bandLo = 0;            // contiguous equal-height band
    int bandHi = 0;
    double effectiveArea = 0;  // biome-weighted blocks inside the 3D shell
    int64_t coveredBlocks = 0; // unweighted valid blocks
};

// v3.15.x: 建议挂机点的平台顶面绝对 Y（“基岩层”）：
//   Java 1.18+ / Bedrock = -63；Java 1.18 之前 = 1。
//   建议挂机绝对 Y = afkBaseYForVersion(...) + HeightResult::bestHeight。
//   挂机点对话框与主搜索结果列表共用同一函数，避免两处口径漂移。
int afkBaseYForVersion(int javaVersionId, bool bedrock) noexcept;

class HeightRefiner {
public:
    HeightRefiner(Version version, int64_t seed, int javaVersionId, Platform platform,
                  bool useBiome);
    bool valid() const noexcept { return m_valid; }

    // Searches the 16x16 block neighborhood (coarse step 4, fine step 1),
    // H=0..24, and returns the best center and equal-height band.
    HeightResult refine(int blockX, int blockZ, int innerRadius, int outerRadius,
                        int neighborhoodRadius = 16);

private:
    bool m_valid = false;
    bool m_useBiome = false;
    Version m_version = Version::Java;
    int64_t m_seed = 0;
    BiomeMap m_biome;
};

} // namespace slime

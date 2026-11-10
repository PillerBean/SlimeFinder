// tools/seeded_ring_probe.cpp — 验证 GPU 正方形预筛 + AfkPlanner 种子精修链路。
#include "core/AfkPlanner.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const int W = argc > 1 ? std::atoi(argv[1]) : 2000;
    const int H = argc > 2 ? std::atoi(argv[2]) : 2000;
    const int64_t seed = argc > 3 ? std::atoll(argv[3]) : 4545;
    std::string err;
    auto gpu = slimegpu::createGpuScanAdapter(&err);
    if (!gpu || !gpu->available()) {
        std::printf("GPU unavailable: %s\n", err.c_str());
        return 2;
    }
    const int minX = -W / 2, minZ = -H / 2;
    const int maxX = minX + W - 1, maxZ = minZ + H - 1;
    slime::DensityPrescanParams pp;
    pp.version = slime::Version::Java;
    pp.worldSeed = seed;
    pp.minX = minX; pp.maxX = maxX; pp.minZ = minZ; pp.maxZ = maxZ;
    pp.gridSize = 15;
    pp.minDensity = 0.18;
    pp.bandRows = 1024;
    pp.topPerBand = 512;
    pp.maxSeeds = 4000;
    pp.minSeparation = 16;
    std::vector<slime::DensitySeed> seeds;
    std::atomic<bool> cancel{ false };
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = gpu->prescanDensitySeeds(pp, cancel, seeds, &err);
    const double scanMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("prescan ok=%d seeds=%zu time=%.1f ms err=%s\n", (int)ok, seeds.size(), scanMs, err.c_str());
    if (!ok || seeds.empty()) return 1;

    slime::AfkPlanOptions o;
    o.area = slime::Rect{ minX, maxX, minZ, maxZ };
    o.radius = 128;
    o.innerRadius = 24;
    o.topN = 20;
    o.minDensity = 0.0;
    o.minSeparation = 16;
    o.seedCandidates.reserve(seeds.size());
    for (const auto& sd : seeds) o.seedCandidates.emplace_back(sd.cx, sd.cz);
    const auto pred = [seed](int cx, int cz) {
        return slime::SlimeEngine::isSlimeChunk(slime::Version::Java, seed, cx, cz);
    };
    const auto t1 = std::chrono::steady_clock::now();
    const auto cands = slime::AfkPlanner::plan(o, pred);
    const double refineMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    std::printf("seeded plan: %zu candidates, %.1f ms\n", cands.size(), refineMs);
    for (size_t i = 0; i < cands.size() && i < 5; ++i)
        std::printf("  #%zu (%d,%d) coverage %.2f%%\n", i + 1, cands[i].x, cands[i].z, cands[i].coveragePercent);
    return 0;
}

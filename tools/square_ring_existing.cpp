// tools/square_ring_existing.cpp — 可行性检查：现有正方形密度搜索的输出能否作为环形种子。
#include "core/AfkPlanner.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <atomic>
#include <climits>
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
    const int g = argc > 3 ? std::atoi(argv[3]) : 15;
    const int target = argc > 4 ? std::atoi(argv[4]) : 4000;
    const int ignore = argc > 5 ? std::atoi(argv[5]) : 0;
    const int useGpu = argc > 6 ? std::atoi(argv[6]) : 1;
    const int64_t seed = 4545;
    const int R = 128, inner = 24;
    const int minX = -W / 2, minZ = -H / 2;
    const int maxX = minX + W - 1, maxZ = minZ + H - 1;

    std::unique_ptr<slime::GpuScan> gpu;
    if (useGpu) {
        std::string err;
        gpu = slimegpu::createGpuScanAdapter(&err);
        if (!gpu || !gpu->available()) gpu.reset();
    }

    slime::SearchOptions o;
    o.version = slime::Version::Java;
    o.worldSeed = seed;
    o.minX = minX; o.maxX = maxX; o.minZ = minZ; o.maxZ = maxZ;
    o.mode = slime::Mode::Density;
    o.algorithm = slime::Algorithm::Exact;
    o.gridSize = g;
    o.minDensity = 0.0;
    o.targetCount = target;
    o.stopAtTarget = false;
    o.smartRange = false;
    o.ignoreThreshold = ignore != 0;
    o.threads = 12;
    o.startX = 0;
    o.startZ = 0;
    o.gpuScan = gpu.get();

    std::atomic<bool> cancel{ false };
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    const auto t0 = std::chrono::steady_clock::now();
    eng.run(o, cancel, slime::ProgressFn(), out);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("square g=%d target=%d ignore=%d gpu=%d: %.1f ms, results=%zu, gpuUsed=%d\n",
                g, target, ignore, useGpu, ms, out.results.size(), (int)out.gpuMatchUsed);
    if (out.results.empty()) return 0;
    int64_t sumX = 0, sumZ = 0, minx = INT64_MAX, maxx = INT64_MIN, minz = INT64_MAX, maxz = INT64_MIN;
    for (const auto& m : out.results) {
        const int cx = (m.minX + m.maxX) / 2, cz = (m.minZ + m.maxZ) / 2;
        sumX += cx; sumZ += cz;
        minx = std::min<int64_t>(minx, cx); maxx = std::max<int64_t>(maxx, cx);
        minz = std::min<int64_t>(minz, cz); maxz = std::max<int64_t>(maxz, cz);
    }
    std::printf("  result center range: x[%lld,%lld] z[%lld,%lld], centroid=(%lld,%lld)\n",
                (long long)minx, (long long)maxx, (long long)minz, (long long)maxz,
                (long long)(sumX / (int64_t)out.results.size()), (long long)(sumZ / (int64_t)out.results.size()));

    // 在这些方形结果中心上直接算环形覆盖率（不做局部搜索），看能到多少
    slime::AfkPlanOptions ro;
    ro.area = slime::Rect{ minX, maxX, minZ, maxZ };
    ro.radius = R;
    ro.innerRadius = inner;
    const auto pred = [seed](int cx, int cz) {
        return slime::SlimeEngine::isSlimeChunk(slime::Version::Java, seed, cx, cz);
    };
    double best = 0.0;
    int bestX = 0, bestZ = 0;
    const int step = std::max(1, (int)out.results.size() / 400);   // 最多抽查 400 个
    for (size_t i = 0; i < out.results.size(); i += step) {
        const auto& m = out.results[i];
        const int bx = (m.minX + m.maxX) / 2 * 16 + 8;
        const int bz = (m.minZ + m.maxZ) / 2 * 16 + 8;
        const auto mm = slime::AfkPlanner::measureExact(bx, bz, ro, pred);
        if (mm.coveragePercent > best) { best = mm.coveragePercent; bestX = bx; bestZ = bz; }
    }
    std::printf("  best ring coverage at square centers: %.2f%% (%d,%d)\n", best, bestX, bestZ);
    return 0;
}

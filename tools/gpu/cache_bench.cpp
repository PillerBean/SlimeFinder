#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/SlimeEngine.h"
#include "core/CacheManager.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <utility>
#include <vector>

int main(int argc, char** argv)
{
    constexpr int half = 1024;
    const slime::Rect region{ -half, half - 1, -half, half - 1 };
    slime::CacheManager cache;
    std::vector<std::pair<int, int>> slimeChunks;
    slimeChunks.reserve(static_cast<size_t>(half * 2) * half * 2 / 10 + 16);
    for (int z = region.minZ; z <= region.maxZ; ++z) {
        for (int x = region.minX; x <= region.maxX; ++x) {
            if (slime::SlimeEngine::isSlimeChunk(slime::Version::Bedrock, 0, x, z)) {
                slimeChunks.emplace_back(x, z);
            }
        }
    }
    cache.mergeRegion(region, slimeChunks);
    std::printf("cache entries=%lld ranges=%zu covers=%d\n",
                static_cast<long long>(cache.entryCount()), cache.ranges().size(),
                cache.covers(region) ? 1 : 0);

    std::string gerr;
    auto gpu = slimegpu::createGpuScanAdapter(&gerr);
    if (!gpu) {
        std::fprintf(stderr, "gpu unavailable: %s\n", gerr.c_str());
        return 1;
    }

    slime::SearchOptions opt;
    opt.version = slime::Version::Bedrock;
    opt.worldSeed = 0;
    opt.minX = region.minX;
    opt.maxX = region.maxX;
    opt.minZ = region.minZ;
    opt.maxZ = region.maxZ;
    opt.mode = slime::Mode::Density;
    opt.gridSize = 2;
    opt.minDensity = 0.5;
    opt.targetCount = 50;
    opt.threads = 0;
    opt.useCache = !(argc > 1 && std::string(argv[1]) == "nocache");
    opt.cache = &cache;
    opt.gpuScan = (argc > 1 && std::string(argv[1]) == "cpu") ? nullptr : gpu.get();

    std::atomic<bool> cancel{ false };
    slime::ProgressFn noop;
    for (int i = 0; i < 5; ++i) {
        slime::SearchOutcome out;
        slime::SlimeEngine eng;
        const auto t0 = std::chrono::steady_clock::now();
        eng.run(opt, cancel, noop, out);
        const auto t1 = std::chrono::steady_clock::now();
        const double total = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::printf("run %d ok=%d gpu=%d cacheSkipped=%d cacheHits=%lld scan=%.2f fill=%.2f total=%.2f ms hits=%lld\n",
                    i + 1, out.ok ? 1 : 0, out.gpuScanUsed ? 1 : 0, out.cacheFillSkipped ? 1 : 0,
                    static_cast<long long>(out.cacheHitPoints), out.stats.scanMs, out.stats.fillMs,
                    total, static_cast<long long>(out.totalFound));
    }
    return 0;
}

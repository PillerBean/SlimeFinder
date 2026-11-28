// Probe real CacheStore banded search vs plain Bedrock search.
#include "cache/CacheStore.h"
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>

int main(int argc, char** argv)
{
    const std::filesystem::path root = argc > 1 ? argv[1] : "D:/cache";
    slime::cache::CacheStore store(root);
    if (!store.load()) {
        std::fprintf(stderr, "cache load failed: %s\n", root.string().c_str());
        return 1;
    }
    std::printf("packs=%zu\n", store.packs().size());
    if (store.packs().empty()) return 1;

    const auto& p = store.packs().front();
    const int side = std::getenv("PROBE_SIDE") ? std::atoi(std::getenv("PROBE_SIDE")) : 256;
    const int band = std::getenv("PROBE_BAND") ? std::atoi(std::getenv("PROBE_BAND")) : side;
    const int minX = p.rect.minX, minZ = p.rect.minZ;
    const int maxX = std::min(p.rect.minX + side - 1, p.rect.maxX);
    const int maxZ = std::min(p.rect.minZ + side - 1, p.rect.maxZ);
    std::printf("range=(%d..%d,%d..%d) pack=%s\n", minX, maxX, minZ, maxZ, p.file.c_str());

    slime::SearchOptions opt;
    opt.version = slime::Version::Bedrock;
    opt.minX = minX; opt.maxX = maxX; opt.minZ = minZ; opt.maxZ = maxZ;
    opt.mode = slime::Mode::Density;
    opt.gridSize = 5;
    opt.minDensity = 0.2;
    opt.algorithm = slime::Algorithm::Exact;
    opt.threads = 1;
    opt.targetCount = 10;
    opt.useCache = true;
    opt.cacheBandSize = band;

    const slime::cache::CacheSnapshot snap = store.snapshot();
    auto* storePtr = &store;
    opt.cacheBandMayCover = [storePtr, snap](const slime::Rect& band) {
        return storePtr->coversRange(snap, band);
    };
    opt.cacheBandLoader = [storePtr, snap](const slime::Rect& band) {
        auto cm = std::make_shared<slime::CacheManager>();
        std::string err;
        storePtr->fillCacheManagerRange(snap, *cm, band, &err);
        return cm;
    };

    slime::SlimeEngine engine;
    slime::SearchOutcome banded;
    engine.runBanded(opt, std::atomic<bool>{}, {}, banded);
    std::printf("banded ok=%d results=%zu chunked=%d hitPoints=%lld fillSkipped=%d ms=%.1f err=%s\n",
                banded.ok ? 1 : 0, banded.results.size(), banded.chunkedPath ? 1 : 0,
                static_cast<long long>(banded.cacheHitPoints), banded.cacheFillSkipped ? 1 : 0,
                banded.stats.totalMs, banded.error.c_str());

    slime::SearchOptions plainOpt = opt;
    plainOpt.useCache = false;
    plainOpt.cacheBandMayCover = nullptr;
    plainOpt.cacheBandLoader = nullptr;
    slime::SearchOutcome plain;
    engine.run(plainOpt, std::atomic<bool>{}, {}, plain);
    std::printf("plain ok=%d results=%zu ms=%.1f err=%s\n",
                plain.ok ? 1 : 0, plain.results.size(), plain.stats.totalMs, plain.error.c_str());
    return (banded.ok && plain.ok) ? 0 : 2;
}





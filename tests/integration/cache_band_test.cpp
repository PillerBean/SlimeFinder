#include "core/SlimeEngine.h"
#include "core/CacheManager.h"
#include "cache/CacheStore.h"

#include <cstdio>
#include <memory>
#include <vector>

namespace {
bool sameResults(const std::vector<slime::MatchResult>& a, const std::vector<slime::MatchResult>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].minX != b[i].minX || a[i].maxX != b[i].maxX ||
            a[i].minZ != b[i].minZ || a[i].maxZ != b[i].maxZ) return false;
    }
    return true;
}

bool runCase(const char* name, slime::SearchOptions opt)
{
    slime::SlimeEngine engine;
    slime::SearchOutcome plain;
    engine.run(opt, std::atomic<bool>{}, {}, plain);
    if (!plain.ok) {
        std::printf("%s plain failed: %s\n", name, plain.error.c_str());
        return false;
    }
    slime::SearchOutcome banded;
    opt.cacheBandLoader = [](const slime::Rect&) {
        return std::make_shared<slime::CacheManager>();
    };
    opt.cacheBandSize = 64;
    engine.runBanded(opt, std::atomic<bool>{}, {}, banded);
    if (!banded.ok) {
        std::printf("%s banded failed: %s\n", name, banded.error.c_str());
        return false;
    }
    if (!sameResults(plain.results, banded.results)) {
        std::printf("%s RESULT MISMATCH plain=%zu banded=%zu\n", name, plain.results.size(),
                    banded.results.size());
        for (size_t i = 0; i < (std::min)(plain.results.size(), banded.results.size()); ++i) {
            if (plain.results[i].minX != banded.results[i].minX ||
                plain.results[i].minZ != banded.results[i].minZ) {
                std::printf("  first diff %zu: plain %d,%d banded %d,%d\n", i,
                            plain.results[i].minX, plain.results[i].minZ,
                            banded.results[i].minX, banded.results[i].minZ);
                break;
            }
        }
        return false;
    }
    std::printf("%s OK results=%zu\n", name, plain.results.size());
    return true;
}
} // namespace

int main()
{
    slime::SearchOptions strict;
    strict.version = slime::Version::Bedrock;
    strict.minX = -160; strict.maxX = 159;
    strict.minZ = -160; strict.maxZ = 159;
    strict.mode = slime::Mode::Strict;
    strict.algorithm = slime::Algorithm::Exact;
    strict.threads = 1;
    strict.targetCount = 200;
    strict.pattern = slime::Pattern(2, 2);
    strict.pattern.set(0, 0, slime::CellState::Green);
    strict.pattern.set(0, 1, slime::CellState::Green);
    strict.pattern.set(1, 0, slime::CellState::Green);
    strict.pattern.set(1, 1, slime::CellState::Green);

    slime::SearchOptions density = strict;
    density.mode = slime::Mode::Density;
    density.gridSize = 5;
    density.minDensity = 0.2;
    density.targetCount = 200;

    if (!runCase("strict-empty-cache", strict)) return 1;
    if (!runCase("density-empty-cache", density)) return 1;

    // 有缓存：加载器为每个 band 构造一份实际 Bedrock 缓存，结果必须与无缓存一致。
    {
        slime::SearchOptions cached = strict;
        cached.minX = -128; cached.maxX = 127;
        cached.minZ = -128; cached.maxZ = 127;
        cached.cacheBandSize = 64;
        cached.cacheBandLoader = [](const slime::Rect& band) {
            auto cm = std::make_shared<slime::CacheManager>();
            std::vector<std::pair<int, int>> slime;
            for (int z = band.minZ; z <= band.maxZ; ++z) {
                for (int x = band.minX; x <= band.maxX; ++x) {
                    if (slime::SlimeEngine::isSlimeChunkBedrockMT(x, z)) slime.emplace_back(x, z);
                }
            }
            cm->mergeRaw({ band }, slime);
            return cm;
        };
        slime::SlimeEngine engine;
        slime::SearchOutcome plain;
        slime::SearchOptions plainOpt = cached;
        plainOpt.cacheBandLoader = nullptr;
        engine.run(plainOpt, std::atomic<bool>{}, {}, plain);
        slime::SearchOutcome banded;
        engine.runBanded(cached, std::atomic<bool>{}, {}, banded);
        if (!plain.ok || !banded.ok || !sameResults(plain.results, banded.results)) {
            std::printf("cached RESULT MISMATCH plain=%zu banded=%zu\n", plain.results.size(),
                        banded.results.size());
            return 1;
        }
        std::printf("cached-bedrock OK results=%zu\n", plain.results.size());
    }
    // 真实 CacheStore：只加载与 band 相交的 pack，结果应与无缓存一致。
    if (const char* root = std::getenv("SLIME_TEST_CACHE")) {
        slime::cache::CacheStore store{ std::filesystem::path(root) };
        std::string storeErr;
        if (!store.load(&storeErr, false)) {
            std::printf("store load failed: %s\n", storeErr.c_str());
            return 1;
        }
        slime::SearchOptions cached = strict;
        cached.minX = -128; cached.maxX = 127;
        cached.minZ = -128; cached.maxZ = 127;
        cached.cacheBandSize = 64;
        cached.cacheBandLoader = [&store](const slime::Rect& band) {
            auto cm = std::make_shared<slime::CacheManager>();
            std::string err;
            store.fillCacheManagerRange(*cm, band, &err);
            return cm;
        };
        slime::SlimeEngine engine;
        slime::SearchOutcome plain;
        slime::SearchOptions plainOpt = cached;
        plainOpt.cacheBandLoader = nullptr;
        engine.run(plainOpt, std::atomic<bool>{}, {}, plain);
        slime::SearchOutcome banded;
        engine.runBanded(cached, std::atomic<bool>{}, {}, banded);
        if (!plain.ok || !banded.ok || !sameResults(plain.results, banded.results)) {
            std::printf("real store RESULT MISMATCH plain=%zu banded=%zu\n", plain.results.size(),
                        banded.results.size());
            return 1;
        }
        std::printf("real-store OK results=%zu packs=%zu\n", plain.results.size(), store.packs().size());
    }
    std::printf("cache band test OK\n");
    return 0;
}

// Phase 3 分带对拍：完整 band 缓存 vs CPU 扫描 vs 旧引擎。
#include "core/CacheManager.h"
#include "core/next/Pipeline.h"
#include "core/SlimeEngine.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {
bool same(const std::vector<slime::MatchResult>& a, const std::vector<slime::MatchResult>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].minX != b[i].minX || a[i].maxX != b[i].maxX ||
            a[i].minZ != b[i].minZ || a[i].maxZ != b[i].maxZ) return false;
    }
    return true;
}
} // namespace

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    _putenv_s("OMP_NUM_THREADS", "1");
    int failures = 0;
    for (auto version : { slime::Version::Java, slime::Version::Bedrock }) {
        for (auto mode : { slime::Mode::Strict, slime::Mode::Density }) {
            slime::SearchOptions opt;
            opt.version = version;
            opt.worldSeed = 12345;
            opt.minX = -300;
            opt.maxX = 299;
            opt.minZ = -300;
            opt.maxZ = 299;
            opt.mode = mode;
            opt.algorithm = slime::Algorithm::Exact;
            opt.gridSize = 5;
            opt.minDensity = 0.3;
            opt.targetCount = 50;
            opt.threads = 1;
            opt.directionLocked = true;
            if (mode == slime::Mode::Strict) {
                slime::Pattern p(2, 2);
                for (int r = 0; r < 2; ++r)
                    for (int c = 0; c < 2; ++c) p.set(r, c, slime::CellState::Green);
                opt.pattern = p;
            }

            std::atomic<bool> cancel{ false };
            slime::SearchOutcome oldOut;
            slime::SlimeEngine oldEngine;
            oldEngine.run(opt, cancel, slime::ProgressFn(), oldOut);

            // 先用新框架生成完整区域缓存，再交给分带读取。
            slime::next::Bitmap full;
            slime::next::ScanStats scan;
            slime::next::CpuDeterminer cd;
            std::string err;
            if (!cd.run(opt, cancel, full, scan, &err)) {
                std::printf("cache build failed: %s\n", err.c_str());
                ++failures;
                continue;
            }
            auto cache = std::make_shared<slime::CacheManager>();
            const slime::Rect fullRect{ opt.minX, opt.maxX, opt.minZ, opt.maxZ };
            if (!cache->addPackedWords(fullRect, full.words().data(), full.words().size())) {
                std::printf("addPackedWords failed\n");
                ++failures;
                continue;
            }

            for (int band : { 64, 100 }) {
                slime::next::MaterializeResult cachedOut, plainOut;
                slime::next::MaterializePipeline pipeline;
                slime::SearchOptions cached = opt;
                cached.useCache = true;
                cached.cacheBandSize = band;
                cached.cacheBandLoader = [cache](const slime::Rect&) { return cache; };
                cached.cacheBandMayCover = [](const slime::Rect&) { return true; };
                const bool cachedOk = pipeline.runBanded(cached, cancel, cachedOut, &err);
                slime::SearchOptions plain = opt;
                plain.cacheBandSize = band;
                plain.cacheBandLoader = [](const slime::Rect&) { return std::shared_ptr<slime::CacheManager>(); };
                const bool plainOk = pipeline.runBanded(plain, cancel, plainOut, &err);
                const bool sameCached = oldOut.ok && cachedOk && same(oldOut.results, cachedOut.results) &&
                                        oldOut.totalFound == cachedOut.totalFound;
                const bool samePlain = oldOut.ok && plainOk && same(oldOut.results, plainOut.results) &&
                                       oldOut.totalFound == plainOut.totalFound;
                std::printf("%-8s %-8s band=%3d cache=%s plain=%s cachedBackend=%s\n",
                            version == slime::Version::Java ? "Java" : "Bedrock",
                            mode == slime::Mode::Strict ? "pattern" : "density", band,
                            sameCached ? "SAME" : "DIFF", samePlain ? "SAME" : "DIFF",
                            cachedOut.scan.backend.c_str());
                if (!sameCached || !samePlain) ++failures;
            }
        }
    }
    std::printf("PHASE3-BANDED %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

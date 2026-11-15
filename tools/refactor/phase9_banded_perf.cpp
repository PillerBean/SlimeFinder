// Phase 9：Banded 新路径与旧 runBanded 的耗时对比。
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"
#include "core/next/Pipeline.h"

#include <atomic>
#include <chrono>
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
    slime::SearchOptions opt;
    opt.version = slime::Version::Bedrock;
    opt.worldSeed = 12345;
    opt.minX = -512;
    opt.maxX = 511;
    opt.minZ = -512;
    opt.maxZ = 511;
    opt.mode = slime::Mode::Density;
    opt.algorithm = slime::Algorithm::Exact;
    opt.gridSize = 5;
    opt.minDensity = 0.3;
    opt.targetCount = 50;
    opt.threads = 8;

    slime::next::Bitmap full;
    slime::next::ScanStats scan;
    slime::next::CpuDeterminer cd;
    std::string err;
    std::atomic<bool> cancel{ false };
    if (!cd.run(opt, cancel, full, scan, &err)) {
        std::printf("cache build failed: %s\n", err.c_str());
        return 1;
    }
    auto cache = std::make_shared<slime::CacheManager>();
    const slime::Rect rect{ opt.minX, opt.maxX, opt.minZ, opt.maxZ };
    if (!cache->addPackedWords(rect, full.words().data(), full.words().size())) return 1;

    opt.useCache = true;
    opt.cacheBandSize = 256;
    opt.cacheBandLoader = [cache](const slime::Rect&) { return cache; };
    opt.cacheBandMayCover = [](const slime::Rect&) { return true; };

    slime::SearchOutcome oldOut;
    slime::SlimeEngine oldEngine;
    const auto t0 = std::chrono::steady_clock::now();
    oldEngine.runBanded(opt, cancel, slime::ProgressFn(), oldOut);
    const auto t1 = std::chrono::steady_clock::now();

    slime::next::MaterializeResult newOut;
    slime::next::MaterializePipeline pipeline;
    const auto t2 = std::chrono::steady_clock::now();
    const bool ok = pipeline.runBanded(opt, cancel, newOut, &err);
    const auto t3 = std::chrono::steady_clock::now();

    const double oldMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double newMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    const bool sameRes = oldOut.ok && ok && same(oldOut.results, newOut.results) &&
                         oldOut.totalFound == newOut.totalFound;
    std::printf("banded-perf: old=%.2fms new=%.2fms ratio=%.2fx %s backend=%s\n", oldMs, newMs,
                newMs / (oldMs > 0 ? oldMs : 1.0), sameRes ? "SAME" : "DIFF",
                newOut.scan.backend.c_str());
    std::printf("  old=%zu/%lld new=%zu/%lld\n", oldOut.results.size(),
                static_cast<long long>(oldOut.totalFound), newOut.results.size(),
                static_cast<long long>(newOut.totalFound));
    if (oldOut.results.size() && newOut.results.size()) {
        std::printf("  old0=(%d,%d) new0=(%d,%d)\n", oldOut.results[0].minX, oldOut.results[0].minZ,
                    newOut.results[0].minX, newOut.results[0].minZ);
    }
    slime::SearchOptions plain = opt;
    plain.useCache = false;
    plain.cacheBandLoader = nullptr;
    plain.cacheBandMayCover = nullptr;
    slime::SearchOutcome plainOut;
    oldEngine.run(plain, cancel, slime::ProgressFn(), plainOut);
    const bool plainSameResults = same(plainOut.results, newOut.results);
    std::printf("  plain=%zu/%lld plain-results-vs-new=%s (totalFound semantics differ by design)\n",
                plainOut.results.size(), static_cast<long long>(plainOut.totalFound),
                plainSameResults ? "SAME" : "DIFF");
    std::printf("PHASE9-BANDED-PERF %s\n", sameRes ? "ALL PASS" : "FAIL");
    return sameRes ? 0 : 1;
}

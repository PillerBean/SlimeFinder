// Phase 8：SearchTask 兼容性（Banded 新路径、完整缓存回退、构建模式隔离）。
#include "core/CacheManager.h"
#include "core/SearchTask.h"
#include "core/SlimeEngine.h"
#include "core/AfkPlanner.h"
#include "core/next/Pipeline.h"
#include "core/next/FusedGpu.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
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

slime::SearchOptions base(slime::Version v)
{
    slime::SearchOptions o;
    o.version = v;
    o.worldSeed = 12345;
    o.minX = -128;
    o.maxX = 127;
    o.minZ = -128;
    o.maxZ = 127;
    o.mode = slime::Mode::Density;
    o.algorithm = slime::Algorithm::Exact;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.targetCount = 20;
    o.threads = 2;
    return o;
}

std::shared_ptr<slime::CacheManager> buildCache(const slime::SearchOptions& o)
{
    slime::next::Bitmap bmp;
    slime::next::ScanStats st;
    slime::next::CpuDeterminer cd;
    std::string err;
    std::atomic<bool> cancel{ false };
    if (!cd.run(o, cancel, bmp, st, &err)) return {};
    auto c = std::make_shared<slime::CacheManager>();
    const slime::Rect r{ o.minX, o.maxX, o.minZ, o.maxZ };
    if (!c->addPackedWords(r, bmp.words().data(), bmp.words().size())) return {};
    return c;
}

} // namespace

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    _putenv_s("SLIME_NEXT_PIPELINE", "1");
    int failures = 0;

    // 1) Banded 新路径 + 进度
    {
        slime::SearchOptions opt = base(slime::Version::Bedrock);
        auto cache = buildCache(opt);
        if (!cache) {
            std::printf("cache build failed\n");
            return 1;
        }
        opt.useCache = true;
        opt.cacheBandSize = 64;
        opt.cacheBandLoader = [cache](const slime::Rect&) { return cache; };
        opt.cacheBandMayCover = [](const slime::Rect&) { return true; };
        slime::SearchOptions oldOpt = opt;
        oldOpt.useCache = false;
        oldOpt.cacheBandLoader = nullptr;
        oldOpt.cacheBandMayCover = nullptr;
        std::atomic<bool> cancel{ false };
        slime::SearchOutcome oldOut;
        slime::SlimeEngine oldEngine;
        oldEngine.run(oldOpt, cancel, slime::ProgressFn(), oldOut);

        slime::SearchOutcome newOut;
        std::atomic<bool> done{ false };
        std::atomic<bool> sawProgress{ false };
        double last = -1.0;
        slime::SearchTask task;
        task.start(opt, [&](double f, bool, double, double) {
            if (f > last) {
                last = f;
                sawProgress.store(true, std::memory_order_release);
            }
        }, [&](const slime::SearchOutcome& o) {
            newOut = o;
            done.store(true, std::memory_order_release);
        });
        task.wait();
        const bool ok = done.load() && oldOut.ok && newOut.ok && same(oldOut.results, newOut.results) &&
                        newOut.totalFound == static_cast<int64_t>(newOut.results.size());
        std::printf("banded-new: done=%d ok=%d progress=%d final=%.3f live=%lld scanned=%lld total=%lld results=%zu/%lld %s\n",
                    done.load() ? 1 : 0, newOut.ok ? 1 : 0, sawProgress.load() ? 1 : 0, last,
                    static_cast<long long>(task.liveMatchCount()),
                    static_cast<long long>(task.liveScannedCount()),
                    static_cast<long long>(task.liveTotalCount()), newOut.results.size(),
                    static_cast<long long>(newOut.totalFound), ok ? "SAME" : "DIFF");
        if (!ok || !sawProgress.load() || task.liveScannedCount() <= 0 || task.liveTotalCount() <= 0) ++failures;
    }

    // 2) 完整缓存覆盖：当前原型不接管，应回退旧路径且结果一致
    {
        slime::SearchOptions opt = base(slime::Version::Bedrock);
        auto cache = buildCache(opt);
        if (!cache) {
            std::printf("cache build failed\n");
            ++failures;
        } else {
            opt.useCache = true;
            opt.cache = cache.get();
            slime::SearchOptions oldOpt = opt;
            oldOpt.useCache = false;
            oldOpt.cache = nullptr;
            std::atomic<bool> cancel{ false };
            slime::SearchOutcome oldOut;
            slime::SlimeEngine oldEngine;
            oldEngine.run(oldOpt, cancel, slime::ProgressFn(), oldOut);
            slime::SearchOutcome newOut;
            std::atomic<bool> done{ false };
            slime::SearchTask task;
            task.start(opt, slime::ProgressFn(), [&](const slime::SearchOutcome& o) {
                newOut = o;
                done.store(true, std::memory_order_release);
            });
            task.wait();
            const bool ok = done.load() && oldOut.ok && newOut.ok && same(oldOut.results, newOut.results) &&
                            oldOut.totalFound == newOut.totalFound;
            std::printf("full-cache-fallback: done=%d ok=%d %s\n", done.load() ? 1 : 0,
                        newOut.ok ? 1 : 0, ok ? "SAME" : "DIFF");
            if (!ok) ++failures;
        }
    }

    // 3) 构建模式：feature flag 也必须走旧路径（这里只验证不崩溃且 ok）
    {
        slime::SearchOptions opt = base(slime::Version::Bedrock);
        opt.buildMode = true;
        opt.useCache = true;
        opt.minX = -64;
        opt.maxX = 63;
        opt.minZ = -64;
        opt.maxZ = 63;
        auto cache = std::make_shared<slime::CacheManager>();
        opt.cache = cache.get();
        opt.cacheWrite = cache.get();
        slime::SearchOutcome out;
        std::atomic<bool> done{ false };
        slime::SearchTask task;
        task.start(opt, slime::ProgressFn(), [&](const slime::SearchOutcome& o) {
            out = o;
            done.store(true, std::memory_order_release);
        });
        task.wait();
        std::printf("build-isolation: done=%d ok=%d entries=%lld\n", done.load() ? 1 : 0,
                    out.ok ? 1 : 0, static_cast<long long>(cache->entryCount()));
        int64_t expected = 0;
        for (int z = opt.minZ; z <= opt.maxZ; ++z) {
            for (int x = opt.minX; x <= opt.maxX; ++x) {
                if (slime::SlimeEngine::isSlimeChunk(opt.version, opt.worldSeed, x, z)) ++expected;
            }
        }
        std::printf("build-content: expected=%lld actual=%lld\n", static_cast<long long>(expected),
                    static_cast<long long>(cache->entryCount()));
        if (!done.load() || !out.ok || cache->entryCount() <= 0 || cache->entryCount() != expected) ++failures;
    }

    // 4) 超大范围 Fused 分带进度（>8192 锚点行才切 band）
    {
        slime::SearchOptions opt = base(slime::Version::Java);
        opt.minX = -32;
        opt.maxX = 31;
        opt.minZ = -4200;
        opt.maxZ = 4200;
        opt.targetCount = 10;
        std::string gpuErr;
        auto gpu = slimegpu::createGpuScanAdapter(&gpuErr);
        if (gpu && gpu->available()) {
            opt.gpuScan = gpu.get();
            std::atomic<bool> cancel{ false };
            slime::SearchOutcome oldOut;
            slime::SlimeEngine oldEngine;
            oldEngine.run(opt, cancel, slime::ProgressFn(), oldOut);

            slime::next::ResultCollector collector(slime::next::CollectorKind::DistanceTopN, opt.targetCount);
            slime::next::FusedGpuStats stats;
            slime::next::FusedGpuPipeline fused;
            std::atomic<int> progressCount{ 0 };
            double last = -1.0;
            const bool ok = fused.runDensity(opt, gpu.get(), cancel, collector, stats, &gpuErr,
                                             [&](double f, bool, double, double) {
                                                 ++progressCount;
                                                 last = f;
                                             });
            const bool sameRes = ok && oldOut.ok && same(oldOut.results, collector.results()) &&
                                 oldOut.totalFound == collector.totalFound();
            std::printf("fused-large-progress: ok=%d progressCount=%d final=%.3f %s\n", ok ? 1 : 0,
                        progressCount.load(), last, sameRes ? "SAME" : "DIFF");
            if (!sameRes || progressCount.load() < 2) ++failures;
        }
    }

    // 5) Fused 超大范围在 band 之间取消
    {
        slime::SearchOptions opt = base(slime::Version::Java);
        opt.minX = -32;
        opt.maxX = 31;
        opt.minZ = -4200;
        opt.maxZ = 4200;
        opt.targetCount = 10;
        std::string gpuErr;
        auto gpu = slimegpu::createGpuScanAdapter(&gpuErr);
        if (gpu && gpu->available()) {
            opt.gpuScan = gpu.get();
            std::atomic<bool> cancel{ false };
            slime::next::ResultCollector collector(slime::next::CollectorKind::DistanceTopN, opt.targetCount);
            slime::next::FusedGpuStats stats;
            slime::next::FusedGpuPipeline fused;
            const bool ok = fused.runDensity(opt, gpu.get(), cancel, collector, stats, &gpuErr,
                                             [&](double f, bool, double, double) {
                                                 if (f >= 0.2) cancel.store(true, std::memory_order_relaxed);
                                             });
            std::printf("fused-cancel: ok=%d cancelled=%d\n", ok ? 1 : 0, cancel.load() ? 1 : 0);
            if (ok || !cancel.load()) ++failures;
        }
    }

    // 7) Fused 2048² 通过 SearchTask：进度、live 计数、取消
    {
        std::string gpuErr;
        auto gpu = slimegpu::createGpuScanAdapter(&gpuErr);
        if (gpu && gpu->available()) {
            slime::SearchOptions opt = base(slime::Version::Java);
            opt.minX = -1024;
            opt.maxX = 1023;
            opt.minZ = -1024;
            opt.maxZ = 1023;
            opt.gridSize = 5;
            opt.targetCount = 10;
            opt.threads = 8;
            opt.gpuScan = gpu.get();
            slime::SearchOptions oldOpt = opt;
            oldOpt.gpuScan = nullptr;
            std::atomic<bool> cancel{ false };
            slime::SearchOutcome oldOut;
            slime::SlimeEngine oldEngine;
            oldEngine.run(oldOpt, cancel, slime::ProgressFn(), oldOut);

            slime::SearchOutcome out;
            std::atomic<bool> done{ false };
            std::atomic<int> progressCount{ 0 };
            double last = -1.0;
            slime::SearchTask task;
            task.start(opt, [&](double f, bool, double, double) {
                ++progressCount;
                last = f;
            }, [&](const slime::SearchOutcome& o) {
                out = o;
                done.store(true, std::memory_order_release);
            });
            task.wait();
            const int64_t area = 2048LL * 2048LL;
            const bool ok = done.load() && oldOut.ok && out.ok && same(oldOut.results, out.results) &&
                            oldOut.totalFound == out.totalFound && progressCount.load() >= 2 &&
                            last >= 0.999 && task.liveScannedCount() == area &&
                            task.liveTotalCount() == area && task.liveMatchCount() == out.totalFound;
            std::printf("fused-searchtask: done=%d progress=%d final=%.3f live=%lld/%lld %s\n",
                        done.load() ? 1 : 0, progressCount.load(), last,
                        static_cast<long long>(task.liveScannedCount()),
                        static_cast<long long>(task.liveTotalCount()), ok ? "SAME" : "DIFF");
            if (!ok) ++failures;

            slime::SearchOutcome cancelledOut;
            std::atomic<bool> cancelledDone{ false };
            slime::SearchTask cancelTask;
            cancelTask.start(opt, [&](double f, bool, double, double) {
                if (f >= 0.25) cancelTask.cancel();
            }, [&](const slime::SearchOutcome& o) {
                cancelledOut = o;
                cancelledDone.store(true, std::memory_order_release);
            });
            cancelTask.wait();
            std::printf("fused-searchtask-cancel: done=%d cancelled=%d\n", cancelledDone.load() ? 1 : 0,
                        cancelledOut.cancelled ? 1 : 0);
            if (!cancelledDone.load() || !cancelledOut.cancelled) ++failures;
        }
    }

    // 8) 环形/AFK 隔离：feature flag 不应影响 AfkPlanner
    {
        slime::AfkPlanOptions a;
        a.area = slime::Rect{ -8, 8, -8, 8 };
        a.radius = 64;
        a.innerRadius = 0;
        a.topN = 3;
        a.minSeparation = 8;
        a.coarseStep = 8;
        a.maxWindowBlocks = 256;
        _putenv_s("SLIME_NEXT_PIPELINE", "0");
        const auto off = slime::AfkPlanner::planForSeed(a, slime::Version::Java, 12345);
        _putenv_s("SLIME_NEXT_PIPELINE", "1");
        const auto on = slime::AfkPlanner::planForSeed(a, slime::Version::Java, 12345);
        bool sameAfk = off.size() == on.size();
        for (size_t i = 0; sameAfk && i < off.size(); ++i) {
            sameAfk = off[i].x == on[i].x && off[i].z == on[i].z &&
                      off[i].coveredBlocks == on[i].coveredBlocks;
        }
        std::printf("afk-isolation: candidates=%zu/%zu %s\n", off.size(), on.size(),
                    sameAfk ? "SAME" : "DIFF");
        if (!sameAfk || off.empty()) ++failures;
    }

    std::printf("PHASE8-COMPAT %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

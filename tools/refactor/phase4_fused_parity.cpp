// Phase 4 对拍：FusedGpu 直接路径 vs 旧 CPU 精确搜索。
#include "core/next/FusedGpu.h"
#include "gpu/GpuScanAdapter.h"
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
    std::string gpuErr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (gpu && !gpu->available()) gpu.reset();
    std::printf("GPU: %s\n", gpu ? gpu->deviceName().c_str() : ("unavailable: " + gpuErr).c_str());
    if (!gpu) return 1;

    int failures = 0;
    for (auto version : { slime::Version::Java, slime::Version::Bedrock }) {
        for (auto mode : { slime::Mode::Strict, slime::Mode::Density }) {
            slime::SearchOptions opt;
            opt.version = version;
            opt.worldSeed = 12345;
            opt.minX = -256;
            opt.maxX = 255;
            opt.minZ = -256;
            opt.maxZ = 255;
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

            slime::next::ResultCollector collector(slime::next::CollectorKind::DistanceTopN, opt.targetCount);
            slime::next::FusedGpuStats stats;
            slime::next::FusedGpuPipeline fused;
            std::string err;
            const bool ok = mode == slime::Mode::Strict
                                ? fused.runPattern(opt, gpu.get(), cancel, collector, stats, &err)
                                : fused.runDensity(opt, gpu.get(), cancel, collector, stats, &err);
            const auto newResults = collector.results();
            const bool sameRes = ok && same(oldOut.results, newResults) &&
                                 oldOut.totalFound == collector.totalFound();
            std::printf("%-8s %-8s old=%zu/%lld fused=%zu/%lld %.2fms %s\n",
                        version == slime::Version::Java ? "Java" : "Bedrock",
                        mode == slime::Mode::Strict ? "pattern" : "density", oldOut.results.size(),
                        static_cast<long long>(oldOut.totalFound), newResults.size(),
                        static_cast<long long>(collector.totalFound()), stats.ms,
                        sameRes ? "SAME" : "DIFF");
            if (!sameRes) {
                std::printf("  err=%s\n", err.c_str());
                ++failures;
            }
        }
    }
    std::printf("PHASE4-FUSED %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

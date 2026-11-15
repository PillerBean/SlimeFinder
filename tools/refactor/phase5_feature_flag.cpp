// Phase 5：验证 SearchTask feature flag 默认关闭，开启时走 Planner/PlanExecutor。
#include "core/SearchTask.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

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
    if (!gpu) {
        std::printf("GPU unavailable: %s\n", gpuErr.c_str());
        return 1;
    }

    slime::SearchOptions opt;
    opt.version = slime::Version::Java;
    opt.worldSeed = 12345;
    opt.minX = -256;
    opt.maxX = 255;
    opt.minZ = -256;
    opt.maxZ = 255;
    opt.mode = slime::Mode::Density;
    opt.algorithm = slime::Algorithm::Exact;
    opt.gridSize = 5;
    opt.minDensity = 0.3;
    opt.targetCount = 30;
    opt.threads = 4;
    opt.directionLocked = true;
    opt.gpuScan = gpu.get();

    slime::SearchOptions oldOpt = opt;
    oldOpt.gpuScan = nullptr;
    std::atomic<bool> cancel{ false };
    slime::SearchOutcome oldOut;
    slime::SlimeEngine oldEngine;
    oldEngine.run(oldOpt, cancel, slime::ProgressFn(), oldOut);

    _putenv_s("SLIME_NEXT_PIPELINE", "1");
    slime::SearchOutcome newOut;
    std::atomic<bool> done{ false };
    slime::SearchTask task;
    task.start(opt, slime::ProgressFn(), [&](const slime::SearchOutcome& o) {
        newOut = o;
        done.store(true, std::memory_order_release);
    });
    task.wait();

    const bool sameRes = done.load() && oldOut.ok && newOut.ok && same(oldOut.results, newOut.results) &&
                         oldOut.totalFound == newOut.totalFound;
    std::printf("feature-on: done=%d ok=%d results=%zu/%lld gpuMatch=%d chunked=%d total=%.2fms %s\n",
                done.load() ? 1 : 0, newOut.ok ? 1 : 0, newOut.results.size(),
                static_cast<long long>(newOut.totalFound), newOut.gpuMatchUsed ? 1 : 0,
                newOut.chunkedPath ? 1 : 0, newOut.stats.totalMs, sameRes ? "SAME" : "DIFF");

    _putenv_s("SLIME_NEXT_PIPELINE", "0");
    slime::SearchOutcome offOut;
    std::atomic<bool> offDone{ false };
    slime::SearchTask taskOff;
    taskOff.start(opt, slime::ProgressFn(), [&](const slime::SearchOutcome& o) {
        offOut = o;
        offDone.store(true, std::memory_order_release);
    });
    taskOff.wait();
    const bool offSame = offDone.load() && oldOut.ok && offOut.ok && same(oldOut.results, offOut.results) &&
                         oldOut.totalFound == offOut.totalFound;
    std::printf("feature-off: done=%d ok=%d results=%zu/%lld %s\n", offDone.load() ? 1 : 0,
                offOut.ok ? 1 : 0, offOut.results.size(), static_cast<long long>(offOut.totalFound),
                offSame ? "SAME" : "DIFF");
    if (!sameRes || !newOut.gpuMatchUsed || !offSame) {
        std::printf("FEATURE-FLAG FAIL\n");
        return 1;
    }
    std::printf("FEATURE-FLAG ALL PASS\n");
    return 0;
}

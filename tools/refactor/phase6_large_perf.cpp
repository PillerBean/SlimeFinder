// Phase 6：大范围结果与耗时对比（不改变产品路径）。
#include "core/SlimeEngine.h"
#include "core/next/PlanExecutor.h"
#include "gpu/GpuScanAdapter.h"

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

struct Case {
    const char* name;
    slime::Version version;
    slime::Mode mode;
    int w, h;
    int64_t seed;
    int target;
    bool gpu;
    int64_t classicLimit;
};

bool runCase(const Case& c, const slime::GpuScan* gpu)
{
    slime::SearchOptions opt;
    opt.version = c.version;
    opt.worldSeed = c.seed;
    opt.minX = -c.w / 2;
    opt.maxX = opt.minX + c.w - 1;
    opt.minZ = -c.h / 2;
    opt.maxZ = opt.minZ + c.h - 1;
    opt.mode = c.mode;
    opt.algorithm = slime::Algorithm::Exact;
    opt.gridSize = 5;
    opt.minDensity = 0.3;
    opt.targetCount = c.target;
    opt.threads = 8;
    opt.directionLocked = true;
    if (c.mode == slime::Mode::Strict) {
        slime::Pattern p(2, 2);
        for (int r = 0; r < 2; ++r)
            for (int col = 0; col < 2; ++col) p.set(r, col, slime::CellState::Green);
        opt.pattern = p;
    }
    if (c.gpu && gpu) opt.gpuScan = gpu;

    slime::next::MachineCaps caps;
    caps.gpuAvailable = c.gpu && gpu != nullptr;
    caps.gpuIsAmd = true;
    caps.classicChunkLimit = c.classicLimit;
    slime::next::PlannerInput pin;
    pin.options = opt;
    pin.caps = caps;
    const auto plan = slime::next::makeExecutionPlan(pin);

    slime::SearchOptions oldOpt = opt;
    oldOpt.gpuScan = nullptr;
    std::atomic<bool> cancel{ false };
    slime::SearchOutcome oldOut;
    slime::SlimeEngine oldEngine;
    const auto t0 = std::chrono::steady_clock::now();
    oldEngine.run(oldOpt, cancel, slime::ProgressFn(), oldOut);
    const auto t1 = std::chrono::steady_clock::now();

    slime::next::MaterializeResult newOut;
    std::string err;
    slime::next::PlanExecutor executor;
    const auto t2 = std::chrono::steady_clock::now();
    const bool ok = executor.run(opt, plan, cancel, newOut, &err);
    const auto t3 = std::chrono::steady_clock::now();

    const double oldMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double newMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    const bool sameRes = oldOut.ok && ok && same(oldOut.results, newOut.results) &&
                         oldOut.totalFound == newOut.totalFound;
    std::printf("%-22s plan=%d part=%d old=%8.1fms new=%8.1fms ratio=%5.2fx %s\n", c.name,
                static_cast<int>(plan.family), static_cast<int>(plan.partition.kind), oldMs, newMs,
                newMs / (oldMs > 0.0 ? oldMs : 1.0), sameRes ? "SAME" : "DIFF");
    if (!sameRes) std::printf("  err=%s\n", err.c_str());
    return sameRes;
}
} // namespace

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    std::string gpuErr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (gpu && !gpu->available()) gpu.reset();
    std::printf("GPU: %s\n", gpu ? gpu->deviceName().c_str() : ("unavailable: " + gpuErr).c_str());

    const Case cases[] = {
        { "Java-pattern-2048", slime::Version::Java, slime::Mode::Strict, 2048, 2048, 12345, 50, true,
          int64_t{ 1 } << 40 },
        { "Java-density-2048", slime::Version::Java, slime::Mode::Density, 2048, 2048, 12345, 50, true,
          int64_t{ 1 } << 40 },
        { "Bedrock-density-1024", slime::Version::Bedrock, slime::Mode::Density, 1024, 1024, 12345, 50, true,
          int64_t{ 1 } << 40 },
        { "Bedrock-density-chunk", slime::Version::Bedrock, slime::Mode::Density, 1500, 1500, 12345, 50, false,
          100000 },
    };

    // 预热 OpenCL，避免把首次编译/初始化算进 Java pattern。
    if (gpu) {
        slime::SearchOptions warm;
        warm.version = slime::Version::Java;
        warm.worldSeed = 12345;
        warm.minX = -128;
        warm.maxX = 127;
        warm.minZ = -128;
        warm.maxZ = 127;
        warm.mode = slime::Mode::Density;
        warm.algorithm = slime::Algorithm::Exact;
        warm.gridSize = 5;
        warm.minDensity = 0.3;
        warm.targetCount = 10;
        warm.threads = 8;
        warm.gpuScan = gpu.get();
        slime::next::PlannerInput pin;
        pin.options = warm;
        pin.caps.gpuAvailable = true;
        pin.caps.gpuIsAmd = true;
        const auto plan = slime::next::makeExecutionPlan(pin);
        slime::next::MaterializeResult ignored;
        slime::next::PlanExecutor executor;
        std::atomic<bool> cancel{ false };
        std::string werr;
        executor.run(warm, plan, cancel, ignored, &werr);
    }

    int failures = 0;
    for (const Case& c : cases) {
        if (!runCase(c, gpu.get())) ++failures;
    }
    std::printf("PHASE6-LARGE %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

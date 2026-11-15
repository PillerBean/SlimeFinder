// Phase 7：Java pattern 性能拆解（旧CPU / 新CPU Materialize / 新Chunked / 新FusedGPU）。
#include "core/SlimeEngine.h"
#include "core/next/PlanExecutor.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
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

slime::SearchOptions base()
{
    slime::SearchOptions opt;
    opt.version = slime::Version::Java;
    opt.worldSeed = 12345;
    opt.minX = -1024;
    opt.maxX = 1023;
    opt.minZ = -1024;
    opt.maxZ = 1023;
    opt.mode = slime::Mode::Strict;
    opt.algorithm = slime::Algorithm::Exact;
    opt.targetCount = 50;
    opt.threads = 8;
    opt.directionLocked = true;
    slime::Pattern p(2, 2);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c) p.set(r, c, slime::CellState::Green);
    opt.pattern = p;
    return opt;
}

double runNew(const slime::SearchOptions& opt, const slime::next::MachineCaps& caps, std::string* err)
{
    slime::next::PlannerInput pin;
    pin.options = opt;
    pin.caps = caps;
    const auto plan = slime::next::makeExecutionPlan(pin);
    slime::next::PlanExecutor executor;
    slime::next::MaterializeResult out;
    std::atomic<bool> cancel{ false };
    const auto t0 = std::chrono::steady_clock::now();
    if (!executor.run(opt, plan, cancel, out, err)) return -1.0;
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
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

    slime::SearchOptions opt = base();
    opt.gpuScan = gpu.get();

    slime::next::MachineCaps cpuCaps;
    cpuCaps.gpuAvailable = false;
    cpuCaps.classicChunkLimit = int64_t{ 1 } << 40;
    slime::next::MachineCaps chunkCaps = cpuCaps;
    chunkCaps.classicChunkLimit = 100000;
    slime::next::MachineCaps gpuCaps;
    gpuCaps.gpuAvailable = true;
    gpuCaps.gpuIsAmd = true;
    gpuCaps.classicChunkLimit = int64_t{ 1 } << 40;

    slime::SearchOptions oldOpt = opt;
    oldOpt.gpuScan = nullptr;
    std::atomic<bool> cancel{ false };
    slime::SearchOutcome reference;
    slime::SlimeEngine oldEngine;
    oldEngine.run(oldOpt, cancel, slime::ProgressFn(), reference);

    // 预热 GPU，把 OpenCL 首次初始化从测量中剔除。
    std::string err;
    runNew(opt, gpuCaps, &err);

    struct Variant {
        const char* name;
        slime::next::MachineCaps caps;
        bool old;
    };
    const Variant variants[] = {
        { "old-CPU", cpuCaps, true },
        { "new-CPU-materialize", cpuCaps, false },
        { "new-CPU-chunked", chunkCaps, false },
        { "new-fused-GPU", gpuCaps, false },
    };

    int failures = 0;
    for (const auto& v : variants) {
        std::vector<double> times;
        slime::SearchOutcome lastOld;
        slime::next::MaterializeResult lastNew;
        for (int i = 0; i < 7; ++i) {
            if (v.old) {
                slime::SearchOutcome out;
                const auto t0 = std::chrono::steady_clock::now();
                oldEngine.run(oldOpt, cancel, slime::ProgressFn(), out);
                times.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                lastOld = out;
            } else {
                slime::next::PlannerInput pin;
                pin.options = opt;
                pin.caps = v.caps;
                const auto plan = slime::next::makeExecutionPlan(pin);
                slime::next::PlanExecutor executor;
                slime::next::MaterializeResult out;
                const auto t0 = std::chrono::steady_clock::now();
                const bool ok = executor.run(opt, plan, cancel, out, &err);
                times.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                if (!ok) {
                    std::printf("%s failed: %s\n", v.name, err.c_str());
                    ++failures;
                }
                lastNew = out;
            }
        }
        std::sort(times.begin(), times.end());
        const bool resOk = v.old ? (reference.ok && lastOld.ok && same(reference.results, lastOld.results) &&
                                   reference.totalFound == lastOld.totalFound)
                                 : (reference.ok && same(reference.results, lastNew.results) &&
                                    reference.totalFound == lastNew.totalFound);
        std::printf("%-22s first=%7.2fms median=%7.2fms min=%7.2fms %s\n", v.name, times.front(),
                    times[times.size() / 2], times.front(), resOk ? "SAME" : "DIFF");
        if (!resOk) ++failures;
    }
    std::printf("PHASE7-PATTERN-PERF %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

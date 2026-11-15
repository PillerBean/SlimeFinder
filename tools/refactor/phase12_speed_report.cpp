// Phase 12：新分支速度状态统一采样（warmup + 5 次中位数）。
#include "core/CacheManager.h"
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

struct Timed {
    double median = 0.0;
    double min = 0.0;
    double max = 0.0;
    size_t results = 0;
    int64_t total = 0;
    std::vector<slime::MatchResult> vec;
    double scanMs = 0.0;
    double matchMs = 0.0;
};

slime::SearchOptions makeOpt(slime::Version v, slime::Mode m, int w, int h, int64_t seed, int target)
{
    slime::SearchOptions o;
    o.version = v;
    o.worldSeed = seed;
    o.minX = -w / 2;
    o.maxX = o.minX + w - 1;
    o.minZ = -h / 2;
    o.maxZ = o.minZ + h - 1;
    o.mode = m;
    o.algorithm = slime::Algorithm::Exact;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.targetCount = target;
    o.threads = 8;
    o.directionLocked = true;
    if (m == slime::Mode::Strict) {
        slime::Pattern p(2, 2);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) p.set(r, c, slime::CellState::Green);
        o.pattern = p;
    }
    return o;
}

double elapsedMs(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

Timed oldRun(const slime::SearchOptions& input)
{
    slime::SearchOptions opt = input;
    opt.gpuScan = nullptr;
    std::atomic<bool> cancel{ false };
    std::vector<double> t;
    slime::SearchOutcome last;
    for (int i = 0; i < 6; ++i) {
        slime::SlimeEngine engine;
        const auto a = std::chrono::steady_clock::now();
        engine.run(opt, cancel, slime::ProgressFn(), last);
        const auto b = std::chrono::steady_clock::now();
        if (i > 0 && last.ok) t.push_back(elapsedMs(a, b));
    }
    std::sort(t.begin(), t.end());
    Timed r;
    r.median = t[t.size() / 2];
    r.min = t.front();
    r.max = t.back();
    r.results = last.results.size();
    r.total = last.totalFound;
    r.vec = last.results;
    return r;
}

Timed newRun(const slime::SearchOptions& input, const slime::next::MachineCaps& caps)
{
    slime::next::PlannerInput pin;
    pin.options = input;
    pin.caps = caps;
    const auto plan = slime::next::makeExecutionPlan(pin);
    std::vector<double> t;
    slime::next::MaterializeResult last;
    for (int i = 0; i < 6; ++i) {
        slime::next::PlanExecutor executor;
        slime::next::MaterializeResult out;
        std::atomic<bool> cancel{ false };
        std::string err;
        const auto a = std::chrono::steady_clock::now();
        const bool ok = executor.run(input, plan, cancel, out, &err);
        const auto b = std::chrono::steady_clock::now();
        if (i > 0 && ok) t.push_back(elapsedMs(a, b));
        last = std::move(out);
    }
    std::sort(t.begin(), t.end());
    Timed r;
    r.median = t[t.size() / 2];
    r.min = t.front();
    r.max = t.back();
    r.results = last.results.size();
    r.total = last.totalFound;
    r.vec = last.results;
    r.scanMs = last.scan.ms;
    r.matchMs = last.match.ms;
    return r;
}

void report(const char* name, const Timed& oldT, const Timed& newT, const char* note = "")
{
    const bool sameRes = same(oldT.vec, newT.vec) && oldT.total == newT.total;
    std::printf("%-24s old=%7.2fms new=%7.2fms ratio=%5.2fx old/new=%zu/%lld %zu/%lld %s %s\n", name,
                oldT.median, newT.median, newT.median / oldT.median, oldT.results,
                static_cast<long long>(oldT.total), newT.results, static_cast<long long>(newT.total),
                sameRes ? "SAME" : "DIFF", note);
}

} // namespace

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    std::string gpuErr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (gpu && !gpu->available()) gpu.reset();
    std::printf("GPU: %s | warmup=1 samples=5 medians\n",
                gpu ? gpu->deviceName().c_str() : ("unavailable: " + gpuErr).c_str());

    slime::next::MachineCaps cpuCaps;
    cpuCaps.gpuAvailable = false;
    cpuCaps.classicChunkLimit = int64_t{ 1 } << 40;
    slime::next::MachineCaps gpuCaps;
    gpuCaps.gpuAvailable = gpu != nullptr;
    gpuCaps.gpuIsAmd = true;
    gpuCaps.classicChunkLimit = int64_t{ 1 } << 40;
    slime::next::MachineCaps chunkCaps = cpuCaps;
    chunkCaps.classicChunkLimit = 100000;

    // GPU warmup
    if (gpu) {
        auto w = makeOpt(slime::Version::Java, slime::Mode::Density, 256, 256, 1, 10);
        w.gpuScan = gpu.get();
        (void)newRun(w, gpuCaps);
    }

    {
        auto opt = makeOpt(slime::Version::Java, slime::Mode::Strict, 2048, 2048, 12345, 50);
        opt.gpuScan = gpu.get();
        const Timed o = oldRun(opt), n = newRun(opt, gpuCaps);
        // 额外采样一次 CPU materialize，便于看清“为什么 CPU 新路径没开”。
        const Timed c = newRun(opt, cpuCaps);
        slime::SearchOptions noGpu = opt;
        noGpu.gpuScan = nullptr;
        slime::SearchOptions ref = noGpu;
        std::atomic<bool> cancel{ false };
        slime::SearchOutcome refOut;
        slime::SlimeEngine e;
        e.run(ref, cancel, slime::ProgressFn(), refOut);
        report("Java pattern 2048", o, n, "fused");
        std::printf("%-24s cpu-materialize median=%7.2fms scan=%7.2fms match=%7.2fms\n", "",
                    c.median, c.scanMs, c.matchMs);
    }

    {
        auto opt = makeOpt(slime::Version::Java, slime::Mode::Density, 2048, 2048, 12345, 50);
        opt.gpuScan = gpu.get();
        const Timed o = oldRun(opt), n = newRun(opt, gpuCaps);
        const Timed c = newRun(opt, cpuCaps);
        slime::SearchOptions noGpu = opt;
        noGpu.gpuScan = nullptr;
        std::atomic<bool> cancel{ false };
        slime::SearchOutcome refOut;
        slime::SlimeEngine e;
        e.run(noGpu, cancel, slime::ProgressFn(), refOut);
        report("Java density 2048", o, n, "fused-2band");
        std::printf("%-24s cpu-materialize median=%7.2fms scan=%7.2fms match=%7.2fms\n", "",
                    c.median, c.scanMs, c.matchMs);
    }

    {
        auto opt = makeOpt(slime::Version::Bedrock, slime::Mode::Density, 1024, 1024, 12345, 50);
        opt.gpuScan = gpu.get();
        const Timed o = oldRun(opt), n = newRun(opt, gpuCaps);
        slime::SearchOptions noGpu = opt;
        noGpu.gpuScan = nullptr;
        std::atomic<bool> cancel{ false };
        slime::SearchOutcome refOut;
        slime::SlimeEngine e;
        e.run(noGpu, cancel, slime::ProgressFn(), refOut);
        report("Bedrock density 1024", o, n, "fused");
    }

    {
        auto opt = makeOpt(slime::Version::Bedrock, slime::Mode::Density, 1500, 1500, 12345, 50);
        const Timed o = oldRun(opt), n = newRun(opt, chunkCaps);
        slime::SearchOptions noGpu = opt;
        noGpu.gpuScan = nullptr;
        std::atomic<bool> cancel{ false };
        slime::SearchOutcome refOut;
        slime::SlimeEngine e;
        e.run(noGpu, cancel, slime::ProgressFn(), refOut);
        report("Bedrock chunked 1500", o, n, "chunked");
    }

    // Banded full-cache: old runBanded vs new runBanded
    {
        auto opt = makeOpt(slime::Version::Bedrock, slime::Mode::Density, 1024, 1024, 12345, 50);
        slime::next::Bitmap full;
        slime::next::ScanStats scan;
        slime::next::CpuDeterminer cd;
        std::string err;
        std::atomic<bool> cancel{ false };
        if (cd.run(opt, cancel, full, scan, &err)) {
            auto cache = std::make_shared<slime::CacheManager>();
            const slime::Rect rect{ opt.minX, opt.maxX, opt.minZ, opt.maxZ };
            if (cache->addPackedWords(rect, full.words().data(), full.words().size())) {
                opt.useCache = true;
                opt.cacheBandSize = 256;
                opt.cacheBandLoader = [cache](const slime::Rect&) { return cache; };
                opt.cacheBandMayCover = [](const slime::Rect&) { return true; };
                std::vector<double> ot, nt;
                slime::SearchOutcome oldOut;
                slime::next::MaterializeResult newOut;
                for (int i = 0; i < 6; ++i) {
                    slime::SlimeEngine oldEngine;
                    const auto a = std::chrono::steady_clock::now();
                    oldEngine.runBanded(opt, cancel, slime::ProgressFn(), oldOut);
                    const auto b = std::chrono::steady_clock::now();
                    slime::next::MaterializePipeline p;
                    const auto c = std::chrono::steady_clock::now();
                    const bool ok = p.runBanded(opt, cancel, newOut, &err);
                    const auto d = std::chrono::steady_clock::now();
                    if (i > 0 && ok) {
                        ot.push_back(elapsedMs(a, b));
                        nt.push_back(elapsedMs(c, d));
                    }
                }
                std::sort(ot.begin(), ot.end());
                std::sort(nt.begin(), nt.end());
                const bool sameRes = oldOut.ok && same(oldOut.results, newOut.results) &&
                                     oldOut.totalFound == newOut.totalFound;
                std::printf("%-24s old=%7.2fms new=%7.2fms ratio=%5.2fx old/new=%zu/%lld %zu/%lld %s banded\n",
                            "Bedrock banded 1024", ot[ot.size() / 2], nt[nt.size() / 2],
                            nt[nt.size() / 2] / ot[ot.size() / 2], oldOut.results.size(),
                            static_cast<long long>(oldOut.totalFound), newOut.results.size(),
                            static_cast<long long>(newOut.totalFound), sameRes ? "SAME" : "DIFF");
            }
        }
    }

    return 0;
}

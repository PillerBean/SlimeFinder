// Phase 13：拆解“旧版为什么慢”：CPU/GPU 路径与反馈开销。
#include "core/SlimeEngine.h"
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

slime::SearchOptions makeOpt(slime::Version v, slime::Mode m, int w, int h, int target)
{
    slime::SearchOptions o;
    o.version = v;
    o.worldSeed = 12345;
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

struct Result {
    double median = 0.0;
    bool ok = false;
    bool gpuScan = false;
    bool gpuMatch = false;
    size_t results = 0;
    int64_t total = 0;
};

Result runOld(const slime::SearchOptions& input, bool useGpu, bool feedback)
{
    slime::SearchOptions opt = input;
    if (!useGpu) opt.gpuScan = nullptr;
    std::atomic<int64_t> liveMatches{ 0 }, liveScanned{ 0 }, liveTotal{ 0 };
    if (feedback) {
        opt.liveMatches = &liveMatches;
        opt.liveScanned = &liveScanned;
        opt.liveTotal = &liveTotal;
    }
    slime::ProgressFn cb;
    if (feedback) cb = [](double, bool, double, double) {};
    std::vector<double> times;
    slime::SearchOutcome last;
    for (int i = 0; i < 6; ++i) {
        slime::SlimeEngine e;
        std::atomic<bool> cancel{ false };
        const auto a = std::chrono::steady_clock::now();
        e.run(opt, cancel, cb, last);
        const auto b = std::chrono::steady_clock::now();
        if (i > 0) times.push_back(std::chrono::duration<double, std::milli>(b - a).count());
    }
    std::sort(times.begin(), times.end());
    Result r;
    r.median = times[times.size() / 2];
    r.ok = last.ok;
    r.gpuScan = last.gpuScanUsed;
    r.gpuMatch = last.gpuMatchUsed;
    r.results = last.results.size();
    r.total = last.totalFound;
    return r;
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

    const struct {
        const char* name;
        slime::Version v;
        slime::Mode m;
        int w, h;
    } cases[] = {
        { "Java pattern 2048", slime::Version::Java, slime::Mode::Strict, 2048, 2048 },
        { "Java density 2048", slime::Version::Java, slime::Mode::Density, 2048, 2048 },
        { "Bedrock density 1024", slime::Version::Bedrock, slime::Mode::Density, 1024, 1024 },
    };

    for (const auto& c : cases) {
        auto opt = makeOpt(c.v, c.m, c.w, c.h, 50);
        opt.gpuScan = gpu.get();
        (void)runOld(opt, true, false); // warmup
        const Result cpu = runOld(opt, false, false);
        const Result cpuFb = runOld(opt, false, true);
        const Result gpuR = runOld(opt, true, false);
        std::printf("%-20s oldCPU=%7.2fms oldCPU+feedback=%7.2fms oldGPU=%7.2fms gpuScan=%d gpuMatch=%d\n",
                    c.name, cpu.median, cpuFb.median, gpuR.median, gpuR.gpuScan ? 1 : 0,
                    gpuR.gpuMatch ? 1 : 0);
        std::printf("%-20s feedbackOverhead=%+.1f%% gpuSpeedup=%.2fx results=%zu/%lld\n", "",
                    (cpuFb.median / cpu.median - 1.0) * 100.0, gpuR.median / cpu.median, cpu.results,
                    static_cast<long long>(cpu.total));
    }
    return 0;
}

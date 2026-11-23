// Search speed matrix for SlimeFinder 3.13.12.
// Covers mode/algorithm/backend/stop/smart/threads and ETA vs actual.
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using Clock = std::chrono::steady_clock;

namespace {

slime::SearchOptions makeOptions(int side, slime::Mode mode, slime::Algorithm alg)
{
    slime::SearchOptions o;
    o.version = slime::Version::Java;
    o.worldSeed = 12345;
    o.minX = -(side / 2);
    o.maxX = side / 2 - 1;
    o.minZ = -(side / 2);
    o.maxZ = side / 2 - 1;
    o.mode = mode;
    o.algorithm = alg;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.targetCount = 200000;
    o.stopAtTarget = false;
    o.smartRange = false;
    o.threads = 0;
    return o;
}

void runOne(const char* name, const slime::SearchOptions& opt, const slime::GpuScan* gpu,
            bool withEstimate)
{
    slime::SearchOptions o = opt;
    o.gpuScan = gpu;
    slime::SearchEstimate est;
    const bool estOk = withEstimate && slime::SlimeEngine::estimateSearch(o, o.threads, est);

    slime::SlimeEngine engine;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    const auto t0 = Clock::now();
    engine.run(o, cancel, nullptr, out);
    const double actual = std::chrono::duration<double>(Clock::now() - t0).count();
    if (!out.ok) {
        std::printf("%s\tFAIL\t%s\n", name, out.error.c_str());
        return;
    }
    const double ratio = estOk && est.totalSec > 0.0 ? actual / est.totalSec : 0.0;
    std::printf("%s\tok\t%.4f\t%.4f\t%.3f\t%lld\t%lld\t%lld\t%d/%d\t%d\n",
                name, estOk ? est.totalSec : -1.0, actual, ratio,
                static_cast<long long>(out.stats.scannedChunks),
                static_cast<long long>(out.stats.windowsEvaluated),
                static_cast<long long>(out.totalFound),
                out.gpuScanUsed ? 1 : 0, out.gpuMatchUsed ? 1 : 0, out.stoppedEarly ? 1 : 0);
}

slime::Pattern greenPattern(int n)
{
    slime::Pattern p(n, n);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) p.set(r, c, slime::CellState::Green);
    return p;
}

} // namespace

int main()
{
    if (!std::getenv("SLIME_BENCH_DEFAULT_THRESHOLD"))
        _putenv_s("SLIME_GPU_MIN_CHUNKS", "1");
    if (!std::getenv("SLIME_REUSE_BITMAP"))
        _putenv_s("SLIME_REUSE_BITMAP", "0");

    std::string gpuErr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (gpu && !gpu->available()) gpu.reset();
    std::printf("case\tstatus\test_s\tactual_s\tratio\tscanned\twindows\tfound\tgpu_scan/match\tstopped\n");
    std::printf("# gpu=%s err=%s\n", gpu ? gpu->deviceName().c_str() : "none", gpuErr.c_str());

    for (int side : { 4096, 16384 }) {
        const std::string tag = "s" + std::to_string(side);
        for (const char* backend : { "cpu", "gpu" }) {
            const slime::GpuScan* g = std::string(backend) == "gpu" ? gpu.get() : nullptr;
            if (std::string(backend) == "gpu" && !g) continue;
            {
                slime::SearchOptions o = makeOptions(side, slime::Mode::Density, slime::Algorithm::Exact);
                runOne((tag + "-density-exact-" + backend).c_str(), o, g, true);
            }
            {
                slime::SearchOptions o = makeOptions(side, slime::Mode::Strict, slime::Algorithm::Exact);
                o.pattern = greenPattern(2);
                runOne((tag + "-pattern-" + backend).c_str(), o, g, true);
            }
            {
                slime::SearchOptions o = makeOptions(side, slime::Mode::Density, slime::Algorithm::Exact);
                o.stopAtTarget = true;
                o.targetCount = 10;
                runOne((tag + "-density-stop10-" + backend).c_str(), o, g, true);
            }
            {
                slime::SearchOptions o = makeOptions(side, slime::Mode::Density, slime::Algorithm::Exact);
                o.smartRange = true;
                runOne((tag + "-density-smart-" + backend).c_str(), o, g, true);
            }
        }
        for (slime::Algorithm alg : { slime::Algorithm::Layered, slime::Algorithm::Sample,
                                      slime::Algorithm::Brute }) {
            slime::SearchOptions o = makeOptions(side, slime::Mode::Density, alg);
            if (alg == slime::Algorithm::Layered) { o.layerBlock = 16; o.layerRatio = 0.8; }
            if (alg == slime::Algorithm::Sample) { o.sampleStep = 8; o.sampleExact = false; }
            const char* algName = alg == slime::Algorithm::Layered ? "layered"
                                : alg == slime::Algorithm::Sample ? "sample" : "brute";
            runOne((tag + "-density-" + algName + "-cpu").c_str(), o, nullptr, true);
        }
    }

    // Thread scaling (CPU density exact, same range).
    for (int threads : { 1, 4, 0 }) {
        slime::SearchOptions o = makeOptions(4096, slime::Mode::Density, slime::Algorithm::Exact);
        o.threads = threads;
        runOne(("s4096-threads" + std::to_string(threads)).c_str(), o, nullptr, true);
    }
    return 0;
}

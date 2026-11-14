// Benchmark full search modes, including scan + matching.
//
// Modes reported:
//   pattern (2x2 all-green), density exact, density layered,
//   density sample (estimate), density sample-exact, density brute.
// CPU/GPU are compared where the engine supports GPU matching.
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

using Clock = std::chrono::steady_clock;

struct Row {
    std::string mode;
    std::string backend;
    bool ok = false;
    double bestMs = 1e100;
    double statsTotalMs = 0.0;
    double statsScanMs = 0.0;
    int64_t chunks = 0;
    int64_t found = 0;
    int64_t windows = 0;
    bool gpuScan = false;
    bool gpuMatch = false;
    std::string error;
};

slime::SearchOptions baseOptions(slime::Version version, slime::Mode mode, slime::Algorithm alg)
{
    slime::SearchOptions o;
    o.version = version;
    o.worldSeed = 12345;
    o.minX = -1024;
    o.maxX = 1023;
    o.minZ = -1024;
    o.maxZ = 1023;
    o.mode = mode;
    o.algorithm = alg;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.targetCount = 200000;
    o.stopAtTarget = false;
    o.smartRange = false;
    o.threads = 0;
    o.useCache = false;
    return o;
}

Row runOne(const std::string& mode, const std::string& backend, const slime::SearchOptions& opt,
           const slime::GpuScan* gpu)
{
    Row row;
    row.mode = mode;
    row.backend = backend;
    slime::SearchOptions o = opt;
    o.gpuScan = gpu;
    for (int rep = 0; rep < 2; ++rep) {
        std::atomic<bool> cancel{ false };
        slime::SlimeEngine eng;
        slime::SearchOutcome out;
        const auto t0 = Clock::now();
        eng.run(o, cancel, slime::ProgressFn(), out);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (!out.ok) {
            row.error = out.error;
            return row;
        }
        row.ok = true;
        row.chunks = out.stats.scannedChunks;
        row.found = out.totalFound;
        row.windows = out.stats.windowsEvaluated;
        row.gpuScan = out.gpuScanUsed;
        row.gpuMatch = out.gpuMatchUsed;
        if (rep == 0 || ms < row.bestMs) {
            row.bestMs = ms;
            row.statsTotalMs = out.stats.totalMs;
            row.statsScanMs = out.stats.scanMs;
        }
    }
    return row;
}

void print(const Row& r)
{
    if (!r.ok) {
        std::printf("%-18s %-5s FAIL %s\n", r.mode.c_str(), r.backend.c_str(), r.error.c_str());
        return;
    }
    constexpr int64_t rangeChunks = int64_t{ 2048 } * 2048;
    const double rate = r.bestMs > 0.0 ? static_cast<double>(rangeChunks) / (r.bestMs / 1000.0) : 0.0;
    const double statsTotal = r.statsTotalMs > 0.0 ? r.statsTotalMs : r.bestMs;
    const double statsScan = std::max(0.0, std::min(statsTotal, r.statsScanMs));
    const double matchMs = std::max(0.0, statsTotal - statsScan);
    const double matchPct = statsTotal > 0.0 ? matchMs * 100.0 / statsTotal : 0.0;
    std::printf("%-24s %-5s %9.2f ms  %9.2f 亿/s  scan=%5.2f ms  match=%5.2f ms (%4.1f%%)  found=%-7lld gpu=%d/%d\n",
                r.mode.c_str(), r.backend.c_str(), r.bestMs, rate / 1e8,
                statsScan, matchMs, matchPct,
                static_cast<long long>(r.found), r.gpuScan ? 1 : 0, r.gpuMatch ? 1 : 0);
}

} // namespace

int main()
{
    _putenv_s("SLIME_GPU_MIN_CHUNKS", "1");
    std::string gpuErr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (gpu && !gpu->available()) gpu.reset();
    std::printf("SlimeFinder v3.13.0 mode benchmark\n");
    std::printf("range=2048x2048 chunks=%d threads=auto\n", 2048 * 2048);
    if (gpu) std::printf("gpu=%s\n", gpu->deviceName().c_str());
    else std::printf("gpu=unavailable (%s)\n", gpuErr.c_str());
    std::printf("\n%-24s %-5s %12s  %14s  %s\n", "mode", "dev", "total", "throughput", "scan / match");

    slime::Pattern p2(2, 2);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c) p2.set(r, c, slime::CellState::Green);

    for (slime::Version version : { slime::Version::Java, slime::Version::Bedrock }) {
        const std::string v = version == slime::Version::Java ? "Java-" : "Bedrock-";
        {
            slime::SearchOptions o = baseOptions(version, slime::Mode::Strict,
                                                 slime::Algorithm::Exact);
            o.pattern = p2;
            o.directionLocked = true;
            print(runOne(v + "pattern", "CPU", o, nullptr));
            if (gpu) print(runOne(v + "pattern", "GPU", o, gpu.get()));
        }
        {
            slime::SearchOptions o = baseOptions(version, slime::Mode::Density,
                                                 slime::Algorithm::Exact);
            print(runOne(v + "density-exact", "CPU", o, nullptr));
            if (gpu) print(runOne(v + "density-exact", "GPU", o, gpu.get()));
        }
        {
            slime::SearchOptions o = baseOptions(version, slime::Mode::Density,
                                                 slime::Algorithm::Layered);
            o.layerBlock = 16;
            o.layerRatio = 0.8;
            print(runOne(v + "density-layered", "CPU", o, nullptr));
        }
        {
            slime::SearchOptions o = baseOptions(version, slime::Mode::Density,
                                                 slime::Algorithm::Sample);
            o.sampleStep = 8;
            o.sampleExact = false;
            print(runOne(v + "density-sample", "CPU", o, nullptr));
        }
        {
            slime::SearchOptions o = baseOptions(version, slime::Mode::Density,
                                                 slime::Algorithm::Sample);
            o.sampleStep = 8;
            o.sampleExact = true;
            print(runOne(v + "density-sample-exact", "CPU", o, nullptr));
        }
        {
            slime::SearchOptions o = baseOptions(version, slime::Mode::Density,
                                                 slime::Algorithm::Brute);
            print(runOne(v + "density-brute", "CPU", o, nullptr));
        }
    }
    return 0;
}

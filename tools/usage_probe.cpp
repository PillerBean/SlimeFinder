// tools/usage_probe.cpp — 真实使用场景压测（全图/取消/环形），不是单元测试。
// 用法:
//   usage_probe density_full
//   usage_probe density_early
//   usage_probe density_cancel <seconds>
//   usage_probe ring_full
//   usage_probe ring_full_gpu
//   usage_probe ring_exact_2k
#include "core/AfkPlanner.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

using Clock = std::chrono::steady_clock;

static double memMB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
#endif
    return 0.0;
}

static double peakMB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
#endif
    return 0.0;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: usage_probe density_full|density_early|density_cancel|ring_full|ring_full_gpu|ring_exact_2k [secs]\n");
        return 2;
    }
    const std::string mode = argv[1];
    const bool useGpu = (mode.find("gpu") != std::string::npos);
    std::unique_ptr<slime::GpuScan> gpu;
    if (useGpu) {
        std::string err;
        gpu = slimegpu::createGpuScanAdapter(&err);
        std::printf("GPU: %s\n", gpu && gpu->available() ? "available" : ("unavailable: " + err).c_str());
        if (!gpu || !gpu->available()) gpu.reset();
    }

    constexpr int kJavaBound = 1875000;

    if (mode.rfind("ring", 0) == 0) {
        slime::AfkPlanOptions o;
        if (mode == "ring_exact_2k") {
            o.area = slime::Rect{ -1000, 999, -1000, 999 };
            o.scanStride = 1;
        } else {
            o.area = slime::Rect{ -kJavaBound, kJavaBound, -kJavaBound, kJavaBound };
            o.scanStride = 0;
        }
        o.radius = 128;
        o.innerRadius = 24;
        o.minDensity = argc > 2 ? std::atof(argv[2]) : 0.0;
        o.topN = 5;
        o.minSeparation = 24;
        o.gpuScan = gpu.get();
        o.rowSlime = [](int cz, int cx0, int n, uint8_t* out) {
            slime::SlimeEngine::fillSlimeRowJava(4545, cz, cx0, n, out);
        };
        const auto pred = [](int cx, int cz) { return slime::SlimeEngine::isSlimeChunk(slime::Version::Java, 4545, cx, cz); };
        const auto t0 = Clock::now();
        const auto cands = slime::AfkPlanner::plan(o, pred);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        std::printf("ring %s: %.1f ms, candidates=%zu, mem=%.0f MB, peak=%.0f MB\n",
                    mode.c_str(), ms, cands.size(), memMB(), peakMB());
        if (!cands.empty())
            std::printf("  best=(%d,%d) coveredBlocks=%lld coverage=%.3f%%\n", cands.front().x, cands.front().z,
                        static_cast<long long>(cands.front().coveredBlocks), cands.front().coveragePercent);
        return 0;
    }

    slime::SearchOptions o;
    o.version = slime::Version::Java;
    o.worldSeed = 4545;
    o.minX = -kJavaBound;
    o.maxX = kJavaBound;
    o.minZ = -kJavaBound;
    o.maxZ = kJavaBound;
    o.mode = slime::Mode::Density;
    o.algorithm = slime::Algorithm::Exact;
    o.gridSize = 8;
    o.minDensity = 0.30;
    o.targetCount = 50;
    o.threads = 12;
    o.startX = 0;
    o.startZ = 0;
    o.gpuScan = gpu.get();
    if (mode.find("early") != std::string::npos) {
        o.stopAtTarget = true;
        o.smartRange = true;
    } else if (mode.find("stop") != std::string::npos) {
        o.stopAtTarget = true;
        o.smartRange = false;
    }

    std::atomic<bool> cancel{ false };
    std::atomic<double> cancelAtMs{ -1.0 };
    std::thread canceller;
    const auto runStart = Clock::now();
    if (mode.find("cancel") != std::string::npos) {
        const double secs = argc > 2 ? std::atof(argv[2]) : 5.0;
        canceller = std::thread([&, secs]() {
            std::this_thread::sleep_for(std::chrono::duration<double>(secs));
            cancelAtMs.store(std::chrono::duration<double, std::milli>(Clock::now() - runStart).count());
            cancel.store(true, std::memory_order_relaxed);
            std::printf("  [cancel] requested after %.1f s\n", secs);
        });
    }

    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    const auto t0 = Clock::now();
    eng.run(o, cancel, slime::ProgressFn(), out);
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (canceller.joinable()) canceller.join();
    std::printf("density %s: %.1f ms  cancelAt=%.1f cancelResp=%.1f ok=%d cancelled=%d results=%zu scanned=%lld totalFound=%lld gpuScan=%d gpuMatch=%d stoppedEarly=%d mem=%.0f peak=%.0f MB\n",
                mode.c_str(), ms, cancelAtMs.load(), ms - cancelAtMs.load(), (int)out.ok, (int)out.cancelled, out.results.size(),
                static_cast<long long>(out.stats.scannedChunks), static_cast<long long>(out.totalFound),
                (int)out.gpuScanUsed, (int)out.gpuMatchUsed, (int)out.stoppedEarly, memMB(), peakMB());
    if (!out.error.empty()) std::printf("  error: %s\n", out.error.c_str());
    if (!out.gpuFallbackReason.empty()) std::printf("  gpuFallback: %s\n", out.gpuFallbackReason.c_str());
    return 0;
}

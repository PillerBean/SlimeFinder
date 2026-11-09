// opt_probe44.cpp - GPU 大区域搜索的进度反馈探针
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const int W = argc > 1 ? std::atoi(argv[1]) : 70000;
    const int H = argc > 2 ? std::atoi(argv[2]) : 70000;
    std::string err;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&err);
    std::printf("GPU: %s available=%d err=%s\n", gpu ? gpu->deviceName().c_str() : "null",
                gpu ? (int)gpu->available() : 0, err.c_str());
    if (!gpu) return 1;

    std::atomic<int64_t> liveScanned{ 0 };
    std::atomic<int64_t> liveTotal{ 0 };
    std::atomic<int64_t> liveMatches{ 0 };

    slime::SearchOptions o;
    o.version = slime::Version::Java;
    o.worldSeed = 4545;
    o.minX = -W / 2;
    o.maxX = o.minX + W - 1;
    o.minZ = -H / 2;
    o.maxZ = o.minZ + H - 1;
    o.mode = slime::Mode::Density;
    o.gridSize = 8;
    o.minDensity = 0.6;
    o.targetCount = 5;
    o.threads = 8;
    o.smartRange = false;
    o.stopAtTarget = false;
    o.gpuScan = gpu.get();
    o.liveScanned = &liveScanned;
    o.liveTotal = &liveTotal;
    o.liveMatches = &liveMatches;

    int calls = 0;
    int64_t firstMs = -1, lastMs = -1;
    int64_t maxScanned = 0;
    double lastFrac = -1.0;
    const auto t0 = std::chrono::steady_clock::now();
    slime::ProgressFn prog = [&](double frac, bool scanning, double cps, double eta) {
        const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
        if (firstMs < 0) firstMs = ms;
        lastMs = ms;
        maxScanned = std::max(maxScanned, liveScanned.load());
        if (calls < 6 || calls % 20000 == 0) {
            std::printf("  #%d t=%lldms frac=%.4f scanning=%d cps=%.0f eta=%.0f scanned=%lld total=%lld\n",
                        calls, (long long)ms, frac, (int)scanning, cps, eta,
                        (long long)liveScanned.load(), (long long)liveTotal.load());
        }
        ++calls;
        lastFrac = frac;
    };

    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    eng.run(o, cancel, prog, out);

    std::printf("结果: ok=%d GPU扫描=%d GPU匹配=%d 回退=%s 结果数=%zu 用时=%.1f ms\n", (int)out.ok,
                (int)out.gpuScanUsed, (int)out.gpuMatchUsed, out.gpuFallbackReason.c_str(), out.results.size(),
                out.stats.totalMs);
    std::printf("进度事件: %d 次（首次 %lld ms，末次 %lld ms）最大已扫描=%lld 总扫描=%lld 最后frac=%.4f\n", calls,
                (long long)firstMs, (long long)lastMs, (long long)maxScanned,
                (long long)liveTotal.load(), lastFrac);
    return 0;
}

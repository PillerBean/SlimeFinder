#include "core/SlimeEngine.h"

#include <atomic>
#include <chrono>
#include <cstdio>

int main()
{
    slime::SearchOptions opt;
    opt.version = slime::Version::Java;
    opt.worldSeed = 12345;
    opt.minX = 0;
    opt.maxX = 16383;
    opt.minZ = 0;
    opt.maxZ = 16383;
    opt.mode = slime::Mode::Density;
    opt.algorithm = slime::Algorithm::Exact;
    opt.gridSize = 5;
    opt.minDensity = 0.3;
    opt.targetCount = 1;
    opt.stopAtTarget = false;
    opt.threads = 4;

    slime::SearchEstimate before;
    const bool okBefore = slime::SlimeEngine::estimateSearch(opt, 4, before);

    std::atomic<bool> cancel{ false };
    slime::SearchOutcome out;
    slime::SlimeEngine engine;
    const auto t0 = std::chrono::steady_clock::now();
    engine.run(opt, cancel, nullptr, out);
    const double actual = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!out.ok) { std::printf("run failed: %s\n", out.error.c_str()); return 1; }
    slime::SlimeEngine::recordActualTiming(opt, out.stats, out.gpuScanUsed);

    slime::SearchEstimate after;
    const bool okAfter = slime::SlimeEngine::estimateSearch(opt, 4, after);
    std::printf("sample_count_before=%d est_before=%.3f actual=%.3f est_after=%.3f calibrated=%d ok=%d/%d\n",
                before.sampleCount, before.totalSec, actual, after.totalSec, after.calibrated ? 1 : 0,
                okBefore ? 1 : 0, okAfter ? 1 : 0);

    opt.stopAtTarget = true;
    opt.targetCount = 10;
    slime::SearchEstimate stopBefore;
    const bool stopOk = slime::SlimeEngine::estimateSearch(opt, 4, stopBefore);
    const auto t1 = std::chrono::steady_clock::now();
    slime::SearchOutcome stopOut;
    engine.run(opt, cancel, nullptr, stopOut);
    const double stopActual = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    if (stopOut.ok) {
        slime::SlimeEngine::recordActualTiming(opt, stopOut.stats, stopOut.gpuScanUsed);
        slime::SearchEstimate stopAfter;
        const bool stopAfterOk = slime::SlimeEngine::estimateSearch(opt, 4, stopAfter);
        std::printf("stop sample_before=%d est_before=%.3f actual=%.3f est_after=%.3f calibrated=%d ok=%d/%d\n",
                    stopBefore.sampleCount, stopBefore.totalSec, stopActual, stopAfter.totalSec,
                    stopAfter.calibrated ? 1 : 0, stopOk ? 1 : 0, stopAfterOk ? 1 : 0);
    }
    return 0;
}


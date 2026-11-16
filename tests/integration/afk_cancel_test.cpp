#include "core/AfkPlanner.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    slime::AfkPlanOptions opt;
    opt.area = slime::Rect{ -20000, 19999, -20000, 19999 };
    opt.radius = 128;
    opt.topN = 8;
    opt.minSeparation = 24;
    opt.coarseStep = 4;
    opt.maxWindowBlocks = 2048;
    const auto pred = [](int x, int z) { return ((x * 31 + z * 17) & 7) == 0; };

    std::atomic<bool> cancel{ false };
    std::vector<slime::AfkCandidate> result;
    const auto t0 = std::chrono::steady_clock::now();
    std::thread worker([&]() { result = slime::AfkPlanner::plan(opt, pred, &cancel); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto cancelAt = std::chrono::steady_clock::now();
    cancel.store(true, std::memory_order_relaxed);
    worker.join();
    const double cancelMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cancelAt).count();
    const double totalMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("cancel_response=%.1fms total=%.1fms results=%zu\n", cancelMs, totalMs, result.size());
    if (!result.empty()) return 1;
    if (cancelMs > 1500.0) return 1;
    std::printf("AFK-CANCEL OK\n");
    return 0;
}

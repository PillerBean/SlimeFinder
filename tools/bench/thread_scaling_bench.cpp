// Repeated thread-scaling benchmark: separates short-run noise from real regressions.
#include "core/SlimeEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <vector>

using Clock = std::chrono::steady_clock;

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    for (int side : { 4096, 16384 }) {
        for (int threads : { 1, 4, 0 }) {
            std::vector<double> ms;
            slime::SearchOptions o;
            o.version = slime::Version::Java;
            o.worldSeed = 12345;
            o.minX = -(side / 2);
            o.maxX = side / 2 - 1;
            o.minZ = -(side / 2);
            o.maxZ = side / 2 - 1;
            o.mode = slime::Mode::Density;
            o.algorithm = slime::Algorithm::Exact;
            o.gridSize = 5;
            o.minDensity = 0.3;
            o.targetCount = 200000;
            o.threads = threads;
            for (int rep = 0; rep < 10; ++rep) {
                slime::SlimeEngine eng;
                slime::SearchOutcome out;
                std::atomic<bool> cancel{ false };
                const auto t0 = Clock::now();
                eng.run(o, cancel, nullptr, out);
                const double v = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                if (out.ok) ms.push_back(v);
            }
            if (ms.empty()) {
                std::printf("side=%d threads=%d FAIL\n", side, threads);
                continue;
            }
            std::sort(ms.begin(), ms.end());
            const double median = (ms[ms.size() / 2] + ms[(ms.size() - 1) / 2]) * 0.5;
            std::printf("side=%d threads=%d n=%zu min=%.2f median=%.2f p90=%.2f max=%.2f ms\n",
                        side, threads, ms.size(), ms.front(), median,
                        ms[static_cast<size_t>(ms.size() * 0.9)], ms.back());
        }
    }
    return 0;
}

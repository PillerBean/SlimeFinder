// opt_probe43.cpp - 最优候选模式（忽略阈值）性能与正确性冒烟
#include "core/SlimeEngine.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

struct Res {
    bool ok = false;
    double ms = 0;
    size_t n = 0;
    int64_t found = 0;
    std::vector<float> dens;
};

static Res run(const slime::SearchOptions& o)
{
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    slime::ProgressFn noop;
    const auto t0 = std::chrono::steady_clock::now();
    eng.run(o, cancel, noop, out);
    Res r;
    r.ok = out.ok;
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    r.n = out.results.size();
    r.found = out.totalFound;
    for (const auto& m : out.results) r.dens.push_back(m.density);
    return r;
}

int main(int argc, char** argv)
{
    const int w = argc > 1 ? std::atoi(argv[1]) : 4096;
    slime::SearchOptions base;
    base.version = slime::Version::Java;
    base.worldSeed = 12345;
    base.minX = -w / 2;
    base.maxX = base.minX + w - 1;
    base.minZ = -w / 2;
    base.maxZ = base.minZ + w - 1;
    base.mode = slime::Mode::Density;
    base.gridSize = 5;
    base.minDensity = 0.3;
    base.targetCount = 20;
    base.threads = 12;
    base.startX = 0;
    base.startZ = 0;

    const Res normal = run(base);
    slime::SearchOptions bestOpt = base;
    bestOpt.ignoreThreshold = true;
    const Res best = run(bestOpt);
    bool mono = true;
    for (size_t i = 1; i < best.dens.size(); ++i)
        if (best.dens[i] > best.dens[i - 1] + 1e-6f) mono = false;
    std::printf("%d² g=5 N=20：阈值模式 %8.1f ms（结果 %zu）| 最优候选 %8.1f ms（结果 %zu，评估 %lld）| 密度单调=%s\n",
                w, normal.ms, normal.n, best.ms, best.n, static_cast<long long>(best.found), mono ? "是" : "否");
    return 0;
}

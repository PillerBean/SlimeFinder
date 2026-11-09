// opt_probe40.cpp - GPU 行带匹配（智能范围/提前停止）效果 + 与 CPU 一致性
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {
struct Res {
    std::vector<std::pair<int, int>> pts;
    double ms = 0;
};

Res run(slime::SearchOptions o, slime::GpuScan* gpu)
{
    o.gpuScan = gpu;
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    slime::ProgressFn noop;
    const auto t0 = std::chrono::steady_clock::now();
    eng.run(o, cancel, noop, out);
    Res r;
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    for (const auto& m : out.results) r.pts.emplace_back(m.minX, m.minZ);
    std::sort(r.pts.begin(), r.pts.end());   // 并列距离的顺序可能不同，按坐标排序后比较
    return r;
}

slime::SearchOptions mk(int w, int h, int g, double thr, int N, bool stop, bool smart, bool bedrock)
{
    slime::SearchOptions o;
    o.version = bedrock ? slime::Version::Bedrock : slime::Version::Java;
    o.worldSeed = bedrock ? 0 : 12345;
    o.minX = -w / 2;
    o.maxX = o.minX + w - 1;
    o.minZ = -h / 2;
    o.maxZ = o.minZ + h - 1;
    o.mode = slime::Mode::Density;
    o.gridSize = g;
    o.minDensity = thr;
    o.targetCount = N;
    o.stopAtTarget = stop;
    o.smartRange = smart;
    o.threads = 12;
    o.startX = 0;
    o.startZ = 0;
    return o;
}
} // namespace

int main()
{
    std::string gerr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gerr);
    if (!gpu) {
        std::printf("GPU 不可用：%s\n", gerr.c_str());
        return 0;
    }
    std::mt19937 rng(31415u);
    int bad = 0;
    for (int iter = 0; iter < 20; ++iter) {
        const bool bedrock = (iter % 5) == 4;
        const int w = 4200 + static_cast<int>(rng() % 8000);
        const int h = 4200 + static_cast<int>(rng() % 8000);
        const int g = 2 + static_cast<int>(rng() % 6);
        const int N = 1 + static_cast<int>(rng() % 10);
        const double thr = 0.2 + 0.1 * static_cast<double>(rng() % 6);
        std::printf("=== %s %dx%d g=%d thr=%.1f N=%d ===\n", bedrock ? "bedrock" : "java", w, h, g, thr, N);
        const auto base = mk(w, h, g, thr, N, false, false, bedrock);
        const Res cpuFull = run(base, nullptr);
        const Res gpuFull = run(base, gpu.get());
        if (cpuFull.pts != gpuFull.pts) { ++bad; std::printf("  [DIFF full]\n"); }
        auto smartOpt = mk(w, h, g, thr, N, false, true, bedrock);
        const Res cpuSmart = run(smartOpt, nullptr);
        const Res gpuSmart = run(smartOpt, gpu.get());
        if (cpuSmart.pts != gpuSmart.pts) { ++bad; std::printf("  [DIFF smart] cpu %zu / gpu %zu\n", cpuSmart.pts.size(), gpuSmart.pts.size()); }
        auto stopOpt = mk(w, h, g, thr, N, true, false, bedrock);
        const Res cpuStop = run(stopOpt, nullptr);
        const Res gpuStop = run(stopOpt, gpu.get());
        if (cpuStop.pts != gpuStop.pts) { ++bad; std::printf("  [DIFF stop] cpu %zu / gpu %zu\n", cpuStop.pts.size(), gpuStop.pts.size()); }
        auto bothOpt = mk(w, h, g, thr, N, true, true, bedrock);
        const Res cpuBoth = run(bothOpt, nullptr);
        const Res gpuBoth = run(bothOpt, gpu.get());
        if (cpuBoth.pts != gpuBoth.pts) { ++bad; std::printf("  [DIFF both] cpu %zu / gpu %zu\n", cpuBoth.pts.size(), gpuBoth.pts.size()); }
        std::printf("  全量 C%7.1f/G%7.1f | 智能 C%7.1f/G%7.1f | 早停 C%7.1f/G%7.1f | 两者 C%7.1f/G%7.1f ms\n",
                    cpuFull.ms, gpuFull.ms, cpuSmart.ms, gpuSmart.ms, cpuStop.ms, gpuStop.ms, cpuBoth.ms,
                    gpuBoth.ms);
        std::fflush(stdout);
    }
    std::printf("\n不一致 %d 项 -> %s\n", bad, bad == 0 ? "全部一致" : "存在差异");
    return bad == 0 ? 0 : 1;
}

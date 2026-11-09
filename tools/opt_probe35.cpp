// opt_probe35.cpp - GPU 路径 vs CPU 路径一致性 fuzz
//   密度：引擎 GPU 流式（强制门槛 0）；严格：SLIME_GPU_PATTERN=1 图案流式。
//   两组结果必须逐项一致（同参数、同区域）。
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
    bool gpuUsed = false;
};

Res run(slime::SearchOptions o, bool useGpu, slime::GpuScan* gpu)
{
    o.gpuScan = useGpu ? gpu : nullptr;
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
} // namespace

int main()
{
    _putenv_s("SLIME_GPU_MIN_CHUNKS", "0");   // 强制小区域也走 GPU 流式（便于覆盖）
    std::string gerr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gerr);
    if (!gpu) {
        std::printf("GPU 不可用：%s\n", gerr.c_str());
        return 0;
    }
    std::mt19937 rng(777u);
    int bad = 0, cases = 0;
    for (int iter = 0; iter < 30; ++iter) {
        const bool strict = (iter % 3) == 2;
        const slime::Version ver = (iter % 4 == 0) ? slime::Version::Bedrock : slime::Version::Java;
        const int w = 2048 + static_cast<int>(rng() % 3072);
        const int h = 2048 + static_cast<int>(rng() % 3072);
        const int g = strict ? 2 : (2 + static_cast<int>(rng() % 6));
        const double thr = 0.2 + 0.1 * static_cast<double>(rng() % 6);
        const int N = 3 + static_cast<int>(rng() % 15);
        slime::SearchOptions o;
        o.version = ver;
        o.worldSeed = (ver == slime::Version::Java) ? 12345 : 0;
        o.minX = -w / 2;
        o.maxX = o.minX + w - 1;
        o.minZ = -h / 2;
        o.maxZ = o.minZ + h - 1;
        o.mode = strict ? slime::Mode::Strict : slime::Mode::Density;
        if (strict) {
            o.pattern = decltype(o.pattern)(g, g);
            for (int r = 0; r < g; ++r)
                for (int c = 0; c < g; ++c) o.pattern.set(r, c, slime::CellState::Green);
        } else {
            o.gridSize = g;
            o.minDensity = thr;
        }
        o.targetCount = N;
        o.threads = 12;
        o.startX = 0;
        o.startZ = 0;
        const Res cpu = run(o, false, gpu.get());
        const Res gc = run(o, true, gpu.get());
        ++cases;
        const bool ok = (cpu.pts == gc.pts);
        if (!ok) {
            ++bad;
            std::printf("[DIFF] %s %s %dx%d g=%d thr=%.1f N=%d: cpu %zu / gpu %zu\n",
                        ver == slime::Version::Java ? "java" : "bedrock", strict ? "strict" : "density", w, h, g,
                        thr, N, cpu.pts.size(), gc.pts.size());
        } else {
            std::printf("case %2d %-7s %-7s %4dx%-4d g=%d thr=%.1f N=%-2d | cpu %7.2f / gpu %7.2f ms | 一致\n", iter,
                        ver == slime::Version::Java ? "java" : "bedrock", strict ? "strict" : "density", w, h, g,
                        thr, N, cpu.ms, gc.ms);
        }
        std::fflush(stdout);
    }
    std::printf("\n共 %d 例，不一致 %d 例 -> %s\n", cases, bad, bad == 0 ? "全部一致" : "存在差异");
    return bad == 0 ? 0 : 1;
}

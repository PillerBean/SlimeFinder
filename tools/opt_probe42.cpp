// opt_probe42.cpp - 单例诊断：指定区域/参数跑一次（GPU 可选），用于 SLIME_DEBUG 分段定位
//   用法: opt_probe42 w h g thr N gpu(0/1) [stop] [smart]
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 7) {
        std::printf("用法: opt_probe42 w h g thr N gpu\n");
        return 1;
    }
    const int w = std::atoi(argv[1]), h = std::atoi(argv[2]), g = std::atoi(argv[3]);
    const double thr = std::atof(argv[4]);
    const int N = std::atoi(argv[5]);
    const bool useGpu = std::atoi(argv[6]) != 0;
    const bool stop = argc > 7 && std::atoi(argv[7]) != 0;
    const bool smart = argc > 8 && std::atoi(argv[8]) != 0;

    std::unique_ptr<slime::GpuScan> gpu;
    if (useGpu) {
        std::string err;
        gpu = slimegpu::createGpuScanAdapter(&err);
        if (!gpu) {
            std::printf("GPU 不可用：%s\n", err.c_str());
            return 0;
        }
    }
    slime::SearchOptions o;
    o.version = slime::Version::Java;
    o.worldSeed = 12345;
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
    o.gpuScan = gpu.get();
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    slime::ProgressFn noop;
    const auto t0 = std::chrono::steady_clock::now();
    eng.run(o, cancel, noop, out);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%dx%d g=%d thr=%.2f N=%d gpu=%d stop=%d smart=%d：总 %.2f ms，扫描 %.2f / 匹配 %.2f，命中 %lld，显示 %zu，chunked=%d\n",
                w, h, g, thr, N, useGpu ? 1 : 0, stop ? 1 : 0, smart ? 1 : 0, ms, out.stats.scanMs,
                out.stats.matchMs, static_cast<long long>(out.totalFound), out.results.size(),
                out.chunkedPath ? 1 : 0);
    return 0;
}

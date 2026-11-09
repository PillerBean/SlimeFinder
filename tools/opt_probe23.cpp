// opt_probe23.cpp - 流程分析：引擎各阶段的真实并行度与耗时占比
//   目的：找出「综合效率」被哪一段拖住（扫描 / 匹配 / 结果收集 / 固定开销）。
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

struct Run {
    double total = 0, scan = 0, match = 0;
    size_t results = 0;
    int64_t found = 0;
};

Run runOnce(slime::Version v, int64_t seed, int w, int h, slime::Mode mode, int g, double thr, int threads,
            int target, slime::GpuScan* gpu = nullptr)
{
    slime::SearchOptions o;
    o.version = v;
    o.worldSeed = seed;
    o.minX = -w / 2;
    o.maxX = o.minX + w - 1;
    o.minZ = -h / 2;
    o.maxZ = o.minZ + h - 1;
    o.mode = mode;
    o.gridSize = g;
    o.minDensity = thr;
    o.threads = threads;
    o.targetCount = target;
    o.gpuScan = gpu;
    std::atomic<bool> cancel{ false };
    slime::ProgressFn noop;
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    const auto t0 = Clock::now();
    eng.run(o, cancel, noop, out);
    Run r;
    r.total = msSince(t0);
    r.scan = out.stats.scanMs;
    r.match = out.stats.matchMs;
    r.results = out.results.size();
    r.found = out.totalFound;
    return r;
}

} // namespace

int main()
{
    std::printf("逻辑核心: %u\n\n", std::thread::hardware_concurrency());
    std::printf("%-34s %8s %8s %8s %8s %10s\n", "场景", "总计", "扫描", "匹配", "其他", "命中数");

    const struct {
        const char* name;
        slime::Version v;
        int w, h;
        slime::Mode mode;
        int g;
        double thr;
    } cases[] = {
        { "Java 8192² 密度 g=2 thr=0.5", slime::Version::Java, 8192, 8192, slime::Mode::Density, 2, 0.5 },
        { "Java 8192² 密度 g=5 thr=0.3", slime::Version::Java, 8192, 8192, slime::Mode::Density, 5, 0.3 },
        { "Java 8192² 密度 g=5 thr=1.0", slime::Version::Java, 8192, 8192, slime::Mode::Density, 5, 1.0 },
        { "Java 4096² 密度 g=2 thr=0.5", slime::Version::Java, 4096, 4096, slime::Mode::Density, 2, 0.5 },
        { "基岩 4096² 密度 g=2 thr=0.5", slime::Version::Bedrock, 4096, 4096, slime::Mode::Density, 2, 0.5 },
    };

    for (const auto& c : cases) {
        for (int th : { 1, 4, 12 }) {
            const Run r = runOnce(c.v, 12345, c.w, c.h, c.mode, c.g, c.thr, th, 50);
            std::printf("%-34s %8.2f %8.2f %8.2f %8.2f %10lld  [线程=%d]\n", c.name, r.total, r.scan, r.match,
                        r.total - r.scan - r.match, static_cast<long long>(r.found), th);
        }
        std::printf("\n");
    }

    // ---- v3.8.1：全 GPU 流式匹配 vs CPU（稀疏命中场景）----
    {
        std::string err;
        std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&err);
        if (!gpu) {
            std::printf("GPU 适配器不可用：%s\n", err.c_str());
            return 0;
        }
        struct Case {
            const char* name;
            slime::Version v;
            int w, h, g;
            double thr;
        } cases[] = {
            { "Java 16384² 密度 g=5 thr=0.5", slime::Version::Java, 16384, 16384, 5, 0.5 },
            { "Java 32768² 密度 g=5 thr=0.5", slime::Version::Java, 32768, 32768, 5, 0.5 },
            { "基岩 16384² 密度 g=5 thr=0.5", slime::Version::Bedrock, 16384, 16384, 5, 0.5 },
        };
        std::printf("\n%-34s %10s %10s %10s %8s %10s\n", "场景（稀疏命中）", "CPU 总", "GPU 总", "命中", "加速", "GPU 命中");
        for (const auto& c : cases) {
            const Run rc = runOnce(c.v, 12345, c.w, c.h, slime::Mode::Density, c.g, c.thr, 12, 50, nullptr);
            const Run rg = runOnce(c.v, 12345, c.w, c.h, slime::Mode::Density, c.g, c.thr, 12, 50, gpu.get());
            std::printf("%-34s %10.2f %10.2f %10lld %7.2fx %10lld\n", c.name, rc.total, rg.total,
                        static_cast<long long>(rc.found), rc.total / rg.total,
                        static_cast<long long>(rg.found));
        }
    }
    return 0;
}
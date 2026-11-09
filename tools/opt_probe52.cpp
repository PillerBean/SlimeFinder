// opt_probe52.cpp - 环形搜索（AFK 覆盖）大范围吞吐基准（CPU/GPU）
#include "core/AfkPlanner.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

using Clock = std::chrono::steady_clock;

int main()
{
    std::string err;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&err);
    if (!gpu || !gpu->available()) {
        std::printf("GPU 不可用：%s（只跑 CPU）\n", err.c_str());
        gpu.reset();
    }
    struct Case { const char* name; int chunks; int stride; };
    const Case cases[] = {
        { "2k² 精确", 2000, 1 },
        { "2k² 抽样", 2000, 0 },
        { "10k² 抽样", 10000, 0 },
        { "30k² 抽样", 30000, 0 },
        { "Java 全图 抽样", 3750001, 0 },
    };
    for (const Case& c : cases) {
        for (int useGpu = 0; useGpu <= (gpu ? 1 : 0); ++useGpu) {
            slime::AfkPlanOptions o;
            const int half = c.chunks / 2;
            o.area.minX = -half;
            o.area.maxX = half - 1;
            o.area.minZ = -half;
            o.area.maxZ = half - 1;
            o.radius = 128;
            o.innerRadius = 24;
            o.minDensity = 0.0;
            o.topN = 5;
            o.minSeparation = 24;
            o.coarseStep = 4;
            o.scanStride = c.stride;
            o.statBands = 4;
            o.gpuScan = useGpu ? gpu.get() : nullptr;
            o.rowSlime = [](int cz, int cx0, int n, uint8_t* out) {
                slime::SlimeEngine::fillSlimeRowJava(4545, cz, cx0, n, out);
            };
            const auto pred = [](int cx, int cz) {
                return slime::SlimeEngine::isSlimeChunk(slime::Version::Java, 4545, cx, cz);
            };
            const auto t0 = Clock::now();
            const auto res = slime::AfkPlanner::plan(o, pred);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            const double chunks = static_cast<double>(c.chunks) * c.chunks;
            const double rate = chunks / (ms / 1000.0);
            std::printf("%-12s %-4s %9.1f ms | 结果 %zu | %7.2f 亿区块/秒 | 全图约 %8.1f 分钟\n", c.name,
                        useGpu ? "GPU" : "CPU", ms, res.size(), rate / 1e8,
                        1.40624925e13 / rate / 60.0);
        }
    }
    return 0;
}

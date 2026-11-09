// opt_probe22.cpp - 产品级对比：AfkPlanner::plan 走 CPU 覆盖 vs GPU 覆盖
//   直接链接 slime_core + slime_gpu，用与 GUI 相同的入口（AfkPlanOptions::gpuScan），
//   不修改产品代码。
#include "core/AfkPlanner.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

slime::AfkPlanOptions makeOptions(slime::Rect area, const slime::GpuScan* gpu, int radius, int inner)
{
    slime::AfkPlanOptions o;
    o.area = area;
    o.radius = radius;
    o.innerRadius = inner;
    o.topN = 8;
    o.minSeparation = 24;
    o.coarseStep = 4;
    o.scanStride = 0;
    o.statBands = 4;
    o.gpuScan = gpu;
    return o;
}

int runCase(slime::GpuScan* gpu, const char* name, slime::Rect area, int radius, int inner, int64_t seed)
{
    // 谓词：Java 版判定（与引擎一致，直接调用引擎的静态入口）
    const auto pred = [seed](int cx, int cz) {
        return slime::SlimeEngine::isSlimeChunk(slime::Version::Java, seed, cx, cz);
    };

    slime::AfkPlanOptions cpuOpt = makeOptions(area, nullptr, radius, inner);
    const auto t0 = Clock::now();
    auto cpu = slime::AfkPlanner::plan(cpuOpt, pred);
    const double cpuMs = msSince(t0);

    double gpuMs = -1.0;
    std::vector<slime::AfkCandidate> gpuRes;
    if (gpu != nullptr) {
        slime::AfkPlanOptions gpuOpt = makeOptions(area, gpu, radius, inner);
        const auto t1 = Clock::now();
        gpuRes = slime::AfkPlanner::plan(gpuOpt, pred);
        gpuMs = msSince(t1);
    }

    bool same = (gpuMs > 0.0) && cpu.size() == gpuRes.size();
    if (same) {
        for (size_t i = 0; i < cpu.size(); ++i) {
            if (cpu[i].x != gpuRes[i].x || cpu[i].z != gpuRes[i].z ||
                cpu[i].coveredBlocks != gpuRes[i].coveredBlocks) {
                same = false;
                break;
            }
        }
    }
    std::printf("%-22s CPU %7.2f ms | GPU %7.2f ms | 加速 %5.1fx | 候选 %zu/%zu | 结果一致=%s\n", name, cpuMs,
                gpuMs, gpuMs > 0 ? cpuMs / gpuMs : 0.0, cpu.size(), gpuRes.size(), same ? "是" : "(无GPU/不一致)");
    return same ? 0 : 1;
}

} // namespace

int main()
{
    std::string err;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&err);
    std::printf("GPU 适配器：%s\n", gpu ? "已创建" : ("不可用（" + err + "）").c_str());

    int bad = 0;
    bad += runCase(gpu.get(), "40x40 区块（精确路径）", slime::Rect{ -20, 20, -20, 20 }, 128, 24, 12345);
    bad += runCase(gpu.get(), "60x60 区块（精确路径）", slime::Rect{ -30, 30, -30, 30 }, 128, 24, 12345);
    bad += runCase(gpu.get(), "400x400 R128/r24", slime::Rect{ -200, 200, -200, 200 }, 128, 24, 12345);
    bad += runCase(gpu.get(), "1000x1000 R128/r24", slime::Rect{ -500, 500, -500, 500 }, 128, 24, 12345);
    bad += runCase(gpu.get(), "2000x2000 R128/r24", slime::Rect{ -1000, 1000, -1000, 1000 }, 128, 24, 12345);
    bad += runCase(gpu.get(), "2000x2000 R64/r0", slime::Rect{ -1000, 1000, -1000, 1000 }, 64, 0, 777);
    std::printf("\n不一致用例数：%d\n", bad);
    return 0;
}

// opt_probe51.cpp - 结果自优化（GPU 提前停止）正确性 + 速度对拍
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

static slime::SearchOutcome run(const slime::SearchOptions& o, slime::GpuScan* gpu)
{
    slime::SearchOptions opt = o;
    opt.gpuScan = gpu;
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    eng.run(opt, cancel, slime::ProgressFn(), out);
    return out;
}

static bool same(const slime::SearchOutcome& a, const slime::SearchOutcome& b, std::string* why)
{
    if (a.results.size() != b.results.size()) {
        *why = "size " + std::to_string(a.results.size()) + " vs " + std::to_string(b.results.size());
        return false;
    }
    for (size_t i = 0; i < a.results.size(); ++i) {
        const auto& x = a.results[i];
        const auto& y = b.results[i];
        if (x.minX != y.minX || x.minZ != y.minZ || x.maxX != y.maxX || x.maxZ != y.maxZ ||
            std::fabs(x.density - y.density) > 1e-6f) {
            *why = "item " + std::to_string(i) + " (" + std::to_string(x.minX) + "," + std::to_string(x.minZ) +
                   ") vs (" + std::to_string(y.minX) + "," + std::to_string(y.minZ) + ")";
            return false;
        }
    }
    return true;
}

int main()
{
    std::string err;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&err);
    if (!gpu || !gpu->available()) {
        std::printf("GPU 不可用：%s\n", err.c_str());
        return 1;
    }
    std::printf("GPU: %s\n", gpu->deviceName().c_str());
    // 预热：第一次 GPU 调用包含 OpenCL 会话/内核初始化，不计入测速
    {
        slime::SearchOptions wopt;
        wopt.version = slime::Version::Java;
        wopt.worldSeed = 4545;
        wopt.minX = -512;
        wopt.maxX = 511;
        wopt.minZ = -512;
        wopt.maxZ = 511;
        wopt.mode = slime::Mode::Density;
        wopt.gridSize = 5;
        wopt.minDensity = 0.3;
        wopt.targetCount = 50;
        wopt.threads = 8;
        wopt.gpuResultSelfOptimize = true;
        (void)run(wopt, gpu.get());
    }
    struct Case { const char* name; int w, h, g; double thr; int64_t target; int startX, startZ; };
    const Case cases[] = {
        { "8k g5 .3 N50 中心", 8192, 8192, 5, 0.30, 50, 0, 0 },
        { "8k g5 .3 N50 角落", 8192, 8192, 5, 0.30, 50, 5000, -5000 },
        { "8k g5 .5 N100", 8192, 8192, 5, 0.50, 100, 0, 0 },
        { "4k g8 .3 N20", 4096, 4096, 8, 0.30, 20, 0, 0 },
        { "4k g8 .3 N500", 4096, 4096, 8, 0.30, 500, 0, 0 },
        { "2k g5 .3 N1 外部起点", 2000, 2000, 5, 0.30, 1, 9000, 9000 },
        { "6k g3 .4 N50", 6144, 6144, 3, 0.40, 50, 0, 0 },
    };
    for (const Case& c : cases) {
        slime::SearchOptions o;
        o.version = slime::Version::Java;
        o.worldSeed = 4545;
        o.minX = -c.w / 2;
        o.maxX = o.minX + c.w - 1;
        o.minZ = -c.h / 2;
        o.maxZ = o.minZ + c.h - 1;
        o.mode = slime::Mode::Density;
        o.gridSize = c.g;
        o.minDensity = c.thr;
        o.targetCount = c.target;
        o.startX = c.startX;
        o.startZ = c.startZ;
        o.threads = 8;
        o.gpuResultSelfOptimize = false;
        const auto t0 = std::chrono::steady_clock::now();
        const slime::SearchOutcome cpu = run(o, nullptr);
        const auto t1 = std::chrono::steady_clock::now();
        slime::SearchOptions og = o;
        og.gpuResultSelfOptimize = true;
        const auto t2 = std::chrono::steady_clock::now();
        const slime::SearchOutcome gpuRes = run(og, gpu.get());
        const auto t3 = std::chrono::steady_clock::now();
        const double cpuMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        slime::SearchOptions ogOld = o;
        ogOld.gpuResultSelfOptimize = false;
        const auto t4 = std::chrono::steady_clock::now();
        const slime::SearchOutcome gpuOld = run(ogOld, gpu.get());
        const auto t5 = std::chrono::steady_clock::now();
        const double gpuOldMs = std::chrono::duration<double, std::milli>(t5 - t4).count();
        const double gpuMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
        std::string why;
        const bool ok = same(cpu, gpuRes, &why);
        std::string whyOld;
        const bool okOld = same(cpu, gpuOld, &whyOld);
        std::printf("%-22s CPU %7.1f | GPU旧 %7.2f | GPU自优化 %7.2f ms | 自优化结果 %s | 旧结果 %s | retained=%lld\n",
                    c.name, cpuMs, gpuOldMs, gpuMs, ok ? "一致" : "不一致", okOld ? "一致" : "不一致",
                    static_cast<long long>(gpuRes.totalFound));
    }
    return 0;
}

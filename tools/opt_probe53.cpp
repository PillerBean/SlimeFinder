// opt_probe53.cpp - 图案 GPU 流式 vs CPU 正确性对拍（含多图案/旋转）
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

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
        if (x.minX != y.minX || x.minZ != y.minZ || x.maxX != y.maxX || x.maxZ != y.maxZ) {
            *why = "item " + std::to_string(i) + " (" + std::to_string(x.minX) + "," + std::to_string(x.minZ) +
                   ") vs (" + std::to_string(y.minX) + "," + std::to_string(y.minZ) + ")";
            return false;
        }
    }
    return true;
}

static void fillPattern(slime::Pattern& p, const int* cells, int r, int c)
{
    for (int i = 0; i < r * c; ++i) {
        slime::CellState st = slime::CellState::Ignore;
        if (cells[i] == 1) st = slime::CellState::Green;
        else if (cells[i] == 2) st = slime::CellState::White;
        p.set(i / c, i % c, st);
    }
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
#if defined(_WIN32)
    _putenv_s("SLIME_GPU_PATTERN", "1");
#else
    setenv("SLIME_GPU_PATTERN", "1", 1);
#endif
    struct Case {
        const char* name;
        int w, h;
        int r, c;
        int cells[9];
        bool lock;
        int64_t target;
    };
    const Case cases[] = {
        { "8k 1x1 全绿", 8192, 8192, 1, 1, { 1, 0, 0, 0, 0, 0, 0, 0, 0 }, true, 50 },
        { "8k 1x1 白色", 8192, 8192, 1, 1, { 2, 0, 0, 0, 0, 0, 0, 0, 0 }, true, 50 },
        { "8k 2x2 全绿", 8192, 8192, 2, 2, { 1, 1, 1, 1, 0, 0, 0, 0, 0 }, true, 50 },
        { "20k 2x2 全绿", 20000, 20000, 2, 2, { 1, 1, 1, 1, 0, 0, 0, 0, 0 }, true, 50 },
        { "20k 3x3 边框绿+中心白", 20000, 20000, 3, 3, { 1, 1, 1, 1, 2, 1, 1, 1, 1 }, true, 30 },
        { "20k 3x3 单格绿+忽略", 20000, 20000, 3, 3, { 0, 0, 0, 0, 1, 0, 0, 0, 0 }, true, 50 },
        { "8k 2x2 允许旋转", 8192, 8192, 2, 2, { 1, 2, 2, 1, 0, 0, 0, 0, 0 }, false, 50 },
        { "8k 2x2 全绿 外部起点", 8192, 8192, 2, 2, { 1, 1, 1, 1, 0, 0, 0, 0, 0 }, true, 50 },
    };
    for (const Case& cs : cases) {
        slime::SearchOptions o;
        o.version = slime::Version::Java;
        o.worldSeed = 4545;
        o.minX = -cs.w / 2;
        o.maxX = o.minX + cs.w - 1;
        o.minZ = -cs.h / 2;
        o.maxZ = o.minZ + cs.h - 1;
        o.mode = slime::Mode::Strict;
        o.directionLocked = cs.lock;
        o.targetCount = cs.target;
        o.startX = 0;
        o.startZ = 0;
        if (!cs.lock) { /* 旋转模式起点用 0 即可 */ }
        slime::Pattern p(cs.r, cs.c);
        fillPattern(p, cs.cells, cs.r, cs.c);
        o.pattern = p;

        slime::SearchOptions cpuOpt = o;
        cpuOpt.gpuScan = nullptr;
        const auto t0 = Clock::now();
        const auto cpuRes = run(cpuOpt, nullptr);
        const auto t1 = Clock::now();
        const auto gpuRes = run(o, gpu.get());
        const auto t2 = Clock::now();
        std::string why;
        const bool ok = same(cpuRes, gpuRes, &why);
        std::printf("%-26s CPU %8.2f ms | GPU %7.2f ms | %s | gpuUsed=%d\n", cs.name,
                    std::chrono::duration<double, std::milli>(t1 - t0).count(),
                    std::chrono::duration<double, std::milli>(t2 - t1).count(), ok ? "一致" : "不一致",
                    gpuRes.gpuMatchUsed ? 1 : 0);
        if (!ok) std::printf("    差异：%s\n", why.c_str());
    }
    return 0;
}

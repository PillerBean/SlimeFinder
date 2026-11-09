// opt_probe54.cpp - 「环形模式匹配」基准：按环带生成图案，CPU vs GPU 图案流式（与社区环形口径对照用）
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using Clock = std::chrono::steady_clock;

int main()
{
    std::string err;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&err);
    if (!gpu || !gpu->available()) {
        std::printf("GPU 不可用：%s（跑 CPU）\n", err.c_str());
        gpu.reset();
    }
#if defined(_WIN32)
    _putenv_s("SLIME_GPU_PATTERN", "1");
#else
    setenv("SLIME_GPU_PATTERN", "1", 1);
#endif
    struct Case { const char* name; int size; int rin; int rout; };
    const Case cases[] = {
        { "20k² 环 r=4 厚1", 20000, 4, 4 },
        { "20k² 环 r=5 厚1", 20000, 5, 5 },
        { "100k² 环 r=4 厚1", 100000, 4, 4 },
    };
    for (const Case& c : cases) {
        slime::Pattern p(2 * c.rout + 1, 2 * c.rout + 1);
        int active = 0;
        for (int r = 0; r < p.rows(); ++r) {
            for (int col = 0; col < p.cols(); ++col) {
                const int dx = col - c.rout;
                const int dz = r - c.rout;
                const double d = std::sqrt(static_cast<double>(dx * dx + dz * dz));
                if (d >= c.rin && d <= c.rout) {
                    p.set(r, col, slime::CellState::Green);
                    ++active;
                }
            }
        }
        slime::SearchOptions o;
        o.version = slime::Version::Java;
        o.worldSeed = 4545;
        o.minX = -c.size / 2;
        o.maxX = o.minX + c.size - 1;
        o.minZ = -c.size / 2;
        o.maxZ = o.minZ + c.size - 1;
        o.mode = slime::Mode::Strict;
        o.directionLocked = true;
        o.targetCount = 50;
        o.pattern = p;
        const double chunks = static_cast<double>(c.size) * c.size;
        {
            slime::SearchOptions cpu = o;
            cpu.gpuScan = nullptr;
            slime::SlimeEngine eng;
            slime::SearchOutcome out;
            std::atomic<bool> cancel{ false };
            const auto t0 = Clock::now();
            eng.run(cpu, cancel, slime::ProgressFn(), out);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            std::printf("%-18s CPU %8.1f ms | %7.2f 亿区块/秒 | 命中 %lld | active=%d\n", c.name, ms,
                        chunks / (ms / 1000.0) / 1e8, static_cast<long long>(out.totalFound), active);
        }
        if (gpu) {
            o.gpuScan = gpu.get();
            slime::SlimeEngine eng;
            slime::SearchOutcome out;
            std::atomic<bool> cancel{ false };
            const auto t0 = Clock::now();
            eng.run(o, cancel, slime::ProgressFn(), out);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            std::printf("%-18s GPU %8.1f ms | %7.2f 亿区块/秒 | 命中 %lld | used=%d\n", c.name, ms,
                        chunks / (ms / 1000.0) / 1e8, static_cast<long long>(out.totalFound),
                        out.gpuMatchUsed ? 1 : 0);
        }
    }
    return 0;
}

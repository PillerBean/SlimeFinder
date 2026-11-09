// opt_probe28.cpp - 图案流式 GPU 路径 vs CPU 路径（同进程对跑，逐项比对）
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;

slime::SearchOptions makeOpt(int w, int h, bool lock, int pat = 2)
{
    slime::SearchOptions o;
    o.version = slime::Version::Java;
    o.worldSeed = 12345;
    o.minX = -w / 2;
    o.maxX = o.minX + w - 1;
    o.minZ = -h / 2;
    o.maxZ = o.minZ + h - 1;
    o.mode = slime::Mode::Strict;
    o.pattern = decltype(o.pattern)(pat, pat);
    for (int r = 0; r < pat; ++r)
        for (int c = 0; c < pat; ++c) o.pattern.set(r, c, slime::CellState::Green);
    o.directionLocked = lock;
    o.threads = 12;
    o.targetCount = 50;
    return o;
}

struct R {
    double ms = 0;
    size_t n = 0;
    int64_t found = 0;
    bool ok = false;
};

R run(const slime::SearchOptions& o)
{
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    slime::ProgressFn noop;
    const auto t0 = Clock::now();
    eng.run(o, cancel, noop, out);
    R r;
    r.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    r.n = out.results.size();
    r.found = out.totalFound;
    r.ok = out.ok;
    return r;
}
} // namespace

int main()
{
    std::string err;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&err);
    std::printf("GPU: %s\n", gpu ? "已创建" : ("不可用 " + err).c_str());

    const struct {
        const char* name;
        int w, h;
        bool lock;
        int pat;
    } cases[] = {
        { "2x2 全绿 2048² 方向锁定", 2048, 2048, true, 2 },
        { "2x2 全绿 8192² 方向锁定", 8192, 8192, true, 2 },
        { "2x2 全绿 4096² 含旋转", 4096, 4096, false, 2 },
        { "2x2 全绿 8192² 含旋转", 8192, 8192, false, 2 },
        { "5x5 全绿 8192² 方向锁定", 8192, 8192, true, 5 },
        { "2x2 5500² 方向锁定(刚过门槛)", 5500, 5500, true, 2 },
        { "2x2 6144² 方向锁定", 6144, 6144, true, 2 },
        { "2x2 7000² 方向锁定", 7000, 7000, true, 2 },
    };
    for (const auto& c : cases) {
        const R a = run(makeOpt(c.w, c.h, c.lock, c.pat));
        slime::SearchOptions gpuOpt = makeOpt(c.w, c.h, c.lock, c.pat);
        gpuOpt.gpuScan = gpu.get();
        const R b = run(gpuOpt);
        const bool same = (a.n == b.n) && (a.found == b.found);
        std::printf("%-30s CPU %7.2f ms 命中 %lld | GPU %7.2f ms 命中 %lld | 一致=%s | 加速 %.2fx\n", c.name,
                    a.ms, static_cast<long long>(a.found), b.ms, static_cast<long long>(b.found),
                    same ? "是" : "否", a.ms / b.ms);
    }
    return 0;
}

// opt_probe50.cpp - 综合性能/一致性矩阵（引擎级）
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct Sc {
    const char* name;
    int ver;      // 0 java 1 bedrock
    int mode;     // 0 density 1 strict
    int alg;      // 0 exact 1 layered 2 sample 3 brute
    int g;
    double thr;
    int target;
    int stop;
    int smart;
    int ignore;
    int W;
    int H;
    int gpu;
    int threads;
};

struct Res {
    double ms = 0;
    size_t n = 0;
    bool ok = false;
    bool gpuScan = false, gpuMatch = false, early = false;
    std::string fallback;
    long long scanned = 0;
    std::vector<std::string> keys;
};

static Res run(const Sc& s, slime::GpuScan* gpu)
{
    slime::SearchOptions o;
    o.version = s.ver ? slime::Version::Bedrock : slime::Version::Java;
    o.worldSeed = s.ver ? 0 : 4545;
    o.minX = -s.W / 2;
    o.maxX = o.minX + s.W - 1;
    o.minZ = -s.H / 2;
    o.maxZ = o.minZ + s.H - 1;
    o.mode = s.mode ? slime::Mode::Strict : slime::Mode::Density;
    o.algorithm = static_cast<slime::Algorithm>(s.alg);
    o.gridSize = s.g;
    o.minDensity = s.thr;
    o.targetCount = s.target;
    o.stopAtTarget = s.stop != 0;
    o.smartRange = s.smart != 0;
    o.ignoreThreshold = s.ignore != 0;
    o.threads = s.threads;
    o.startX = 0;
    o.startZ = 0;
    o.gpuScan = s.gpu ? gpu : nullptr;
    if (s.mode) {
        slime::Pattern p(2, 2);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) p.set(r, c, slime::CellState::Green);
        o.pattern = p;
    }
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    const auto t0 = std::chrono::steady_clock::now();
    eng.run(o, cancel, slime::ProgressFn(), out);
    Res r;
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    r.n = out.results.size();
    r.ok = out.ok;
    r.gpuScan = out.gpuScanUsed;
    r.gpuMatch = out.gpuMatchUsed;
    r.early = out.stoppedEarly;
    r.fallback = out.gpuFallbackReason;
    r.scanned = out.stats.scannedChunks;
    for (const auto& m : out.results) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%.4f", m.minX, m.maxX, m.minZ, m.maxZ, m.density);
        r.keys.push_back(buf);
    }
    return r;
}

int main(int argc, char** argv)
{
    std::string err;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&err);
    if (!gpu || !gpu->available()) {
        std::printf("GPU 不可用：%s（只用 CPU 跑矩阵）\n", err.c_str());
        gpu.reset();
    } else {
        std::printf("GPU: %s\n", gpu->deviceName().c_str());
    }

    const Sc scs[] = {
        // 密度 Java：尺寸/GPU/阈值/早停
        { "J-dens 2k g5 .3", 0, 0, 0, 5, 0.30, 50, 0, 0, 0, 2000, 2000, 0, 8 },
        { "J-dens 2k g5 .3 GPU", 0, 0, 0, 5, 0.30, 50, 0, 0, 0, 2000, 2000, 1, 8 },
        { "J-dens 20k g8 .3", 0, 0, 0, 8, 0.30, 50, 0, 0, 0, 20000, 20000, 0, 8 },
        { "J-dens 20k g8 .3 GPU", 0, 0, 0, 8, 0.30, 50, 0, 0, 0, 20000, 20000, 1, 8 },
        { "J-dens 20k thr0", 0, 0, 0, 8, 0.0, 50, 0, 0, 0, 20000, 20000, 0, 8 },
        { "J-dens 20k thr0 GPU", 0, 0, 0, 8, 0.0, 50, 0, 0, 0, 20000, 20000, 1, 8 },
        { "J-dens 20k stop+smart", 0, 0, 0, 8, 0.30, 50, 1, 1, 0, 20000, 20000, 0, 8 },
        { "J-dens 20k stop+smart GPU", 0, 0, 0, 8, 0.30, 50, 1, 1, 0, 20000, 20000, 1, 8 },
        { "J-dens 20k stop only GPU", 0, 0, 0, 8, 0.30, 50, 1, 0, 0, 20000, 20000, 1, 8 },
        { "J-dens 70k chunk GPU", 0, 0, 0, 8, 0.30, 20, 0, 0, 0, 70000, 70000, 1, 8 },
        { "J-dens 70k chunk CPU", 0, 0, 0, 8, 0.30, 20, 0, 0, 0, 70000, 70000, 0, 8 },
        // 图案 Java
        { "J-pat 2x2 2k GPU", 0, 1, 0, 0, 0.0, 50, 0, 0, 0, 2000, 2000, 1, 8 },
        { "J-pat 2x2 20k", 0, 1, 0, 0, 0.0, 50, 0, 0, 0, 20000, 20000, 0, 8 },
        { "J-pat 2x2 20k GPU", 0, 1, 0, 0, 0.0, 50, 0, 0, 0, 20000, 20000, 1, 8 },
        { "J-pat 2x2 20k stop+smart", 0, 1, 0, 0, 0.0, 50, 1, 1, 0, 20000, 20000, 0, 8 },
        // 算法
        { "J-dens 20k layered GPU", 0, 0, 1, 8, 0.30, 50, 0, 0, 0, 20000, 20000, 1, 8 },
        { "J-dens 20k sample GPU", 0, 0, 2, 8, 0.30, 50, 0, 0, 0, 20000, 20000, 1, 8 },
        { "J-dens 4k brute", 0, 0, 3, 8, 0.30, 50, 0, 0, 0, 4000, 4000, 0, 8 },
        // 忽略阈值（最优候选）
        { "J-best 2k", 0, 0, 0, 5, 0.90, 20, 0, 0, 1, 2000, 2000, 0, 8 },
        { "J-best 20k", 0, 0, 0, 8, 0.90, 20, 0, 0, 1, 20000, 20000, 0, 8 },
        { "J-best 70k chunk", 0, 0, 0, 8, 0.90, 20, 0, 0, 1, 70000, 70000, 0, 8 },
        // 基岩
        { "B-dens 2k g5", 1, 0, 0, 5, 0.30, 50, 0, 0, 0, 2000, 2000, 0, 8 },
        { "B-dens 2k GPU", 1, 0, 0, 5, 0.30, 50, 0, 0, 0, 2000, 2000, 1, 8 },
        { "B-dens 20k GPU", 1, 0, 0, 8, 0.30, 50, 0, 0, 0, 20000, 20000, 1, 8 },
        { "B-dens 20k CPU", 1, 0, 0, 8, 0.30, 50, 0, 0, 0, 20000, 20000, 0, 8 },
        { "B-best 2k", 1, 0, 0, 5, 0.90, 20, 0, 0, 1, 2000, 2000, 0, 8 },
        { "J-pat 20k stop only", 0, 1, 0, 0, 0.0, 50, 1, 0, 0, 20000, 20000, 0, 8 },
        { "J-pat 20k smart only", 0, 1, 0, 0, 0.0, 50, 0, 1, 0, 20000, 20000, 0, 8 },
        { "J-pat 20k stop+smart", 0, 1, 0, 0, 0.0, 50, 1, 1, 0, 20000, 20000, 0, 8 },
        { "B-dens 20k thr0", 1, 0, 0, 8, 0.0, 50, 0, 0, 0, 20000, 20000, 0, 8 },
        { "J-best 20k GPU", 0, 0, 0, 8, 0.90, 20, 0, 0, 1, 20000, 20000, 1, 8 },
        { "J-dens 8k g5 GPU", 0, 0, 0, 5, 0.30, 50, 0, 0, 0, 8192, 8192, 1, 12 },
        { "J-dens 8k g5 .5 GPU", 0, 0, 0, 5, 0.50, 50, 0, 0, 0, 8192, 8192, 1, 12 },
        { "J-dens 8k g5 .7 GPU", 0, 0, 0, 5, 0.70, 50, 0, 0, 0, 8192, 8192, 1, 12 },
        { "J-dens 16k g5 GPU", 0, 0, 0, 5, 0.30, 50, 0, 0, 0, 16384, 16384, 1, 12 },
        { "J-dens 16k g5 CPU", 0, 0, 0, 5, 0.30, 50, 0, 0, 0, 16384, 16384, 0, 12 },
        { "J-dens 8k g5 CPU", 0, 0, 0, 5, 0.30, 50, 0, 0, 0, 8192, 8192, 0, 12 },
        { "J-pat 40k CPU", 0, 1, 0, 0, 0.0, 50, 0, 0, 0, 40000, 40000, 0, 8 },
        { "J-pat 40k GPU", 0, 1, 0, 0, 0.0, 50, 0, 0, 0, 40000, 40000, 1, 8 },
        { "J-pat 100k CPU", 0, 1, 0, 0, 0.0, 50, 0, 0, 0, 100000, 100000, 0, 8 },
        { "J-pat 100k GPU", 0, 1, 0, 0, 0.0, 50, 0, 0, 0, 100000, 100000, 1, 8 },
    };

    std::printf("%-28s %8s %6s %3s %3s %3s %10s %s\n", "scenario", "ms", "n", "gS", "gM", "e", "scanned", "fallback");
    for (const Sc& s : scs) {
        const Res r = run(s, gpu.get());
        std::printf("%-28s %8.1f %6zu %3d %3d %3d %10lld %s\n", s.name, r.ms, r.n, (int)r.gpuScan,
                    (int)r.gpuMatch, (int)r.early, r.scanned, r.fallback.c_str());
    }
    return 0;
}

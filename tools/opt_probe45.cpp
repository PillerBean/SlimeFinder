// opt_probe45.cpp - 诊断：忽略阈值 / 数量够就停 / GPU 早停 行为矩阵
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    // 参数：w h thr target ignore stop smart gpu
    const int W = argc > 1 ? std::atoi(argv[1]) : 4000;
    const int H = argc > 2 ? std::atoi(argv[2]) : 4000;
    const double thr = argc > 3 ? std::atof(argv[3]) : 0.0;
    const int target = argc > 4 ? std::atoi(argv[4]) : 10;
    const int ignore = argc > 5 ? std::atoi(argv[5]) : 0;
    const int stop = argc > 6 ? std::atoi(argv[6]) : 0;
    const int smart = argc > 7 ? std::atoi(argv[7]) : 0;
    const int useGpu = argc > 8 ? std::atoi(argv[8]) : 0;
    const int verSel = argc > 9 ? std::atoi(argv[9]) : 0;

    std::unique_ptr<slime::GpuScan> gpu;
    if (useGpu) {
        std::string err;
        gpu = slimegpu::createGpuScanAdapter(&err);
        if (!gpu || !gpu->available()) {
            std::printf("GPU 不可用：%s\n", err.c_str());
            return 1;
        }
    }

    slime::SearchOptions o;
    o.version = (verSel != 0) ? slime::Version::Bedrock : slime::Version::Java;
    o.worldSeed = 4545;
    o.minX = -W / 2;
    o.maxX = o.minX + W - 1;
    o.minZ = -H / 2;
    o.maxZ = o.minZ + H - 1;
    o.mode = slime::Mode::Density;
    o.gridSize = 8;
    o.minDensity = thr;
    o.targetCount = target;
    o.ignoreThreshold = ignore != 0;
    o.stopAtTarget = stop != 0;
    o.smartRange = smart != 0;
    o.threads = 8;
    o.startX = 0;
    o.startZ = 0;
    o.gpuScan = gpu.get();

    std::atomic<bool> cancel{ false };
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    const auto t0 = std::chrono::steady_clock::now();
    eng.run(o, cancel, slime::ProgressFn(), out);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    bool desc = true;
    for (size_t i = 1; i < out.results.size(); ++i)
        if (out.results[i].density > out.results[i - 1].density + 1e-6f) desc = false;
    std::printf("W=%d H=%d thr=%.2f target=%d ignore=%d stop=%d smart=%d gpu=%d => ok=%d\n", W, H, thr, target,
                ignore, stop, smart, useGpu, (int)out.ok);
    std::printf("  用时 %.1f ms | 结果 %zu | 总命中 %lld | 已扫描 %lld | GPU扫描=%d GPU匹配=%d | 早停=%d | 密度降序=%d\n",
                ms, out.results.size(), (long long)out.totalFound, (long long)out.stats.scannedChunks,
                (int)out.gpuScanUsed, (int)out.gpuMatchUsed, (int)out.stoppedEarly, (int)desc);
    if (!out.error.empty()) std::printf("  错误：%s\n", out.error.c_str());
    if (!out.gpuFallbackReason.empty()) std::printf("  GPU 回退：%s\n", out.gpuFallbackReason.c_str());

    // v3.10.10 诊断：忽略阈值（最优候选）模式 —— 与暴力枚举的「密度最优 N」对拍
    if (ignore) {
        const int g = o.gridSize;
        const int64_t axMin = o.minX, axMax = o.maxX - g + 1;
        const int64_t azMin = o.minZ, azMax = o.maxZ - g + 1;
        struct Cand { int cnt; int64_t d2; int x, z; };
        std::vector<Cand> all;
        const int64_t half = (g - 1) / 2;
        for (int64_t z = azMin; z <= azMax; ++z) {
            for (int64_t x = axMin; x <= axMax; ++x) {
                int cnt = 0;
                for (int dz = 0; dz < g; ++dz)
                    for (int dx = 0; dx < g; ++dx)
                        if (slime::SlimeEngine::isSlimeChunk(o.version, o.worldSeed, (int)(x + dx), (int)(z + dz))) ++cnt;
                const int64_t ddx = (x + half) - o.startX, ddz = (z + half) - o.startZ;
                all.push_back(Cand{cnt, ddx * ddx + ddz * ddz, (int)x, (int)z});
            }
        }
        std::sort(all.begin(), all.end(), [](const Cand& a, const Cand& b) {
            if (a.cnt != b.cnt) return a.cnt > b.cnt;
            if (a.d2 != b.d2) return a.d2 < b.d2;
            if (a.z != b.z) return a.z < b.z;
            return a.x < b.x;
        });
        const size_t n = (std::min)(all.size(), out.results.size());
        size_t mismatch = 0;
        for (size_t i = 0; i < n; ++i) {
            const int want = all[i].cnt;
            const int got = (int)std::lround(out.results[i].density * g * g);
            if (out.results[i].minX != all[i].x || out.results[i].minZ != all[i].z || want != got) ++mismatch;
        }
        std::printf("  [最优对拍] 暴力候选=%zu 引擎结果=%zu 前 %zu 条不一致=%zu
", all.size(), out.results.size(), n, mismatch);
    }
    if (!out.results.empty()) {
        std::printf("  首条：密度 %.3f 区块(%d,%d) | 末条：密度 %.3f\n", out.results.front().density,
                    out.results.front().minX, out.results.front().minZ, out.results.back().density);
    }
    return 0;
}

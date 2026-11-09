// opt_probe46.cpp - 最优候选（忽略阈值）正确性 + 速度对比探针
#include "core/SlimeEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
    const int W = argc > 1 ? std::atoi(argv[1]) : 300;
    const int H = argc > 2 ? std::atoi(argv[2]) : 200;
    const int g = argc > 3 ? std::atoi(argv[3]) : 5;
    const int target = argc > 4 ? std::atoi(argv[4]) : 20;
    const int64_t seed = 4545;

    const auto run = [&](bool ignore) {
        slime::SearchOptions o;
        o.version = slime::Version::Java;
        o.worldSeed = seed;
        o.minX = -W / 2;
        o.maxX = o.minX + W - 1;
        o.minZ = -H / 2;
        o.maxZ = o.minZ + H - 1;
        o.mode = slime::Mode::Density;
        o.gridSize = g;
        o.minDensity = 0.9;
        o.targetCount = target;
        o.threads = 8;
        o.ignoreThreshold = ignore;
        o.stopAtTarget = false;
        o.smartRange = false;
        slime::SlimeEngine eng;
        slime::SearchOutcome out;
        std::atomic<bool> cancel{ false };
        const auto t0 = std::chrono::steady_clock::now();
        eng.run(o, cancel, slime::ProgressFn(), out);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return std::make_pair(ms, out);
    };

    const auto [msBest, best] = run(true);
    const auto [msNormal, normal] = run(false);
    std::printf("区域 %dx%d g=%d N=%d：最优模式 %.1f ms（结果 %zu）｜普通模式 %.1f ms（结果 %zu，阈值 0.9 生效）\n",
                W, H, g, target, msBest, best.results.size(), msNormal, normal.results.size());

    // 暴力对拍（仅小区域）
    if (static_cast<long long>(W) * H <= 4000000LL) {
        struct Cand { int cnt; long long d2; int x, z; };
        std::vector<Cand> all;
        const int half = (g - 1) / 2;
        for (int z = -H / 2; z + g - 1 <= -H / 2 + H - 1; ++z) {
            for (int x = -W / 2; x + g - 1 <= -W / 2 + W - 1; ++x) {
                int cnt = 0;
                for (int dz = 0; dz < g; ++dz)
                    for (int dx = 0; dx < g; ++dx)
                        if (slime::SlimeEngine::isSlimeChunk(slime::Version::Java, seed, x + dx, z + dz)) ++cnt;
                const long long ddx = (x + half), ddz = (z + half);
                all.push_back(Cand{ cnt, ddx * ddx + ddz * ddz, x, z });
            }
        }
        std::sort(all.begin(), all.end(), [](const Cand& a, const Cand& b) {
            if (a.cnt != b.cnt) return a.cnt > b.cnt;
            if (a.d2 != b.d2) return a.d2 < b.d2;
            if (a.z != b.z) return a.z < b.z;
            return a.x < b.x;
        });
        const size_t n = std::min(all.size(), best.results.size());
        size_t mismatch = 0;
        for (size_t i = 0; i < n; ++i) {
            const int want = all[i].cnt;
            const int got = static_cast<int>(std::lround(best.results[i].density * g * g));
            if (best.results[i].minX != all[i].x || best.results[i].minZ != all[i].z || want != got) {
                ++mismatch;
                if (mismatch <= 5) std::printf("   #%zu 期望(cnt=%d,%d,%d) 实际(cnt=%d,%d,%d)\n", i, want, all[i].x, all[i].z, got, best.results[i].minX, best.results[i].minZ);
            }
        }
        std::printf("  暴力对拍：候选 %zu，前 %zu 条不一致 %zu\n", all.size(), n, mismatch);
    }
    return 0;
}

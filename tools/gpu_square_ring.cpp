// tools/gpu_square_ring.cpp — 实验：GPU 正方形密度 Top-K（按窗口计数）→ 环形种子质量/耗时。
// 目的：验证“自动正方形预筛 + 环形修正”能否把全图压到半小时级。
#include "core/AfkPlanner.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuSession.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;

static double gpuDensityTopK(slimegpu::GpuSession& gs, int64_t seed, int W, int H, int g,
                             double thr, int bandRows, int topPerBand, int topK,
                             std::vector<std::pair<int, int>>& seeds, double& gpuMs,
                             long long& totalHits)
{
    const int minX = -W / 2, minZ = -H / 2;
    const int maxX = minX + W - 1, maxZ = minZ + H - 1;
    const int axMin = minX, axMax = maxX - g + 1;
    const int azMin = minZ, azMax = maxZ - g + 1;
    const int w = axMax - axMin + g;
    const int thrCount = static_cast<int>(std::ceil(thr * static_cast<double>(g) * g - 1e-9));
    std::vector<std::pair<int, std::pair<int, int>>> best;   // (count, cx, cz)
    std::atomic<bool> cancel{ false };
    const auto t0 = Clock::now();
    totalHits = 0;
    for (int az0 = azMin; az0 <= azMax; az0 += bandRows) {
        const int az1 = std::min(azMax, az0 + bandRows - 1);
        const int h = az1 - az0 + g;
        std::vector<slimegpu::GpuSession::StreamDensityHit> hits;
        std::string err;
        uint32_t total = 0;
        const bool ok = gs.matchDensityStream(seed, false, g, std::max(1, thrCount), w, h,
                                              axMin, axMax, az0, az1,
                                              4000000u, 0, 0, cancel, hits, &err, &total,
                                              false, 0, 0);
        if (!ok) {
            std::fprintf(stderr, "GPU band failed: %s\n", err.c_str());
            return -1.0;
        }
        totalHits += total;
        // 每带只留计数最高的若干条，避免全图命中占内存
        std::partial_sort(hits.begin(),
                          hits.begin() + static_cast<long>(std::min<size_t>(hits.size(), topPerBand)),
                          hits.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
        const size_t n = std::min<size_t>(hits.size(), topPerBand);
        for (size_t i = 0; i < n; ++i)
            best.emplace_back(static_cast<int>(hits[i].count),
                              std::make_pair(static_cast<int>(hits[i].x + g / 2),
                                             static_cast<int>(hits[i].z + g / 2)));
    }
    gpuMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    std::partial_sort(best.begin(),
                      best.begin() + static_cast<long>(std::min<size_t>(best.size(), topK)),
                      best.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    seeds.clear();
    for (size_t i = 0; i < best.size() && static_cast<int>(i) < topK; ++i) seeds.push_back(best[i].second);
    return gpuMs;
}

int main(int argc, char** argv)
{
    const int W = argc > 1 ? std::atoi(argv[1]) : 2000;
    const int H = argc > 2 ? std::atoi(argv[2]) : 2000;
    const int g = argc > 3 ? std::atoi(argv[3]) : 15;
    const double thr = argc > 4 ? std::atof(argv[4]) : 0.20;
    const int topK = argc > 5 ? std::atoi(argv[5]) : 4000;
    const int bandRows = argc > 6 ? std::atoi(argv[6]) : 1024;
    const int refineRadius = argc > 7 ? std::atoi(argv[7]) : 0;
    const int minSep = argc > 8 ? std::atoi(argv[8]) : 0;
    const int64_t seed = 4545;

    slimegpu::GpuSession gs;
    std::string err;
    if (!gs.init(&err) || !gs.available()) {
        std::fprintf(stderr, "GPU unavailable: %s\n", err.c_str());
        return 2;
    }
    std::printf("GPU: %s\n", gs.device().name.c_str());

    std::vector<std::pair<int, int>> seeds;
    double gpuMs = 0.0;
    long long totalHits = 0;
    const double rc = gpuDensityTopK(gs, seed, W, H, g, thr, bandRows, 512, topK, seeds, gpuMs, totalHits);
    if (rc < 0) return 1;
    std::printf("square GPU g=%d thr=%.2f bandRows=%d: %.1f ms, hits=%lld, seeds=%zu\n",
                g, thr, bandRows, gpuMs, totalHits, seeds.size());

    // 可选：种子去重（按区块间距），避免前 4000 个种子全挤在同一个高密度岛
    if (minSep > 0) {
        std::vector<std::pair<int, int>> spread;
        const int sep2 = minSep * minSep;
        for (const auto& sd : seeds) {
            bool keep = true;
            for (const auto& k : spread) {
                const int dx = sd.first - k.first, dz = sd.second - k.second;
                if (dx * dx + dz * dz < sep2) { keep = false; break; }
            }
            if (keep) spread.push_back(sd);
        }
        std::printf("seed spread (minSep=%d): %zu -> %zu\n", minSep, seeds.size(), spread.size());
        seeds.swap(spread);
    }

    // 环形覆盖率：只测种子中心（不展开），先看方形 Top-K 是否已接近环形最优
    slime::AfkPlanOptions ro;
    ro.area = slime::Rect{ -W / 2, W / 2 - 1, -H / 2, H / 2 - 1 };
    ro.radius = 128;
    ro.innerRadius = 24;
    const auto pred = [seed](int cx, int cz) {
        return slime::SlimeEngine::isSlimeChunk(slime::Version::Java, seed, cx, cz);
    };
    double best = 0.0;
    int bx = 0, bz = 0;
    const int step = std::max(1, static_cast<int>(seeds.size()) / 500);
    const auto t0 = Clock::now();
    for (size_t i = 0; i < seeds.size(); i += step) {
        const auto mm = slime::AfkPlanner::measureExact(seeds[i].first * 16 + 8,
                                                        seeds[i].second * 16 + 8, ro, pred);
        if (mm.coveragePercent > best) { best = mm.coveragePercent; bx = seeds[i].first; bz = seeds[i].second; }
    }
    const double checkMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    std::printf("best ring at square centers: %.2f%% (%d,%d) check=%.1f ms\n", best, bx, bz, checkMs);

    if (refineRadius > 0 && !seeds.empty()) {
        // 结构实验：把每个种子周围 ±r 区块的环形候选全部精算（生产可换成更省的分级核）
        const auto rt0 = Clock::now();
        const int r = refineRadius;
        long long evals = 0;
        for (const auto& sd : seeds) {
            for (int dz = -r; dz <= r; ++dz) {
                for (int dx = -r; dx <= r; ++dx) {
                    const auto mm = slime::AfkPlanner::measureExact(
                        (sd.first + dx) * 16 + 8, (sd.second + dz) * 16 + 8, ro, pred);
                    ++evals;
                    if (mm.coveragePercent > best) {
                        best = mm.coveragePercent;
                        bx = sd.first + dx;
                        bz = sd.second + dz;
                    }
                }
            }
        }
        const double refineMs = std::chrono::duration<double, std::milli>(Clock::now() - rt0).count();
        std::printf("refine r=%d: seeds=%zu evals=%lld time=%.1f ms, best=%.2f%% (%d,%d)\n",
                    r, seeds.size(), evals, refineMs, best, bx, bz);
        std::printf("TOTAL square+ring: %.1f ms (%.2f min)\n", gpuMs + refineMs, (gpuMs + refineMs) / 60000.0);
    }
    return 0;
}

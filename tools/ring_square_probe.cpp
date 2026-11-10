// tools/ring_square_probe.cpp — 试验：正方形/方形环预筛能否召回环形最优候选。
// 不是产品代码：仅用于比较不同正方形尺寸的召回率、相关性与筛选成本。
#include "core/AfkPlanner.h"
#include "core/SlimeEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

struct ScoreMap {
    int W = 0, H = 0;          // 候选中心网格（区块）
    int minX = 0, minZ = 0;
    std::vector<float> v;      // score[W*H]
};

int main(int argc, char** argv)
{
    const int W = argc > 1 ? std::atoi(argv[1]) : 2000;
    const int H = argc > 2 ? std::atoi(argv[2]) : 2000;
    const int R = 128;
    const int inner = 24;
    const int64_t seed = argc > 3 ? std::atoll(argv[3]) : 4545;
    const int minX = -W / 2, minZ = -H / 2;
    const int maxX = minX + W - 1, maxZ = minZ + H - 1;

    const auto isSlime = [seed](int cx, int cz) {
        return slime::SlimeEngine::isSlimeChunk(slime::Version::Java, seed, cx, cz);
    };

    // 1) 环形精确基准（2k² 落在 400 万格上限内）
    slime::AfkPlanOptions ringOpt;
    ringOpt.area = slime::Rect{ minX, maxX, minZ, maxZ };
    ringOpt.radius = R;
    ringOpt.innerRadius = inner;
    ringOpt.topN = 20;
    ringOpt.minDensity = 0.0;
    ringOpt.minSeparation = 128;
    ringOpt.scanStride = 1;   // 精确逐区块
    const auto ringT0 = Clock::now();
    const auto ringCands = slime::AfkPlanner::plan(ringOpt, isSlime);
    const double ringMs = std::chrono::duration<double, std::milli>(Clock::now() - ringT0).count();
    std::printf("ring exact W=%d H=%d: %.1f ms, candidates=%zu\n", W, H, ringMs, ringCands.size());
    for (size_t i = 0; i < ringCands.size(); ++i) {
        std::printf("  #%zu (%d,%d) coverage %.2f%%\n", i + 1, ringCands[i].x, ringCands[i].z,
                    ringCands[i].coveragePercent);
    }

    // 2) 史莱姆位图 + 2D 前缀和（允许方形窗口越界到 pad 区域）
    int maxG = 65;
    const int innerG = 3;
    const int pad = maxG / 2 + 2;
    const int bx0 = minX - pad, bz0 = minZ - pad;
    const int bw = W + 2 * pad, bh = H + 2 * pad;
    std::vector<uint8_t> bits(static_cast<size_t>(bw) * bh, 0);
    for (int z = 0; z < bh; ++z)
        for (int x = 0; x < bw; ++x)
            bits[static_cast<size_t>(z) * bw + x] = isSlime(bx0 + x, bz0 + z) ? 1 : 0;

    std::vector<int> pref(static_cast<size_t>(bw + 1) * (bh + 1), 0);
    for (int z = 0; z < bh; ++z) {
        int rowSum = 0;
        for (int x = 0; x < bw; ++x) {
            rowSum += bits[static_cast<size_t>(z) * bw + x];
            pref[static_cast<size_t>(z + 1) * (bw + 1) + (x + 1)] =
                pref[static_cast<size_t>(z) * (bw + 1) + (x + 1)] + rowSum;
        }
    }
    const auto rectSum = [&](int cx, int cz, int g) {
        // 以 (cx,cz) 区块中心为中心，边长 g（奇数）的方形
        const int x0 = cx - g / 2 - bx0, z0 = cz - g / 2 - bz0;
        const int x1 = x0 + g, z1 = z0 + g;
        return pref[static_cast<size_t>(z1) * (bw + 1) + x1] -
               pref[static_cast<size_t>(z0) * (bw + 1) + x1] -
               pref[static_cast<size_t>(z1) * (bw + 1) + x0] +
               pref[static_cast<size_t>(z0) * (bw + 1) + x0];
    };

    // 3) 递归/随机样本上的环形覆盖率（用于相关性）
    slime::AfkPlanOptions metricOpt = ringOpt;
    metricOpt.topN = 1;
    std::vector<int> sampleCx, sampleCz;
    const int sampleN = 6000;
    for (int i = 0; i < sampleN; ++i) {
        const int idx = static_cast<int>((static_cast<long long>(i) * 2654435761u) % (W * H));
        sampleCx.push_back(minX + idx % W);
        sampleCz.push_back(minZ + idx / W);
    }
    std::vector<double> ringSample(sampleN);
    for (int i = 0; i < sampleN; ++i) {
        const auto m = slime::AfkPlanner::measureExact(sampleCx[i] * 16 + 8, sampleCz[i] * 16 + 8,
                                                       metricOpt, isSlime);
        ringSample[i] = m.coveragePercent;
    }

    struct Variant { const char* name; int outerG; int innerG; };
    const Variant variants[] = {
        { "sq 5", 5, 0 },
        { "sq 9", 9, 0 },
        { "sq 13", 13, 0 },
        { "sq 15", 15, 0 },
        { "sq 17", 17, 0 },
        { "sq 19", 19, 0 },
        { "sq 21", 21, 0 },
        { "sq 25", 25, 0 },
        { "sq 17-in3", 17, 3 },
    };
    const double seedFracs[] = { 0.01, 0.05, 0.10, 0.20 };
    const int dists[] = { 4, 8 };

    std::printf("\n%-12s %6s %8s %7s %5s %5s %5s %5s %5s  %s\n", "variant", "ms", "corr", "top%",
                "r100", "r400", "r1k", "r4k", "r16k", "cascade");
    std::vector<std::pair<std::string, std::vector<std::pair<int, int>>>> savedSeeds;
    for (const Variant& v : variants) {
        std::vector<float> score(static_cast<size_t>(W) * H);
        const auto t0 = Clock::now();
        for (int z = 0; z < H; ++z) {
            for (int x = 0; x < W; ++x) {
                const int cx = minX + x, cz = minZ + z;
                int s = rectSum(cx, cz, v.outerG);
                if (v.innerG > 0) s -= rectSum(cx, cz, v.innerG);
                score[static_cast<size_t>(z) * W + x] = static_cast<float>(s);
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

        // 相关性：方形分数 vs 环形覆盖率
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        for (int i = 0; i < sampleN; ++i) {
            const int x = sampleCx[i] - minX, z = sampleCz[i] - minZ;
            const double a = score[static_cast<size_t>(z) * W + x];
            const double b = ringSample[i];
            sx += a; sy += b; sxx += a * a; syy += b * b; sxy += a * b;
        }
        const double n = sampleN;
        const double cov = sxy - sx * sy / n;
        const double va = sxx - sx * sx / n, vb = syy - sy * sy / n;
        const double corr = (va > 1e-12 && vb > 1e-12) ? cov / std::sqrt(va * vb) : 0.0;

        // 基准环形候选在方形分数中的平均百分位
        std::vector<float> tmp = score;
        std::sort(tmp.begin(), tmp.end());
        double avgPct = 0.0;
        for (const auto& c : ringCands) {
            const int x = c.x / 16 - minX, z = c.z / 16 - minZ;
            if (x < 0 || x >= W || z < 0 || z >= H) continue;
            const float s = score[static_cast<size_t>(z) * W + x];
            const size_t below = std::lower_bound(tmp.begin(), tmp.end(), s) - tmp.begin();
            avgPct += 100.0 * static_cast<double>(below) / static_cast<double>(tmp.size());
        }
        if (!ringCands.empty()) avgPct /= static_cast<double>(ringCands.size());

        // 固定种子数量（模拟不同预筛预算）：环形基准候选附近 D 区块内是否有方形种子
        const int seedCounts[] = { 100, 400, 1000, 4000, 16000 };
        double rec[5] = {};
        double cascadeBest = 0.0;
        for (int fi = 0; fi < 5; ++fi) {
            const size_t keep = static_cast<size_t>(seedCounts[fi]);
            std::vector<float> sorted = score;
            std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(keep), sorted.end(),
                             std::greater<float>());
            const float threshold = sorted[keep];
            std::vector<uint8_t> seedGrid(static_cast<size_t>(W) * H, 0);
            std::vector<std::pair<int, int>> seeds;
            for (int z = 0; z < H; ++z)
                for (int x = 0; x < W; ++x)
                    if (score[static_cast<size_t>(z) * W + x] >= threshold) {
                        seedGrid[static_cast<size_t>(z) * W + x] = 1;
                        seeds.emplace_back(minX + x, minZ + z);
                    }
            {
                const int D = 8;
                int hit = 0, total = 0;
                for (const auto& c : ringCands) {
                    const int fx = c.x >= 0 ? c.x / 16 : -((-c.x + 15) / 16);
                    const int fz = c.z >= 0 ? c.z / 16 : -((-c.z + 15) / 16);
                    bool found = false;
                    for (int dz = -D; dz <= D && !found; ++dz) {
                        for (int dx = -D; dx <= D && !found; ++dx) {
                            const int x = fx + dx - minX, z = fz + dz - minZ;
                            if (x < 0 || x >= W || z < 0 || z >= H) continue;
                            if (seedGrid[static_cast<size_t>(z) * W + x]) found = true;
                        }
                    }
                    ++total;
                    if (found) ++hit;
                }
                rec[fi] = total ? 100.0 * hit / total : 0.0;
            }
            if (seedCounts[fi] == 4000) {
                savedSeeds.emplace_back(v.name, seeds);
            }
            // 级联质量：只对前 4000 个方形种子做 ±1 区块局部环形重算，看最好能找到多少覆盖率
            if (seedCounts[fi] == 4000) {
                for (int i = 0; i < static_cast<int>(seeds.size()) && i < 4000; ++i) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const auto m = slime::AfkPlanner::measureExact(
                                (seeds[i].first + dx) * 16 + 8, (seeds[i].second + dz) * 16 + 8,
                                metricOpt, isSlime);
                            cascadeBest = (std::max)(cascadeBest, m.coveragePercent);
                        }
                    }
                }
            }
        }
        double ringBest = ringCands.empty() ? 0.0 : ringCands.front().coveragePercent;
        std::printf("%-12s %6.1f %8.3f %7.1f%% %5.0f%% %5.0f%% %5.0f%% %5.0f%% %5.0f%%  cascade %.2f%% (exact %.2f%%)\n",
                    v.name, ms, corr, avgPct, rec[0], rec[1], rec[2], rec[3], rec[4],
                    cascadeBest, ringBest);
    }

    // 4) 级联半径/多尺寸组合：种子只是“区域”，在种子附近更大范围做环形精确复算
    double exactBest = ringCands.empty() ? 0.0 : ringCands.front().coveragePercent;
    const double cellUs = 0.0;   // 仅输出总耗时
    const auto cascadeRun = [&](const std::vector<std::pair<int, int>>& seeds, int radius) {
        const auto t0 = Clock::now();
        double best = 0.0;
        for (const auto& sd : seeds) {
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const auto m = slime::AfkPlanner::measureExact(
                        (sd.first + dx) * 16 + 8, (sd.second + dz) * 16 + 8, metricOpt, isSlime);
                    best = (std::max)(best, m.coveragePercent);
                }
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        return std::make_pair(best, ms);
    };
    std::printf("\n%-18s %8s %10s %10s\n", "cascade", "radius", "best%", "ms");
    std::vector<std::pair<int, int>> unionSeeds;
    for (const auto& kv : savedSeeds) {
        if (kv.first == "sq 13" || kv.first == "sq 15" || kv.first == "sq 17")
            unionSeeds.insert(unionSeeds.end(), kv.second.begin(), kv.second.end());
        for (int rad : { 1, 8 }) {
            const auto r = cascadeRun(kv.second, rad);
            std::printf("%-18s %8d %9.2f%% %10.1f\n", kv.first.c_str(), rad, r.first, r.second);
        }
    }
    for (int rad : { 1, 8 }) {
        const auto r = cascadeRun(unionSeeds, rad);
        std::printf("%-18s %8d %9.2f%% %10.1f\n", "union 13/15/17", rad, r.first, r.second);
    }
    std::printf("exact best: %.2f%%\n", exactBest);

    // 5) 稀疏正方形预筛：模拟“全图不可能铺满前缀和，只能隔步采样方形分数”
    std::printf("\n%-10s %10s %10s %10s %10s\n", "stride", "sqPoints", "seeds", "best%", "ms");
    for (int stride : { 2, 4, 8, 16 }) {
        struct Q { float score; int cx, cz; };
        std::vector<Q> pts;
        for (int cz = minZ; cz <= maxZ; cz += stride)
            for (int cx = minX; cx <= maxX; cx += stride)
                pts.push_back(Q{ static_cast<float>(rectSum(cx, cz, 15)), cx, cz });
        const size_t seedN = (std::min)(pts.size(), static_cast<size_t>(2000));
        std::partial_sort(pts.begin(), pts.begin() + static_cast<long>(seedN), pts.end(),
                          [](const Q& a, const Q& b) { return a.score > b.score; });
        std::vector<std::pair<int, int>> seeds;
        for (size_t i = 0; i < seedN; ++i) seeds.emplace_back(pts[i].cx, pts[i].cz);
        const auto r = cascadeRun(seeds, 8);
        std::printf("%-10d %10zu %10zu %9.2f%% %10.1f\n", stride, pts.size(), seedN, r.first, r.second);
    }
    return 0;
}

// 小网格反例穷举：2^(G*G) 个史莱姆配置，每个配置都做：
//   1) 方形预筛 Top-K
//   2) AfkPlanner::plan
//   3) 全 1 方块 oracle
//   4) 失败则记录配置、覆盖差距、生成概率
//
// 这是“缩小半径的精确模型”，不是全图 Java 种子模型。
#include "core/AfkPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

using slime::AfkCandidate;
using slime::AfkPlanOptions;
using slime::SlimeChunkPredicate;

int envInt(const char* name, int fallback)
{
    const char* v = std::getenv(name);
    return v ? std::atoi(v) : fallback;
}

struct Setup {
    int G = 4;
    int R = 32;
    int inner = 8;
    int K = 2;
    int square = 3;
};

int popcount16(uint32_t v)
{
    int n = 0;
    while (v) { v &= (v - 1); ++n; }
    return n;
}

int popcount64(uint64_t v)
{
    int n = 0;
    while (v) { v &= (v - 1); ++n; }
    return n;
}

} // namespace

int main()
{
    const Setup s{ envInt("CE_G", 4), envInt("CE_R", 32), envInt("CE_INNER", 8),
                   envInt("CE_K", 2), envInt("CE_SQUARE", 3) };
    const int randomSamples = envInt("CE_RANDOM", 0);
    const int G = s.G;
    const int cells = G * G;
    const int blocks = G * 16;
    const int centers = blocks * blocks;
    if (randomSamples == 0 && cells > 24) {
        std::printf("CE cells=%d too many; use G<=4 (2^24 max)\n", cells);
        return 2;
    }
    if (cells > 64) {
        std::printf("CE cells=%d >64 unsupported\n", cells);
        return 2;
    }

    // 每个候选中心对每个 chunk 的精确覆盖格数。
    std::vector<uint16_t> w(static_cast<size_t>(centers) * cells, 0);
    for (int cz = 0; cz < blocks; ++cz) {
        for (int cx = 0; cx < blocks; ++cx) {
            const size_t ci = static_cast<size_t>(cz) * blocks + cx;
            for (int cc = 0; cc < cells; ++cc) {
                const int chunkX = cc % G;
                const int chunkZ = cc / G;
                uint16_t n = 0;
                for (int bz = 0; bz < 16; ++bz) {
                    for (int bx = 0; bx < 16; ++bx) {
                        const int dx = cx - (chunkX * 16 + bx);
                        const int dz = cz - (chunkZ * 16 + bz);
                        const int d2 = dx * dx + dz * dz;
                        if (d2 >= s.inner * s.inner && d2 <= s.R * s.R) ++n;
                    }
                }
                w[ci * cells + cc] = n;
            }
        }
    }

    const uint64_t total = 1ULL << cells;
    if (randomSamples > 0) {
        std::mt19937_64 rng(0x5EED1234ULL);
        std::bernoulli_distribution pick(0.1);
        uint64_t failures = 0;
        int printed = 0;
        for (int sample = 0; sample < randomSamples; ++sample) {
            uint64_t mask = 0;
            for (int i = 0; i < cells; ++i)
                if (pick(rng)) mask |= (1ULL << i);
            int oracle = 0, ox = 0, oz = 0;
            for (int cz = 0; cz < blocks; ++cz) {
                for (int cx = 0; cx < blocks; ++cx) {
                    const size_t ci = static_cast<size_t>(cz) * blocks + cx;
                    int cov = 0;
                    for (int cc = 0; cc < cells; ++cc)
                        if (mask & (1ULL << cc)) cov += w[ci * cells + cc];
                    if (cov > oracle) { oracle = cov; ox = cx; oz = cz; }
                }
            }
            struct Seed { int score, cx, cz; };
            std::vector<Seed> seeds;
            const int half = s.square / 2;
            for (int cz = 0; cz < G; ++cz)
                for (int cx = 0; cx < G; ++cx) {
                    int score = 0;
                    for (int dz = -half; dz <= half; ++dz)
                        for (int dx = -half; dx <= half; ++dx) {
                            const int x = cx + dx, z = cz + dz;
                            if (x < 0 || z < 0 || x >= G || z >= G) continue;
                            if (mask & (1ULL << (z * G + x))) ++score;
                        }
                    seeds.push_back({ score, cx, cz });
                }
            std::sort(seeds.begin(), seeds.end(), [](const Seed& a, const Seed& b) {
                return a.score > b.score;
            });
            const SlimeChunkPredicate pred = [&](int cx, int cz) {
                if (cx < 0 || cz < 0 || cx >= G || cz >= G) return false;
                return (mask & (1ULL << (cz * G + cx))) != 0;
            };
            AfkPlanOptions opt;
            opt.area = slime::Rect{ 0, G - 1, 0, G - 1 };
            opt.radius = s.R;
            opt.innerRadius = s.inner;
            opt.topN = 1;
            opt.minSeparation = 0;
            opt.coarseStep = 1;
            opt.maxWindowBlocks = 4096;
            const int take = std::min<int>(s.K, static_cast<int>(seeds.size()));
            for (int i = 0; i < take; ++i) opt.seedCandidates.emplace_back(seeds[i].cx, seeds[i].cz);
            const auto got = slime::AfkPlanner::plan(opt, pred, nullptr);
            const int gotBlocks = got.empty() ? 0 : static_cast<int>(got.front().coveredBlocks);
            if (gotBlocks < oracle) {
                ++failures;
                if (printed < 20) {
                    ++printed;
                    std::printf("CE-RANDOM-FAIL sample=%d mask=%llu slime=%d got=%d oracle=%d at=(%d,%d) K=%d\n",
                                sample, static_cast<unsigned long long>(mask), popcount64(mask), gotBlocks,
                                oracle, ox, oz, take);
                }
            }
        }
        const long double p = static_cast<long double>(failures) / randomSamples;
        std::printf("CE-RANDOM G=%d R=%d inner=%d square=%d K=%d samples=%d failures=%llu "
                    "p_hat=%.12Le\n",
                    G, s.R, s.inner, s.square, s.K, randomSamples,
                    static_cast<unsigned long long>(failures), p);
        return 0;
    }

    uint64_t failures = 0;
    long double failMass = 0.0L;
    int printed = 0;
    long double pSlime = 0.1L;

    for (uint64_t mask = 0; mask < total; ++mask) {
        const int kSlime = popcount16(static_cast<uint32_t>(mask));
        const long double mass = std::pow(pSlime, kSlime) * std::pow(1.0L - pSlime, cells - kSlime);

        // 精确 oracle：所有方块中心。
        int oracle = 0;
        int ox = 0, oz = 0;
        for (int cz = 0; cz < blocks; ++cz) {
            for (int cx = 0; cx < blocks; ++cx) {
                const size_t ci = static_cast<size_t>(cz) * blocks + cx;
                int cov = 0;
                for (int cc = 0; cc < cells; ++cc)
                    if (mask & (1ULL << cc)) cov += w[ci * cells + cc];
                if (cov > oracle) { oracle = cov; ox = cx; oz = cz; }
            }
        }

        // 方形预筛 Top-K（这里用 g=3 的小窗口，保持小图可算）。
        struct Seed { int score, cx, cz; };
        std::vector<Seed> seeds;
        const int half = s.square / 2;
        for (int cz = 0; cz < G; ++cz) {
            for (int cx = 0; cx < G; ++cx) {
                int score = 0;
                for (int dz = -half; dz <= half; ++dz)
                    for (int dx = -half; dx <= half; ++dx) {
                        const int x = cx + dx, z = cz + dz;
                        if (x < 0 || z < 0 || x >= G || z >= G) continue;
                        if (mask & (1ULL << (z * G + x))) ++score;
                    }
                seeds.push_back({ score, cx, cz });
            }
        }
        std::sort(seeds.begin(), seeds.end(), [](const Seed& a, const Seed& b) {
            return a.score > b.score;
        });

        const SlimeChunkPredicate pred = [&](int cx, int cz) {
            if (cx < 0 || cz < 0 || cx >= G || cz >= G) return false;
            return (mask & (1ULL << (cz * G + cx))) != 0;
        };
        AfkPlanOptions opt;
        opt.area = slime::Rect{ 0, G - 1, 0, G - 1 };
        opt.radius = s.R;
        opt.innerRadius = s.inner;
        opt.topN = 1;
        opt.minSeparation = 0;
        opt.coarseStep = 1;
        opt.maxWindowBlocks = 4096;
        const int take = std::min<int>(s.K, static_cast<int>(seeds.size()));
        for (int i = 0; i < take; ++i) opt.seedCandidates.emplace_back(seeds[i].cx, seeds[i].cz);
        const auto got = slime::AfkPlanner::plan(opt, pred, nullptr);
        const int gotBlocks = got.empty() ? 0 : static_cast<int>(got.front().coveredBlocks);

        if (gotBlocks < oracle) {
            ++failures;
            failMass += mass;
            if (printed < 20) {
                ++printed;
                std::printf("CE-FAIL mask=%llu slime=%d got=%d oracle=%d at=(%d,%d) seedK=%d mass=%.12Le\n",
                            static_cast<unsigned long long>(mask), kSlime, gotBlocks, oracle, ox, oz, take, mass);
            }
        }
    }

    const long double pWindow = failMass;
    // 仅作模型外推：把全图拆成互不重叠的 4×4 区块窗口。
    const long double windowsPerAxis = 3750000.0L / static_cast<long double>(G);
    const long double windows = windowsPerAxis * windowsPerAxis;
    long double pAnyFull = 0.0L;
    if (pWindow > 0.0L && pWindow < 1.0L)
        pAnyFull = 1.0L - std::exp(windows * std::log1p(-pWindow));

    std::printf("CE-SUMMARY G=%d R=%d inner=%d square=%d K=%d configs=%llu failures=%llu "
                "P_window=%.12Le P_full_windows=%.12Le\n",
                G, s.R, s.inner, s.square, s.K,
                static_cast<unsigned long long>(total), static_cast<unsigned long long>(failures),
                pWindow, pAnyFull);
    return 0;
}

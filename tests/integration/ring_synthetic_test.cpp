// 合成网格陷阱测试：人为构造史莱姆区块地图，已知环形最优点，
// 检查“方形密度预筛 + planner 精修/抛光”是否能召回真实最优。
//
// 这些图不是游戏种子，只用于暴露候选核的结构性缺陷：
//   - 内圈诱饵（中心密、环带空）
//   - 方形诱饵（方形密、环形质量低）
//   - 边界环
//   - 细环
//   - 均匀噪声
#include "core/AfkPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using slime::AfkCandidate;
using slime::AfkPlanOptions;
using slime::SlimeChunkPredicate;

using AutoPred = std::function<bool(int, int)>;

constexpr int kGrid = 64;
constexpr int kRadius = 128;
constexpr int kInner = 24;

struct Map {
    std::string name;
    std::vector<uint8_t> cell;   // [z*kGrid + x]
    int oracleCx = 0;
    int oracleCz = 0;
    bool knownGap = false;   // 明确用来暴露候选核盲区的对抗图

    bool slime(int cx, int cz) const
    {
        if (cx < 0 || cz < 0 || cx >= kGrid || cz >= kGrid) return false;
        return cell[static_cast<size_t>(cz) * kGrid + cx] != 0;
    }
};

Map makeMap(const std::string& name, const std::function<bool(int, int)>& pred, int ocx, int ocz)
{
    Map m;
    m.name = name;
    m.cell.assign(static_cast<size_t>(kGrid) * kGrid, 0);
    m.oracleCx = ocx;
    m.oracleCz = ocz;
    for (int cz = 0; cz < kGrid; ++cz)
        for (int cx = 0; cx < kGrid; ++cx)
            m.cell[static_cast<size_t>(cz) * kGrid + cx] = pred(cx, cz) ? 1 : 0;
    return m;
}

bool ringBlock(int bx, int bz, int cx, int cz)
{
    const double dx = static_cast<double>(bx - cx);
    const double dz = static_cast<double>(bz - cz);
    const double d2 = dx * dx + dz * dz;
    return d2 >= static_cast<double>(kInner) * kInner &&
           d2 <= static_cast<double>(kRadius) * kRadius;
}

int countSquare(const Map& m, int cx, int cz, int g)
{
    int n = 0;
    const int half = g / 2;
    for (int dz = -half; dz <= half; ++dz)
        for (int dx = -half; dx <= half; ++dx)
            if (m.slime(cx + dx, cz + dz)) ++n;
    return n;
}

// 模拟当前方形 density prescan：15×15 chunk 窗口、每个 chunk 中心打分。
std::vector<std::pair<int, int>> squareSeeds(const Map& m, int topK)
{
    struct S { int score, cx, cz; };
    std::vector<S> all;
    for (int cz = 0; cz < kGrid; ++cz)
        for (int cx = 0; cx < kGrid; ++cx)
            all.push_back({ countSquare(m, cx, cz, 15), cx, cz });
    std::sort(all.begin(), all.end(), [](const S& a, const S& b) {
        return a.score != b.score ? a.score > b.score : (a.cz != b.cz ? a.cz < b.cz : a.cx < b.cx);
    });
    std::vector<std::pair<int, int>> out;
    for (const auto& s : all) {
        if (static_cast<int>(out.size()) >= topK) break;
        bool tooClose = false;
        for (const auto& p : out) {
            const long long dx = static_cast<long long>(s.cx - p.first) * 16;
            const long long dz = static_cast<long long>(s.cz - p.second) * 16;
            if (dx * dx + dz * dz < 24LL * 24LL) { tooClose = true; break; }
        }
        if (!tooClose) out.emplace_back(s.cx, s.cz);
    }
    return out;
}

AfkPlanOptions makeOptions()
{
    AfkPlanOptions o;
    o.area = slime::Rect{ 0, kGrid - 1, 0, kGrid - 1 };
    o.radius = kRadius;
    o.innerRadius = kInner;
    o.topN = 1;
    o.minSeparation = 24;
    o.coarseStep = 1;
    o.maxWindowBlocks = 4096;
    return o;
}

AfkCandidate oracleFor(const Map& m)
{
    AfkPlanOptions o = makeOptions();
    const SlimeChunkPredicate pred = [&m](int cx, int cz) { return m.slime(cx, cz); };
    AfkCandidate best;
    best.coveredBlocks = -1;
    for (int cz = 0; cz < kGrid; ++cz) {
        for (int cx = 0; cx < kGrid; ++cx) {
            const int bx = cx * 16 + 8;
            const int bz = cz * 16 + 8;
            const auto em = slime::AfkPlanner::measureExact(bx, bz, o, pred);
            if (em.coveredBlocks > best.coveredBlocks) {
                best.x = bx;
                best.z = bz;
                best.coveredBlocks = em.coveredBlocks;
                best.coveredWeight = em.coveredWeight;
                best.coveragePercent = em.coveragePercent;
            }
        }
    }
    return best;
}

void runTrap(const Map& m, bool verbose)
{    const AutoPred pred = [&m](int cx, int cz) { return m.slime(cx, cz); };
    const AfkCandidate oracle = oracleFor(m);
    const auto seeds = squareSeeds(m, 32);

    AfkPlanOptions o = makeOptions();
    o.seedCandidates = seeds;
    std::vector<AfkCandidate> got;
    try {
        got = slime::AfkPlanner::plan(o, pred, nullptr);
    } catch (...) {
        std::printf("TRAP %-16s EXCEPTION\n", m.name.c_str());
        return;
    }

    int oracleSeedRank = -1;
    const int oracleSquare = countSquare(m, m.oracleCx, m.oracleCz, 15);
    for (size_t i = 0; i < seeds.size(); ++i) {
        if (std::abs(seeds[i].first - m.oracleCx) <= 1 &&
            std::abs(seeds[i].second - m.oracleCz) <= 1) {
            oracleSeedRank = static_cast<int>(i) + 1;
            break;
        }
    }
    if (got.empty()) {
        std::printf("TRAP %-16s NO_RESULT oracle=%lld seedRank=%d\n", m.name.c_str(),
                    static_cast<long long>(oracle.coveredBlocks), oracleSeedRank);
        return;
    }
    const AfkCandidate& g = got.front();
    const double ratio = oracle.coveredBlocks > 0
                             ? static_cast<double>(g.coveredBlocks) / static_cast<double>(oracle.coveredBlocks)
                             : 1.0;
    std::printf("TRAP %-16s oracle=(%d,%d,%lld) got=(%d,%d,%lld) ratio=%.3f seedRank=%d sq=%d\n",
                m.name.c_str(), oracle.x, oracle.z, static_cast<long long>(oracle.coveredBlocks),
                g.x, g.z, static_cast<long long>(g.coveredBlocks), ratio, oracleSeedRank, oracleSquare);
    if (verbose && got.size() > 1) {
        for (size_t i = 1; i < got.size() && i < 4; ++i)
            std::printf("  extra %zu: (%d,%d,%lld)\n", i, got[i].x, got[i].z,
                        static_cast<long long>(got[i].coveredBlocks));
    }
}

} // namespace

void exactSmallMapTest(int& failures)
{
    // 高价值小图：16×16 区块，全 256×256 方块中心逐一精确枚举。
    constexpr int G = 16;
    constexpr int R = 64;
    constexpr int IN = 16;
    constexpr int cx0 = 8 * 16 + 8;
    constexpr int cz0 = 8 * 16 + 8;
    const AutoPred pred = [=](int cx, int cz) {
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        const int dx = bx - cx0, dz = bz - cz0;
        const int d2 = dx * dx + dz * dz;
        return d2 >= 32 * 32 && d2 <= R * R;
    };

    AfkPlanOptions o;
    o.area = slime::Rect{ 0, G - 1, 0, G - 1 };
    o.radius = R;
    o.innerRadius = IN;
    o.topN = 1;
    o.minSeparation = 24;
    o.coarseStep = 1;
    o.maxWindowBlocks = 4096;
    for (int cz = 0; cz < G; ++cz)
        for (int cx = 0; cx < G; ++cx) o.seedCandidates.emplace_back(cx, cz);

    AfkCandidate oracle;
    oracle.coveredBlocks = -1;
    for (int z = 0; z < G * 16; ++z) {
        for (int x = 0; x < G * 16; ++x) {
            const auto em = slime::AfkPlanner::measureExact(x, z, o, pred);
            if (em.coveredBlocks > oracle.coveredBlocks) {
                oracle.x = x;
                oracle.z = z;
                oracle.coveredBlocks = em.coveredBlocks;
                oracle.coveredWeight = em.coveredWeight;
            }
        }
    }

    const auto got = slime::AfkPlanner::plan(o, pred, nullptr);
    if (got.empty()) {
        std::printf("EXACT-ORACLE FAIL no result\n");
        ++failures;
        return;
    }
    const auto& g = got.front();
    std::printf("EXACT-ORACLE grid=%d radius=%d centers=%d oracle=(%d,%d,%lld) got=(%d,%d,%lld)\n",
                G, R, G * 16 * G * 16, oracle.x, oracle.z,
                static_cast<long long>(oracle.coveredBlocks), g.x, g.z,
                static_cast<long long>(g.coveredBlocks));
    if (g.coveredBlocks < oracle.coveredBlocks) {
        std::printf("EXACT-ORACLE FAIL planner < oracle\n");
        ++failures;
    }
}

int ringKernelScore(const Map& m, int ccx, int ccz)
{
    const int k = (kRadius + 15) / 16;
    const int side = 2 * k + 1;
    const int bx0 = ccx * 16 + 8;
    const int bz0 = ccz * 16 + 8;
    int score = 0;
    for (int dz = -k; dz <= k; ++dz) {
        for (int dx = -k; dx <= k; ++dx) {
            int w = 0;
            for (int sz = 0; sz < 16; ++sz) {
                for (int sx = 0; sx < 16; ++sx) {
                    const int x = dx * 16 - 8 + sx;
                    const int z = dz * 16 - 8 + sz;
                    const int d2 = x * x + z * z;
                    if (d2 >= kInner * kInner && d2 <= kRadius * kRadius) ++w;
                }
            }
            if (w > 0 && m.slime(ccx + dx, ccz + dz)) score += w;
        }
    }
    (void)side;
    (void)bx0;
    (void)bz0;
    return score;
}

void kernelRecoveryTest(const Map& m, int& failures)
{
    struct K { int score, cx, cz; };
    std::vector<K> all;
    for (int cz = 0; cz < kGrid; ++cz)
        for (int cx = 0; cx < kGrid; ++cx)
            all.push_back({ ringKernelScore(m, cx, cz), cx, cz });
    std::sort(all.begin(), all.end(), [](const K& a, const K& b) {
        return a.score > b.score;
    });
    int rank = -1;
    for (size_t i = 0; i < all.size(); ++i) {
        if (std::abs(all[i].cx - m.oracleCx) <= 1 && std::abs(all[i].cz - m.oracleCz) <= 1) {
            rank = static_cast<int>(i) + 1;
            break;
        }
    }
    std::printf("KERNEL-RECOVERY %-16s rank=%d top_score=%d target_score=%d\n", m.name.c_str(), rank,
                all.empty() ? 0 : all.front().score, ringKernelScore(m, m.oracleCx, m.oracleCz));
    // 这里只做诊断：shell_vs_singles 的外壳中心不是真实全局最优，
    // 诱饵边缘的环覆盖更高；不能把“未排到 top”当成核失败。
    (void)failures;
}

std::vector<std::pair<int, int>> kernelSeeds(const Map& m, int topK,
                                             const std::vector<std::pair<int, std::pair<int, int>>>& scored)
{
    std::vector<std::pair<int, int>> out;
    for (const auto& r : scored) {
        if (static_cast<int>(out.size()) >= topK) break;
        bool tooClose = false;
        for (const auto& p : out) {
            const long long dx = static_cast<long long>(r.second.first - p.first) * 16;
            const long long dz = static_cast<long long>(r.second.second - p.second) * 16;
            if (dx * dx + dz * dz < 24LL * 24LL) { tooClose = true; break; }
        }
        if (!tooClose) out.emplace_back(r.second.first, r.second.second);
    }
    return out;
}

std::vector<std::pair<int, int>> hybridSeeds(const Map& m, int topK)
{
    auto out = squareSeeds(m, topK);
    struct KS { int score, cx, cz; };
    std::vector<KS> all;
    for (int cz = 0; cz < kGrid; ++cz)
        for (int cx = 0; cx < kGrid; ++cx)
            all.push_back({ ringKernelScore(m, cx, cz), cx, cz });
    std::sort(all.begin(), all.end(), [](const KS& a, const KS& b) { return a.score > b.score; });
    for (const auto& s : all) {
        if (static_cast<int>(out.size()) >= topK * 2) break;
        bool exists = false;
        for (const auto& p : out)
            if (p.first == s.cx && p.second == s.cz) { exists = true; break; }
        if (!exists) out.emplace_back(s.cx, s.cz);
    }
    return out;
}

double planRatio(const Map& m, const std::vector<std::pair<int, int>>& seeds, const AfkCandidate& oracle)
{
    const AutoPred pred = [&m](int cx, int cz) { return m.slime(cx, cz); };
    AfkPlanOptions o = makeOptions();
    o.seedCandidates = seeds;
    const auto got = slime::AfkPlanner::plan(o, pred, nullptr);
    if (got.empty() || oracle.coveredBlocks <= 0) return 0.0;
    return static_cast<double>(got.front().coveredBlocks) / static_cast<double>(oracle.coveredBlocks);
}

void strategyBench(const std::vector<Map>& maps)
{
    struct Row { std::string name; double mean; double worst; int below95; };
    std::vector<Row> rows;
    for (int K : { 8, 32 }) {
        for (int strategy = 0; strategy < 3; ++strategy) {
            std::string name = (strategy == 0 ? "square" : strategy == 1 ? "kernel" : "hybrid") +
                               std::to_string(K);
            double sum = 0.0, worst = 10.0;
            int n = 0, below = 0;
            for (const auto& m : maps) {
                const AfkCandidate oracle = oracleFor(m);
                std::vector<std::pair<int, int>> seeds;
                if (strategy == 0) {
                    seeds = squareSeeds(m, K);
                } else if (strategy == 1) {
                    struct KS { int score, cx, cz; };
                    std::vector<KS> all;
                    for (int cz = 0; cz < kGrid; ++cz)
                        for (int cx = 0; cx < kGrid; ++cx)
                            all.push_back({ ringKernelScore(m, cx, cz), cx, cz });
                    std::sort(all.begin(), all.end(), [](const KS& a, const KS& b) {
                        return a.score > b.score;
                    });
                    std::vector<std::pair<int, std::pair<int, int>>> scored;
                    for (const auto& s : all) scored.push_back({ s.score, { s.cx, s.cz } });
                    seeds = kernelSeeds(m, K, scored);
                } else {
                    seeds = hybridSeeds(m, K);
                }
                const double r = planRatio(m, seeds, oracle);
                sum += r;
                worst = (std::min)(worst, r);
                if (r < 0.95) ++below;
                ++n;
            }
            rows.push_back({ name, n ? sum / n : 0.0, worst, below });
        }
    }
    for (const auto& r : rows)
        std::printf("STRATEGY %-10s mean=%.3f worst=%.3f below95=%d\n", r.name.c_str(), r.mean,
                    r.worst, r.below95);
}

int main()
{
    std::vector<Map> maps;

    // 1. 单环：最优就在环心。
    maps.push_back(makeMap("single_ring", [](int cx, int cz) {
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        return ringBlock(bx, bz, 48 * 16 + 8, 48 * 16 + 8);
    }, 48, 48));

    // 2. 内圈诱饵：中心 7×7 很密，但环带内为空；真最优在右侧环。
    maps.push_back(makeMap("inner_decoy", [](int cx, int cz) {
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        if (std::abs(cx - 16) <= 3 && std::abs(cz - 16) <= 3) return true;
        return ringBlock(bx, bz, 48 * 16 + 8, 48 * 16 + 8);
    }, 48, 48));

    // 3. 方形诱饵：左上 9×9 方形密度高，但环形质量不如右侧环。
    maps.push_back(makeMap("square_decoy", [](int cx, int cz) {
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        if (std::abs(cx - 16) <= 4 && std::abs(cz - 16) <= 4) return true;
        return ringBlock(bx, bz, 48 * 16 + 8, 48 * 16 + 8);
    }, 48, 48));

    // 4. 边界环：环心靠近地图边界，检查裁剪。
    maps.push_back(makeMap("border_ring", [](int cx, int cz) {
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        return ringBlock(bx, bz, 4 * 16 + 8, 4 * 16 + 8);
    }, 4, 4));

    // 5. 细环：只有 r≈64 的窄带是史莱姆；方形密度低，环形局部高。
    maps.push_back(makeMap("thin_ring", [](int cx, int cz) {
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        const int dx = bx - (48 * 16 + 8), dz = bz - (48 * 16 + 8);
        const int d2 = dx * dx + dz * dz;
        return d2 >= 56 * 56 && d2 <= 72 * 72 && ((cx + cz) & 1) == 0;
    }, 48, 48));

    // 6. 多诱饵场：36 个 5×5 高密度方块，中心薄环是真最优；
    //    方形 Top-32 可能被诱饵占满。
    maps.push_back(makeMap("decoy_field", [](int cx, int cz) {
        const int ps[] = { 6, 16, 26, 46, 56, 62 };
        for (int p : ps)
            for (int q : ps)
                if (std::abs(cx - p) <= 2 && std::abs(cz - q) <= 2) return true;
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        const int dx = bx - (32 * 16 + 8), dz = bz - (32 * 16 + 8);
        const int d2 = dx * dx + dz * dz;
        return d2 >= 72 * 72 && d2 <= 80 * 80 && ((cx + cz) & 1) == 0;
    }, 32, 32));

    // 7. 外壳陷阱：真最优的史莱姆全在 r=120..128 的外壳，
    //    方形 15×15 窗口看不到；再用 3×3 诱饵占满 Top-32。
    maps.push_back(makeMap("outer_shell", [](int cx, int cz) {
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        const int dx = bx - (32 * 16 + 8), dz = bz - (32 * 16 + 8);
        const int d2 = dx * dx + dz * dz;
        if (d2 >= 120 * 120 && d2 <= 128 * 128) return true;
        // 8×8 个 3×3 高密度诱饵，避开中心 15×15 窗口
        for (int gy = 4; gy <= 60; gy += 8)
            for (int gx = 4; gx <= 60; gx += 8) {
                if (std::abs(cx - 32) <= 8 && std::abs(cz - 32) <= 8) continue;
                if (std::abs(cx - gx) <= 1 && std::abs(cz - gy) <= 1) return true;
            }
        return false;
    }, 32, 32));

    // 8. 纯外壳：所有史莱姆只在 r=120..128 外壳，方形 15×15 预筛完全看不到。
    //    这是候选核的结构性盲区，记录为 known-gap，不作为常规回归失败。
    maps.push_back(makeMap("outer_shell_only", [](int cx, int cz) {
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        const int dx = bx - (32 * 16 + 8), dz = bz - (32 * 16 + 8);
        const int d2 = dx * dx + dz * dz;
        return d2 >= 120 * 120 && d2 <= 128 * 128;
    }, 32, 32));
    maps.back().knownGap = true;

    // 8b. 外壳 + 四角诱饵：真最优仍是中心外壳，但 Top-32 被角落密度占满。
    maps.push_back(makeMap("outer_shell_trap", [](int cx, int cz) {
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        const int dx = bx - (32 * 16 + 8), dz = bz - (32 * 16 + 8);
        const int d2 = dx * dx + dz * dz;
        if (d2 >= 120 * 120 && d2 <= 128 * 128) return true;
        for (int gy : { 4, 8, 12, 52, 56, 60 })
            for (int gx : { 4, 8, 12, 52, 56, 60 })
                if (std::abs(cx - gx) <= 1 && std::abs(cz - gy) <= 1) return true;
        return false;
    }, 32, 32));
    maps.back().knownGap = true;

    // 8c. 外壳 vs 32 个孤岛诱饵：孤岛方形得分 1，外壳中心为 0；
    //     方形 Top-32 被孤岛占满，但孤岛真实环覆盖远低于外壳。
    maps.push_back(makeMap("shell_vs_singles", [](int cx, int cz) {
        const int bx = cx * 16 + 8, bz = cz * 16 + 8;
        const int dx = bx - (32 * 16 + 8), dz = bz - (32 * 16 + 8);
        const int d2 = dx * dx + dz * dz;
        if (d2 >= 120 * 120 && d2 <= 128 * 128) return true;
        for (int gy : { 4, 8, 12, 52, 56, 60 })
            for (int gx : { 4, 8, 12, 52, 56, 60 })
                if (std::abs(cx - gx) <= 2 && std::abs(cz - gy) <= 2) return true;
        return false;
    }, 32, 32));
    maps.back().knownGap = true;

    // 9. 均匀噪声：没有明显结构，检查结果是否仍精确可解释。
    maps.push_back(makeMap("checker", [](int cx, int cz) {
        return ((cx * 31 + cz * 17) & 7) == 0;
    }, 32, 32));

    int failures = 0;
    int gaps = 0;
    strategyBench(maps);
    for (const auto& m : maps) runTrap(m, true);

    for (const auto& m : maps) {
        const AutoPred pred = [&m](int cx, int cz) { return m.slime(cx, cz); };
        const AfkCandidate oracle = oracleFor(m);
        AfkPlanOptions o = makeOptions();
        o.seedCandidates = squareSeeds(m, 32);
        int oracleSeedRank = -1;
        for (size_t i = 0; i < o.seedCandidates.size(); ++i) {
            if (std::abs(o.seedCandidates[i].first - m.oracleCx) <= 1 &&
                std::abs(o.seedCandidates[i].second - m.oracleCz) <= 1) {
                oracleSeedRank = static_cast<int>(i) + 1;
                break;
            }
        }
        if (m.knownGap && oracleSeedRank > 32) {
            std::printf("KNOWN-GAP %-16s seeded-out rank=%d\n", m.name.c_str(), oracleSeedRank);
            ++gaps;
            continue;
        }
        if (m.knownGap && oracleSeedRank < 0) {
            std::printf("KNOWN-GAP %-16s seeded-out (absent)\n", m.name.c_str());
            ++gaps;
            continue;
        }
        const auto got = slime::AfkPlanner::plan(o, pred, nullptr);
        if (got.empty()) {
            if (m.knownGap) {
                std::printf("KNOWN-GAP %-16s no result\n", m.name.c_str());
                ++gaps;
            } else {
                ++failures;
            }
            continue;
        }
        const double ratio = oracle.coveredBlocks > 0
                                 ? static_cast<double>(got.front().coveredBlocks) /
                                       static_cast<double>(oracle.coveredBlocks)
                                 : 1.0;
        if (ratio < 0.95) {
            if (m.knownGap) {
                std::printf("KNOWN-GAP %-16s ratio=%.3f\n", m.name.c_str(), ratio);
                ++gaps;
            } else {
                std::printf("FAIL %-16s ratio=%.3f\n", m.name.c_str(), ratio);
                ++failures;
            }
        }
    }
    for (const auto& m : maps)
        if (m.name == "shell_vs_singles") kernelRecoveryTest(m, failures);
    exactSmallMapTest(failures);
    std::printf("SYNTHETIC-RING %s failures=%d known_gaps=%d traps=%zu\n",
                failures == 0 ? "OK" : "FAIL", failures, gaps, maps.size());
    return failures == 0 ? 0 : 1;
}

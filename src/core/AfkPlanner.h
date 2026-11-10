// v3.5.2（任务 1）：环形覆盖挂机点规划（精度 1 格 = 1/16 区块）
//
// 为什么需要：史莱姆农场要「站对位置」—— 玩家附近 R 格（默认 128）内的史莱姆区块面积越大，
// 可刷怪的格数越多、效率越高。史莱姆区块是 16x16，所以最优站位常常落在**区块角点**上
// （同时覆盖 4 个区块）；只按「区块中心」给建议会漏掉这些更优的点。
//
// 算法（环形 = 以候选点为圆心、半径 R 的圆）：
//   1) 在候选窗口内按 1 格精度铺出史莱姆区块的占用图；
//   2) 做二维前缀和（积分图）；
//   3) 圆内求和：对每一行 dz∈[-R,R]，用圆方程算出该行的半宽 w=floor(√(R²-dz²))，
//      一次 rowSum(x-w .. x+w) 即可 —— 每个候选点 O(R)；
//   4) 先按 4 格步长粗扫，再对前若干名在 ±3 格内按 1 格精修 ⇒ 既保证 1/16 区块精度，又不慢。
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/SlimeEngine.h" // Rect / Version

namespace slime {

class BiomeMap;

// 一个挂机点候选（坐标为方块坐标 = 格）
struct AfkCandidate {
    int x = 0;                    // 挂机点方块坐标 X
    int z = 0;                    // 挂机点方块坐标 Z
    int64_t coveredBlocks = 0;    // 圆内史莱姆区块格数（每区块按 16x16 = 256 格计）
    int coveredChunks = 0;        // 圆内触及的史莱姆区块数
    double coveragePercent = 0.0; // coveredBlocks / (πR²) × 100
    bool onChunkCorner = false;   // 是否正落在区块角点（x、z 都是 16 的倍数）
    int chunkX = 0;               // 所在区块坐标（便于对照地图）
    int chunkZ = 0;
    double coveredWeight = 0.0;   // v3.5.2（任务 3）：加权覆盖（= Σ 每格权重），未启用加权时等于 coveredBlocks
    double avgWeight = 0.0;       // 覆盖格的平均权重（>1 表示沼泽类加成）
    double centroidX = 0.0;       // 覆盖到的史莱姆区块的几何重心（方块坐标，便于地图预览）
    double centroidZ = 0.0;
    double centerDist = 0.0;      // 该点到重心的距离（打平覆盖面积时的取点依据）
};

struct AfkPlanOptions {
    Rect area;                  // 候选挂机点范围（区块坐标，含边界）
    int radius = 128;           // 外半径（方块）：生物刷新的最大距离（默认 128）
    int innerRadius = 0;        // v3.5.8 内半径：过近不刷（环形模式由 UI 传 24；0 = 整圆）
    double minDensity = 0.0;    // v3.5.8 最小密度（环带内覆盖率 0~1），低于此值的候选被过滤
    int topN = 5;               // 返回前 N 个
    int minSeparation = 24;     // 候选点之间的最小间距（方块），避免给出几乎重合的点
    int coarseStep = 4;         // 粗扫步长（方块）；最终结果仍是 1 格精度
    int maxWindowBlocks = 2048; // 计算窗口上限（防止超大范围爆内存）
    // v3.5.13：强制走「区块级粗扫」路径（单元测试用来对照精确路径的质量；默认 false）
    bool forceCoarse = false;
    // v3.5.17：环形扫描模式 —— 0 = 自动（大范围按需放大粗扫步长，快速）；
    //   1 = 强制逐区块（精确，超大范围会很慢）；>1 = 显式指定粗扫步长（诊断用）
    int scanStride = 0;
    // v3.5.18：严格统计的行带数（并行度/开销权衡；高级调试参数）
    int statBands = 4;
    // v3.7.6：可选 GPU 覆盖计算（为空 / 不可用 / 区域太小 / 加权模式 ⇒ 走 CPU）
    const class GpuScan* gpuScan = nullptr;
    // v3.10.12.11：正方形密度预筛得到的种子（区块坐标）。非空时跳过粗扫/抽样，直接精修这些种子。
    std::vector<std::pair<int, int>> seedCandidates;

    // v3.5.2（任务 3）：群系权重回调（方块坐标 → 权重，0 = 该群系不刷史莱姆）。
    // 为空 = 纯面积模式；非空 = 按加权覆盖排序，且计算窗口收窄到 1024 方块。
    std::function<double(int blockX, int blockZ)> weightAt;
    // v3.5.15：可选的「整行批量判定」回调（cz, cx0, n, out）—— 提供时占用图按行走它，
    //   比逐点 isSlime 快数倍（Java 版直接用 SlimeEngine::fillSlimeRowJava）。须线程安全。
    std::function<void(int cz, int cx0, int n, uint8_t* out)> rowSlime;
};

// 判定函数：该区块是否史莱姆区块（可来自引擎，也可来自缓存）
using SlimeChunkPredicate = std::function<bool(int chunkX, int chunkZ)>;

class AfkPlanner {
public:
    // 规划挂机点（按覆盖面积降序，最多 topN 个；area 非法/超出窗口上限时返回空）
    static std::vector<AfkCandidate> plan(const AfkPlanOptions& opt, const SlimeChunkPredicate& isSlime);

    // 便捷入口：按游戏版本 + 世界种子判定
    static std::vector<AfkCandidate> planForSeed(const AfkPlanOptions& opt, Version version, int64_t worldSeed);

    // 统计区块矩形内史莱姆区块的格数（1 格精度，供 UI/测试展示）
    static int64_t slimeBlocksInArea(const Rect& chunkArea, const SlimeChunkPredicate& isSlime);

    // v3.5.9：严格 1 格统计 —— 逐格遍历环带 [inner, outer]，面积与总权重都是精确值
    //   weightAt 为空时按 1.0 计（总权重 = 有效格数）
    struct ExactMetrics {
        int64_t coveredBlocks = 0;   // 环带内史莱姆格数
        double coveredWeight = 0.0;  // 这些格的权重之和（0 权重群系不计入）
        int coveredChunks = 0;       // 环带触及的史莱姆区块数
        double coveragePercent = 0.0; // coveredBlocks / 环带面积 × 100
    };
    static ExactMetrics measureExact(int blockX, int blockZ, const AfkPlanOptions& opt,
                                     const SlimeChunkPredicate& isSlime);

    // v3.5.14：只统计 dz ∈ [dzFrom, dzTo] 的行带（多线程分行带并行累加；各带相加 == 整体结果）。
    //   countChunks 只有一带设为 true（区块计数与行带无关，重复算会多计）。
    static ExactMetrics measureExactRows(int blockX, int blockZ, const AfkPlanOptions& opt,
                                         const SlimeChunkPredicate& isSlime, int dzFrom, int dzTo,
                                         bool countChunks);

    // v3.5.14：对候选列表做「严格 1 格」重算（候选 × 行带 摊成任务并行走；每线程自带一个 BiomeMap）。
    //   useBiome = false（基岩版/无群系）时按 1.0 权重统计。结果写回 cands 的统计字段。
    struct StrictStatsInfo {
        size_t samples = 0;   // 实际采样的 1:4 格子数
        size_t cells = 0;     // 窗口总格子数
    };
    static StrictStatsInfo measureStrictForCandidates(std::vector<AfkCandidate>& cands, const AfkPlanOptions& opt,
                                                      const SlimeChunkPredicate& isSlime, int javaVersionId,
                                                      int64_t seed, bool useBiome);

    // 某点的圆内覆盖（格 / 区块数），供地图预览与自检复用
    static AfkCandidate evaluate(int blockX, int blockZ, int radius, const SlimeChunkPredicate& isSlime);

    // 暴力参考实现：逐候选点、逐格求和（仅用于单元测试对照，慢）
    static std::vector<AfkCandidate> planBruteForce(const AfkPlanOptions& opt, const SlimeChunkPredicate& isSlime);

    // v3.5.6：大范围（远距离找点）专用 —— 区块级粗扫 + 局部 1 格精修。
    // 半径 R 始终是「生物刷新半径」（固定），area 只是允许放置挂机点的搜索范围。
    static std::vector<AfkCandidate> planChunkCoarse(const AfkPlanOptions& opt, const SlimeChunkPredicate& isSlime);

    // 粗扫阶段建议的步长（区块）：超大范围时自动 >1，最终点仍是 1 格精度
    static int coarseChunkStride(int spanChunks);

    // v3.5.14：规划器使用的线程池大小（= 逻辑核心数，与 OpenMP 的 ICV 无关）
    static int threadPoolSize() noexcept;

    // v3.5.14：排序用权重网格的取样比例（1:64 / 1:128 / 1:256）。
    //   只影响「候选排序」的粗糙度（最终统计仍是 1:4 逐格精确），大范围时自动放宽以控住采样量。
    static int rankingGridScale(int spanChunks) noexcept;

private:
    // v3.5.13：块级精确路径（方块分辨率前缀和 + 粗扫 + ±step 精修）。
    // 由 plan() 在「无加权的小范围」时调用；planChunkCoarse 的抛光步骤也直接调它（不再经过路由，
    // 因此不会出现 plan ⇄ planChunkCoarse 的相互递归）。
    static std::vector<AfkCandidate> planExactBlockLevel(const AfkPlanOptions& opt,
                                                         const SlimeChunkPredicate& isSlime);
};

} // namespace slime

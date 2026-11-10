#pragma once

// v3.9.10（流程优化/设置重构 阶段 A）：把「结果要求」与「运行环境」拆开。
//
//   SearchPlan  —— 只含**会改变结果**的开关（模式、目标数量、阈值/图案、停止策略、精度算法、最优候选…）。
//                  快捷方案（预设）只允许设置这些字段；用户改动任何一项 ⇒ 方案变为「自定义」。
//   RuntimeEnv  —— 只含**不影响结果**的开关（GPU、线程、内存、缓存、进度回调指针、种子/版本）。
//                  方案/预设**不读写**这里任何字段；切换方案不得改变 GPU 等开关状态。
//
// toOptions() 是两者合成 SearchOptions 的**唯一映射**（单测覆盖，保证与直接构造逐字段一致）。
// 说明：本文件为 3.9.10 的模型骨架；预设表/冲突裁决/检测在 3.9.11 接入 UI。

#include "core/SlimeEngine.h"

#include <atomic>
#include <cstdint>

namespace slime {

// 结果要求（方案控制）
struct SearchPlan {
    // 范围与起点（区块坐标，含边界）
    int minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    int64_t startX = 0;
    int64_t startZ = 0;

    // 模式与目标
    Mode mode = Mode::Strict;   // 与 SearchOptions 默认一致（UI 预设会显式设为密度）
    int64_t targetCount = 50;
    bool stopAtTarget = false; // 达到目标数量即停（无损）
    bool smartRange = false;   // 由近及远

    // 密度模式
    int gridSize = 5;
    double minDensity = 0.3;
    // 最优候选模式（3.9.12 落地）：忽略阈值，按密度取最优 N 个。
    // 引擎侧在 3.9.10 尚未消费该字段；UI 先按「冲突裁决」置灰阈值/早停。
    bool ignoreThreshold = false;

    // 图案模式（严格匹配；无匹配要求开关）
    Pattern pattern;
    bool directionLocked = true;

    // 精度算法（会改变结果，因此属于结果要求；放高级区）
    Algorithm algorithm = Algorithm::Exact;
    int layerBlock = 16;
    double layerRatio = 0.8;
    int sampleStep = 8;
    bool sampleExact = false;
};

// 运行环境（只影响速度/资源；方案不可见、不可改）
struct RuntimeEnv {
    Version version = Version::Java;
    int64_t worldSeed = 0;

    // GPU（独立开关，见 3.9.11 方案与加速正交）
    const GpuScan* gpuScan = nullptr;
    bool gpuEnabled = false;
    bool gpuPatternStream = false;
    int gpuLimitPercent = 100;

    // 资源
    int threads = 0;
    int cpuLimitPercent = 100;
    int64_t memBudgetBytes = 0;

    // 缓存（基岩）
    bool useCache = false;
    bool buildMode = false;
    const CacheManager* cache = nullptr;
    CacheManager* cacheWrite = nullptr;
    int64_t cacheBudgetBytes = 0;
    bool cacheWriteDisabled = false;
    std::mutex* cacheReadMu = nullptr;
    int collectSkipZ0 = 0;
    int collectSkipZ1 = -1;

    // 进度/统计回传（只做展示，不参与判定）
    std::atomic<int64_t>* liveMatches = nullptr;
    std::atomic<int64_t>* liveScanned = nullptr;
    std::atomic<int64_t>* liveTotal = nullptr;
    std::atomic<int64_t>* liveRecorded = nullptr;
    std::atomic<int64_t>* liveSkippedCovered = nullptr;
    std::atomic<int>* livePhase = nullptr;
};

// 唯一映射：SearchPlan + RuntimeEnv → SearchOptions。
// 断言（单测）：默认值构造出的结果与 SearchOptions 默认值逐字段一致；
//             RuntimeEnv 的 GPU/线程等字段只影响对应选项，不与 SearchPlan 字段交叉。
inline SearchOptions toOptions(const SearchPlan& plan, const RuntimeEnv& env) noexcept
{
    SearchOptions o;
    // ── 运行环境（先写，结果要求字段不依赖它） ──
    o.version = env.version;
    o.worldSeed = env.worldSeed;
    o.gpuScan = env.gpuEnabled ? env.gpuScan : nullptr;
    o.threads = env.threads;
    o.cpuLimitPercent = env.cpuLimitPercent;
    o.memBudgetBytes = env.memBudgetBytes;
    o.useCache = env.useCache;
    o.buildMode = env.buildMode;
    o.cache = env.cache;
    o.cacheWrite = env.cacheWrite;
    o.cacheBudgetBytes = env.cacheBudgetBytes;
    o.cacheWriteDisabled = env.cacheWriteDisabled;
    o.cacheReadMu = env.cacheReadMu;
    o.collectSkipZ0 = env.collectSkipZ0;
    o.collectSkipZ1 = env.collectSkipZ1;
    o.liveMatches = env.liveMatches;
    o.liveScanned = env.liveScanned;
    o.liveTotal = env.liveTotal;
    o.liveRecorded = env.liveRecorded;
    o.liveSkippedCovered = env.liveSkippedCovered;
    o.livePhase = env.livePhase;

    // ── 结果要求 ──
    o.minX = plan.minX;
    o.maxX = plan.maxX;
    o.minZ = plan.minZ;
    o.maxZ = plan.maxZ;
    o.startX = plan.startX;
    o.startZ = plan.startZ;
    o.mode = plan.mode;
    o.targetCount = plan.targetCount;
    o.stopAtTarget = plan.stopAtTarget;
    o.smartRange = plan.smartRange;
    o.gridSize = plan.gridSize;
    o.minDensity = plan.minDensity;
    o.pattern = plan.pattern;
    o.directionLocked = plan.directionLocked;
    o.ignoreThreshold = plan.ignoreThreshold;
    o.algorithm = plan.algorithm;
    o.layerBlock = plan.layerBlock;
    o.layerRatio = plan.layerRatio;
    o.sampleStep = plan.sampleStep;
    o.sampleExact = plan.sampleExact;
    return o;
}

} // namespace slime

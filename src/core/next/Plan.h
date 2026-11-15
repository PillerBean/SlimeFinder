// 新框架计划模型（只定义数据，不实现执行）
// 目的：把主分支里分散在 runImpl/runBanded/runChunked/buildMode 的策略显式化。
#pragma once

#include "core/SlimeEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace slime::next {

enum class PlanFamily : uint8_t {
    Search,       // 普通搜索：扫描 + 匹配 + 结果
    CacheBuild,   // 只判定 + 写缓存，不匹配
    Analytic,     // 零阈值/最优候选等解析路径
    Banded,       // 外层按 band 加载缓存，内层是 SearchPlan
    Ring,         // 环形/AFK 规划（独立于全图搜索）
};

enum class DeterminerKind : uint8_t {
    Cpu,
    Gpu,
    Cache,   // 直接从缓存位图填充
    None,    // 融合 GPU/解析路径不需要独立判定
    Sample,  // 采样只生成稀疏位图
};

enum class MatcherKind : uint8_t {
    CpuPattern,
    CpuDensity,
    FusedGpuPattern,
    FusedGpuDensity,
    Analytic,
    None,    // 缓存构建
};

enum class CollectorKind : uint8_t {
    DistanceTopN,   // d2 升序，rank 平局
    DensityTopN,    // 窗口史莱姆数降序，再距离/rank
    CountOnly,      // 构建模式只需要计数
};

enum class CacheMode : uint8_t {
    None,
    ReadOnly,
    BuildWrite,
    BandRead,
};

enum class PartitionKind : uint8_t {
    Single,   // 经典整幅位图
    Chunked,  // 大范围 z 分块
    Banded,   // 缓存 band + halo
};

struct PartitionPlan {
    PartitionKind kind = PartitionKind::Single;
    int64_t rowsPerChunk = 0;
    int64_t chunkConcurrency = 1;
    int bandSize = 8192;
    int halo = 0;
    bool cacheFirst = false;
};

struct ProgressPlan {
    // 与现有 GUI 兼容：普通搜索 0~0.75 扫描、0.75~1 匹配；
    // 构建/分带可覆盖阶段名和权重。
    double scanEnd = 0.75;
    bool hasMatchPhase = true;
    bool reportsRecorded = false; // 构建模式 liveRecorded
    bool reportsSkipped = false;  // 构建模式 liveSkippedCovered
    bool reportsPhase = false;    // livePhase（缓存/新判定）
};

struct ResourcePlan {
    int threads = 0;
    int cpuLimitPercent = 100;
    int64_t memBudgetBytes = 0;
    int64_t cacheBudgetBytes = 0;
    int64_t targetCount = 50;
};

struct ExecutionPlan {
    PlanFamily family = PlanFamily::Search;
    PartitionPlan partition;
    DeterminerKind determiner = DeterminerKind::Cpu;
    MatcherKind matcher = MatcherKind::CpuDensity;
    CollectorKind collector = CollectorKind::DistanceTopN;
    CacheMode cache = CacheMode::None;
    ProgressPlan progress;
    ResourcePlan resources;

    // 只有 Materialize 计划才产生 Bitmap；Fused/Analytic 直接产出命中。
    bool requiresBitmap = true;
    bool mayReuseBitmap = false;
    bool allowGpu = true;

    // 外层 Banded/Ring 计划可持有子计划描述；执行器负责组合。
    std::vector<ExecutionPlan> children;
};

struct Job {
    slime::SearchOptions options;
    std::string tag;
};

struct MachineCaps {
    bool gpuAvailable = false;
    bool gpuIsAmd = false;
    int64_t patternGpuMinChunks = int64_t{ 16 } << 10;
    int64_t densityGpuMinChunks = int64_t{ 64 } << 10;
    int64_t classicChunkLimit = int64_t{ 64 } << 20; // 经典位图区块上限（可调）
};

struct PlannerInput {
    slime::SearchOptions options;
    MachineCaps caps;
    bool cacheCovers = false; // 当前范围是否已有完整缓存位图
};

// 只读 Planner：把 SearchOptions + 能力/缓存状态变成 ExecutionPlan。
// 不访问 GPU、不扫位图、不产生结果。
ExecutionPlan makeExecutionPlan(const PlannerInput& input);

} // namespace slime::next

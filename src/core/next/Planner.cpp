#include "core/next/Plan.h"

#include <algorithm>

namespace slime::next {

namespace {

int64_t areaOf(const slime::SearchOptions& o)
{
    const int64_t w = static_cast<int64_t>(o.maxX) - o.minX + 1;
    const int64_t h = static_cast<int64_t>(o.maxZ) - o.minZ + 1;
    if (w <= 0 || h <= 0) return 0;
    return w * h;
}

bool useGpuDensityPlan(const PlannerInput& in, int64_t total)
{
    const auto& o = in.options;
    if (!in.caps.gpuAvailable || !o.gpuScan || o.useCache || o.buildMode) return false;
    if (total < in.caps.densityGpuMinChunks) return false;
    if (o.algorithm == slime::Algorithm::Sample) return false;
    if (o.algorithm == slime::Algorithm::Exact) return true;
    return o.algorithm == slime::Algorithm::Layered && o.layerRatio <= 1.0;
}

bool useGpuPatternPlan(const PlannerInput& in, int64_t total)
{
    const auto& o = in.options;
    if (!in.caps.gpuAvailable || !in.caps.gpuIsAmd || !o.gpuScan) return false;
    if (o.mode != slime::Mode::Strict || !o.directionLocked || o.useCache || o.buildMode) return false;
    return total >= in.caps.patternGpuMinChunks;
}

void fillResources(ExecutionPlan& p, const slime::SearchOptions& o)
{
    p.resources.threads = o.threads;
    p.resources.cpuLimitPercent = o.cpuLimitPercent;
    p.resources.memBudgetBytes = o.memBudgetBytes;
    p.resources.cacheBudgetBytes = o.cacheBudgetBytes;
    p.resources.targetCount = o.targetCount;
}

ExecutionPlan makeSearchPlan(const PlannerInput& in, int64_t total)
{
    const auto& o = in.options;
    ExecutionPlan p;
    p.family = PlanFamily::Search;
    fillResources(p, o);
    p.progress.scanEnd = 0.75;
    p.progress.hasMatchPhase = true;
    p.allowGpu = true;

    // 完整缓存覆盖：跳过 GPU 重算，直接缓存位图。
    if (o.useCache && o.version == slime::Version::Bedrock && in.cacheCovers) {
        p.determiner = DeterminerKind::Cache;
        p.cache = CacheMode::ReadOnly;
        p.allowGpu = false;
        p.requiresBitmap = true;
    } else if (o.mode == slime::Mode::Strict && useGpuPatternPlan(in, total)) {
        p.determiner = DeterminerKind::None;
        p.matcher = MatcherKind::FusedGpuPattern;
        p.requiresBitmap = false;
    } else if (o.mode == slime::Mode::Strict) {
        p.determiner = DeterminerKind::Cpu;
        p.matcher = MatcherKind::CpuPattern;
        p.requiresBitmap = true;
    } else if (useGpuDensityPlan(in, total)) {
        p.determiner = DeterminerKind::None;
        p.matcher = MatcherKind::FusedGpuDensity;
        p.requiresBitmap = false;
    } else {
        p.determiner = (in.caps.gpuAvailable && !o.useCache && total >= in.caps.densityGpuMinChunks)
                           ? DeterminerKind::Gpu
                           : DeterminerKind::Cpu;
        p.matcher = MatcherKind::CpuDensity;
        p.requiresBitmap = true;
    }

    if (o.mode == slime::Mode::Density && o.algorithm == slime::Algorithm::Sample) {
        p.determiner = DeterminerKind::Sample;
        p.matcher = MatcherKind::CpuDensity;
        p.requiresBitmap = true;
        p.allowGpu = false;
    }

    if (o.useCache && o.version == slime::Version::Bedrock) p.cache = CacheMode::ReadOnly;
    p.collector = o.ignoreThreshold ? CollectorKind::DensityTopN : CollectorKind::DistanceTopN;

    // 与现有实现一致的粗略分块条件：经典位图超限、构建、缓存+提前停止。
    const bool needChunk = total > in.caps.classicChunkLimit || o.buildMode ||
                           (o.stopAtTarget && o.useCache && o.cache != nullptr);
    p.partition.kind = needChunk ? PartitionKind::Chunked : PartitionKind::Single;
    p.partition.cacheFirst = o.stopAtTarget && o.useCache;

    p.mayReuseBitmap = !o.useCache && !o.buildMode && !o.stopAtTarget && !o.smartRange &&
                       o.algorithm != slime::Algorithm::Sample && o.mode == slime::Mode::Density &&
                       p.determiner == DeterminerKind::Cpu && p.requiresBitmap &&
                       total <= in.caps.classicChunkLimit;
    return p;
}

} // namespace

ExecutionPlan makeExecutionPlan(const PlannerInput& input)
{
    const auto& o = input.options;
    const int64_t total = areaOf(o);

    // 构建模式：只判定 + 写缓存，不匹配；必须分块。
    if (o.buildMode) {
        ExecutionPlan p;
        p.family = PlanFamily::CacheBuild;
        p.partition.kind = PartitionKind::Chunked;
        p.determiner = DeterminerKind::Cpu;
        p.matcher = MatcherKind::None;
        p.collector = CollectorKind::CountOnly;
        p.cache = CacheMode::BuildWrite;
        p.requiresBitmap = true;
        p.allowGpu = false;
        p.progress.scanEnd = 1.0;
        p.progress.hasMatchPhase = false;
        p.progress.reportsRecorded = true;
        p.progress.reportsSkipped = true;
        p.progress.reportsPhase = true;
        fillResources(p, o);
        return p;
    }

    // 分带缓存：外层 band 计划，内层仍走 SearchPlan。
    if (o.cacheBandLoader) {
        ExecutionPlan p;
        p.family = PlanFamily::Banded;
        p.partition.kind = PartitionKind::Banded;
        p.partition.bandSize = o.cacheBandSize;
        p.partition.halo = o.mode == slime::Mode::Strict
                               ? (std::max)({ o.pattern.rows(), o.pattern.cols(), 1 })
                               : o.gridSize;
        p.cache = CacheMode::BandRead;
        p.requiresBitmap = false;
        p.allowGpu = true;
        p.progress.scanEnd = 1.0;
        p.progress.hasMatchPhase = false;
        fillResources(p, o);

        PlannerInput childIn = input;
        childIn.options.cacheBandLoader = nullptr;
        childIn.options.cacheBandMayCover = nullptr;
        childIn.options.cache = nullptr;
        childIn.cacheCovers = false;
        p.children.push_back(makeExecutionPlan(childIn));
        return p;
    }

    // 最小密度 0：解析求最近 N，不扫描全图。
    if (o.mode == slime::Mode::Density && o.algorithm == slime::Algorithm::Exact &&
        !o.ignoreThreshold && o.minDensity <= 0.0) {
        ExecutionPlan p;
        p.family = PlanFamily::Analytic;
        p.determiner = DeterminerKind::None;
        p.matcher = MatcherKind::Analytic;
        p.collector = CollectorKind::DistanceTopN;
        p.requiresBitmap = false;
        p.allowGpu = false;
        p.progress.scanEnd = 1.0;
        p.progress.hasMatchPhase = false;
        fillResources(p, o);
        return p;
    }

    return makeSearchPlan(input, total);
}

} // namespace slime::next

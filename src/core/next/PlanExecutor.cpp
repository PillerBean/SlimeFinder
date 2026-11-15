#include "core/next/PlanExecutor.h"

namespace slime::next {

bool PlanExecutor::supported(const slime::SearchOptions& opt, const ExecutionPlan& plan,
                             std::string* why)
{
    const auto fail = [&](const char* s) {
        if (why) *why = s;
        return false;
    };
    if (plan.family == PlanFamily::CacheBuild) return fail("缓存构建仍走旧引擎");
    if (plan.family == PlanFamily::Analytic) return fail("解析路径仍走旧引擎");
    if (plan.family == PlanFamily::Ring) return fail("环形/AFK 仍走旧引擎");
    if (opt.buildMode) return fail("构建模式仍走旧引擎");
    if (opt.ignoreThreshold || opt.stopAtTarget || opt.smartRange) {
        return fail("smart/stop/best 仍走旧引擎");
    }
    if (opt.mode == slime::Mode::Strict && opt.algorithm != slime::Algorithm::Exact) {
        return fail("严格模式非精确算法仍走旧引擎");
    }
    if (opt.mode == slime::Mode::Density && opt.algorithm != slime::Algorithm::Exact) {
        return fail("密度非精确算法仍走旧引擎");
    }
    if (opt.mode == slime::Mode::Strict && !opt.directionLocked) {
        return fail("旋转图案仍走旧引擎");
    }
    if (plan.determiner == DeterminerKind::Cache) return fail("完整缓存命中仍走旧引擎");
    if (opt.useCache && !opt.cacheBandLoader) return fail("非分带缓存仍走旧引擎");
    if (plan.family == PlanFamily::Banded && !opt.cacheBandLoader) return fail("Banded 缺少 loader");
    if (plan.family == PlanFamily::Search && plan.partition.kind == PartitionKind::Banded) {
        return fail("Search 计划不应带 Banded partition");
    }
    if (plan.matcher == MatcherKind::FusedGpuPattern || plan.matcher == MatcherKind::FusedGpuDensity) {
        if (!opt.gpuScan || !opt.gpuScan->available()) return fail("GPU 不可用");
    }
    if (plan.matcher != MatcherKind::CpuPattern && plan.matcher != MatcherKind::CpuDensity &&
        plan.matcher != MatcherKind::FusedGpuPattern && plan.matcher != MatcherKind::FusedGpuDensity) {
        return fail("当前原型不支持的 matcher");
    }
    return true;
}

bool PlanExecutor::run(const slime::SearchOptions& opt, const ExecutionPlan& plan,
                       const std::atomic<bool>& cancel, MaterializeResult& out, std::string* err,
                       const slime::ProgressFn& progress) const
{
    if (!supported(opt, plan, err)) return false;

    if (plan.family == PlanFamily::Banded) {
        MaterializePipeline p;
        return p.runBanded(opt, cancel, out, err, progress);
    }
    if (plan.family != PlanFamily::Search) {
        if (err) *err = "PlanExecutor 暂不支持该计划家族";
        return false;
    }

    if (plan.matcher == MatcherKind::FusedGpuPattern || plan.matcher == MatcherKind::FusedGpuDensity) {
        ResultCollector collector(CollectorKind::DistanceTopN, opt.targetCount);
        FusedGpuStats stats;
        FusedGpuPipeline fused;
        const bool ok = plan.matcher == MatcherKind::FusedGpuPattern
                            ? fused.runPattern(opt, opt.gpuScan, cancel, collector, stats, err)
                            : fused.runDensity(opt, opt.gpuScan, cancel, collector, stats, err);
        if (!ok) return false;
        out.results = collector.results();
        out.totalFound = collector.totalFound();
        out.scan.ms = 0.0;
        out.scan.backend = "gpu-fused";
        out.scan.gpuUsed = true;
        out.scan.chunks = 0;
        out.match.ms = stats.ms;
        out.match.windows = stats.windows;
        out.match.totalFound = stats.totalFound;
        out.cancelled = cancel.load(std::memory_order_relaxed);
        if (progress) progress(1.0, false, 0.0, 0.0);
        return true;
    }

    MaterializePipeline pipeline;
    if (plan.partition.kind == PartitionKind::Chunked) {
        const int64_t rows = plan.partition.rowsPerChunk > 0 ? plan.partition.rowsPerChunk : 256;
        return pipeline.runChunked(opt, rows, cancel, out, err, progress);
    }
    return pipeline.run(opt, cancel, out, err, progress);
}

} // namespace slime::next

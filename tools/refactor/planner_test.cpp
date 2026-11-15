// Phase 1 Planner 决策自检：只验证计划选择，不执行搜索。
#include "core/next/Plan.h"

#include <cstdio>
#include <string>

using namespace slime;
using namespace slime::next;

static PlannerInput base(Version v, Mode m)
{
    PlannerInput in;
    in.options.version = v;
    in.options.mode = m;
    in.options.minX = -512;
    in.options.maxX = 511;
    in.options.minZ = -512;
    in.options.maxZ = 511;
    in.options.algorithm = Algorithm::Exact;
    in.options.gridSize = 5;
    in.options.minDensity = 0.3;
    in.options.targetCount = 50;
    in.caps.classicChunkLimit = int64_t{ 1 } << 30;
    in.caps.densityGpuMinChunks = 65536;
    in.caps.patternGpuMinChunks = 16384;
    return in;
}

static int failures = 0;
static void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

int main()
{
    {
        PlannerInput in = base(Version::Java, Mode::Strict);
        in.caps.gpuAvailable = true;
        in.caps.gpuIsAmd = true;
        in.options.gpuScan = reinterpret_cast<const GpuScan*>(1);
        in.options.directionLocked = true;
        const auto p = makeExecutionPlan(in);
        expect(p.family == PlanFamily::Search, "strict family");
        expect(p.matcher == MatcherKind::FusedGpuPattern, "AMD strict -> fused pattern");
        expect(!p.requiresBitmap, "fused pattern no bitmap");
        expect(p.determiner == DeterminerKind::None, "fused pattern no determiner");
    }
    {
        PlannerInput in = base(Version::Java, Mode::Strict);
        in.caps.gpuAvailable = true;
        in.caps.gpuIsAmd = false;
        in.options.gpuScan = reinterpret_cast<const GpuScan*>(1);
        const auto p = makeExecutionPlan(in);
        expect(p.matcher == MatcherKind::CpuPattern, "non-AMD strict -> CPU pattern");
        expect(p.requiresBitmap, "CPU pattern bitmap");
    }
    {
        PlannerInput in = base(Version::Bedrock, Mode::Density);
        in.caps.gpuAvailable = true;
        in.options.gpuScan = reinterpret_cast<const GpuScan*>(1);
        const auto p = makeExecutionPlan(in);
        expect(p.matcher == MatcherKind::FusedGpuDensity, "GPU density fused");
        expect(!p.requiresBitmap, "fused density no bitmap");
    }
    {
        PlannerInput in = base(Version::Bedrock, Mode::Density);
        in.options.buildMode = true;
        const auto p = makeExecutionPlan(in);
        expect(p.family == PlanFamily::CacheBuild, "build family");
        expect(p.matcher == MatcherKind::None, "build no matcher");
        expect(p.collector == CollectorKind::CountOnly, "build count only");
        expect(p.partition.kind == PartitionKind::Chunked, "build chunked");
        expect(p.progress.reportsRecorded, "build progress recorded");
    }
    {
        PlannerInput in = base(Version::Bedrock, Mode::Density);
        in.options.cacheBandLoader = [](const Rect&) { return std::shared_ptr<CacheManager>(); };
        const auto p = makeExecutionPlan(in);
        expect(p.family == PlanFamily::Banded, "banded family");
        expect(p.partition.kind == PartitionKind::Banded, "banded partition");
        expect(!p.children.empty(), "banded child");
    }
    {
        PlannerInput in = base(Version::Java, Mode::Density);
        in.options.minDensity = 0.0;
        const auto p = makeExecutionPlan(in);
        expect(p.family == PlanFamily::Analytic, "minDensity 0 analytic");
        expect(p.matcher == MatcherKind::Analytic, "analytic matcher");
        expect(!p.requiresBitmap, "analytic no bitmap");
    }
    {
        PlannerInput in = base(Version::Java, Mode::Density);
        in.options.ignoreThreshold = true;
        const auto p = makeExecutionPlan(in);
        expect(p.collector == CollectorKind::DensityTopN, "best density collector");
    }
    {
        PlannerInput in = base(Version::Java, Mode::Density);
        in.options.algorithm = Algorithm::Sample;
        const auto p = makeExecutionPlan(in);
        expect(p.determiner == DeterminerKind::Sample, "sample determiner");
        expect(p.requiresBitmap, "sample bitmap");
    }
    {
        PlannerInput in = base(Version::Bedrock, Mode::Density);
        in.options.useCache = true;
        in.cacheCovers = true;
        const auto p = makeExecutionPlan(in);
        expect(p.determiner == DeterminerKind::Cache, "cache determiner");
        expect(p.cache == CacheMode::ReadOnly, "cache read only");
        expect(!p.allowGpu, "cache no GPU");
    }
    if (failures == 0) std::printf("Planner ALL PASS\n");
    return failures == 0 ? 0 : 1;
}

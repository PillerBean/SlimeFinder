// Phase 5：Planner 四类计划选择 + 随机参数矩阵对拍。
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"
#include "core/next/Pipeline.h"
#include "core/next/PlanExecutor.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

bool same(const std::vector<slime::MatchResult>& a, const std::vector<slime::MatchResult>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].minX != b[i].minX || a[i].maxX != b[i].maxX ||
            a[i].minZ != b[i].minZ || a[i].maxZ != b[i].maxZ) return false;
    }
    return true;
}

slime::SearchOptions makeCase(slime::Version v, slime::Mode m, int x0, int z0, int w, int h,
                              int64_t seed, int target)
{
    slime::SearchOptions o;
    o.version = v;
    o.worldSeed = seed;
    o.minX = x0;
    o.maxX = x0 + w - 1;
    o.minZ = z0;
    o.maxZ = z0 + h - 1;
    o.mode = m;
    o.algorithm = slime::Algorithm::Exact;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.targetCount = target;
    o.threads = 1;
    o.directionLocked = true;
    if (m == slime::Mode::Strict) {
        slime::Pattern p(2, 2);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) p.set(r, c, slime::CellState::Green);
        o.pattern = p;
    }
    return o;
}

bool runAndCompare(const char* name, const slime::SearchOptions& opt,
                   const slime::next::MachineCaps& caps, const slime::GpuScan* gpu,
                   slime::next::PlanFamily wantFamily, bool wantFused)
{
    slime::next::PlannerInput in;
    in.options = opt;
    in.caps = caps;
    const auto plan = slime::next::makeExecutionPlan(in);
    if (wantFamily != plan.family) {
        std::printf("%-22s PLAN FAMILY MISMATCH got=%d want=%d\n", name, static_cast<int>(plan.family),
                    static_cast<int>(wantFamily));
        return false;
    }
    if (wantFused && plan.matcher != slime::next::MatcherKind::FusedGpuPattern &&
        plan.matcher != slime::next::MatcherKind::FusedGpuDensity) {
        std::printf("%-22s PLAN NOT FUSED\n", name);
        return false;
    }
    std::string why;
    if (!slime::next::PlanExecutor::supported(opt, plan, &why)) {
        std::printf("%-22s UNSUPPORTED: %s\n", name, why.c_str());
        return false;
    }

    slime::SearchOptions oldOpt = opt;
    oldOpt.gpuScan = nullptr;
    oldOpt.useCache = false;
    oldOpt.cache = nullptr;
    oldOpt.cacheBandLoader = nullptr;
    oldOpt.cacheBandMayCover = nullptr;
    std::atomic<bool> cancel{ false };
    slime::SearchOutcome oldOut;
    slime::SlimeEngine oldEngine;
    oldEngine.run(oldOpt, cancel, slime::ProgressFn(), oldOut);

    slime::next::MaterializeResult newOut;
    std::string err;
    slime::next::PlanExecutor executor;
    const bool ok = executor.run(opt, plan, cancel, newOut, &err);
    const bool sameResults = oldOut.ok && ok && same(oldOut.results, newOut.results);
    const bool totalOk = plan.family == slime::next::PlanFamily::Banded
                             ? newOut.totalFound == static_cast<int64_t>(newOut.results.size())
                             : oldOut.totalFound == newOut.totalFound;
    const bool sameRes = sameResults && totalOk;
    std::printf("%-22s plan=%d matcher=%d part=%d %s\n", name, static_cast<int>(plan.family),
                static_cast<int>(plan.matcher), static_cast<int>(plan.partition.kind),
                sameRes ? "SAME" : "DIFF");
    if (!sameRes) std::printf("  err=%s\n", err.c_str());
    return sameRes;
}

} // namespace

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    std::string gpuErr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (gpu && !gpu->available()) gpu.reset();
    std::printf("GPU: %s\n", gpu ? gpu->deviceName().c_str() : ("unavailable: " + gpuErr).c_str());

    int failures = 0;
    slime::next::MachineCaps cpuCaps;
    cpuCaps.gpuAvailable = false;
    cpuCaps.classicChunkLimit = int64_t{ 1 } << 40;
    slime::next::MachineCaps chunkCaps = cpuCaps;
    chunkCaps.classicChunkLimit = 10000;
    slime::next::MachineCaps gpuCaps;
    gpuCaps.gpuAvailable = gpu != nullptr;
    gpuCaps.gpuIsAmd = true;
    gpuCaps.classicChunkLimit = int64_t{ 1 } << 40;

    auto material = makeCase(slime::Version::Java, slime::Mode::Density, -128, -128, 256, 256, 12345, 30);
    if (!runAndCompare("explicit-materialize", material, cpuCaps, nullptr, slime::next::PlanFamily::Search, false))
        ++failures;

    auto chunk = makeCase(slime::Version::Bedrock, slime::Mode::Density, -256, -256, 512, 512, 12345, 30);
    if (!runAndCompare("explicit-chunked", chunk, chunkCaps, nullptr, slime::next::PlanFamily::Search, false))
        ++failures;

    if (gpu) {
        auto fusedDensity = makeCase(slime::Version::Java, slime::Mode::Density, -256, -256, 512, 512, 12345, 30);
        fusedDensity.gpuScan = gpu.get();
        if (!runAndCompare("explicit-fused-density", fusedDensity, gpuCaps, gpu.get(),
                           slime::next::PlanFamily::Search, true))
            ++failures;

        auto fusedPattern = makeCase(slime::Version::Java, slime::Mode::Strict, -128, -128, 256, 256, 12345, 30);
        fusedPattern.gpuScan = gpu.get();
        if (!runAndCompare("explicit-fused-pattern", fusedPattern, gpuCaps, gpu.get(),
                           slime::next::PlanFamily::Search, true))
            ++failures;
    }

    // Banded：先构造完整缓存，再由 Planner 选择 Banded。
    {
        auto band = makeCase(slime::Version::Bedrock, slime::Mode::Density, -128, -128, 256, 256, 12345, 30);
        slime::next::Bitmap full;
        slime::next::ScanStats scan;
        std::string err;
        slime::next::CpuDeterminer cd;
        std::atomic<bool> cancel{ false };
        if (cd.run(band, cancel, full, scan, &err)) {
            auto cache = std::make_shared<slime::CacheManager>();
            const slime::Rect rect{ band.minX, band.maxX, band.minZ, band.maxZ };
            if (cache->addPackedWords(rect, full.words().data(), full.words().size())) {
                band.useCache = true;
                band.cacheBandSize = 64;
                band.cacheBandLoader = [cache](const slime::Rect&) { return cache; };
                band.cacheBandMayCover = [](const slime::Rect&) { return true; };
                if (!runAndCompare("explicit-banded", band, cpuCaps, nullptr,
                                   slime::next::PlanFamily::Banded, false))
                    ++failures;
            }
        }
    }

    // 随机矩阵：4 个 cycle，每个 8 个 case。
    uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto rnd = [&]() {
        state ^= state << 7;
        state ^= state >> 9;
        return state;
    };
    int randomCases = 0;
    int randomFailures = 0;
    for (int cycle = 0; cycle < 4; ++cycle) {
        for (int i = 0; i < 8; ++i) {
            const auto v = (rnd() & 1) ? slime::Version::Java : slime::Version::Bedrock;
            const auto m = (rnd() & 1) ? slime::Mode::Strict : slime::Mode::Density;
            const int w = 128 + static_cast<int>(rnd() % 385);
            const int h = 128 + static_cast<int>(rnd() % 385);
            const int x0 = -static_cast<int>(rnd() % 5000);
            const int z0 = -static_cast<int>(rnd() % 5000);
            const int target = 10 + static_cast<int>(rnd() % 90);
            auto opt = makeCase(v, m, x0, z0, w, h, static_cast<int64_t>(rnd()), target);
            const bool useGpu = gpu && (rnd() & 1);
            if (useGpu) opt.gpuScan = gpu.get();
            slime::next::MachineCaps caps;
            caps.gpuAvailable = useGpu;
            caps.gpuIsAmd = true;
            caps.classicChunkLimit = (rnd() % 3 == 0) ? 50000 : (int64_t{ 1 } << 40);
            slime::next::PlannerInput pin;
            pin.options = opt;
            pin.caps = caps;
            const auto plan = slime::next::makeExecutionPlan(pin);
            std::string why;
            if (!slime::next::PlanExecutor::supported(opt, plan, &why)) {
                std::printf("random unsupported: %s\n", why.c_str());
                ++randomFailures;
                continue;
            }
            slime::SearchOptions oldOpt = opt;
            oldOpt.gpuScan = nullptr;
            std::atomic<bool> cancel{ false };
            slime::SearchOutcome oldOut;
            slime::SlimeEngine oldEngine;
            oldEngine.run(oldOpt, cancel, slime::ProgressFn(), oldOut);
            slime::next::MaterializeResult newOut;
            std::string err;
            slime::next::PlanExecutor executor;
            const bool ok = executor.run(opt, plan, cancel, newOut, &err);
            const bool sameRes = oldOut.ok && ok && same(oldOut.results, newOut.results) &&
                                 oldOut.totalFound == newOut.totalFound;
            ++randomCases;
            if (!sameRes) {
                ++randomFailures;
                std::printf("random DIFF cycle=%d i=%d version=%d mode=%d area=%dx%d target=%d plan=%d err=%s\n",
                            cycle + 1, i, static_cast<int>(v), static_cast<int>(m), w, h, target,
                            static_cast<int>(plan.family), err.c_str());
            }
        }
    }
    std::printf("RANDOM-MATRIX cases=%d failures=%d\n", randomCases, randomFailures);
    failures += randomFailures;
    std::printf("PHASE5-PLAN-MATRIX %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

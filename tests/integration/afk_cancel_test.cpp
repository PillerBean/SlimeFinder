// v3.15.x（flow-ring-cancel2）：环形规划器取消响应回归。
//   覆盖：sampling 抽样、块级精确路径、区块粗扫/1 格精修/抛光路径，以及
//   measureStrictForCandidates / measureExactRows 的「取消不写回半截结果」语义。
//   每个场景都断言：置取消位 -> worker 返回 < 1500ms，且取消后不产出部分结果。
#include "core/AfkPlanner.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

int g_failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

struct CancelOutcome {
    double responseMs = 0.0;
    bool finishedBeforeCancel = false;
};

// 启动 worker，等 delayMs 后置取消位，再 join；返回「置位 → worker 返回」的耗时。
template <typename Fn>
CancelOutcome cancelAfter(std::chrono::milliseconds delay, Fn&& fn)
{
    std::atomic<bool> cancel{ false };
    std::atomic<bool> done{ false };
    CancelOutcome out;
    std::thread worker([&]() {
        fn(cancel);
        done.store(true, std::memory_order_relaxed);
    });
    const auto deadline = Clock::now() + delay;
    while (Clock::now() < deadline && !done.load(std::memory_order_relaxed))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (done.load(std::memory_order_relaxed)) {
        worker.join();
        out.finishedBeforeCancel = true;
        return out;
    }
    const auto t0 = Clock::now();
    cancel.store(true, std::memory_order_relaxed);
    worker.join();
    out.responseMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return out;
}

void report(const char* name, const CancelOutcome& out, size_t results)
{
    std::printf("%-28s cancel_response=%.1fms results=%zu%s\n", name, out.responseMs, results,
                out.finishedBeforeCancel ? " (finished before cancel)" : "");
}

const slime::SlimeChunkPredicate kPred = [](int x, int z) { return ((x * 31 + z * 17) & 7) == 0; };

// 预热规划器的静态核缓存（subOffsetKernels / kernelCache 按半径缓存）。
//   这样计时的取消场景一进入就会落在散射/精修/抛光等真正需要覆盖的循环里，
//   而不是把时间都花在首次建核上。
void warmKernels(int radius, int inner)
{
    slime::AfkPlanOptions opt;
    opt.area = slime::Rect{ 0, 0, 0, 0 };
    opt.radius = radius;
    opt.innerRadius = inner;
    opt.topN = 1;
    opt.minSeparation = 1;
    opt.coarseStep = 1;
    opt.maxWindowBlocks = 2048;
    opt.weightAt = [](int, int) { return 1.0; };
    opt.seedCandidates.emplace_back(0, 0);
    (void)slime::AfkPlanner::plan(opt, kPred, nullptr);
}

// 1) 超大范围 -> 等距抽样分支（原有覆盖，保留）。
void testSampling()
{
    slime::AfkPlanOptions opt;
    opt.area = slime::Rect{ -20000, 19999, -20000, 19999 };
    opt.radius = 128;
    opt.topN = 8;
    opt.minSeparation = 24;
    opt.coarseStep = 4;
    opt.maxWindowBlocks = 2048;
    std::vector<slime::AfkCandidate> result;
    const CancelOutcome out = cancelAfter(std::chrono::milliseconds(5), [&](std::atomic<bool>& cancel) {
        result = slime::AfkPlanner::plan(opt, kPred, &cancel);
    });
    report("sampling(40000x40000)", out, result.size());
    check(!out.finishedBeforeCancel, "sampling: worker still running when cancelled");
    check(result.empty(), "sampling: cancelled plan returns no partial results");
    check(out.responseMs < 1500.0, "sampling: cancel response < 1500ms");
}

// 2) 无加权小窗口 -> planExactBlockLevel（占用图/前缀和/粗扫/±step 精修）。
void testExactBlockLevel()
{
    slime::AfkPlanOptions opt;
    opt.area = slime::Rect{ -32, 31, -32, 31 };
    opt.radius = 512;
    opt.innerRadius = 24;
    opt.topN = 8;
    opt.minSeparation = 24;
    opt.coarseStep = 4;
    opt.maxWindowBlocks = 4096;   // 窗口 2080x2080，仍走块级精确路径
    std::vector<slime::AfkCandidate> result;
    const CancelOutcome out = cancelAfter(std::chrono::milliseconds(20), [&](std::atomic<bool>& cancel) {
        result = slime::AfkPlanner::plan(opt, kPred, &cancel);
    });
    report("exact-block(32x32,R512)", out, result.size());
    check(!out.finishedBeforeCancel, "exact: worker still running when cancelled");
    check(result.empty(), "exact: cancelled plan returns no partial results");
    check(out.responseMs < 1500.0, "exact: cancel response < 1500ms");
}

// 3) 加权 -> 区块级粗扫 + 散射（非 sampling 路径）。
//    192x192 区块、R=1024 时散射要对上万个史莱姆区块各铺 (2k+1)^2=17161 个格点，
//    取消必然落在散射 OpenMP 段内部（每 16 个区块 / 每行收集都有检查点）。
void testCoarseScatter()
{
    warmKernels(1024, 24);
    slime::AfkPlanOptions opt;
    opt.area = slime::Rect{ -96, 95, -96, 95 };
    opt.radius = 1024;
    opt.innerRadius = 24;
    opt.topN = 32;
    opt.minSeparation = 8;
    opt.coarseStep = 1;
    opt.maxWindowBlocks = 2048;
    opt.weightAt = [](int, int) { return 1.0; };
    std::vector<slime::AfkCandidate> result;
    const CancelOutcome out = cancelAfter(std::chrono::milliseconds(10), [&](std::atomic<bool>& cancel) {
        result = slime::AfkPlanner::plan(opt, kPred, &cancel);
    });
    report("coarse-scatter(192x192,R1024)", out, result.size());
    check(!out.finishedBeforeCancel, "coarse-scatter: worker still running when cancelled");
    check(result.empty(), "coarse-scatter: cancelled plan returns no partial results");
    check(out.responseMs < 1500.0, "coarse-scatter: cancel response < 1500ms");
}

// 4) 种子模式（正方形预筛） -> 只做 1 格精修/抛光，R=1024 使每个候选的
//    256×(2k+1)^2 内层足够长，确保取消落在 refine 内部。
void testSeedRefine()
{
    warmKernels(1024, 24);
    slime::AfkPlanOptions opt;
    opt.area = slime::Rect{ -16, 15, -16, 15 };
    opt.radius = 1024;
    opt.innerRadius = 24;
    opt.topN = 64;
    opt.minSeparation = 4;
    opt.coarseStep = 1;
    opt.maxWindowBlocks = 2048;
    opt.weightAt = [](int, int) { return 1.0; };
    for (int i = 0; i < 512; ++i) {
        const int cx = -16 + (i % 16);
        const int cz = -16 + ((i / 16) % 16);
        opt.seedCandidates.emplace_back(cx, cz);
    }
    std::vector<slime::AfkCandidate> result;
    const CancelOutcome out = cancelAfter(std::chrono::milliseconds(8), [&](std::atomic<bool>& cancel) {
        result = slime::AfkPlanner::plan(opt, kPred, &cancel);
    });
    report("seed-refine(512,R1024)", out, result.size());
    check(!out.finishedBeforeCancel, "seed-refine: worker still running when cancelled");
    check(result.empty(), "seed-refine: cancelled plan returns no partial results");
    check(out.responseMs < 1500.0, "seed-refine: cancel response < 1500ms");
}

// 5) measureStrictForCandidates：取消路径不得把「部分行带」写回候选。
void testStrictNoWriteback()
{
    constexpr int kCands = 512;
    std::vector<slime::AfkCandidate> cands(static_cast<size_t>(kCands));
    for (int i = 0; i < kCands; ++i) {
        auto& c = cands[static_cast<size_t>(i)];
        c.x = (i % 32) * 512;
        c.z = (i / 32) * 512;
        c.coveredBlocks = -12345;
        c.coveredWeight = -1.5;
        c.coveredChunks = -7;
        c.coveragePercent = -9.5;
        c.avgWeight = -2.5;
    }
    slime::AfkPlanOptions opt;
    opt.radius = 1024;
    opt.innerRadius = 24;
    opt.statBands = 16;
    slime::AfkPlanner::StrictStatsInfo info;
    const CancelOutcome out = cancelAfter(std::chrono::milliseconds(15), [&](std::atomic<bool>& cancel) {
        info = slime::AfkPlanner::measureStrictForCandidates(cands, opt, kPred, 0, 0, false, &cancel);
    });
    bool untouched = true;
    for (const auto& c : cands) {
        if (c.coveredBlocks != -12345 || c.coveredWeight != -1.5 || c.coveredChunks != -7 ||
            c.coveragePercent != -9.5 || c.avgWeight != -2.5) {
            untouched = false;
            break;
        }
    }
    report("strict-stats(512x,R1024)", out, static_cast<size_t>(info.samples));
    check(!out.finishedBeforeCancel, "strict: worker still running when cancelled");
    check(untouched, "strict: cancelled measureStrictForCandidates does not write back partial stats");
    check(out.responseMs < 1500.0, "strict: cancel response < 1500ms");
}

// 6) measureExactRows：取消后返回全 0 指标，而不是半截行带。
void testExactRowsZero()
{
    slime::AfkPlanOptions opt;
    opt.radius = 8192;
    opt.innerRadius = 0;
    slime::AfkPlanner::ExactMetrics m;
    const CancelOutcome out = cancelAfter(std::chrono::milliseconds(10), [&](std::atomic<bool>& cancel) {
        m = slime::AfkPlanner::measureExactRows(0, 0, opt, kPred, -8192, 8192, true, &cancel);
    });
    report("exact-rows(R8192)", out, 0);
    check(!out.finishedBeforeCancel, "rows: worker still running when cancelled");
    check(m.coveredBlocks == 0 && m.coveredWeight == 0.0 && m.coveredChunks == 0,
          "rows: cancelled measureExactRows returns zeroed metrics (no partial band)");
    check(out.responseMs < 1500.0, "rows: cancel response < 1500ms");
}

} // namespace

int main(int argc, char** argv)
{
    // 可选过滤：第一个参数匹配场景名（调试单场景取消响应用）。
    const char* only = (argc > 1) ? argv[1] : nullptr;
    const auto want = [only](const char* name) {
        return only == nullptr || std::strstr(name, only) != nullptr;
    };
    if (want("sampling")) testSampling();
    if (want("exact-block")) testExactBlockLevel();
    if (want("coarse-scatter")) testCoarseScatter();
    if (want("seed-refine")) testSeedRefine();
    if (want("strict-stats")) testStrictNoWriteback();
    if (want("exact-rows")) testExactRowsZero();
    if (g_failures != 0) {
        std::printf("AFK-CANCEL FAILED (%d checks)\n", g_failures);
        return 1;
    }
    std::printf("AFK-CANCEL OK\n");
    return 0;
}

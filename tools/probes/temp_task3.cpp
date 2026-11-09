// temp_task3.cpp -- v3.3.8 任务 3：④′ 机制的尺度验证实验（全宽 × 3160 行 ⇒ 11 个 range）
//
// 目的：证明「真值命中率 ≈ 保留 range 比例」的线性关系（④′ 的判别特征），并验证修复后的支撑度清扫把它拉回 100%。
// 步骤：
//   A) 跑一份**完整缓存**（不触发剪枝）并落盘（基准快照）。
//   B) 【预修复机制，构造式】从快照出发：用二分找到「恰好保留 k 个 range」的预算并 pruneToFit，
//      再把被丢掉的 range 用 `mergeRegion(rect, {})` 重新登记（= 旧实现「无条件登记」语义，
//      内存 API 上仍然成立）⇒ 得到 ④′ 的「声明在、条目不在」状态。
//      测量：逐 range 条目率（双峰？）、真值命中率（应 ≈ k/N）、假阳性（应为 0）。
//      注意：全程不 load/save ⇒ 支撑度清扫**不会**介入（这正是预修复状态）。
//   C) 【修复后】把 B 的状态落盘（saveFile 内清扫）→ `loadFile` 再测（期望命中率 100%、假阳性 0、条目数不变）。
//
// 用法：temp_task3.exe [cachePath]（内部强制 SLIME_FORCE_CHUNKED=1 + SLIME_CHUNK_WORDS=1048576）
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace slime;
using Clock = std::chrono::steady_clock;

static SearchOutcome runOnce(const SearchOptions& opt)
{
    SearchOutcome out;
    std::atomic<bool> cancel{ false };
    SlimeEngine e;
    e.run(opt, cancel, nullptr, out);
    return out;
}

struct Measure {
    long long truthSlime = 0, cacheSlime = 0, hits = 0, misses = 0, falsePos = 0, covered = 0, samples = 0;
    double hitRate() const { return truthSlime ? 100.0 * hits / truthSlime : 0.0; }
    double fpRate() const { return (samples - truthSlime) ? 100.0 * falsePos / (samples - truthSlime) : 0.0; }
};

static Measure measureRegion(CacheManager& c, const Rect& r, long long n, uint64_t seed)
{
    Measure m;
    std::mt19937_64 rng(seed);
    const uint64_t w = static_cast<uint64_t>(r.maxX - r.minX + 1);
    const uint64_t h = static_cast<uint64_t>(r.maxZ - r.minZ + 1);
    while (m.samples < n) {
        const int x = r.minX + static_cast<int>(rng() % w);
        const int z = r.minZ + static_cast<int>(rng() % h);
        const int lk = c.lookup(x, z);
        if (lk < 0) continue; // 未被任何声明覆盖：不计入（修复后大量点会落在这里）
        ++m.samples;
        const bool truth = SlimeEngine::isSlimeChunk(Version::Bedrock, 0, x, z);
        if (truth) ++m.truthSlime;
        if (lk == 1) ++m.cacheSlime;
        if (truth && lk == 1) ++m.hits;
        if (truth && lk != 1) ++m.misses;
        if (!truth && lk == 1) ++m.falsePos;
    }
    return m;
}

static void printPerRange(CacheManager& c, long long perRange)
{
    std::printf("    逐 range 抽样率（每 range %lld 点）：", perRange);
    std::mt19937_64 rng(20260921);
    for (size_t i = 0; i < c.ranges().size(); ++i) {
        const Rect& r = c.ranges()[i];
        const uint64_t w = static_cast<uint64_t>(r.maxX - r.minX + 1);
        const uint64_t h = static_cast<uint64_t>(r.maxZ - r.minZ + 1);
        long long slime = 0;
        for (long long k = 0; k < perRange; ++k) {
            const int x = r.minX + static_cast<int>(rng() % w);
            const int z = r.minZ + static_cast<int>(rng() % h);
            if (c.lookup(x, z) == 1) ++slime;
        }
        std::printf(" [%zu]z=%d..%d:%5.2f%%", i, r.minZ, r.maxZ,
                    100.0 * static_cast<double>(slime) / static_cast<double>(perRange));
    }
    std::printf("\n");
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    // 必须强制分块路径（经典路径不写缓存）；SLIME_CHUNK_WORDS 下限 1<<20，故 1048576 ⇒ 每块 319 行
    _putenv_s("SLIME_FORCE_CHUNKED", "1");
    _putenv_s("SLIME_CHUNK_WORDS", "1048576");
#endif
    const std::string path = (argc > 1) ? argv[1]
                                        : std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") +
                                              "\\slime_task3_full.slimecache";
    const Rect R{ -104857, 104857, -1580, 1579 };
    std::printf("=== 任务 3：④′ 尺度验证（区域 %d..%d x %d..%d = %lld 区块）===\n", R.minX, R.maxX, R.minZ,
                R.maxZ, static_cast<long long>(R.maxX - R.minX + 1) * (R.maxZ - R.minZ + 1));

    // ---- A) 完整缓存 ----
    CacheManager full;
    SearchOptions o;
    o.version = Version::Bedrock;
    o.mode = Mode::Density;
    o.gridSize = 5;
    o.minDensity = 0.75;
    o.minX = R.minX; o.maxX = R.maxX; o.minZ = R.minZ; o.maxZ = R.maxZ;
    o.worldSeed = 0;
    o.targetCount = 1;
    o.stopAtTarget = false;
    o.threads = 0;
    o.useCache = true;
    o.cache = &full;
    o.cacheWrite = &full;
    o.cacheBudgetBytes = 0; // 默认 1 GiB ⇒ 不触发剪枝（基准快照）
    const auto t0 = Clock::now();
    const SearchOutcome r = runOnce(o);
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    const size_t N = full.ranges().size();
    const int64_t E = full.entryCount();
    std::printf("A) 完整扫描：ok=%d chunked=%d 增量=%d ranges=%zu entries=%lld 用时=%.0fms\n", r.ok ? 1 : 0,
                r.chunkedPath ? 1 : 0, r.cacheHandledIncrementally ? 1 : 0, N,
                static_cast<long long>(E), ms);
    if (N < 4 || E <= 0) { std::printf("范围/条目不足，实验无法进行\n"); return 2; }
    std::string err;
    if (!full.saveFile(path, err)) { std::printf("saveFile 失败: %s\n", err.c_str()); return 2; }
    std::printf("   快照已落盘：%s（%lld B）\n", path.c_str(), static_cast<long long>(full.sizeBytes()));

    // ---- B/C) 保留 k 个 range（k = N-1 → 预期 ~91%；k ≈ 8/12×N → 预期 ~67%）----
    const size_t targets[2] = { N - 1, (N >= 12) ? (N * 8 / 12) : ((N * 2) / 3) };
    for (int pass = 0; pass < 2; ++pass) {
        const size_t k = targets[pass];
        // 二分：找「pruneToFit 恰好保留 k 个 range」的预算（每次都用快照副本，避免污染）
        int64_t lo = 16, hi = full.sizeBytes();
        CacheManager best;
        size_t bestCount = 0;
        for (int iter = 0; iter < 12; ++iter) {
            const int64_t mid = lo + (hi - lo) / 2;
            CacheManager t = full;
            t.pruneToFit(mid);
            if (t.ranges().size() == k) { best = std::move(t); bestCount = k; break; }
            if (t.ranges().size() > k) hi = mid - 1;
            else lo = mid + 1;
            if (bestCount == 0) { best = std::move(t); bestCount = best.ranges().size(); }
            if (lo > hi) break;
        }
        if (bestCount != k) {
            std::printf("\nB%d) 预算二分未命中目标 %zu（最近得到 %zu）——仍按最近结果继续\n", pass, k, bestCount);
        }

        const std::vector<Rect> before = best.ranges();
        std::set<std::tuple<int, int, int, int>> kept;
        for (const Rect& rr : before) kept.emplace(rr.minX, rr.maxX, rr.minZ, rr.maxZ);
        const std::vector<Rect> allRects = full.ranges();
        int64_t reRegistered = 0;
        for (const Rect& rr : allRects) {
            if (kept.count(std::make_tuple(rr.minX, rr.maxX, rr.minZ, rr.maxZ))) continue;
            best.mergeRegion(rr, {}); // 旧实现语义：无论有没有新增条目都登记覆盖声明
            ++reRegistered;
        }
        const size_t keptCount = before.size();
        const Measure m = measureRegion(best, R, 200000, 777);
        std::printf("\nB%d) 预修复状态：保留 range=%zu/%zu（%.1f%%）、以 0 条目重新登记 %lld 个 ⇒ 声明总数=%zu；"
                    "条目=%lld\n",
                    pass, keptCount, N, 100.0 * static_cast<double>(keptCount) / static_cast<double>(N),
                    static_cast<long long>(reRegistered), best.ranges().size(),
                    static_cast<long long>(best.entryCount()));
        std::printf("    真值比对（20 万点，全部在声明覆盖内）：真值史莱姆=%lld 命中=%lld 漏判=%lld 假阳性=%lld"
                    " ⇒ 命中率=%.2f%%（线性预期 %.1f%%=保留比例） 假阳性率=%.3f%%\n",
                    m.truthSlime, m.hits, m.misses, m.falsePos, m.hitRate(),
                    100.0 * static_cast<double>(keptCount) / static_cast<double>(N), m.fpRate());
        printPerRange(best, 20000);

        // ---- C) 修复后：落盘（saveFile 内清扫）→ 重新加载 → 复测 ----
        std::string e2;
        const int64_t entriesBefore = best.entryCount();
        const size_t halvesBefore = best.ranges().size();
        if (!best.saveFile(path + ".hollow", e2)) { std::printf("saveFile(hollow) 失败: %s\n", e2.c_str()); return 2; }
        const size_t afterSaveSweep = best.ranges().size();
        CacheManager fixed;
        if (!fixed.loadFile(path + ".hollow", true, e2)) { std::printf("load(hollow) 失败: %s\n", e2.c_str()); return 2; }
        const Measure mf = measureRegion(fixed, R, 200000, 777);
        std::printf("C%d) 修复后（支撑度清扫）：声明 %zu →（saveFile 清扫）%zu →（重载）%zu；条目 %lld → %lld（不变=%d）\n",
                    pass, halvesBefore, afterSaveSweep, fixed.ranges().size(),
                    static_cast<long long>(entriesBefore), static_cast<long long>(fixed.entryCount()),
                    entriesBefore == fixed.entryCount() ? 1 : 0);
        std::printf("    真值比对（20 万点，只统计仍被覆盖的点）：覆盖=%lld 真值史莱姆=%lld 命中=%lld 漏判=%lld"
                    " 假阳性=%lld ⇒ 命中率=%.2f%% 假阳性率=%.3f%%\n",
                    mf.samples, mf.truthSlime, mf.hits, mf.misses, mf.falsePos, mf.hitRate(), mf.fpRate());
    }
    std::printf("\n=== 判定 ===\n");
    std::printf("· B 段：命中率随「保留 range 比例」线性变化 ⇒ ④′（声明在、条目不在）确认；假阳性 0 ⇒ 形态一致\n");
    std::printf("· C 段：清扫后命中率 100%% 且条目数不变 ⇒ 修复有效（代价：被剔区域重扫）\n");
    return 0;
}

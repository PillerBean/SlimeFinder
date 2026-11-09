// temp_ab_fillskip.cpp -- v3.3.8 任务 3+5 第 3 步 A/B：缓存位图直接当 grid（填充跳过）的实测收益
//   usage: temp_ab_fillskip.exe [halfW] [halfH]   默认 1024×10000（经典路径，W=2048 宽）
// 场景：① 空缓存全量搜索（填充+收集+匹配）② 整区命中缓存（填充应整段跳过）③ 关缓存基线（对照）
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace slime;
using Clock = std::chrono::steady_clock;

static SearchOutcome runOnce(const SearchOptions& o)
{
    SearchOutcome out;
    std::atomic<bool> cancel{ false };
    SlimeEngine e;
    e.run(o, cancel, nullptr, out);
    return out;
}

static void report(const char* tag, const SearchOutcome& r, double ms)
{
    std::printf("%-28s 墙钟=%7.0fms fill=%6.0fms match=%6.0fms slide=%7.0f(s·ms) 结果=%zu "
                "跳过填充点=%lld 整段跳过=%d 扫描=%lld\n",
                tag, ms, r.stats.fillMs, r.stats.matchMs, r.stats.slideWindowMs, r.results.size(),
                static_cast<long long>(r.cacheHitPoints), r.cacheFillSkipped ? 1 : 0,
                static_cast<long long>(r.stats.scannedChunks));
}

int main(int argc, char** argv)
{
    const int halfW = (argc > 1) ? std::atoi(argv[1]) : 1024;  // W = 2048
    const int halfH = (argc > 2) ? std::atoi(argv[2]) : 10000; // H = 20000
    CacheManager c;
    SearchOptions o;
    o.version = Version::Bedrock;
    o.mode = Mode::Density;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.minX = -halfW; o.maxX = halfW - 1;
    o.minZ = -halfH; o.maxZ = halfH - 1;
    o.worldSeed = 0;
    o.targetCount = 50;
    o.stopAtTarget = false;
    o.threads = 0;
    o.useCache = true;
    o.cache = &c;

    std::printf("=== A/B：缓存位图直接当 grid（区域 %d×%d = %lld 区块）===\n", halfW * 2, halfH * 2,
                static_cast<long long>(halfW) * 4 * halfH);
    auto t0 = Clock::now();
    const SearchOutcome r1 = runOnce(o);
    double ms1 = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    report("① 空缓存（全量填充+收集）", r1, ms1);
    c.mergeRegion(Rect{ o.minX, o.maxX, o.minZ, o.maxZ }, r1.newCacheChunks);
    std::printf("   缓存：声明=%zu 条目=%lld 位图=%.1f MiB\n", c.ranges().size(),
                static_cast<long long>(c.entryCount()), c.bitmapBytes() / 1048576.0);

    t0 = Clock::now();
    const SearchOutcome r2 = runOnce(o);
    double ms2 = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    report("② 整区命中（填充跳过）", r2, ms2);

    SearchOptions off = o;
    off.useCache = false;
    off.cache = nullptr;
    t0 = Clock::now();
    const SearchOutcome r3 = runOnce(off);
    double ms3 = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    report("③ 关缓存基线", r3, ms3);

    bool same = r2.results.size() == r3.results.size();
    for (size_t i = 0; same && i < r2.results.size(); ++i) {
        const MatchResult& a = r2.results[i];
        const MatchResult& b = r3.results[i];
        same = (a.minX == b.minX && a.maxX == b.maxX && a.minZ == b.minZ && a.maxZ == b.maxZ);
    }
    std::printf("结果一致性：② vs ③ 逐位一致=%d；② 相对 ③ 提速=%.2f×（fill %.0f→%.0fms）\n", same ? 1 : 0,
                ms3 / ms2, r3.stats.fillMs, r2.stats.fillMs);
    return 0;
}

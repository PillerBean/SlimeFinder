// temp_case20.cpp -- [20] 缓存完整性用例（临时测试件，不改产品代码）
// 规格（用户 v3.3.8 第 1 步）：
//   200x200 基岩密度搜索；开缓存跑一次→saveFile→clear→loadFile(记 cacheLoadMs)
//   →与逐点真值(SlimeEngine::isSlimeChunk)比对；强制小预算触发 pruneToFit
//   输出：缺失点数/比例、Z 行直方图、每 range 命中数、cacheLoadMs、
//         pruneToFit 是否触发/丢了哪个 range、被丢 range 是否仍被 covers() 当覆盖、
//         与 anchorStep / g_anchorMaxAZ 的相关性
// 覆盖两种路径：经典路径 与 SLIME_FORCE_CHUNKED=1 强制分块路径
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace slime;
using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static void runCase(bool forceChunked)
{
    const int half = 100; // 200x200
    const int minX = -half, maxX = half - 1, minZ = -half, maxZ = half - 1;
    const int64_t budgetBytes = 200000; // 小预算：强制 pruneToFit 触发
    std::printf("\n===== [20] 缓存完整性：%s路径 (200x200, 预算=%lld B) =====\n",
                forceChunked ? "强制分块" : "经典", static_cast<long long>(budgetBytes));

    CacheManager c;
    std::vector<std::pair<size_t, size_t>> rangesBeforeDrop; // (index, entries) 快照用
    SearchOptions opt;
    opt.version = Version::Bedrock;
    opt.mode = Mode::Density;
    opt.gridSize = 5;
    opt.minDensity = 0.75;
    opt.minX = minX; opt.maxX = maxX; opt.minZ = minZ; opt.maxZ = maxZ;
    opt.worldSeed = 0;
    opt.targetCount = 1;
    opt.stopAtTarget = false; // 全范围扫描/收集（漏存测试必须走满）
    opt.threads = 0;
    opt.useCache = true;
    opt.cache = &c;
    opt.cacheWrite = &c;
    opt.cacheBudgetBytes = budgetBytes;

    SlimeEngine eng;
    std::atomic<bool> cancel{ false };
    SearchOutcome out1;
    const auto t0 = Clock::now();
    eng.run(opt, cancel, nullptr, out1);
    const auto t1 = Clock::now();
    std::printf("运行#1(开缓存) ok=%d chunked=%d ms=%.0f scanned=%lld results=%zu\n", out1.ok ? 1 : 0,
                out1.chunkedPath ? 1 : 0, msSince(t0, t1),
                static_cast<long long>(out1.stats.scannedChunks), out1.results.size());
    const size_t rangesAfterRun = c.ranges().size();
    const int64_t entriesAfterRun = c.entryCount();
    std::printf("  收集后: ranges=%zu entries=%lld sizeBytes=%lld\n", rangesAfterRun,
                static_cast<long long>(entriesAfterRun), static_cast<long long>(c.sizeBytes()));

    // 落盘 + 清空 + 重载（记 cacheLoadMs）
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\slime_case20.slimecache";
    std::string err;
    const bool saved = c.saveFile(path, err);
    std::printf("  saveFile=%d err='%s' 文件=%lld B\n", saved ? 1 : 0, err.c_str(),
                static_cast<long long>(c.sizeBytes()));
    c.clear();
    const auto t2 = Clock::now();
    const bool loaded = c.loadFile(path, true, err);
    const auto t3 = Clock::now();
    const double cacheLoadMs = msSince(t2, t3);
    std::printf("  clear+loadFile=%d cacheLoadMs=%.1f ranges=%zu entries=%lld\n", loaded ? 1 : 0,
                cacheLoadMs, c.ranges().size(), static_cast<long long>(c.entryCount()));
    std::printf("  pruneToFit 触发=%s（丢 range 数=%zu）\n",
                rangesAfterRun < static_cast<size_t>(1) + 0 ? "?" : "见上（收集后 ranges 少于写入块数即为触发）",
                rangesAfterRun);
    // 被丢 range 是否仍被 covers() 当覆盖：检查「本应覆盖但不在 ranges」的行段
    for (const Rect& r : c.ranges()) {
        std::printf("  range: %d..%d x %d..%d covers(whole)=%d covers(首行)=%d\n", r.minX, r.maxX, r.minZ,
                    r.maxZ, c.covers(r) ? 1 : 0, c.covers(Rect{ r.minX, r.maxX, r.minZ, r.minZ }) ? 1 : 0);
    }

    // 逐点真值比对（全范围，40,000 点）
    // v3.3.8 候选 ② 后：声明域 = 锚点行域 ⇒ 区域末尾 hMax-1 行不在声明内（lookup==-1），
    // 这类点单独计入「未覆盖」，不再算作「缺失」（缺失 = 声明覆盖内被判成非史莱姆）。
    int64_t truthSlime = 0, storedSlime = 0, missing = 0, extra = 0, coveredPts = 0, uncoveredSlime = 0;
    std::map<int, int> missingPerRow;
    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const bool truth = SlimeEngine::isSlimeChunk(Version::Bedrock, 0, x, z);
            const int lk = c.lookup(x, z);
            if (lk >= 0) ++coveredPts;
            if (truth) ++truthSlime;
            if (lk == 1) ++storedSlime;
            if (truth && lk < 0) ++uncoveredSlime;
            if (truth && lk == 0) { ++missing; ++missingPerRow[z]; }
            if (!truth && lk == 1) ++extra;
        }
    }
    std::printf("  逐点：真值史莱姆=%lld 缓存史莱姆=%lld 缺失=%lld 未覆盖(真值为史莱姆但不在声明内)=%lld "
                "多余=%lld 覆盖率=%.1f%%\n",
                static_cast<long long>(truthSlime), static_cast<long long>(storedSlime),
                static_cast<long long>(missing), static_cast<long long>(uncoveredSlime),
                static_cast<long long>(extra), 100.0 * static_cast<double>(coveredPts) / 40000.0);
    if (truthSlime > 0) {
        std::printf("  >>> 缺失率=%.2f%%  一致率=%.2f%%  cacheLoadMs=%.1f\n",
                    100.0 * static_cast<double>(missing) / static_cast<double>(truthSlime),
                    100.0 * static_cast<double>((40000 - missing - extra)) / 40000.0, cacheLoadMs);
    }
    if (!missingPerRow.empty()) {
        std::printf("  Z 行直方图（缺失行，最多列 10 行）：\n");
        int shown = 0;
        for (const auto& kv : missingPerRow) {
            std::printf("    z=%d 缺失=%d\n", kv.first, kv.second);
            if (++shown >= 10) break;
        }
        const int firstZ = missingPerRow.begin()->first;
        const int lastZ = missingPerRow.rbegin()->first;
        std::printf("    缺失行范围: z=%d..%d（共 %zu 行有缺失）\n", firstZ, lastZ, missingPerRow.size());
    }
    std::printf("  与 anchorStep/g_anchorMaxAZ 相关性：强制分块时 anchorStep=rows-5+1；若缺失集中在某行段见上直方图\n");
}

int main()
{
    runCase(false);
    if (std::getenv("SLIME_FORCE_CHUNKED")) runCase(true);
    else std::printf("\n（未设置 SLIME_FORCE_CHUNKED，跳过强制分块路径）\n");
    return 0;
}

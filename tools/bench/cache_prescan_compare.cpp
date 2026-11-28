// tools/bench/cache_prescan_compare.cpp - P1 验证探针（v3.15.x）
//
// 目的：验证「缓存预筛 cachePrescanSeeds」与「GPU 正方形预筛 prescanDensitySeeds」
//   在相同区域/参数下给出相同候选种子，并对比环形/k3 规划的耗时与结果；
//   同时支持对真实本地缓存（CacheStore）做同样的耗时/结果对照。
//
// 用法：
//   slime_cache_prescan_compare synthetic [size=4096]        # 用引擎 Bedrock 判定合成缓存（严格对照）
//   slime_cache_prescan_compare real [size=4096] [cache根]   # 用真实本地缓存（仅诊断；缓存来源未必等于本机算法）
//
// 可选环境变量：
//   CPS_GRID（默认 15）/ CPS_MIN_DENSITY（默认 0.18）/ CPS_BAND_ROWS（默认 4096）
//   CPS_TOP_PER_BAND（默认 512）/ CPS_MAX_SEEDS（默认 4000）/ CPS_SEP（默认 16）
//   CPS_STRICT_PARITY=1  synthetic 下要求种子逐条一致（把 topPerBand 调大、bandRows 调小可做到）
//   CPS_SKIP_GPU=1  只跑缓存预筛
//   CPS_SKIP_PLAN=1 跳过环形/k3 规划对照
//
// 退出码：0 = 对照通过（真缓存模式下只要求两条路径都能产出种子）；2 = 参数/加载失败；
//         3 = 缓存种子的窗口计数与位图不一致，或 strict 模式下两条路径种子不一致。

#include "cache/CacheStore.h"
#include "core/AfkPlanner.h"
#include "core/CacheManager.h"
#include "core/CachePrescan.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace slime;
using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static int envInt(const char* name, int def)
{
    const char* v = std::getenv(name);
    if (!v || !*v) return def;
    return std::atoi(v);
}

static double envDouble(const char* name, double def)
{
    const char* v = std::getenv(name);
    if (!v || !*v) return def;
    return std::atof(v);
}

static DensityPrescanParams makeParams(const Rect& r)
{
    DensityPrescanParams p;
    p.version = Version::Bedrock;   // 本地缓存仅 Bedrock；GPU 预筛同样按 Bedrock 判定
    p.worldSeed = 0;                // Bedrock 判定与种子无关
    p.minX = r.minX;
    p.maxX = r.maxX;
    p.minZ = r.minZ;
    p.maxZ = r.maxZ;
    p.gridSize = envInt("CPS_GRID", 15);
    p.minDensity = envDouble("CPS_MIN_DENSITY", 0.18);
    p.bandRows = envInt("CPS_BAND_ROWS", 4096);
    p.topPerBand = envInt("CPS_TOP_PER_BAND", 512);
    p.maxSeeds = envInt("CPS_MAX_SEEDS", 4000);
    p.minSeparation = envInt("CPS_SEP", 16);
    return p;
}

// 用引擎 Bedrock 判定在内存里构造一个完整覆盖 rect 的缓存位图（与 CacheManager 行宽一致）。
static bool synthCache(CacheManager& cm, const Rect& rect, std::string* err)
{
    const int w = rect.maxX - rect.minX + 1;
    const int h = rect.maxZ - rect.minZ + 1;
    const int64_t wordsPerRow = (w + 63) / 64;
    std::vector<uint64_t> words(static_cast<size_t>(wordsPerRow * h), 0);
    std::vector<uint8_t> row(static_cast<size_t>(w));
    for (int z = 0; z < h; ++z) {
        SlimeEngine::fillSlimeRowBedrock(rect.minZ + z, rect.minX, w, row.data());
        uint64_t* dst = words.data() + static_cast<size_t>(wordsPerRow) * static_cast<size_t>(z);
        for (int x = 0; x < w; ++x) {
            if (row[static_cast<size_t>(x)]) dst[x >> 6] |= (1ULL << (x & 63));
        }
    }
    if (!cm.addPackedWords(rect, words.data(), static_cast<size_t>(words.size()))) {
        if (err) *err = "addPackedWords failed";
        return false;
    }
    return cm.hasData();
}

static void printSeeds(const char* label, const std::vector<DensitySeed>& seeds, double ms)
{
    std::printf("%s seeds=%zu ms=%.1f", label, seeds.size(), ms);
    if (!seeds.empty()) {
        std::printf(" top=(%d,%d,count=%d) last=(%d,%d,count=%d)", seeds.front().cx, seeds.front().cz,
                    seeds.front().count, seeds.back().cx, seeds.back().cz, seeds.back().count);
    }
    std::printf("\n");
}

static bool sameSeeds(const std::vector<DensitySeed>& a, const std::vector<DensitySeed>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].cx != b[i].cx || a[i].cz != b[i].cz || a[i].count != b[i].count) return false;
    }
    return true;
}

// 用缓存位图复核每个种子的窗口计数（CPU 侧独立于 CachePrescan 的校验）。
static int countBadSeedCounts(const CacheManager& cm, const std::vector<DensitySeed>& seeds, int g)
{
    int bad = 0;
    for (const auto& s : seeds) {
        int n = 0;
        for (int dz = 0; dz < g; ++dz) {
            for (int dx = 0; dx < g; ++dx) {
                const int x = s.cx - g / 2 + dx;
                const int z = s.cz - g / 2 + dz;
                if (cm.lookup(x, z) == 1) ++n;
            }
        }
        if (n != s.count) ++bad;
    }
    return bad;
}

// 计数多重集是否一致：预筛按 count 取 top，平局时两实现可能选到不同窗口，
// 因此「集合不同但计数分布相同」不算语义不一致（探针据此区分真错与平局差异）。
static bool sameCountMultiset(const std::vector<DensitySeed>& a, const std::vector<DensitySeed>& b)
{
    if (a.size() != b.size()) return false;
    std::vector<int> ca, cb;
    ca.reserve(a.size());
    cb.reserve(b.size());
    for (const auto& s : a) ca.push_back(s.count);
    for (const auto& s : b) cb.push_back(s.count);
    std::sort(ca.begin(), ca.end());
    std::sort(cb.begin(), cb.end());
    return ca == cb;
}

struct PlanResult {
    double baseMs = 0.0;
    double k3Ms = 0.0;
    double planMs = 0.0;
    int k3Candidates = -1;
    bool cancelled = false;
    std::vector<AfkCandidate> cands;
};

// 复刻 MainWindowRingMap.inc 的 k3 档流程（预筛种子 → 基线精修取下界 → 36-offset 上界过滤 → 精修）。
static PlanResult runK3Flow(const AfkPlanOptions& base, const SlimeChunkPredicate& pred,
                            const std::vector<std::pair<int, int>>& seeds)
{
    PlanResult r;
    AfkPlanOptions o = base;
    o.seedCandidates = seeds;
    const auto t0 = Clock::now();
    r.cands = AfkPlanner::plan(o, pred, nullptr);
    r.baseMs = msSince(t0);
    const int lower = r.cands.empty() ? 0 : static_cast<int>(r.cands.front().coveredBlocks);
    const auto t1 = Clock::now();
    const auto filtered = AfkPlanner::k3UpperBoundSeeds(o, seeds, pred, lower);
    r.k3Ms = msSince(t1);
    r.k3Candidates = static_cast<int>(filtered.size());
    if (!filtered.empty()) o.seedCandidates = filtered;
    const auto t2 = Clock::now();
    r.cands = AfkPlanner::plan(o, pred, nullptr);
    r.planMs = msSince(t2);
    return r;
}

static void reportPlan(const char* label, const PlanResult& r)
{
    std::printf("%s baseMs=%.1f k3Ms=%.1f planMs=%.1f k3Candidates=%d cands=%zu", label, r.baseMs, r.k3Ms,
                r.planMs, r.k3Candidates, r.cands.size());
    if (!r.cands.empty()) {
        const auto& c = r.cands.front();
        std::printf(" best=(%d,%d) blocks=%lld pct=%.4f", c.x, c.z, static_cast<long long>(c.coveredBlocks),
                    c.coveragePercent);
    }
    std::printf("\n");
}

int main(int argc, char** argv)
{
    const std::string mode = (argc > 1) ? argv[1] : "synthetic";
    const int size = (argc > 2) ? std::max(256, std::atoi(argv[2])) : 4096;
    const bool synthetic = (mode != "real");
    const bool skipGpu = envInt("CPS_SKIP_GPU", 0) != 0;
    const bool skipPlan = envInt("CPS_SKIP_PLAN", 0) != 0;

    Rect rect;
    CacheManager cm;
    std::string err;

    if (synthetic) {
        const int half = size / 2;
        rect = Rect{ -half, half - 1, -half, half - 1 };
        // 对齐到 64 区块，避免行宽/尾位差异干扰对照。（CachePrescan 与 GPU 都不要求对齐，
        //   这里只让合成缓存的行打包与 CacheManager 行宽严格一致。）
        const auto t0 = Clock::now();
        if (!synthCache(cm, rect, &err)) {
            std::printf("SYNTH FAIL: %s\n", err.c_str());
            return 2;
        }
        std::printf("synth region=(%d,%d)-(%d,%d) chunks=%lld bitmapMB=%.2f buildMs=%.1f\n", rect.minX,
                    rect.minZ, rect.maxX, rect.maxZ,
                    static_cast<long long>(static_cast<int64_t>(size) * size),
                    static_cast<double>(cm.bitmapBytes()) / (1024.0 * 1024.0), msSince(t0));
    } else {
        const char* rootArg = (argc > 3) ? argv[3] : nullptr;
        std::filesystem::path root;
        if (rootArg) {
            root = rootArg;
        } else {
            std::printf("REAL FAIL: 需要传入缓存根目录（含 index.json）\n");
            return 2;
        }
        cache::CacheStore store(root);
        std::string loadErr;
        if (!store.load(&loadErr, true)) {
            std::printf("REAL FAIL: load %s: %s\n", root.string().c_str(), loadErr.c_str());
            return 2;
        }
        const auto& packs = store.packs();
        std::printf("real cache root=%s packs=%zu compressedMB=%.1f\n", root.string().c_str(), packs.size(),
                    static_cast<double>(store.totalCompressedBytes()) / (1024.0 * 1024.0));
        if (packs.empty()) {
            std::printf("REAL FAIL: 缓存没有 pack\n");
            return 2;
        }
        const auto& e = packs.front();
        const int sizeC = std::min(size, static_cast<int>(std::min(e.rect.width(), e.rect.height())));
        const int half = sizeC / 2;
        const int64_t cx = (static_cast<int64_t>(e.rect.minX) + e.rect.maxX) / 2;
        const int64_t cz = (static_cast<int64_t>(e.rect.minZ) + e.rect.maxZ) / 2;
        rect = Rect{ static_cast<int>(cx - half), static_cast<int>(cx - half + sizeC - 1),
                     static_cast<int>(cz - half), static_cast<int>(cz - half + sizeC - 1) };
        const auto snap = store.snapshot();
        if (!store.coversRange(snap, rect)) {
            std::printf("REAL FAIL: 选出的区域未被缓存完整覆盖\n");
            return 2;
        }
        const auto t0 = Clock::now();
        if (!store.fillCacheManagerRange(snap, cm, rect, &err) || !cm.hasData()) {
            std::printf("REAL FAIL: fillCacheManagerRange: %s\n", err.c_str());
            return 2;
        }
        std::printf("real region=(%d,%d)-(%d,%d) chunks=%lld bitmapMB=%.2f loadMs=%.1f\n", rect.minX, rect.minZ,
                    rect.maxX, rect.maxZ,
                    static_cast<long long>(static_cast<int64_t>(sizeC) * sizeC),
                    static_cast<double>(cm.bitmapBytes()) / (1024.0 * 1024.0), msSince(t0));
    }

    const DensityPrescanParams pp = makeParams(rect);
    std::printf("params grid=%d minDensity=%.3f bandRows=%d topPerBand=%d maxSeeds=%d sep=%d\n", pp.gridSize,
                pp.minDensity, pp.bandRows, pp.topPerBand, pp.maxSeeds, pp.minSeparation);

    std::vector<DensitySeed> cacheSeeds;
    std::string cErr;
    const auto tc = Clock::now();
    const bool cacheOk = cachePrescanSeeds(cm, pp, nullptr, cacheSeeds, &cErr);
    const double cacheMs = msSince(tc);
    if (!cacheOk) {
        std::printf("CACHE PRESCAN FAIL: %s\n", cErr.c_str());
        return 2;
    }
    printSeeds("CACHE", cacheSeeds, cacheMs);
    const int badCacheCounts = countBadSeedCounts(cm, cacheSeeds, pp.gridSize);
    std::printf("CACHE seed-count verify: bad=%d/%zu\n", badCacheCounts, cacheSeeds.size());
    if (badCacheCounts != 0) {
        std::printf("CACHE PRESCAN FAIL: 种子窗口计数与缓存位图不一致\n");
        return 3;
    }

    std::vector<DensitySeed> gpuSeeds;
    double gpuMs = 0.0;
    bool gpuOk = false;
    if (!skipGpu) {
        std::string gpuErr;
        auto gpu = slimegpu::createGpuScanAdapter(&gpuErr);
        if (gpu && gpu->available()) {
            const auto tg = Clock::now();
            gpuOk = gpu->prescanDensitySeeds(pp, std::atomic<bool>{}, gpuSeeds, &gpuErr);
            gpuMs = msSince(tg);
            if (!gpuOk) std::printf("GPU PRESCAN FAIL: %s\n", gpuErr.c_str());
        } else {
            std::printf("GPU unavailable: %s\n", gpuErr.c_str());
        }
        if (gpuOk) {
            printSeeds("GPU", gpuSeeds, gpuMs);
            const bool same = sameSeeds(cacheSeeds, gpuSeeds);
            int common = 0;
            std::vector<std::pair<int, int>> gset;
            gset.reserve(gpuSeeds.size());
            for (const auto& s : gpuSeeds) gset.emplace_back(s.cx, s.cz);
            std::sort(gset.begin(), gset.end());
            for (const auto& s : cacheSeeds)
                if (std::binary_search(gset.begin(), gset.end(), std::make_pair(s.cx, s.cz))) ++common;
            const bool sameCounts = sameCountMultiset(cacheSeeds, gpuSeeds);
            const int badGpuCounts = countBadSeedCounts(cm, gpuSeeds, pp.gridSize);
            std::printf(
                "PARITY seedsIdentical=%d setCommon=%zu cache=%zu gpu=%zu countMultisetEqual=%d gpuCountBad=%d "
                "speedup=%.2fx\n",
                same ? 1 : 0, common, cacheSeeds.size(), gpuSeeds.size(), sameCounts ? 1 : 0, badGpuCounts,
                gpuMs / std::max(1e-9, cacheMs));
            if (synthetic && envInt("CPS_STRICT_PARITY", 0) != 0 && !same) {
                std::printf("SYNTH PARITY FAIL (strict): cacheOnly=%zu gpuOnly=%zu\n", cacheSeeds.size() - common,
                            gpuSeeds.size() - common);
                return 3;
            }
        }
    }

    if (skipPlan) return 0;

    // 环形/k3 规划对照（缓存谓词；Bedrock 无群系权重）。
    const SlimeChunkPredicate pred = [&cm](int cx, int cz) {
        const int hit = cm.lookup(cx, cz);
        return hit == 1;
    };
    AfkPlanOptions base;
    base.area = Rect{ rect.minX + 8, rect.maxX - 8, rect.minZ + 8, rect.maxZ - 8 };
    base.radius = std::max(16, envInt("CPS_RADIUS", 128));
    base.innerRadius = std::max(0, envInt("CPS_INNER", 24));
    base.topN = 1;
    base.minDensity = 0.0;
    base.minSeparation = 24;
    base.coarseStep = 4;
    base.statBands = 4;
    base.scanStride = 0;
    base.gpuScan = nullptr;
    base.rowSlime = [](int cz, int cx0, int n, uint8_t* out) {
        SlimeEngine::fillSlimeRowBedrock(cz, cx0, n, out);
    };

    std::vector<std::pair<int, int>> cachePairs;
    cachePairs.reserve(cacheSeeds.size());
    for (const auto& s : cacheSeeds) cachePairs.emplace_back(s.cx, s.cz);
    const PlanResult cachePlan = runK3Flow(base, pred, cachePairs);
    reportPlan("PLAN-CACHE", cachePlan);

    if (gpuOk) {
        std::vector<std::pair<int, int>> gpuPairs;
        gpuPairs.reserve(gpuSeeds.size());
        for (const auto& s : gpuSeeds) gpuPairs.emplace_back(s.cx, s.cz);
        const PlanResult gpuPlan = runK3Flow(base, pred, gpuPairs);
        reportPlan("PLAN-GPU  ", gpuPlan);
        const bool sameBest = !cachePlan.cands.empty() && !gpuPlan.cands.empty() &&
                              cachePlan.cands.front().x == gpuPlan.cands.front().x &&
                              cachePlan.cands.front().z == gpuPlan.cands.front().z &&
                              cachePlan.cands.front().coveredBlocks == gpuPlan.cands.front().coveredBlocks;
        std::printf("PLAN PARITY bestCandidate identical=%d\n", sameBest ? 1 : 0);
    }

    std::printf("RESULT OK\n");
    return 0;
}

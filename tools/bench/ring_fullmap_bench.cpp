#include "core/AfkPlanner.h"
#include "core/BiomeMap.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace slime;
using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static int floorDiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int main(int argc, char** argv)
{
    const bool usePrescan = argc < 2 || std::string(argv[1]) != "coarse";
    const bool localMode = argc > 1 && std::string(argv[1]) == "local";
    const int topN = std::max(1, std::atoi(std::getenv("RING_TOP_N") ? std::getenv("RING_TOP_N") : "1"));
    const int maxSeeds = std::max(1, std::atoi(std::getenv("RING_MAX_SEEDS") ? std::getenv("RING_MAX_SEEDS") : "4000"));
    const int gridSize = std::max(1, std::atoi(std::getenv("RING_GRID") ? std::getenv("RING_GRID") : "15"));
    const int minDensityPct = std::atoi(std::getenv("RING_MIN_DENSITY") ? std::getenv("RING_MIN_DENSITY") : "18");
    const int minSep = std::atoi(std::getenv("RING_MIN_SEP") ? std::getenv("RING_MIN_SEP") : "24");
    const int topPerBand = std::max(1, std::atoi(std::getenv("RING_TOP_PER_BAND") ? std::getenv("RING_TOP_PER_BAND") : "512"));
    const int localRadius = std::max(1, std::atoi(std::getenv("RING_LOCAL_RADIUS") ? std::getenv("RING_LOCAL_RADIUS") : "512"));
    const int localStep = std::max(1, std::atoi(std::getenv("RING_LOCAL_STEP") ? std::getenv("RING_LOCAL_STEP") : "32"));
    const int prescanSep = std::max(1, std::atoi(std::getenv("RING_PRESCAN_SEP") ? std::getenv("RING_PRESCAN_SEP") : "16"));
    const int planSeeds = std::max(1, std::atoi(std::getenv("RING_PLAN_SEEDS") ? std::getenv("RING_PLAN_SEEDS") : "4000"));
    const bool weightedSeeds = std::getenv("RING_WEIGHTED_SEEDS") && std::string(std::getenv("RING_WEIGHTED_SEEDS")) == "1";
    const bool ringRankSeeds = std::getenv("RING_RING_RANK") && std::string(std::getenv("RING_RING_RANK")) == "1";
    const bool aroundSeeds = std::getenv("RING_AROUND") && std::string(std::getenv("RING_AROUND")) == "1";
    const bool unweightedPlan = std::getenv("RING_UNWEIGHTED_PLAN") && std::string(std::getenv("RING_UNWEIGHTED_PLAN")) == "1";
    const int aroundRadius = std::max(1, std::atoi(std::getenv("RING_AROUND_RADIUS") ? std::getenv("RING_AROUND_RADIUS") : "128"));
    const int aroundStep = std::max(1, std::atoi(std::getenv("RING_AROUND_STEP") ? std::getenv("RING_AROUND_STEP") : "32"));
    const int baseX = std::atoi(std::getenv("RING_CX") ? std::getenv("RING_CX") : "3472965");
    const int baseZ = std::atoi(std::getenv("RING_CZ") ? std::getenv("RING_CZ") : "-13446988");
    const int64_t seed = std::strtoll(std::getenv("RING_SEED") ? std::getenv("RING_SEED") : "0", nullptr, 10);
    const int minX = -1875000, maxX = 1875000;
    const int minZ = -1875000, maxZ = 1875000;
    const int outer = std::max(1, std::atoi(std::getenv("RING_RADIUS") ? std::getenv("RING_RADIUS") : "128"));
    const int inner = std::max(0, std::atoi(std::getenv("RING_INNER") ? std::getenv("RING_INNER") : "24"));

    const bool pointMode = std::getenv("RING_POINT") && std::string(std::getenv("RING_POINT")) == "1";
    if (pointMode) {
        errno = 0;
        const char* seedText = std::getenv("RING_SEED") ? std::getenv("RING_SEED") : "0";
        const int64_t pointSeed = std::strtoll(seedText, nullptr, 10);
        if (errno == ERANGE) {
            std::printf("POINT seed=%s ERROR=int64 overflow\n", seedText);
            return 2;
        }
        const int cx = std::atoi(std::getenv("RING_CX") ? std::getenv("RING_CX") : "0");
        const int cz = std::atoi(std::getenv("RING_CZ") ? std::getenv("RING_CZ") : "0");
        const bool weighted = !std::getenv("RING_WEIGHTED") || std::string(std::getenv("RING_WEIGHTED")) != "0";
        AfkPlanOptions po;
        po.area = Rect{ minX, maxX, minZ, maxZ };
        po.radius = outer;
        po.innerRadius = inner;
        po.topN = 1;
        const auto pointPred = [pointSeed](int x, int z) {
            return SlimeEngine::isSlimeChunk(Version::Java, pointSeed, x, z);
        };
        const auto t0 = Clock::now();
        if (weighted) {
            std::vector<AfkCandidate> cands(1);
            cands[0].x = cx;
            cands[0].z = cz;
            AfkPlanner::measureStrictForCandidates(cands, po, pointPred, defaultJavaVersionId(), pointSeed,
                                                   true, nullptr);
            std::printf("POINT weighted seed=%lld cx=%d cz=%d blocks=%lld weight=%.6f pct=%.6f chunks=%d ms=%.1f\n",
                        static_cast<long long>(pointSeed), cx, cz,
                        static_cast<long long>(cands[0].coveredBlocks), cands[0].coveredWeight,
                        cands[0].coveragePercent, cands[0].coveredChunks, msSince(t0));
        } else {
            const auto m = AfkPlanner::measureExact(cx, cz, po, pointPred);
            std::printf("POINT unweighted seed=%lld cx=%d cz=%d blocks=%lld pct=%.6f chunks=%d ms=%.1f\n",
                        static_cast<long long>(pointSeed), cx, cz,
                        static_cast<long long>(m.coveredBlocks), m.coveragePercent, m.coveredChunks, msSince(t0));
        }
        return 0;
    }

    const auto tTotal = Clock::now();
    const auto tPrep = Clock::now();

    std::string gpuErr;
    auto gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (gpu && !gpu->available()) gpu.reset();
    std::printf("gpu=%s err=%s\n", gpu ? gpu->deviceName().c_str() : "none", gpuErr.c_str());

    const auto pred = [seed](int cx, int cz) { return SlimeEngine::isSlimeChunk(Version::Java, seed, cx, cz); };

    const int javaVersionId = defaultJavaVersionId();
    BiomeMap biome;
    const bool biomeOk = biome.init(javaVersionId, seed);
    std::shared_ptr<BiomeWeightGrid> wgrid;
    int weightScale = 0;
    int gridW = 0, gridH = 0;
    if (biomeOk) {
        int gs = AfkPlanner::rankingGridScale((std::max)(maxX - minX + 1, maxZ - minZ + 1));
        const int pad = (outer + 15) / 16 + 2;
        const int b0x = (minX - pad) * 16, b0z = (minZ - pad) * 16;
        const int b1x = (maxX + pad) * 16, b1z = (maxZ + pad) * 16;
        while (static_cast<int64_t>((b1x - b0x) / gs + 1) * ((b1z - b0z) / gs + 1) > 2000000LL && gs < (1 << 20))
            gs <<= 1;
        weightScale = gs;
        const int c0x = floorDiv(b0x, gs), c0z = floorDiv(b0z, gs);
        const int c1x = floorDiv(b1x, gs), c1z = floorDiv(b1z, gs);
        gridW = c1x - c0x + 1;
        gridH = c1z - c0z + 1;
        wgrid = std::make_shared<BiomeWeightGrid>();
        if (wgrid->init(&biome, Platform::Java, gs, c0x * gs, c0z * gs, gridW, gridH)) {
            std::printf("weightGrid=%dx%d scale=%d cells=%zu\n", gridW, gridH, gs, wgrid->cells());
        } else {
            wgrid.reset();
        }
    }
    const double prepMs = msSince(tPrep);

    AfkPlanOptions o;
    o.area = Rect{ minX, maxX, minZ, maxZ };
    o.radius = outer;
    o.innerRadius = inner;
    o.topN = topN;
    o.minDensity = 0.0;
    o.minSeparation = minSep;
    o.statBands = 4;
    o.scanStride = 0;
    o.coarseStep = 4;
    o.gpuScan = gpu.get();
    o.rowSlime = [seed](int cz, int cx0, int n, uint8_t* out) {
        SlimeEngine::fillSlimeRowJava(seed, cz, cx0, n, out);
    };
    if (wgrid && !unweightedPlan) {
        o.weightAt = [wgrid](int bx, int bz) { return wgrid->weightAt(bx, bz); };
    }

    if (localMode) {
        std::vector<AfkCandidate> cands;
        const int r = localRadius;
        for (int dz = -r; dz <= r; dz += localStep) {
            for (int dx = -r; dx <= r; dx += localStep) {
                AfkCandidate c;
                c.x = baseX + dx;
                c.z = baseZ + dz;
                cands.push_back(c);
            }
        }
        const auto tl = Clock::now();
        if (biomeOk) AfkPlanner::measureStrictForCandidates(cands, o, pred, javaVersionId, seed, true, nullptr);
        const double localMs = msSince(tl);
        const auto best = std::max_element(cands.begin(), cands.end(), [](const AfkCandidate& a, const AfkCandidate& b) {
            return a.coveredWeight < b.coveredWeight;
        });
        std::printf("LOCAL ok=%d candidates=%zu local_ms=%.1f total_ms=%.1f cx=%d cz=%d blocks=%lld "
                    "weight=%.3f pct=%.6f\n",
                    biomeOk ? 1 : 0, cands.size(), localMs, msSince(tTotal), best->x, best->z,
                    static_cast<long long>(best->coveredBlocks), best->coveredWeight, best->coveragePercent);
        return 0;
    }

    double prescanMs = 0.0;
    bool prescanUsed = false;
    bool prescanFallback = false;
    if (usePrescan) {
        const auto t0 = Clock::now();
        if (gpu) {
            DensityPrescanParams pp;
            pp.version = Version::Java;
            pp.worldSeed = seed;
            pp.minX = minX; pp.maxX = maxX;
            pp.minZ = minZ; pp.maxZ = maxZ;
            pp.gridSize = gridSize;
            pp.minDensity = static_cast<double>(minDensityPct) / 100.0;
            pp.bandRows = 4096;
            pp.topPerBand = topPerBand;
            pp.maxSeeds = maxSeeds;
            pp.minSeparation = prescanSep;
            std::vector<DensitySeed> seeds;
            std::string err;
            if (gpu->prescanDensitySeeds(pp, std::atomic<bool>{}, seeds, &err) && !seeds.empty()) {
                if (ringRankSeeds) {
                    std::vector<std::pair<double, DensitySeed>> ranked;
                    ranked.reserve(seeds.size());
                    for (const auto& s : seeds) {
                        int innerCount = 0;
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx)
                                if (pred(s.cx + dx, s.cz + dz)) ++innerCount;
                        const double ringScore = static_cast<double>(std::max(0, s.count - innerCount));
                        ranked.emplace_back(ringScore, s);
                    }
                    std::sort(ranked.begin(), ranked.end(),
                              [](const auto& a, const auto& b) { return a.first > b.first; });
                    seeds.clear();
                    const size_t keep = (std::min)(ranked.size(), static_cast<size_t>(planSeeds));
                    for (size_t i = 0; i < keep; ++i) seeds.push_back(ranked[i].second);
                } else if (weightedSeeds && wgrid) {
                    std::vector<std::pair<double, DensitySeed>> ranked;
                    ranked.reserve(seeds.size());
                    for (const auto& s : seeds) {
                        const int bx = s.cx * 16, bz = s.cz * 16;
                        ranked.emplace_back(static_cast<double>(s.count) * wgrid->weightAt(bx, bz), s);
                    }
                    std::sort(ranked.begin(), ranked.end(),
                              [](const auto& a, const auto& b) { return a.first > b.first; });
                    seeds.clear();
                    const size_t keep = (std::min)(ranked.size(), static_cast<size_t>(planSeeds));
                    for (size_t i = 0; i < keep; ++i) seeds.push_back(ranked[i].second);
                } else if (static_cast<int>(seeds.size()) > planSeeds) {
                    seeds.resize(static_cast<size_t>(planSeeds));
                }
                o.seedCandidates.reserve(seeds.size());
                for (const auto& s : seeds) o.seedCandidates.emplace_back(s.cx, s.cz);
                prescanUsed = true;
                std::printf("prescanSeeds=%zu topN=%d maxSeeds=%d planSeeds=%d grid=%d minDensity=%.2f minSep=%d weightedSeeds=%d\n",
                            seeds.size(), topN, maxSeeds, planSeeds, gridSize,
                            static_cast<double>(minDensityPct) / 100.0, minSep, weightedSeeds ? 1 : 0);
            } else {
                prescanFallback = true;
                std::printf("prescanFallback err=%s\n", err.c_str());
            }
        } else {
            prescanFallback = true;
        }
        prescanMs = msSince(t0);
    }

    const auto tPlan = Clock::now();
    if (aroundSeeds && !o.seedCandidates.empty()) {
        std::vector<AfkCandidate> localCands;
        const int r = aroundRadius;
        localCands.reserve(o.seedCandidates.size() * static_cast<size_t>(((2 * r) / aroundStep + 1) *
                                                                       ((2 * r) / aroundStep + 1)));
        for (const auto& s : o.seedCandidates) {
            const int bx = s.first * 16 + 8;
            const int bz = s.second * 16 + 8;
            for (int dz = -r; dz <= r; dz += aroundStep) {
                for (int dx = -r; dx <= r; dx += aroundStep) {
                    AfkCandidate c;
                    c.x = bx + dx;
                    c.z = bz + dz;
                    localCands.push_back(c);
                }
            }
        }
        const auto tLocal = Clock::now();
        if (biomeOk) AfkPlanner::measureStrictForCandidates(localCands, o, pred, javaVersionId, seed, true, nullptr);
        const double localMs = msSince(tLocal);
        const auto best = std::max_element(localCands.begin(), localCands.end(),
                                           [](const AfkCandidate& a, const AfkCandidate& b) {
                                               return a.coveredWeight < b.coveredWeight;
                                           });
        std::printf("AROUND ok=%d seeds=%zu candidates=%zu around_ms=%.1f total_ms=%.1f cx=%d cz=%d blocks=%lld "
                    "weight=%.3f pct=%.6f\n",
                    biomeOk ? 1 : 0, o.seedCandidates.size(), localCands.size(), localMs, msSince(tTotal),
                    best->x, best->z, static_cast<long long>(best->coveredBlocks), best->coveredWeight,
                    best->coveragePercent);
        return 0;
    }
    std::vector<AfkCandidate> cands;
    try {
        cands = AfkPlanner::plan(o, pred, nullptr);
    } catch (const std::exception& e) {
        std::printf("planException=%s\n", e.what());
        return 2;
    } catch (...) {
        std::printf("planException=unknown\n");
        return 2;
    }
    const double planMs = msSince(tPlan);

    const auto tStrict = Clock::now();
    if (!cands.empty() && biomeOk) {
        AfkPlanner::measureStrictForCandidates(cands, o, pred, javaVersionId, seed, true, nullptr);
    }
    const double strictMs = msSince(tStrict);

    if (!cands.empty()) {
        const auto best = std::max_element(cands.begin(), cands.end(), [](const AfkCandidate& a, const AfkCandidate& b) {
            return a.coveredWeight < b.coveredWeight;
        });
        const double totalMs = msSince(tTotal);
        std::printf("RESULT ok=1 total_ms=%.1f prep_ms=%.1f prescan_ms=%.1f plan_ms=%.1f strict_ms=%.1f "
                    "prescan=%d fallback=%d weight=%d cx=%d cz=%d blocks=%lld weight=%.3f pct=%.6f\n",
                    totalMs, prepMs, prescanMs, planMs, strictMs, prescanUsed ? 1 : 0,
                    prescanFallback ? 1 : 0, wgrid ? 1 : 0, best->x, best->z,
                    static_cast<long long>(best->coveredBlocks), best->coveredWeight, best->coveragePercent);
    } else {
        std::printf("RESULT ok=0 total_ms=%.1f prep_ms=%.1f prescan_ms=%.1f plan_ms=%.1f strict_ms=%.1f "
                    "prescan=%d fallback=%d weight=%d\n",
                    msSince(tTotal), prepMs, prescanMs, planMs, strictMs, prescanUsed ? 1 : 0,
                    prescanFallback ? 1 : 0, wgrid ? 1 : 0);
    }
    return 0;
}

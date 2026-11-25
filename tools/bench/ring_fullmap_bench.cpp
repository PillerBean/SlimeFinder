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
#include <fstream>
#include <iomanip>
#include <memory>
#include <random>
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
    const bool randomMode = argc > 1 && std::string(argv[1]) == "random";
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
    const bool subgridMode = std::getenv("RING_SUBGRID") && std::string(std::getenv("RING_SUBGRID")) == "1";
    const int poolTopSeeds = std::max(1, std::atoi(std::getenv("RING_POOL_TOP_SEEDS") ? std::getenv("RING_POOL_TOP_SEEDS") : "32"));
    const int poolKeep = std::max(1, std::atoi(std::getenv("RING_POOL_K") ? std::getenv("RING_POOL_K") : "10000"));
    const char* poolCsv = std::getenv("RING_POOL_CSV");
    const bool kernelRank = std::getenv("RING_KERNEL_RANK") && std::string(std::getenv("RING_KERNEL_RANK")) == "1";
    const bool kernelPlan = std::getenv("RING_KERNEL_PLAN") && std::string(std::getenv("RING_KERNEL_PLAN")) == "1";
    const bool k3Mode = std::getenv("RING_K3") && std::string(std::getenv("RING_K3")) == "1";
    const int k3Err = std::max(0, std::atoi(std::getenv("RING_K3_ERR") ? std::getenv("RING_K3_ERR") : "860"));
    const int kernelTop = std::max(1, std::atoi(std::getenv("RING_KERNEL_TOP") ? std::getenv("RING_KERNEL_TOP") : "32"));
    const char* kernelCsv = std::getenv("RING_KERNEL_CSV");
    const int kernelSquare = std::max(0, std::atoi(std::getenv("RING_KERNEL_SQUARE") ?
                                                    std::getenv("RING_KERNEL_SQUARE") : "0"));
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
    const bool heightMode = std::getenv("RING_HEIGHT") && std::string(std::getenv("RING_HEIGHT")) == "1";
    const int heightTopN = std::max(1, std::atoi(std::getenv("RING_HEIGHT_TOP_N") ? std::getenv("RING_HEIGHT_TOP_N") : "20"));
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
        const bool weighted = (!std::getenv("RING_WEIGHTED") || std::string(std::getenv("RING_WEIGHTED")) != "0") &&
                              (!std::getenv("RING_VERSION") || std::string(std::getenv("RING_VERSION")) != "bedrock");
        const bool bedrock = std::getenv("RING_VERSION") && std::string(std::getenv("RING_VERSION")) == "bedrock";
        const Version pointVersion = bedrock ? Version::Bedrock : Version::Java;
        AfkPlanOptions po;
        po.area = Rect{ minX, maxX, minZ, maxZ };
        po.radius = outer;
        po.innerRadius = inner;
        po.topN = 1;
        const auto pointPred = [pointSeed, pointVersion](int x, int z) {
            return SlimeEngine::isSlimeChunk(pointVersion, pointSeed, x, z);
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

    if (randomMode) {
        const int n = std::max(1, std::atoi(std::getenv("RING_RANDOM_N") ? std::getenv("RING_RANDOM_N") : "200000"));
        std::mt19937_64 rng(0xC0FFEEu);
        std::uniform_int_distribution<int> distX(-30000000, 29999999);
        std::uniform_int_distribution<int> distZ(-30000000, 29999999);
        std::vector<AfkCandidate> cands;
        cands.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            AfkCandidate c;
            c.x = distX(rng);
            c.z = distZ(rng);
            cands.push_back(c);
        }
        const auto tl = Clock::now();
        if (biomeOk) AfkPlanner::measureStrictForCandidates(cands, o, pred, javaVersionId, seed, true, nullptr);
        const auto best = std::max_element(cands.begin(), cands.end(), [](const AfkCandidate& a, const AfkCandidate& b) {
            return a.coveredWeight < b.coveredWeight;
        });
        std::printf("RANDOM candidates=%d ms=%.1f total_ms=%.1f cx=%d cz=%d blocks=%lld weight=%.3f pct=%.6f\n",
                    n, msSince(tl), msSince(tTotal), best->x, best->z,
                    static_cast<long long>(best->coveredBlocks), best->coveredWeight, best->coveragePercent);
        return 0;
    }

    if (localMode) {        std::vector<AfkCandidate> cands;
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

    if (heightMode && prescanUsed && !o.seedCandidates.empty()) {
        struct HeightHit { double area; int h; int cx; int cz; int ox = 8; int oz = 8; };
        const auto th0 = Clock::now();
        std::vector<HeightHit> hits;
        hits.reserve(o.seedCandidates.size());
        const int radiusChunks = outer / 16;  // 128 blocks -> 8 chunks
        for (const auto& s : o.seedCandidates) {
            const int bx = s.first * 16 + 8;
            const int bz = s.second * 16 + 8;
            double diff[32] = {};
            for (int dz = -radiusChunks; dz <= radiusChunks; ++dz) {
                for (int dx = -radiusChunks; dx <= radiusChunks; ++dx) {
                    const int cx = s.first + dx;
                    const int cz = s.second + dz;
                    if (!pred(cx, cz)) continue;
                    const int wx = bx + dx * 16;
                    const int wz = bz + dz * 16;
                    const double w = spawnWeight(biome.biomeAt(wx, wz), Platform::Java);
                    if (w <= 0.0) continue;
                    const int rho2 = dx * dx * 256 + dz * dz * 256;
                    int lo2 = inner * inner - rho2;
                    if (lo2 < 0) lo2 = 0;
                    const int hi2 = outer * outer - rho2;
                    if (hi2 < lo2) continue;
                    int lo = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(lo2))));
                    int hi = static_cast<int>(std::floor(std::sqrt(static_cast<double>(hi2))));
                    if (lo < 0) lo = 0;
                    if (hi > 24) hi = 24;  // H>24 is dominated by H=24 for non-negative weights
                    if (lo > hi) continue;
                    const double area = w * 256.0;
                    diff[lo] += area;
                    diff[hi + 1] -= area;
                }
            }
            double cur = 0.0, best = 0.0;
            int bestH = 0;
            for (int h = 0; h <= 24; ++h) {
                cur += diff[h];
                if (cur > best + 1e-9) {
                    best = cur;
                    bestH = h;
                }
            }
            if (best > 0.0) hits.push_back(HeightHit{ best, bestH, s.first, s.second });
        }
        std::sort(hits.begin(), hits.end(), [](const HeightHit& a, const HeightHit& b) {
            return a.area > b.area;
        });
        // v3.15.x: refine the top chunk centers over all 16×16 in-chunk offsets.
        const bool refineOffsets = !std::getenv("RING_HEIGHT_REFINE") ||
                                   std::string(std::getenv("RING_HEIGHT_REFINE")) != "0";
        if (refineOffsets) {
            const size_t top = (std::min)(hits.size(), static_cast<size_t>(heightTopN));
            for (size_t i = 0; i < top; ++i) {
                const int cx0 = hits[i].cx;
                const int cz0 = hits[i].cz;
                double wchunk[17][17] = {};
                for (int dz = -8; dz <= 8; ++dz) {
                    for (int dx = -8; dx <= 8; ++dx) {
                        const int bx = (cx0 + dx) * 16 + 8;
                        const int bz = (cz0 + dz) * 16 + 8;
                        wchunk[dz + 8][dx + 8] =
                            spawnWeight(biome.biomeAt(bx, bz), Platform::Java);
                    }
                }
                double bestArea = hits[i].area;
                int bestH = hits[i].h;
                int bestOx = 8, bestOz = 8;
                for (int oy = 0; oy < 16; ++oy) {
                    for (int ox = 0; ox < 16; ++ox) {
                        double diff2[32] = {};
                        for (int dz = -8; dz <= 8; ++dz) {
                            for (int dx = -8; dx <= 8; ++dx) {
                                if (!pred(cx0 + dx, cz0 + dz)) continue;
                                const double w = wchunk[dz + 8][dx + 8];
                                if (w <= 0.0) continue;
                                const int rx = dx * 16 + 8 - ox;
                                const int rz = dz * 16 + 8 - oy;
                                const int rho2 = rx * rx + rz * rz;
                                int lo2 = inner * inner - rho2;
                                if (lo2 < 0) lo2 = 0;
                                const int hi2 = outer * outer - rho2;
                                if (hi2 < lo2) continue;
                                int lo = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(lo2))));
                                int hi = static_cast<int>(std::floor(std::sqrt(static_cast<double>(hi2))));
                                if (lo < 0) lo = 0;
                                if (hi > 24) hi = 24;
                                if (lo > hi) continue;
                                const double area = w * 256.0;
                                diff2[lo] += area;
                                diff2[hi + 1] -= area;
                            }
                        }
                        double cur = 0.0;
                        for (int h = 0; h <= 24; ++h) {
                            cur += diff2[h];
                            if (cur > bestArea + 1e-9) {
                                bestArea = cur;
                                bestH = h;
                                bestOx = ox;
                                bestOz = oy;
                            }
                        }
                    }
                }
                hits[i].area = bestArea;
                hits[i].h = bestH;
                hits[i].ox = bestOx;
                hits[i].oz = bestOz;
                // Encode the refined block center into chunk coordinates+offset in output.
                hits[i].cx = cx0;
                hits[i].cz = cz0;
                // Keep offset for output via a side table.
                if (bestOx != 8 || bestOz != 8) {
                    std::fprintf(stderr, "[HEIGHT] refine cx=%d cz=%d offset=(%d,%d) area=%.3f h=%d\n",
                                 cx0, cz0, bestOx, bestOz, bestArea, bestH);
                }
            }
            std::sort(hits.begin(), hits.end(), [](const HeightHit& a, const HeightHit& b) {
                return a.area > b.area;
            });
        }
        const size_t keep = (std::min)(hits.size(), static_cast<size_t>(heightTopN));
        const double ms = msSince(th0);
        std::printf("HEIGHT seeds=%zu evaluated=%zu topN=%zu ms=%.1f total_ms=%.1f\n",
                    o.seedCandidates.size(), hits.size(), keep, ms, msSince(tTotal));
        for (size_t i = 0; i < keep; ++i)
            std::printf("HEIGHT_RESULT rank=%zu cx=%d cz=%d bx=%d bz=%d h=%d area=%.3f\n",
                        i + 1, hits[i].cx, hits[i].cz,
                        hits[i].cx * 16 + hits[i].ox, hits[i].cz * 16 + hits[i].oz,
                        hits[i].h, hits[i].area);
        return 0;
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
    if (k3Mode && !o.seedCandidates.empty()) {
        // Candidate-level k=3 upper bound. This is not yet a full-map 36-convolution
        // GPU implementation: the seed universe is the square prescan set.
        constexpr int d = 8;
        constexpr int side = 17;
        const int offsets[6] = { 0, 3, 6, 9, 12, 15 };
        std::vector<int> w(static_cast<size_t>(side) * side * 256, 0);
        for (int oy = 0; oy < 16; ++oy) {
            for (int ox = 0; ox < 16; ++ox) {
                for (int dy = -d; dy <= d; ++dy) {
                    for (int dx = -d; dx <= d; ++dx) {
                        int n = 0;
                        for (int by = 0; by < 16; ++by) {
                            for (int bx = 0; bx < 16; ++bx) {
                                const int ry = dy * 16 + by - oy;
                                const int rx = dx * 16 + bx - ox;
                                const int d2 = rx * rx + ry * ry;
                                if (d2 >= 24 * 24 && d2 <= 128 * 128) ++n;
                            }
                        }
                        w[(static_cast<size_t>(dy + d) * side + (dx + d)) * 256 +
                          static_cast<size_t>(oy * 16 + ox)] = n;
                    }
                }
            }
        }
        std::vector<int> upper(o.seedCandidates.size(), 0);
        const auto tu = Clock::now();
        for (size_t i = 0; i < o.seedCandidates.size(); ++i) {
            const auto s = o.seedCandidates[i];
            int best = 0;
            for (int oy : offsets) {
                for (int ox : offsets) {
                    int c = 0;
                    for (int dy = -d; dy <= d; ++dy) {
                        for (int dx = -d; dx <= d; ++dx) {
                            if (!SlimeEngine::isSlimeChunk(Version::Java, seed, s.first + dx, s.second + dy))
                                continue;
                            c += w[(static_cast<size_t>(dy + d) * side + (dx + d)) * 256 +
                                   static_cast<size_t>(oy * 16 + ox)];
                        }
                    }
                    best = (std::max)(best, c);
                }
            }
            upper[i] = best + k3Err;
        }
        AfkPlanOptions base = o;
        const auto tb = Clock::now();
        const auto baseline = AfkPlanner::plan(base, pred, nullptr);
        const int lower = baseline.empty() ? 0 : static_cast<int>(baseline.front().coveredBlocks);
        std::vector<std::pair<int, int>> selected;
        for (size_t i = 0; i < o.seedCandidates.size(); ++i)
            if (upper[i] >= lower) selected.push_back(o.seedCandidates[i]);
        AfkPlanOptions ko = o;
        ko.seedCandidates = selected;
        const auto tk = Clock::now();
        const auto got = AfkPlanner::plan(ko, pred, nullptr);
        const int v = got.empty() ? 0 : static_cast<int>(got.front().coveredBlocks);
        std::printf("K3 seeds=%zu upper_ms=%.1f baseline_ms=%.1f select_ms=%.1f "
                    "candidates=%zu lower=%d V=%d total_ms=%.1f\n",
                    o.seedCandidates.size(), msSince(tu), msSince(tb), msSince(tk),
                    selected.size(), lower, v, msSince(tTotal));
        return 0;
    }

    if (kernelRank && !o.seedCandidates.empty()) {
        // 圆盘环 ∩ 16×16 区块矩形的面积权重。
        // 中心固定落在区块中心 (+8,+8)，所以权重表一次构建、所有 seed 复用。
        const int k = (outer + 15) / 16;
        const int side = 2 * k + 1;
        std::vector<int> w(static_cast<size_t>(side) * side, 0);
        double inner2 = static_cast<double>(inner) * inner;
        double outer2 = static_cast<double>(outer) * outer;
        for (int dz = -k; dz <= k; ++dz) {
            for (int dx = -k; dx <= k; ++dx) {
                int n = 0;
                for (int sz = 0; sz < 16; ++sz) {
                    for (int sx = 0; sx < 16; ++sx) {
                        const double x = dx * 16.0 - 8.0 + sx;
                        const double z = dz * 16.0 - 8.0 + sz;
                        const double d2 = x * x + z * z;
                        if (d2 <= outer2 && d2 >= inner2) ++n;
                    }
                }
                w[static_cast<size_t>(dz + k) * side + (dx + k)] = n;
            }
        }
        struct KernelSeed { int cx, cz; int score; };
        std::vector<KernelSeed> ranked;
        ranked.reserve(o.seedCandidates.size());
        const auto tk = Clock::now();
        for (const auto& s : o.seedCandidates) {
            int score = 0;
            for (int dz = -k; dz <= k; ++dz) {
                for (int dx = -k; dx <= k; ++dx) {
                    const int ww = w[static_cast<size_t>(dz + k) * side + (dx + k)];
                    if (ww <= 0) continue;
                    if (SlimeEngine::isSlimeChunk(Version::Java, seed, s.first + dx, s.second + dz))
                        score += ww;
                }
            }
            ranked.push_back({ s.first, s.second, score });
        }
        std::sort(ranked.begin(), ranked.end(), [](const KernelSeed& a, const KernelSeed& b) {
            return a.score > b.score;
        });
        const size_t take = (std::min)(ranked.size(), static_cast<size_t>(kernelTop));
        if (kernelPlan) {
            AfkPlanOptions ko = o;
            ko.seedCandidates.clear();
            ko.seedCandidates.reserve(take + static_cast<size_t>(kernelSquare));
            for (size_t i = 0; i < take; ++i) ko.seedCandidates.emplace_back(ranked[i].cx, ranked[i].cz);
            if (kernelSquare > 0) {
                const size_t sq = (std::min)(o.seedCandidates.size(), static_cast<size_t>(kernelSquare));
                for (size_t i = 0; i < sq; ++i) {
                    const auto p = o.seedCandidates[i];
                    if (std::find(ko.seedCandidates.begin(), ko.seedCandidates.end(), p) ==
                        ko.seedCandidates.end())
                        ko.seedCandidates.push_back(p);
                }
            }
            const auto tp = Clock::now();
            std::vector<AfkCandidate> planned;
            try {
                planned = AfkPlanner::plan(ko, pred, nullptr);
            } catch (const std::exception& e) {
                std::printf("KERNEL_PLAN error=%s\n", e.what());
                return 1;
            }
            std::printf("KERNEL_PLAN seeds=%zu top=%zu plan_ms=%.1f total_ms=%.1f\n",
                        ranked.size(), take, msSince(tp), msSince(tTotal));
            for (const auto& c : planned) {
                std::printf("KERNEL_PLAN cx=%d cz=%d blocks=%lld weight=%.3f pct=%.6f\n",
                            c.x, c.z, static_cast<long long>(c.coveredBlocks), c.coveredWeight,
                            c.coveragePercent);
            }
            return 0;
        }
        std::vector<AfkCandidate> cands;
        cands.reserve(take * 256);
        for (size_t i = 0; i < take; ++i) {
            const int bx = ranked[i].cx * 16;
            const int bz = ranked[i].cz * 16;
            for (int dz = 0; dz < 16; ++dz)
                for (int dx = 0; dx < 16; ++dx) {
                    AfkCandidate c;
                    c.x = bx + dx;
                    c.z = bz + dz;
                    cands.push_back(c);
                }
        }
        const auto te = Clock::now();
        if (biomeOk) AfkPlanner::measureStrictForCandidates(cands, o, pred, javaVersionId, seed, true, nullptr);
        const auto best = std::max_element(cands.begin(), cands.end(), [](const AfkCandidate& a, const AfkCandidate& b) {
            return a.coveredWeight < b.coveredWeight;
        });
        std::printf("KERNEL seeds=%zu top=%zu candidates=%zu rank_ms=%.1f exact_ms=%.1f total_ms=%.1f "
                    "cx=%d cz=%d blocks=%lld weight=%.3f pct=%.6f\n",
                    ranked.size(), take, cands.size(), msSince(tk), msSince(te), msSince(tTotal),
                    best->x, best->z, static_cast<long long>(best->coveredBlocks), best->coveredWeight,
                    best->coveragePercent);
        std::printf("KERNEL best_seed_kernel_score=%d seed_center=(%d,%d)\n",
                    ranked[0].score, ranked[0].cx * 16 + 8, ranked[0].cz * 16 + 8);
        if (kernelCsv) {
            std::ofstream f(kernelCsv, std::ios::binary);
            f << "cx,cz,kernel_score\n";
            for (const auto& r : ranked)
                f << r.cx * 16 + 8 << ',' << r.cz * 16 + 8 << ',' << r.score << '\n';
        }
        return 0;
    }

    if (poolCsv && !o.seedCandidates.empty()) {
        std::vector<AfkCandidate> pool;
        const size_t seedCount = (std::min)(o.seedCandidates.size(), static_cast<size_t>(poolTopSeeds));
        pool.reserve(seedCount * 256);
        for (size_t i = 0; i < seedCount; ++i) {
            const int bx = o.seedCandidates[i].first * 16;
            const int bz = o.seedCandidates[i].second * 16;
            for (int dz = 0; dz < 16; ++dz)
                for (int dx = 0; dx < 16; ++dx) {
                    AfkCandidate c;
                    c.x = bx + dx;
                    c.z = bz + dz;
                    pool.push_back(c);
                }
        }
        const char* refText = std::getenv("RING_POOL_REF");
        if (refText) {
            int rx = 0, rz = 0;
            if (std::sscanf(refText, "%d,%d", &rx, &rz) == 2) {
                AfkCandidate c;
                c.x = rx;
                c.z = rz;
                pool.push_back(c);
            }
        }
        const auto tl = Clock::now();
        if (biomeOk) AfkPlanner::measureStrictForCandidates(pool, o, pred, javaVersionId, seed, true, nullptr);
        std::sort(pool.begin(), pool.end(), [](const AfkCandidate& a, const AfkCandidate& b) {
            return a.coveredWeight > b.coveredWeight;
        });
        const size_t keep = (std::min)(pool.size(), static_cast<size_t>(poolKeep));
        std::ofstream f(poolCsv, std::ios::binary);
        f << std::fixed << std::setprecision(6);
        f << "seed,cx,cz,blocks,weight,pct\n";
        for (size_t i = 0; i < keep; ++i) {
            f << seed << ',' << pool[i].x << ',' << pool[i].z << ',' << pool[i].coveredBlocks << ','
              << pool[i].coveredWeight << ',' << pool[i].coveragePercent << '\n';
        }
        std::printf("POOL csv=%s seeds=%zu candidates=%zu kept=%zu ms=%.1f total_ms=%.1f\n",
                    poolCsv, seedCount, pool.size(), keep, msSince(tl), msSince(tTotal));
        if (keep > 0) {
            std::printf("POOL best cx=%d cz=%d blocks=%lld weight=%.3f pct=%.6f\n",
                        pool[0].x, pool[0].z, static_cast<long long>(pool[0].coveredBlocks),
                        pool[0].coveredWeight, pool[0].coveragePercent);
        }
        return 0;
    }

    if (subgridMode && !o.seedCandidates.empty()) {
        std::vector<AfkCandidate> all;
        all.reserve(o.seedCandidates.size() * 256);
        for (const auto& s : o.seedCandidates) {
            const int bx = s.first * 16;
            const int bz = s.second * 16;
            for (int dz = 0; dz < 16; ++dz)
                for (int dx = 0; dx < 16; ++dx) {
                    AfkCandidate c;
                    c.x = bx + dx;
                    c.z = bz + dz;
                    all.push_back(c);
                }
        }
        const auto tl = Clock::now();
        if (biomeOk) AfkPlanner::measureStrictForCandidates(all, o, pred, javaVersionId, seed, true, nullptr);
        const auto best = std::max_element(all.begin(), all.end(), [](const AfkCandidate& a, const AfkCandidate& b) {
            return a.coveredWeight < b.coveredWeight;
        });
        std::printf("SUBGRID ok=%d seeds=%zu candidates=%zu ms=%.1f total_ms=%.1f cx=%d cz=%d blocks=%lld "
                    "weight=%.3f pct=%.6f\n",
                    biomeOk ? 1 : 0, o.seedCandidates.size(), all.size(), msSince(tl), msSince(tTotal),
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

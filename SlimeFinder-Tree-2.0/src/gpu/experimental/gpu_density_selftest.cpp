// gpu_density_selftest.cpp - GPU 密度匹配（v3.3.1）独立自检程序
//
// 覆盖：
//   [1] CPU/GPU 一致性：Java 与 基岩版 × g ∈ {1,3,5,10,32} × 阈值 ∈ {0.2,0.4,0.8}
//       × 多种区域（小区域 / 负坐标 / 跨零 / 贴近 int 边界），逐锚点比对
//       命中集合（set<tuple<int,int,int,int>>）+ density 浮点位模式（bit_cast<uint32_t>）
//   [2] 取消延迟：大区域运行中由另一线程置位 cancel，实测「置位 → 返回」耗时
//   [3] maxResults 超限：命中数超上限时必须返回 false 且 err 非空（引擎回退 CPU 全量路径）
//   [4] 吞吐基准：Java 16384×16384 / 基岩 4096×4096（g=5），与同机 CPU 对照 + 全图外推
//   [5] 与引擎 run()（Exact 密度搜索）整链路对照
//
// 用法：gpu_density_selftest [用例编号...] [-v]
// 退出码：0（全部通过）/ 1（有失败）/ 2（无 GPU 设备）

#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"
#include "gpu/GpuSession.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

int g_pass = 0;
int g_fail = 0;
int g_skip = 0;
bool g_debug = false;

void check(bool ok, const std::string& what)
{
    if (ok) {
        ++g_pass;
        std::printf("  PASS  %s\n", what.c_str());
    } else {
        ++g_fail;
        std::printf("  FAIL  %s\n", what.c_str());
    }
    std::fflush(stdout);
}

void skip(const std::string& what)
{
    ++g_skip;
    std::printf("  SKIP  %s\n", what.c_str());
    std::fflush(stdout);
}

double nowMs()
{
    using clock = std::chrono::steady_clock;
    static const clock::time_point t0 = clock::now();
    return std::chrono::duration<double, std::milli>(clock::now() - t0).count();
}

using Quad = std::tuple<int, int, int, int>; // (minX, maxX, minZ, maxZ)

Quad quadOf(const slime::MatchResult& m)
{
    return Quad{ m.minX, m.maxX, m.minZ, m.maxZ };
}

uint32_t bitsOf(float f)
{
    return std::bit_cast<uint32_t>(f);
}

// ---------------------------------------------------------------------------
// CPU 参考实现：与引擎精确模式同构（整数计数 → 一次浮点除法 → 浮点比较）
//   count   = 窗口 [x, x+g) × [z, z+g) 内的史莱姆区块数
//   命中    ⟺ SlimeEngine::densityWindowMeets(count, g, minDensity)
//   density = (float)count / (float)(g*g)
// ---------------------------------------------------------------------------
std::vector<slime::MatchResult> cpuDensityMatch(slime::Version ver, int64_t seed,
                                                int64_t axMin, int64_t axMax,
                                                int64_t azMin, int64_t azMax,
                                                int g, double minDensity)
{
    const int64_t nx = axMax - axMin + 1;
    const int64_t nz = azMax - azMin + 1;
    if (nx <= 0 || nz <= 0) return {};

    std::vector<slime::MatchResult> out;
    const float denom = static_cast<float>(static_cast<int64_t>(g) * g);

#ifdef _OPENMP
#pragma omp parallel
#endif
    {
        std::vector<slime::MatchResult> local;
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 4)
#endif
        for (int64_t i = 0; i < nx; ++i) {
            const int64_t x = axMin + i;
            for (int64_t z = azMin; z <= azMax; ++z) {
                int count = 0;
                for (int dz = 0; dz < g; ++dz) {
                    for (int dx = 0; dx < g; ++dx) {
                        if (slime::SlimeEngine::isSlimeChunk(ver, seed, x + dx, z + dz)) ++count;
                    }
                }
                if (!slime::SlimeEngine::densityWindowMeets(count, g, minDensity)) continue;
                slime::MatchResult mr;
                mr.minX = static_cast<int>(x);
                mr.maxX = static_cast<int>(x + g - 1);
                mr.minZ = static_cast<int>(z);
                mr.maxZ = static_cast<int>(z + g - 1);
                mr.density = static_cast<float>(count) / denom;
                local.push_back(mr);
            }
        }
#ifdef _OPENMP
#pragma omp critical
#endif
        { out.insert(out.end(), local.begin(), local.end()); }
    }
    return out;
}

std::string describeRegion(int64_t axMin, int64_t axMax, int64_t azMin, int64_t azMax, int g,
                           double minDensity)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "x=[%lld,%lld] z=[%lld,%lld] g=%d thr=%.2f",
                  (long long)axMin, (long long)axMax, (long long)azMin, (long long)azMax, g, minDensity);
    return buf;
}

// 一致性比对：命中集合完全一致 + 每个命中 density 位模式一致
struct CompareResult {
    bool setEqual = false;
    bool densityEqual = false;
    size_t gpuCount = 0;
    size_t cpuCount = 0;
    size_t missing = 0;
    size_t extra = 0;
    size_t densityMismatch = 0;
};

CompareResult compareMatches(const std::vector<slime::MatchResult>& gpu,
                             const std::vector<slime::MatchResult>& cpu)
{
    CompareResult r;
    r.gpuCount = gpu.size();
    r.cpuCount = cpu.size();

    std::set<Quad> gs, cs;
    for (const auto& m : gpu) gs.insert(quadOf(m));
    for (const auto& m : cpu) cs.insert(quadOf(m));
    r.setEqual = (gs == cs);
    for (const auto& q : cs) {
        if (!gs.count(q)) ++r.missing;
    }
    for (const auto& q : gs) {
        if (!cs.count(q)) ++r.extra;
    }

    std::vector<std::pair<Quad, uint32_t>> gv, cv;
    gv.reserve(gpu.size());
    cv.reserve(cpu.size());
    for (const auto& m : gpu) gv.emplace_back(quadOf(m), bitsOf(m.density));
    for (const auto& m : cpu) cv.emplace_back(quadOf(m), bitsOf(m.density));
    std::sort(gv.begin(), gv.end());
    std::sort(cv.begin(), cv.end());
    r.densityEqual = (gv.size() == cv.size());
    if (r.densityEqual) {
        for (size_t i = 0; i < gv.size(); ++i) {
            if (gv[i].first != cv[i].first || gv[i].second != cv[i].second) {
                r.densityEqual = false;
                ++r.densityMismatch;
                if (r.densityMismatch <= 3) {
                    std::printf("        density 不一致: 窗口 (%d,%d,%d,%d) gpu=0x%08X cpu=0x%08X\n",
                                std::get<0>(gv[i].first), std::get<1>(gv[i].first),
                                std::get<2>(gv[i].first), std::get<3>(gv[i].first), gv[i].second, cv[i].second);
                }
            }
        }
    }
    return r;
}

std::vector<slime::MatchResult> toMatchResults(const std::vector<slimegpu::DensityHit>& hits, int g)
{
    const float denom = static_cast<float>(static_cast<int64_t>(g) * g);
    std::vector<slime::MatchResult> out;
    out.reserve(hits.size());
    for (const auto& h : hits) {
        slime::MatchResult mr;
        mr.minX = h.x;
        mr.maxX = static_cast<int>(h.x + g - 1);
        mr.minZ = h.z;
        mr.maxZ = static_cast<int>(h.z + g - 1);
        mr.density = static_cast<float>(h.count) / denom;
        out.push_back(mr);
    }
    return out;
}

bool runGpu(slimegpu::GpuSession& session, slime::Version ver, int64_t seed,
            int64_t axMin, int64_t axMax, int64_t azMin, int64_t azMax, int g, double minDensity,
            int64_t maxResults, std::vector<slimegpu::DensityHit>& hits, std::string& err,
            std::string* stats = nullptr, const std::atomic<bool>* cancel = nullptr)
{
    slimegpu::DensityMatchRequest req;
    req.version = (ver == slime::Version::Java) ? 0 : 1;
    req.worldSeed = seed;
    req.axMin = axMin;
    req.axMax = axMax;
    req.azMin = azMin;
    req.azMax = azMax;
    req.gridSize = g;
    req.minDensity = minDensity;
    req.maxResults = maxResults;
    static const std::atomic<bool> neverCancel{ false };
    return session.matchDensity(req, cancel ? *cancel : neverCancel, hits, &err, stats) ==
           slimegpu::DensityMatchStatus::Ok;
}

// ============================ [1] 一致性 ============================

void testConsistency(slimegpu::GpuSession& session)
{
    std::printf("[1] CPU/GPU 一致性（命中集合 + density 位模式）\n");
    const int64_t seed = 1234567890123LL;
    int cases = 0;
    const int failsBefore = g_fail;

    struct Region {
        const char* name;
        int64_t axMin, axMax, azMin, azMax;
    };
    const Region regions[] = {
        { "小区域 200x150", 0, 199, 0, 149 },
        { "窄长 150x200", -75, 74, -100, 99 },
        { "负坐标 400x300", -1000, -601, -500, -201 },
        { "跨零 500x400", -250, 249, -200, 199 },
        { "极窄 1x300", 7, 7, -20, 279 },
        { "贴近 int 边界", 2147480000, 2147480199, -2147479000, -2147478900 },
    };
    const int grids[] = { 1, 3, 5, 10, 32 };
    const double thresholds[] = { 0.2, 0.4, 0.8 };

    for (slime::Version ver : { slime::Version::Java, slime::Version::Bedrock }) {
        for (const Region& rg : regions) {
            for (int g : grids) {
                for (double thr : thresholds) {
                    const int64_t area = (rg.axMax - rg.axMin + 1) * (rg.azMax - rg.azMin + 1);
                    // 大区域只取代表组合（CPU 参考是逐区块朴素实现，控制耗时）
                    if (area > 100000 && !(g == 5 && thr == 0.4)) continue;

                    std::vector<slimegpu::DensityHit> hits;
                    std::string err;
                    const double t0 = nowMs();
                    const bool ok = runGpu(session, ver, seed, rg.axMin, rg.axMax, rg.azMin, rg.azMax,
                                           g, thr, 0, hits, err);
                    const double t1 = nowMs();
                    std::vector<slime::MatchResult> gpu = toMatchResults(hits, g);
                    std::vector<slime::MatchResult> cpu =
                        cpuDensityMatch(ver, seed, rg.axMin, rg.axMax, rg.azMin, rg.azMax, g, thr);
                    const double t2 = nowMs();

                    const std::string label = std::string(ver == slime::Version::Java ? "Java " : "Bedrock ") +
                                              rg.name + " " + describeRegion(rg.axMin, rg.axMax, rg.azMin,
                                                                             rg.azMax, g, thr);
                    ++cases;
                    if (!ok) {
                        check(false, label + " → GPU 返回失败: " + err);
                        continue;
                    }
                    CompareResult cr = compareMatches(gpu, cpu);
                    const bool pass = cr.setEqual && cr.densityEqual;
                    char msg[512];
                    std::snprintf(msg, sizeof(msg),
                                  "%s | 命中 GPU=%zu CPU=%zu 缺失=%zu 多余=%zu 密度不符=%zu | GPU %.1fms CPU %.1fms",
                                  label.c_str(), cr.gpuCount, cr.cpuCount, cr.missing, cr.extra,
                                  cr.densityMismatch, t1 - t0, t2 - t1);
                    check(pass, msg);
                    if (!pass && g_debug) {
                        std::printf("        GPU 前 5 条:");
                        for (size_t i = 0; i < gpu.size() && i < 5; ++i) {
                            std::printf(" (%d,%d,%d,%d,c=%u)", gpu[i].minX, gpu[i].maxX, gpu[i].minZ,
                                        gpu[i].maxZ, hits[i].count);
                        }
                        std::printf("\n        CPU 前 5 条:");
                        for (size_t i = 0; i < cpu.size() && i < 5; ++i) {
                            std::printf(" (%d,%d,%d,%d,%.3f)", cpu[i].minX, cpu[i].maxX, cpu[i].minZ,
                                        cpu[i].maxZ, cpu[i].density);
                        }
                        std::printf("\n");
                    }
                }
            }
        }
    }
    std::printf("  一致性用例 %d 个（失败 %d）\n", cases, g_fail - failsBefore);
}

// ============================ [2] 取消延迟 ============================

void testCancelLatency(slimegpu::GpuSession& session)
{
    std::printf("[2] 取消延迟（Java 8192×8192，g=5，thr=0.3）\n");
    const int64_t span = 8191;
    const int64_t seed = 987654321LL;

    for (int round = 0; round < 3; ++round) {
        std::atomic<bool> cancel{ false };
        std::vector<slimegpu::DensityHit> hits;
        std::string err, stats;
        double setAt = 0.0, returnedAt = 0.0;
        bool ok = false;

        std::thread worker([&] {
            slimegpu::DensityMatchRequest req;
            req.version = 0;
            req.worldSeed = seed;
            req.axMin = 0;
            req.axMax = span;
            req.azMin = 0;
            req.azMax = span;
            req.gridSize = 5;
            req.minDensity = 0.3;
            req.maxResults = 0;
            ok = session.matchDensity(req, cancel, hits, &err, &stats) ==
                 slimegpu::DensityMatchStatus::Cancelled;
            returnedAt = nowMs();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        setAt = nowMs();
        cancel.store(true, std::memory_order_relaxed);
        worker.join();

        const double latency = returnedAt - setAt;
        char msg[400];
        std::snprintf(msg, sizeof(msg),
                      "第 %d 轮：置位→返回 %.1f ms（要求 <1000ms）| 已回传命中 %zu 条（部分结果）| %s",
                      round + 1, latency, hits.size(), ok ? "返回 Cancelled" : "返回值异常");
        check(ok && latency < 1000.0, msg);
    }
}

// ============================ [3] maxResults 超限 ============================

void testMaxResults(slimegpu::GpuSession& session)
{
    std::printf("[3] maxResults 超限 → 返回 TooManyResults（引擎回退 CPU 全量路径）\n");
    const int64_t span = 8191;
    const int64_t seed = 4242424242LL;

    slimegpu::DensityMatchRequest req;
    req.version = 0;
    req.worldSeed = seed;
    req.axMin = 0;
    req.axMax = span;
    req.azMin = 0;
    req.azMax = span;
    req.gridSize = 5;
    req.minDensity = 0.1;
    req.maxResults = 100000;

    std::atomic<bool> cancel{ false };
    std::vector<slimegpu::DensityHit> hits;
    std::string err, stats;
    const double t0 = nowMs();
    const slimegpu::DensityMatchStatus st = session.matchDensity(req, cancel, hits, &err, &stats);
    const double t1 = nowMs();

    char msg[512];
    std::snprintf(msg, sizeof(msg),
                  "返回值=%s（期望 TooManyResults）| err=%s | 从开始到返回 false 用时 %.1f ms | 返回命中 %zu 条（期望 0）",
                  st == slimegpu::DensityMatchStatus::TooManyResults ? "TooManyResults" : "非 TooManyResults",
                  err.empty() ? "(空)" : err.c_str(), t1 - t0, hits.size());
    check(st == slimegpu::DensityMatchStatus::TooManyResults && !err.empty() && hits.empty(), msg);
}

// ============================ [4] 吞吐基准 ============================

struct BenchRow {
    const char* name;
    int64_t axMin, axMax, azMin, azMax;
    int g;
    double thr;
    slime::Version ver;
    int64_t seed;
};

void testThroughput(slimegpu::GpuSession& session)
{
    std::printf("[4] 吞吐基准与全图外推\n");
    const BenchRow rows[] = {
        { "Java 16384x16384 g=5 thr=0.3", 0, 16383, 0, 16383, 5, 0.3, slime::Version::Java, 1234567890123LL },
        { "Bedrock 4096x4096 g=5 thr=0.3", 0, 4095, 0, 4095, 5, 0.3, slime::Version::Bedrock, 0 },
    };

    for (const BenchRow& r : rows) {
        const int64_t nx = r.axMax - r.axMin + 1;
        const int64_t nz = r.azMax - r.azMin + 1;
        const double anchors = static_cast<double>(nx) * static_cast<double>(nz);

        std::vector<slimegpu::DensityHit> hits;
        std::string err, stats;
        const double t0 = nowMs();
        const bool ok = runGpu(session, r.ver, r.seed, r.axMin, r.axMax, r.azMin, r.azMax, r.g, r.thr, 0,
                               hits, err, &stats);
        const double t1 = nowMs();
        const double gpuMs = t1 - t0;

        if (!ok) {
            check(false, std::string(r.name) + " → GPU 失败: " + err);
            continue;
        }
        char msg[512];
        std::snprintf(msg, sizeof(msg), "%s | 锚点 %.6g | GPU %.1f ms | %.2f M锚点/s | 命中 %zu",
                      r.name, anchors, gpuMs, anchors / gpuMs / 1000.0, hits.size());
        check(true, msg);
        std::printf("        GPU 统计: %s\n", stats.substr(0, 200).c_str());

        const double c0 = nowMs();
        std::vector<slime::MatchResult> cpu =
            cpuDensityMatch(r.ver, r.seed, r.axMin, r.axMax, r.azMin, r.azMax, r.g, r.thr);
        const double c1 = nowMs();
        const double cpuMs = c1 - c0;
        std::printf("  → 同机 CPU（%d 线程）%.1f ms | %.2f M锚点/s | 命中 %zu | GPU/CPU 加速比 %.1fx\n",
#ifdef _OPENMP
                    omp_get_max_threads(),
#else
                    1,
#endif
                    cpuMs, anchors / cpuMs / 1000.0, cpu.size(), cpuMs / gpuMs);

        // 全图外推：整图锚点数 ≈ 区块数 = (2R)²（R = 半边长），时间 = 锚点数 ÷ 实测速率
        const double rate = anchors / gpuMs / 1000.0; // M锚点/s
        const double R = (r.ver == slime::Version::Java) ? 1875000.0 : 1048576.0;
        const double fullAnchors = 4.0 * R * R;
        const double fullSec = fullAnchors / (rate * 1e6);
        std::printf("  → 全图外推（±%.0f 区块，锚点数 = (2R)² = %.6g）：%.0f s = %.1f 分钟"
                    "（公式：锚点数 ÷ 实测 %.2f M锚点/s）\n",
                    R, fullAnchors, fullSec, fullSec / 60.0, rate);
        std::printf("     是否达到任务书「全图密度匹配 ≤20 分钟」目标：%s\n",
                    fullSec <= 1200.0 ? "是（按本机实测线性外推）" : "否（见限制因素分析）");
    }
}

// ============================ [5] 与引擎整链路对照 ============================

void testEnginePath(slimegpu::GpuSession& /*session*/)
{
    std::printf("[5] 与引擎 run()（Exact 密度搜索）结果对照\n");
    auto gpuScan = slimegpu::createGpuScanAdapter(nullptr);
    if (!gpuScan) {
        skip("无法创建 GPU 适配器");
        return;
    }
    const int64_t seed = 7777777LL;
    const int g = 5;
    const double thr = 0.35;

    for (slime::Version ver : { slime::Version::Java, slime::Version::Bedrock }) {
        slime::SearchOptions o;
        o.version = ver;
        o.worldSeed = seed;
        o.minX = -300;
        o.maxX = 299;
        o.minZ = -200;
        o.maxZ = 199;
        o.mode = slime::Mode::Density;
        o.gridSize = g;
        o.minDensity = thr;
        o.algorithm = slime::Algorithm::Exact;
        o.targetCount = 1000000;
        o.stopAtTarget = false;
        o.smartRange = false;
        o.threads = 0;

        std::atomic<bool> cancel{ false };
        slime::SearchOutcome cpuOut, gpuOut;
        slime::SlimeEngine engine;
        engine.run(o, cancel, nullptr, cpuOut);
        o.gpuScan = gpuScan.get();
        engine.run(o, cancel, nullptr, gpuOut);

        std::set<Quad> cs, gs;
        for (const auto& m : cpuOut.results) cs.insert(quadOf(m));
        for (const auto& m : gpuOut.results) gs.insert(quadOf(m));
        char msg[400];
        std::snprintf(msg, sizeof(msg), "%s：引擎 Exact 密度搜索 CPU 命中 %lld / GPU 路径命中 %lld → 集合%s",
                      ver == slime::Version::Java ? "Java" : "Bedrock",
                      (long long)cpuOut.totalFound, (long long)gpuOut.totalFound,
                      cs == gs ? "一致" : "不一致");
        check(cpuOut.ok && gpuOut.ok && cs == gs, msg);
    }
}

// ============================ 主程序 ============================

void printDevice(const slimegpu::GpuSession& s)
{
    const auto& d = s.device();
    std::printf("设备：%s (%s)\n", d.name.c_str(), d.vendor.c_str());
    std::printf("  平台 %s | 驱动 %s | %llu CU @ %u MHz | 显存 %.2f GB | 单次分配上限 %.2f GB | 本地内存 %llu KB\n",
                d.platformName.c_str(), d.driverVersion.c_str(), (unsigned long long)d.maxComputeUnits,
                d.maxClockMhz, d.globalMemBytes / 1073741824.0, d.maxMemAllocBytes / 1073741824.0,
                (unsigned long long)(d.localMemBytes / 1024));
    std::printf("  OpenCL %s | 模块 %s\n", d.deviceVersion.c_str(), s.modulePath().c_str());
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv)
{
    std::printf("=== GPU 密度匹配自检（v3.3.1）===\n");
    std::printf("CPU 线程数上限（OpenMP）：%d\n",
#ifdef _OPENMP
                omp_get_max_threads()
#else
                1
#endif
    );
    std::fflush(stdout);

    slimegpu::GpuSession session;
    std::string err;
    if (!session.init(&err)) {
        std::printf("GPU 初始化失败：%s\n", err.c_str());
        std::printf("说明：本机没有可用 OpenCL 设备，无法执行一致性测试（不做静默跳过）。\n");
        const int64_t seed = 1234567890123LL;
        auto cpu = cpuDensityMatch(slime::Version::Java, seed, 0, 63, 0, 63, 5, 0.3);
        std::printf("CPU 参考实现自检：Java 64x64 g=5 thr=0.3 → 命中 %zu 个（参考实现可用）\n", cpu.size());
        std::printf("=== 汇总：PASS %d / FAIL %d / SKIP %d（无 GPU 设备，退出码 2）===\n", g_pass, g_fail,
                    g_skip);
        return 2;
    }
    printDevice(session);

    std::vector<int> want;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0) {
            g_debug = true;
            continue;
        }
        want.push_back(std::atoi(argv[i]));
    }
    const auto runCase = [&](int id) {
        if (!want.empty() && std::find(want.begin(), want.end(), id) == want.end()) return;
        switch (id) {
        case 1: testConsistency(session); break;
        case 2: testCancelLatency(session); break;
        case 3: testMaxResults(session); break;
        case 4: testThroughput(session); break;
        case 5: testEnginePath(session); break;
        default: break;
        }
    };
    for (int id = 1; id <= 5; ++id) runCase(id);

    std::printf("=== 汇总：PASS %d / FAIL %d / SKIP %d ===\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}

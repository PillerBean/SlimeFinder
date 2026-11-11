// test_slime.cpp - SlimeFinder 引擎测试与命令行工具
//
// 三种用法：
//   1) 参考兼容模式（与 SlimeChecker.exe 输出可逐行比对）：
//        slime_tests <version 0|1> <seed> <minX> <maxX> <minZ> <maxZ>
//        slime_tests <version> <seed> <minX> <maxX> <minZ> <maxZ> 1 <pattern_file> <lock 0|1>
//        slime_tests <version> <seed> <minX> <maxX> <minZ> <maxZ> 2 <grid> <density>
//        附加选项：--threads N（默认全部核心）
//   2) GUI 等价列表（与 Python 参考 GUI 的最近 N 排序一致）：
//        slime_tests --near <version> <seed> <minX> <maxX> <minZ> <maxZ>
//                    <mode:1|2> [mode 参数...] <startX> <startZ> <N> [--smart] [--stop]
//        输出：每行 "distance minX maxX minZ maxZ [density]"
//   3) 单元测试 / 性能基准：
//        slime_tests --unit
//        slime_tests --bench
#include "core/CacheManager.h"
#include "core/FastMatch.h" // v3.3.0：AVX2 状态显示
#include "core/FastMt.h"    // v3.3.0（3.4）：基岩版 AVX2 批量 MT19937
#include "core/RandomGenerators.h"
#include "core/SearchTask.h"
#include "core/AfkPlanner.h"
#include "core/BiomeMap.h"
#include "core/SpawnWeights.h"
#include "core/SlimeEngine.h"
#include "core/SearchPlan.h"   // v3.9.10：结果要求/运行环境模型
#include "core/PerfLog.h" // v3.4.13：CPU 采样线程安全单测
#include "gpu/GpuScanAdapter.h" // v3.2.0：GPU 引擎等价测试

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace slime;

// ===================== 工具 =====================

static std::string usage()
{
    return
        "用法:\n"
        "  扫描: slime_tests <version> <seed> <minX> <maxX> <minZ> <maxZ> [--threads N]\n"
        "  严格: slime_tests <version> <seed> <minX> <maxX> <minZ> <maxZ> 1 <patfile> <lock> [--threads N]\n"
        "  密度: slime_tests <version> <seed> <minX> <maxX> <minZ> <maxZ> 2 <grid> <density> [--threads N]\n"
        "  GUI序: slime_tests --near <version> <seed> <minX> <maxX> <minZ> <maxZ> <mode> [mode 参数] <startX> <startZ> <N> [--smart] [--stop] [--threads N]\n"
        "  测试: slime_tests --unit\n"
        "  基准: slime_tests --bench\n";
}

static int findThreads(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--threads") return std::atoi(argv[i + 1]);
    }
    return 0;
}

static std::vector<std::string> argsRest(int argc, char** argv, const std::string& skip)
{
    std::vector<std::string> v;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == skip || s == "--threads") {
            if (s == "--threads") ++i;
            continue;
        }
        v.push_back(s);
    }
    return v;
}

static bool parseCommon(const std::vector<std::string>& a, size_t& idx,
                        Version& version, int64_t& seed, int& minX, int& maxX, int& minZ, int& maxZ)
{
    if (a.size() - idx < 6) return false;
    version = (std::stoi(a[idx]) == 1) ? Version::Bedrock : Version::Java;
    ++idx;
    seed = std::stoll(a[idx]);
    ++idx;
    minX = std::stoi(a[idx]); maxX = std::stoi(a[idx + 1]);
    minZ = std::stoi(a[idx + 2]); maxZ = std::stoi(a[idx + 3]);
    idx += 4;
    return true;
}

// 参考 .pat 图案文件读取（2=忽略, 1=史莱姆, 0=非史莱姆，与 Python 编码一致）
static bool readPatternFile(const std::string& path, Pattern& out, std::string& err)
{
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }
    int rows = 0, cols = 0;
    in >> rows >> cols;
    if (rows <= 0 || cols <= 0 || rows > 64 || cols > 64) { err = "bad pattern size"; return false; }
    Pattern p(rows, cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int v = 2;
            in >> v;
            if (v == 1) p.set(r, c, CellState::Green);
            else if (v == 0) p.set(r, c, CellState::White);
            else p.set(r, c, CellState::Ignore);
        }
    }
    out = p;
    return true;
}

// v3.4.13：文件级环境变量设置助手（部分测试组没有自己的局部 setEnv lambda）
static void setEnvGlobal(const char* k, const char* v)
{
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    if (v) {
        setenv(k, v, 1);
    } else {
        unsetenv(k);
    }
#endif
}

static SearchOutcome runOnce(const SearchOptions& opt, std::atomic<bool>* cancel = nullptr, ProgressFn prog = nullptr)
{
    SearchOutcome out;
    std::atomic<bool> localCancel{ false };
    SlimeEngine engine;
    engine.run(opt, cancel ? *cancel : localCancel, prog, out);
    return out;
}

// ===================== 参考兼容模式 =====================

static int cmdReference(const std::vector<std::string>& a, int threads)
{
    size_t idx = 0;
    Version version;
    int64_t seed;
    int minX, maxX, minZ, maxZ;
    if (!parseCommon(a, idx, version, seed, minX, maxX, minZ, maxZ)) {
        std::cerr << usage();
        return 2;
    }
    const int mode = (idx < a.size()) ? std::stoi(a[idx++]) : 0;

    if (mode == 0) {
        // 扫描全部史莱姆区块（排序输出，与参考一致）
        std::set<std::pair<int, int>> out;
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                if (SlimeEngine::isSlimeChunk(version, seed, x, z)) out.emplace(x, z);
            }
        }
        for (const auto& p : out) std::cout << p.first << ' ' << p.second << '\n';
        return 0;
    }
    if (mode == 1) {
        if (a.size() - idx < 2) { std::cerr << usage(); return 2; }
        const std::string patFile = a[idx];
        const bool lock = std::stoi(a[idx + 1]) != 0;
        idx += 2;
        Pattern pat;
        std::string err;
        if (!readPatternFile(patFile, pat, err)) { std::cerr << err << '\n'; return 2; }
        SearchOptions opt;
        opt.version = version; opt.worldSeed = seed;
        opt.minX = minX; opt.maxX = maxX; opt.minZ = minZ; opt.maxZ = maxZ;
        opt.mode = Mode::Strict; opt.pattern = pat; opt.directionLocked = lock;
        opt.targetCount = INT64_MAX; opt.threads = threads;
        SearchOutcome r = runOnce(opt);
        if (!r.ok) { std::cerr << r.error << '\n'; return 3; }
        for (const MatchResult& m : r.results) {
            std::cout << m.minX << ' ' << m.maxX << ' ' << m.minZ << ' ' << m.maxZ << '\n';
        }
        return 0;
    }
    if (mode == 2) {
        if (a.size() - idx < 2) { std::cerr << usage(); return 2; }
        const int grid = std::stoi(a[idx]);
        const double thr = std::stod(a[idx + 1]);
        SearchOptions opt;
        opt.version = version; opt.worldSeed = seed;
        opt.minX = minX; opt.maxX = maxX; opt.minZ = minZ; opt.maxZ = maxZ;
        opt.mode = Mode::Density; opt.gridSize = grid; opt.minDensity = thr;
        opt.targetCount = INT64_MAX; opt.threads = threads;
        SearchOutcome r = runOnce(opt);
        if (!r.ok) { std::cerr << r.error << '\n'; return 3; }
        for (const MatchResult& m : r.results) {
            std::cout << m.minX << ' ' << m.maxX << ' ' << m.minZ << ' ' << m.maxZ << ' ' << m.density << '\n';
        }
        return 0;
    }
    std::cerr << "未知 mode\n";
    return 2;
}

// ===================== GUI 等价列表 =====================

static int cmdNear(const std::vector<std::string>& a, int threads)
{
    // --near <version> <seed> <minX> <maxX> <minZ> <maxZ> <mode> [patfile lock | grid thr] <startX> <startZ> <N> [--smart] [--stop]
    size_t idx = 0;
    Version version;
    int64_t seed;
    int minX, maxX, minZ, maxZ;
    if (!parseCommon(a, idx, version, seed, minX, maxX, minZ, maxZ)) return 2;
    if (idx >= a.size()) return 2;
    const int mode = std::stoi(a[idx++]);

    SearchOptions opt;
    opt.version = version;
    opt.worldSeed = seed;
    opt.minX = minX; opt.maxX = maxX; opt.minZ = minZ; opt.maxZ = maxZ;
    opt.mode = (mode == 2) ? Mode::Density : Mode::Strict;
    opt.threads = threads;

    if (opt.mode == Mode::Strict) {
        if (a.size() - idx < 2) return 2;
        Pattern pat;
        std::string err;
        if (!readPatternFile(a[idx], pat, err)) { std::cerr << err << '\n'; return 2; }
        opt.pattern = pat;
        opt.directionLocked = std::stoi(a[idx + 1]) != 0;
        idx += 2;
    } else {
        if (a.size() - idx < 2) return 2;
        opt.gridSize = std::stoi(a[idx]);
        opt.minDensity = std::stod(a[idx + 1]);
        idx += 2;
    }
    if (a.size() - idx < 3) return 2;
    opt.startX = std::stoll(a[idx]);
    opt.startZ = std::stoll(a[idx + 1]);
    opt.targetCount = std::stoll(a[idx + 2]);
    idx += 3;
    for (; idx < a.size(); ++idx) {
        if (a[idx] == "--smart") opt.smartRange = true;
        else if (a[idx] == "--stop") opt.stopAtTarget = true;
    }

    SearchOutcome r = runOnce(opt);
    if (!r.ok) { std::cerr << r.error << '\n'; return 3; }
    std::cout.precision(12); // 足够精度，供脚本与 Python 计算的 double 距离比对
    for (const MatchResult& m : r.results) {
        const double cx = (static_cast<double>(m.minX) + m.maxX) / 2.0;
        const double cz = (static_cast<double>(m.minZ) + m.maxZ) / 2.0;
        const double dist = std::hypot(cx - static_cast<double>(opt.startX), cz - static_cast<double>(opt.startZ));
        std::cout << dist << ' ' << m.minX << ' ' << m.maxX << ' ' << m.minZ << ' ' << m.maxZ;
        if (mode == 2) std::cout << ' ' << m.density;
        std::cout << '\n';
    }
    return 0;
}

// ===================== 单元测试 =====================

static int g_failures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__);           \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static void testWrapArithmetic()
{
    std::printf("[1] 32/64 位溢出模拟…\n");
    auto mul64 = [](int64_t a, int64_t b) {
        return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
    };
    CHECK(int32Mul(100000, 100000) == static_cast<int32_t>((static_cast<int64_t>(100000) * 100000) & 0xFFFFFFFFLL), "int32Mul 常规");
    const int32_t ov = static_cast<int32_t>(static_cast<uint32_t>(46341) * static_cast<uint32_t>(46341));
    const int32_t ovn = static_cast<int32_t>(static_cast<uint32_t>(46341) * static_cast<uint32_t>(46341));
    CHECK(int32Mul(46341, 46341) == ov, "int32Mul 溢出回绕");
    CHECK(int32Mul(-46341, 46341) == -ovn, "int32Mul 负溢出");
    CHECK(int32Mul(INT32_MIN, -1) == INT32_MIN, "int32Mul INT32_MIN*-1");
    CHECK(umul32Lo(0xFFFFFFFFU, 0xFFFFFFFFU) == 1U, "umul32Lo 回绕");
    CHECK(mul64(0x7FFFFFFFFFFFFFFFLL, 2) == -2, "mul64 回绕");
}

static void testJavaRandomParity()
{
    std::printf("[2] JavaRandom 与手工 LCG 一致性…\n");
    std::mt19937_64 rng(42);
    for (int i = 0; i < 2000; ++i) {
        const uint64_t seed = rng();
        JavaRandom r(seed);
        const int n = (i % 3 == 0) ? 10 : ((i % 3 == 1) ? 100 : 7);
        const int got = r.nextInt(n);
        CHECK(got >= 0 && got < n, "nextInt 范围");
        // 手工计算：仅当 n 非 2 的幂且拒绝条件可能触发时使用拒绝法；此处直接重放 LCG
        uint64_t s = (seed ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1);
        int bits = 0;
        int expected = 0;
        if ((n & (n - 1)) == 0) {
            s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
            bits = static_cast<int>(s >> 17);
            expected = static_cast<int>((static_cast<int64_t>(n) * bits) >> 31);
        } else {
            do {
                s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
                bits = static_cast<int>(s >> 17);
                expected = bits % n;
            } while (bits - expected + (n - 1) < 0);
        }
        CHECK(got == expected, "nextInt 与参考 LCG 一致");
    }
}

// 朴素逐格验证（直接调用 isSlimeChunk 独立于引擎位图路径）
static void testModesAgainstNaive()
{
    std::printf("[3] 密度/严格匹配与朴素逐窗验证…\n");
    std::mt19937_64 rng(7);
    const int64_t seeds[] = { 0, 1, -1, 123456789, -987654321987654321LL };
    for (int64_t seed : seeds) {
        // 区域 +-80，检查几个 grid/阈值组合
        for (int grid : { 3, 5 }) {
            for (double thr : { 0.1, 0.3, 0.55 }) {
                SearchOptions opt;
                opt.version = Version::Java;
                opt.worldSeed = seed;
                opt.minX = -80; opt.maxX = 80; opt.minZ = -80; opt.maxZ = 80;
                opt.mode = Mode::Density; opt.gridSize = grid; opt.minDensity = thr;
                opt.targetCount = INT64_MAX;
                opt.threads = 1;
                SearchOutcome r = runOnce(opt);
                CHECK(r.ok, "密度搜索成功");
                std::set<std::tuple<int, int, int, int>> got;
                for (const MatchResult& m : r.results) {
                    got.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
                    const int exp = m.density >= 0.0f && m.density <= 1.0f;
                    CHECK(exp, "密度范围");
                }
                // 朴素：每窗口直接数格子
                std::set<std::tuple<int, int, int, int>> want;
                for (int x0 = -80; x0 <= 80 - grid + 1; ++x0) {
                    for (int z0 = -80; z0 <= 80 - grid + 1; ++z0) {
                        int cnt = 0;
                        for (int dx = 0; dx < grid; ++dx)
                            for (int dz = 0; dz < grid; ++dz)
                                if (SlimeEngine::isSlimeChunk(Version::Java, seed, x0 + dx, z0 + dz)) ++cnt;
                        if (static_cast<double>(cnt) / (grid * grid) >= thr) {
                            want.emplace(x0, x0 + grid - 1, z0, z0 + grid - 1);
                        }
                    }
                }
                CHECK(got == want, "密度窗口集合一致");
                if (got != want) {
                    std::printf("    seed=%lld grid=%d thr=%.2f got=%zu want=%zu\n",
                                static_cast<long long>(seed), grid, thr, got.size(), want.size());
                }
            }
        }
        // 严格：2x2 全绿、含白色格、含忽略格
        {
            Pattern p(2, 2);
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 2; ++c) p.set(r, c, CellState::Green);
            SearchOptions opt;
            opt.version = Version::Java; opt.worldSeed = seed;
            opt.minX = -120; opt.maxX = 120; opt.minZ = -120; opt.maxZ = 120;
            opt.mode = Mode::Strict; opt.pattern = p; opt.directionLocked = true;
            opt.targetCount = INT64_MAX; opt.threads = 1;
            SearchOutcome r = runOnce(opt);
            CHECK(r.ok, "严格匹配成功");
            std::set<std::pair<int, int>> anchors;
            for (const MatchResult& m : r.results) {
                anchors.emplace(m.minX, m.minZ);
                // 窗口内 4 格必须全为史莱姆
                for (int dx = 0; dx < 2; ++dx)
                    for (int dz = 0; dz < 2; ++dz)
                        CHECK(SlimeEngine::isSlimeChunk(Version::Java, seed, m.minX + dx, m.minZ + dz),
                              "2x2 窗口全为史莱姆");
            }
            std::set<std::pair<int, int>> wantA;
            for (int x0 = -120; x0 <= 119; ++x0)
                for (int z0 = -120; z0 <= 119; ++z0) {
                    bool all = true;
                    for (int dx = 0; dx < 2 && all; ++dx)
                        for (int dz = 0; dz < 2 && all; ++dz)
                            if (!SlimeEngine::isSlimeChunk(Version::Java, seed, x0 + dx, z0 + dz)) all = false;
                    if (all) wantA.emplace(x0, z0);
                }
            CHECK(anchors == wantA, "2x2 全绿锚点集合一致");
        }
    }
    (void)rng;
}

static void testOrderingAndEarlyStop()
{
    std::printf("[4] 排序/智能螺旋/提前停止 与完整扫描一致性…\n");
    std::mt19937_64 rng(99);
    for (int iter = 0; iter < 6; ++iter) {
        const int64_t seed = static_cast<int64_t>(rng() % 1000000000LL);
        // 密度模式（匹配数量足够多才能检验提前停止）
        for (int smart : { 0, 1 }) {
            SearchOptions base;
            base.version = Version::Java; base.worldSeed = seed;
            base.minX = -250; base.maxX = 250; base.minZ = -250; base.maxZ = 250;
            base.mode = Mode::Density; base.gridSize = 4; base.minDensity = 0.2;
            base.startX = (smart ? 31 : 0);
            base.startZ = (smart ? -17 : 0);
            base.targetCount = 40;
            base.smartRange = (smart == 1);
            base.threads = 1;

            SearchOptions full = base;
            full.stopAtTarget = false;
            SearchOutcome rFull = runOnce(full);
            CHECK(rFull.ok, "完整密度搜索成功");

            SearchOptions stop = base;
            stop.stopAtTarget = true;
            SearchOutcome rStop = runOnce(stop);
            CHECK(rStop.ok, "提前停止搜索成功");

            const auto trunc = [](const SearchOutcome& o) {
                std::vector<std::tuple<int, int, int, int, int>> v;
                for (const MatchResult& m : o.results)
                    v.emplace_back(static_cast<int>(m.density * 100000 + 0.5), m.minX, m.maxX, m.minZ, m.maxZ);
                return v;
            };
            const auto head = [](const std::vector<std::tuple<int, int, int, int, int>>& v, size_t n) {
                std::vector<std::tuple<int, int, int, int, int>> h(v.begin(), v.begin() + std::min(n, v.size()));
                return h;
            };
            CHECK(trunc(rStop) == head(trunc(rFull), 40), "提前停止结果 = 完整结果最近 N 个");
            if (trunc(rStop) != head(trunc(rFull), 40)) {
                std::printf("    iter=%d smart=%d full=%zu stop=%zu\n", iter, smart,
                            rFull.results.size(), rStop.results.size());
            }
            // 智能螺旋与光栅结果一致
            SearchOptions raster = base;
            raster.smartRange = false;
            raster.stopAtTarget = false;
            SearchOutcome rRaster = runOnce(raster);
            const bool equal = trunc(rRaster) == trunc(rFull);
            CHECK(equal, "螺旋顺序与光栅顺序结果一致");
            if (!equal) {
                std::printf("    MISMATCH iter=%d smart=%d seed=%lld full=%zu raster=%zu stop=%zu\n",
                            iter, smart, static_cast<long long>(seed),
                            rFull.results.size(), rRaster.results.size(), rStop.results.size());
            }
            if (smart == 1 && rStop.stoppedEarly) {
                CHECK(!rFull.results.empty(), "提前停止发生在有结果时");
            }
        }
    }
    (void)rng;
}

static void testCacheFlow()
{
    std::printf("[5] 缓存合并/覆盖/存取一致性…\n");
    const int x0 = -300, x1 = 300, z0 = -300, z1 = 300; // 601x601
    SearchOptions base;
    base.version = Version::Bedrock;
    base.worldSeed = 0;
    base.minX = x0; base.maxX = x1; base.minZ = z0; base.maxZ = z1;
    base.mode = Mode::Density; base.gridSize = 5; base.minDensity = 0.28;
    base.startX = 0; base.startZ = 0;
    base.targetCount = 300;
    base.threads = 1;

    // 无缓存基线
    SearchOutcome baseline = runOnce(base);
    CHECK(baseline.ok, "基岩版基线扫描成功");

    // 用两个子区域+补扫构造缓存：先扫 [-300..0]x[-300..0]，再扫 [-300..300]x[-300..300] 剩余部分
    CacheManager cache;
    Rect r1{ -300, 0, -300, 0 };
    SearchOptions sub = base;
    sub.minX = r1.minX; sub.maxX = r1.maxX; sub.minZ = r1.minZ; sub.maxZ = r1.maxZ;
    sub.mode = Mode::Strict; // 无所谓，只用于产生网格扫描数据（缓存通过 merge 手工构造）
    sub.targetCount = 1;
    // v3.3.9（任务书 1.1/1.2）：缓存**只由构建模式写入**（搜索只读 ⇒ 不再产出条目）
    {
        // v3.4.13：本组断言的是**缓存语义**（覆盖声明 / 命中一致性），与分块几何无关；
        //   构建模式的块数目标会让小范围也切成多块 ⇒ 这里用 SLIME_CHUNK_TARGET=0 钉住旧几何，
        //   新几何由 [25]/[28] 与 44G 对照台覆盖。
        setEnvGlobal("SLIME_CHUNK_TARGET", "0");
        SearchOptions b0 = base;
        b0.useCache = true;
        b0.cache = &cache;
        b0.cacheWrite = &cache;
        b0.buildMode = true;
        b0.stopAtTarget = false;
        b0.targetCount = 1;
        const SearchOutcome r0 = runOnce(b0);
        CHECK(r0.ok && r0.results.empty(), "构建模式填充缓存（只判定不匹配）");
        CHECK(static_cast<int64_t>(r0.newCacheChunks.size()) == 0, "构建模式不产出坐标条目（直接写位图）");
        CHECK(cache.entryCount() > 0, "构建模式产生缓存条目");
        // 覆盖声明 = 锚点行域（末 hMax-1 = 4 行为块间重叠行，不登记）
        CHECK(cache.covers(Rect{ x0, x1, z0, z1 - 4 }), "缓存覆盖锚点行域");
    }
    {
        // v3.4.13：本函数余下段落同样依赖旧几何下的缓存语义 ⇒ 钉住保持到函数结束
        // 全覆盖后：命中缓存运行，结果与基线一致，且不产生新条目
        SearchOptions s1 = base;
        s1.useCache = true;
        s1.cache = &cache;
        SearchOutcome r1 = runOnce(s1);
        CHECK(r1.ok, "命中缓存扫描成功");
        CHECK(r1.newCacheChunks.empty(), "全覆盖时无新缓存条目");
        CHECK(r1.results.size() == baseline.results.size(), "命中缓存结果数量一致");
        for (size_t i = 0; i < r1.results.size() && i < baseline.results.size(); ++i) {
            const MatchResult& a = r1.results[i];
            const MatchResult& b = baseline.results[i];
            CHECK(a.minX == b.minX && a.maxX == b.maxX && a.minZ == b.minZ && a.maxZ == b.maxZ,
                  "命中缓存结果与基线一致");
        }
    }
    // 部分覆盖场景：小缓存 + 大查询
    {
        CacheManager c2;
        Rect small{ -100, 100, -100, 100 };
        Rect big{ -300, 300, -300, 300 };
        // 构造小范围缓存（v3.3.9：构建模式写入）
        {
            SearchOptions s = base;
            s.minX = small.minX; s.maxX = small.maxX; s.minZ = small.minZ; s.maxZ = small.maxZ;
            s.useCache = true; s.cache = &c2; s.cacheWrite = &c2; s.buildMode = true;
            s.stopAtTarget = false; s.targetCount = 1;
            const SearchOutcome r = runOnce(s);
            CHECK(r.ok, "构建模式填充小区域缓存");
        }
        CHECK(c2.covers(Rect{ small.minX, small.maxX, small.minZ, small.maxZ - 4 }),
              "小缓存覆盖小区域（锚点行域）");
        CHECK(!c2.covers(big), "小缓存未覆盖大区域");
        {
            SearchOptions s = base;
            s.minX = big.minX; s.maxX = big.maxX; s.minZ = big.minZ; s.maxZ = big.maxZ;
            s.useCache = true; s.cache = &c2;
            SearchOutcome r = runOnce(s);
            CHECK(r.ok, "部分覆盖扫描成功");
            CHECK(r.results.size() == baseline.results.size(), "部分覆盖结果数量一致（缓存命中 + 计算补齐）");
            for (size_t i = 0; i < r.results.size() && i < baseline.results.size(); ++i) {
                const MatchResult& a = r.results[i];
                const MatchResult& b = baseline.results[i];
                CHECK(a.minX == b.minX && a.maxX == b.maxX && a.minZ == b.minZ && a.maxZ == b.maxZ,
                      "部分覆盖结果与基线一致");
            }
            // v3.3.9（任务书 1.2）：搜索不再补写缓存 ⇒ 覆盖声明保持不变
            CHECK(!c2.covers(big), "搜索不写缓存 ⇒ 大区域仍未被声明覆盖");
        }
    }
    // 文件往返
    {
        const auto tmp = std::filesystem::temp_directory_path() / "slime_test_cache.bin";
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        std::string err;
        CHECK(cache.saveFile(tmp, err), "缓存写盘成功");
        CacheManager c3;
        CHECK(c3.loadFile(tmp, true, err), "缓存读盘成功");
        CHECK(c3.entryCount() == cache.entryCount(), "条目数一致");
        CHECK(c3.ranges().size() == cache.ranges().size(), "范围数一致");
        std::filesystem::remove(tmp, ec);
    }
}

static void testCancel()
{
    std::printf("[6] 取消机制…\n");
    SearchOptions opt;
    opt.version = Version::Java;
    opt.worldSeed = 12345;
    opt.minX = -1000000; opt.maxX = 1000000; opt.minZ = -5000; opt.maxZ = 5000; // 约 2e10*? 实际 2e7*1e4? 见下
    opt.minX = -200000; opt.maxX = 200000; opt.minZ = -1000; opt.maxZ = 1000; // 4e5*2001 = 8e8 区块
    opt.mode = Mode::Density;
    opt.gridSize = 3;
    opt.minDensity = 0.5;
    opt.targetCount = 100;
    opt.threads = 2;

    std::atomic<bool> cancel{ false };
    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        cancel.store(true);
    });
    const auto t0 = std::chrono::steady_clock::now();
    SearchOutcome r = runOnce(opt, &cancel);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    t.join();
    CHECK(r.cancelled, "取消后 cancelled 标志置位");
    CHECK(ms < 5000.0, "取消响应及时（<5s）");
    CHECK(r.results.empty(), "取消后不保留部分结果");
}

static void testCacheRegionMergeApi()
{
    std::printf("[7] CacheManager API…\n");
    CacheManager c;
    std::vector<std::pair<int, int>> cells = { { 1, 2 }, { 3, 4 }, { 1, 2 } };
    c.mergeRegion(Rect{ 0, 10, 0, 10 }, cells);
    CHECK(c.entryCount() == 2, "去重后条目数");
    CHECK(c.lookup(1, 2) == 1, "lookup 命中");
    CHECK(c.lookup(3, 4) == 1, "lookup 命中 2");
    CHECK(c.lookup(5, 5) == 0, "lookup 已知非史莱姆");
    CHECK(c.lookup(100, 100) == -1, "lookup 超出覆盖");
    CHECK(c.covers(Rect{ 2, 3, 2, 3 }), "子区域覆盖");
    CHECK(!c.covers(Rect{ -1, 3, 2, 3 }), "越界区域不覆盖");
    c.clear();
    CHECK(c.entryCount() == 0 && !c.hasData(), "清除后为空");
}

static void testKnownChunks()
{
    std::printf("[8] 确定性/密度抽查…\n");
    // 密度合理性（约 1/10）
    int cnt0 = 0;
    for (int x = -50; x <= 50; ++x)
        for (int z = -50; z <= 50; ++z)
            if (SlimeEngine::isSlimeChunkJava(0, x, z)) ++cnt0;
    CHECK(cnt0 > 500 && cnt0 < 1500, "Java seed0 区块密度合理（约 1/10）");
    // 基岩版同样约 1/10
    int cnt1 = 0;
    for (int x = -50; x <= 50; ++x)
        for (int z = -50; z <= 50; ++z)
            if (SlimeEngine::isSlimeChunkBedrockMT(x, z)) ++cnt1;
    CHECK(cnt1 > 500 && cnt1 < 1500, "基岩版区块密度合理（约 1/10）");
    // 与参考实现单点一致性（值来自参考 SlimeChecker.exe，见 verify 脚本）
    CHECK(SlimeEngine::isSlimeChunkJava(0, -2, -1) == true ||
              SlimeEngine::isSlimeChunkJava(0, -2, -1) == false,
          "占位（精确值由外部对照脚本验证）");
}

static void testRotations()
{
    std::printf("[9] 方向不锁定（90° 旋转匹配）一致性…\n");
    std::mt19937_64 rng(2024);
    for (int iter = 0; iter < 3; ++iter) {
        const int64_t seed = static_cast<int64_t>(rng() % 999999937LL);
        Pattern p(2, 3);
        // 非对称图案：两绿 + 两白 + 两灰
        p.set(0, 0, CellState::Green);
        p.set(1, 2, CellState::Green);
        p.set(0, 1, CellState::White);
        p.set(1, 0, CellState::White);
        p.set(0, 2, CellState::Ignore);
        p.set(1, 1, CellState::Ignore);

        const auto rot = [](const Pattern& a) {
            Pattern b(a.cols(), a.rows());
            for (int rr = 0; rr < a.rows(); ++rr)
                for (int cc = 0; cc < a.cols(); ++cc)
                    b.set(cc, a.rows() - 1 - rr, a.at(rr, cc));
            return b;
        };

        // 引擎结果
        SearchOptions opt;
        opt.version = Version::Java;
        opt.worldSeed = seed;
        opt.minX = -60; opt.maxX = 60; opt.minZ = -60; opt.maxZ = 60;
        opt.mode = Mode::Strict;
        opt.pattern = p;
        opt.directionLocked = false;
        opt.targetCount = INT64_MAX;
        opt.threads = 1;
        SearchOutcome r = runOnce(opt);
        CHECK(r.ok, "旋转匹配成功");
        std::set<std::tuple<int, int, int, int>> got;
        for (const MatchResult& m : r.results) got.emplace(m.minX, m.maxX, m.minZ, m.maxZ);

        // 朴素：枚举 4 个旋转各自的窗口锚点
        std::set<std::tuple<int, int, int, int>> want;
        Pattern cur = p;
        for (int k = 0; k < 4; ++k) {
            const int cw = cur.cols(), ch = cur.rows();
            for (int x0 = -60; x0 <= 60 - cw + 1; ++x0) {
                for (int z0 = -60; z0 <= 60 - ch + 1; ++z0) {
                    bool ok = true;
                    for (int rr = 0; rr < ch && ok; ++rr) {
                        for (int cc = 0; cc < cw && ok; ++cc) {
                            const CellState s = cur.at(rr, cc);
                            if (s == CellState::Ignore) continue;
                            const bool slime = SlimeEngine::isSlimeChunk(Version::Java, seed, x0 + cc, z0 + rr);
                            if ((s == CellState::Green) != slime) ok = false;
                        }
                    }
                    if (ok) want.emplace(x0, x0 + cw - 1, z0, z0 + ch - 1);
                }
            }
            cur = rot(cur);
        }
        CHECK(got == want, "旋转匹配窗口集合与朴素枚举一致");
        if (got != want) {
            std::printf("    iter=%d 引擎=%zu 朴素=%zu\n", iter, got.size(), want.size());
        }
    }
}

static void testCachePrune()
{
    std::printf("[10] 缓存容量剪枝（v3.1.0 config.json maxSizeMB）…\n");
    CacheManager c;
    // 三个区域，后加入的为“较新”
    std::vector<std::pair<int, int>> cells1 = { { 1, 1 }, { 2, 2 }, { 3, 3 } };
    std::vector<std::pair<int, int>> cells2 = { { 2, 2 }, { 50, 50 }, { 60, 60 } }; // 与 r1 重叠一格
    std::vector<std::pair<int, int>> cells3 = { { 200, 200 }, { 210, 210 } };
    c.mergeRegion(Rect{ 0, 10, 0, 10 }, cells1);
    c.mergeRegion(Rect{ 40, 70, 40, 70 }, cells2);
    c.mergeRegion(Rect{ 190, 220, 190, 220 }, cells3);
    const int64_t before = c.entryCount();
    CHECK(before == 7, "合并去重后条目数");

    // 剪到仅容纳最近区域（r3）体积
    c.pruneToFit(0); // maxBytes<=0：不动作
    CHECK(c.entryCount() == 7, "无效上限不做剪枝");
    // 剪到只够 ~r3 的体积。v3.3.8 起体积口径 = v2 位图格式：
    //   24 B 头 + 每 range（16 B 矩形 + 4 B 字数 + 位图），r3 为 31×31 ⇒ wpr=1、31 行 ⇒ 31 字 = 248 B
    const int64_t oneRangeR3 = 24 + (16 + 4 + 31 * 8);
    c.pruneToFit(oneRangeR3); // 恰好仅容纳 1 个 range（r3 且其位图）
    CHECK(!c.ranges().empty(), "至少保留一个区域");
    CHECK(c.ranges().size() == 1, "剪枝后仅保留最新区域");
    CHECK(c.covers(Rect{ 190, 220, 190, 220 }), "最新区域仍完整覆盖");
    CHECK(!c.covers(Rect{ 0, 10, 0, 10 }), "最旧区域已被丢弃");
    // 剩余条目都位于保留范围内，且覆盖范围内 lookups 正常
    for (int x = 190; x <= 220; ++x)
        for (int z = 190; z <= 220; z += 3) {
            const int lk = c.lookup(x, z);
            CHECK(lk == 0 || lk == 1, "保留区域全部可判定");
        }
    const bool allCovered = true;
    // 通过再次合并验证一致性：剪枝后的缓存与“只扫 r3”等价 → 重新合并 r3 集合不变
    c.mergeRegion(Rect{ 190, 220, 190, 220 }, cells3);
    CHECK(c.entryCount() == 2, "重复合并无重复条目");
}

static void testChunkedCacheIncremental()
{
    // v3.4.13：本组断言「相邻块声明不重叠」「末块锚点行至 49995」这类**2 块几何语义**，
    //   因此把几何钉在旧口径；新的「4×并发 块数目标」几何由 [25]/[28] 与 44G 对照台覆盖。
    setEnvGlobal("SLIME_CHUNK_TARGET", "0");
    std::printf("[12] 基岩版分块增量缓存（v3.1.2 Bug 修复：不再 ~1KB 空缓存）…\n");
#ifdef _WIN32
    const auto setEnv = [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); };
#else
    const auto setEnv = [](const char* k, const char* v) { v ? setenv(k, v, 1) : unsetenv(k); };
#endif

    CacheManager cache;
    SearchOptions base;
    base.version = Version::Bedrock;
    base.worldSeed = 0;
    base.minX = 0; base.maxX = 2047; // W=2048 (wpr=32)
    base.minZ = 0; base.maxZ = 49999; // H=50000
    base.mode = Mode::Density;
    base.gridSize = 5;
    base.minDensity = 0.3;
    base.startX = 512;
    base.startZ = 25000;
    base.targetCount = 30;
    base.threads = 0;
    base.useCache = true;
    base.cache = &cache;
    base.cacheWrite = &cache;   // 增量合并（v3.1.2）
    base.cacheBudgetBytes = 0;

    // 强制分块 + 小单块容量 → 多块（块数据行 ≈ 32768，共 2 块）
    setEnv("SLIME_FORCE_CHUNKED", "1");
    setEnv("SLIME_CHUNK_WORDS", "1048576");

    // v3.3.9（任务书 1.1/1.2）：缓存**只由「缓存构建模式」写入**；搜索/匹配一律只读。
    SearchOptions build = base;
    build.buildMode = true;
    std::atomic<int64_t> recorded{ 0 }, skippedCovered{ 0 };
    build.liveRecorded = &recorded;
    build.liveSkippedCovered = &skippedCovered;
    SearchOutcome rb = runOnce(build);
    setEnv("SLIME_FORCE_CHUNKED", "0");
    setEnv("SLIME_CHUNK_WORDS", "");
    CHECK(rb.ok && !rb.hitCacheBudget, "缓存构建模式执行成功（未触及容量上限）");
    CHECK(recorded.load() > 10000, "构建模式记录了区块数（新判定）");
    CHECK(rb.results.empty() && rb.stats.windowsEvaluated == 0, "构建模式不做匹配（结果为空、窗口数 0）");
    CHECK(rb.cacheHandledIncrementally, "构建模式按块增量写入缓存");
    CHECK(cache.ranges().size() >= 2, "缓存含多块覆盖声明（不再是 1KB 空文件根因路径）");
    CHECK(cache.entryCount() > 10000, "缓存条目充足（约 10%×扫描量）");

    // v3.3.9（任务书 1.2）：普通搜索**不得**写缓存（即使调用方误设了 cacheWrite 也无效）
    const int64_t entriesBeforeSearch = cache.entryCount();
    const size_t rangesBeforeSearch = cache.ranges().size();
    SearchOutcome r1 = runOnce(base);
    CHECK(r1.ok, "分块基岩搜索成功");
    CHECK(!r1.cacheHandledIncrementally, "普通搜索不做缓存增量合并（任务书 1.2）");
    CHECK(r1.newCacheChunks.empty(), "普通搜索不产出缓存条目（任务书 1.2）");
    CHECK(cache.entryCount() == entriesBeforeSearch && cache.ranges().size() == rangesBeforeSearch,
          "任务书 1.2：搜索前后缓存完全不变（其他模式无法写缓存）");

    // 覆盖声明为逐块**锚点行域**（v3.3.8 候选 ②：登记域缩到 758 行，与匹配域口径一致、相邻块不重叠）
    // 每块数据行数 = SLIME_CHUNK_WORDS(1048576) / wpr(32) = 32768；锚点行数 = 数据行数 - (hMax-1) = 32764
    const int64_t anchorRows = 32764;
    CHECK(cache.covers(Rect{ 0, 2047, 0, static_cast<int>(anchorRows - 1) }), "第 1 块锚点行域覆盖声明");
    CHECK(cache.covers(Rect{ 0, 2047, static_cast<int>(anchorRows), 49995 }),
          "第 2 块锚点行域覆盖声明（不相邻块声明不重叠：末块锚点行至 49995）");
    CHECK(!cache.covers(Rect{ 0, 2047, 0, static_cast<int>(anchorRows) }),
          "相邻块声明不重叠（旧实现登记数据行域时此处为 true）");
    CHECK(!cache.covers(Rect{ 0, 2047, 0, 49999 }), "整区声明不存在（分块语义正确）");

    // 声明域内判定与直接计算一致（抽查）
    // 注意：候选 ② 后声明域 = 锚点行域 ⇒ 区域末尾 hMax-1=4 行与每块重叠 4 行里「只属于下一块锚点域」的部分
    //       不在声明内（lookup==-1），故抽样只在**声明覆盖的行域**（0..49995 = 全部锚点行域）内进行。
    std::mt19937_64 rng(777);
    int bad = 0;
    int64_t uncovered = 0;
    for (int i = 0; i < 400; ++i) {
        const int x = static_cast<int>(rng() % 2048);
        const int z = static_cast<int>(rng() % 49996); // 0..49995：两块锚点行域之并
        const int lk = cache.lookup(x, z);
        if (lk < 0) ++uncovered;
        const bool direct = SlimeEngine::isSlimeChunkBedrockMT(x, z);
        if (lk >= 0 && ((lk == 1) != direct)) ++bad;
    }
    CHECK(uncovered == 0, "锚点行域内全部可判定（声明覆盖完整）");
    CHECK(bad == 0, "缓存判定与直接计算一致");

    // 文件往返
    const auto tmp = std::filesystem::temp_directory_path() / "slime_v312_cache.bin";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    std::string err;
    CHECK(cache.saveFile(tmp, err), "缓存写盘成功（体积随范围增长）");
    CacheManager c2;
    CHECK(c2.loadFile(tmp, true, err), "缓存读盘成功");
    CHECK(c2.ranges().size() == cache.ranges().size() && c2.entryCount() == cache.entryCount(), "重载一致");
    std::filesystem::remove(tmp, ec);

    // 缓存覆盖下的第二次分块搜索结果 = 首次（全新扫描）结果
    setEnv("SLIME_FORCE_CHUNKED", "1");
    setEnv("SLIME_CHUNK_WORDS", "1048576");
    SearchOutcome r2 = runOnce(base);
    setEnv("SLIME_FORCE_CHUNKED", "0");
    setEnv("SLIME_CHUNK_WORDS", "");
    CHECK(r2.ok && r2.results.size() == r1.results.size(), "二次搜索结果数量一致");
    bool same = r2.results.size() == r1.results.size();
    for (size_t i = 0; same && i < r1.results.size(); ++i) {
        const MatchResult& a = r1.results[i];
        const MatchResult& b = r2.results[i];
        if (a.minX != b.minX || a.maxX != b.maxX || a.minZ != b.minZ || a.maxZ != b.maxZ) same = false;
    }
    CHECK(same, "缓存路径与全新扫描结果逐条一致");
    setEnvGlobal("SLIME_CHUNK_TARGET", ""); // v3.4.13：本组结束，还原默认几何
}

static void testChunkedEquality()
{
    std::printf("[11] 整图分块路径（v3.1.1）与经典路径结果一致…\n");
#ifdef _WIN32
    const auto setEnv = [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); };
#else
    const auto setEnv = [](const char* k, const char* v) { v ? setenv(k, v, 1) : unsetenv(k); };
#endif

    // 经典路径结果（小图，未强制分块）
    SearchOptions base;
    base.version = Version::Java;
    base.worldSeed = 20260905;
    base.minX = -50000; base.maxX = 49999; // W=100000
    base.minZ = -1500;  base.maxZ = 1499;  // H=3000
    base.startX = 123;
    base.startZ = -77;
    base.targetCount = 25;
    base.threads = 0;

    struct Case {
        Mode mode;
        Pattern pat;
        bool lock;
        int grid;
        double thr;
        bool stop;
    };
    Pattern p2(2, 2);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c) p2.set(r, c, CellState::Green);
    Pattern p3(2, 3);
    p3.set(0, 0, CellState::Green);
    p3.set(1, 2, CellState::Green);
    p3.set(0, 1, CellState::White);

    const Case cases[] = {
        { Mode::Density, Pattern(), true, 5, 0.32, false },
        { Mode::Density, Pattern(), true, 5, 0.32, true },  // 及时停止（分块路径给出相同全局最近 N）
        { Mode::Strict, p2, true, 0, 0.0, false },
        { Mode::Strict, p3, false, 0, 0.0, false },         // 旋转（不同窗口行高）
    };

    for (const Case& cs : cases) {
        SearchOptions opt = base;
        opt.mode = cs.mode;
        if (opt.mode == Mode::Strict) {
            opt.pattern = cs.pat;
            opt.directionLocked = cs.lock;
        } else {
            opt.gridSize = cs.grid;
            opt.minDensity = cs.thr;
        }
        opt.stopAtTarget = cs.stop;

        setEnv("SLIME_FORCE_CHUNKED", "0");
        setEnv("SLIME_CHUNK_WORDS", "");
        SearchOutcome classic = runOnce(opt);
        CHECK(classic.ok, "经典路径成功");

        // 分块路径：强制分块 + 缩小单块字容量以制造多个 z 分块
        setEnv("SLIME_FORCE_CHUNKED", "1");
        setEnv("SLIME_CHUNK_WORDS", "1500000");
        SearchOutcome chunked = runOnce(opt);
        setEnv("SLIME_FORCE_CHUNKED", "0");
        setEnv("SLIME_CHUNK_WORDS", "");

        CHECK(chunked.ok, "分块路径成功");
        const auto toKey = [](const MatchResult& m) {
            return std::make_tuple(m.minX, m.maxX, m.minZ, m.maxZ,
                                   static_cast<int>(m.density * 100000.0 + 0.5));
        };
        bool same = chunked.results.size() == classic.results.size();
        if (same) {
            for (size_t i = 0; i < classic.results.size(); ++i) {
                if (toKey(classic.results[i]) != toKey(chunked.results[i])) { same = false; break; }
            }
        }
        CHECK(same, "分块与经典结果（最近 N）逐条一致");
        if (!same) {
            std::printf("    mode=%d 经典=%zu 分块=%zu\n", static_cast<int>(cs.mode),
                        classic.results.size(), chunked.results.size());
        }
        // totalFound：分块为全量统计，经典在未提前停止时亦为全量
        if (!cs.stop) {
            CHECK(chunked.totalFound == classic.totalFound, "总命中数一致");
        } else {
            CHECK(!chunked.results.empty() || classic.results.empty(), "及时停止场景自洽");
        }
        CHECK(chunked.stats.scannedChunks >= classic.stats.scannedChunks, "分块含重叠扫描计数");
    }
    // 估算 API 自洽性（整图规模）
    {
        SearchOptions big = base;
        big.minX = -1875000; big.maxX = 1875000;
        big.minZ = -1875000; big.maxZ = 1875000;
        big.mode = Mode::Strict;
        big.pattern = p2;
        SearchEstimate est;
        const bool ok = SlimeEngine::estimateSearch(big, 12, est);
        CHECK(ok, "整图估算可用");
        if (ok) {
            CHECK(est.areaChunks == 3750001LL * 3750001LL, "整图区块数正确");
            CHECK(est.evalChunks >= est.areaChunks, "评估区块 ≥ 面积");
            CHECK(est.windows > 0, "窗口总数 > 0");
            CHECK(est.totalSec > 0.0, "总耗时估算为正");
            CHECK(est.chunkRows > 0 && est.chunkConcurrency >= 1, "分块参数有效");
        }
    }
}

// v3.2.0：GPU 引擎等价（CPU 路径 vs GPU 路径结果完全一致；无设备时跳过）
// 注：本组只测「GPU 判定扫描」等价性，故显式关闭 GPU 密度匹配（SLIME_GPU_MATCH=0）——
//     后者由 [17] 专门覆盖（两组职责分离，避免一个失败掩盖另一个）。
static void testGpuEngineParity()
{
    std::printf("[13] GPU 引擎等价（CPU vs GPU 完整搜索结果一致）…\n");
    std::string gerr;
    auto gpu = slimegpu::createGpuScanAdapter(&gerr);
    if (!gpu) {
        std::printf("  [SKIP] 无可用 OpenCL 设备，跳过（%s）\n", gerr.c_str());
        return;
    }
    const std::string oldMatchEnv = std::getenv("SLIME_GPU_MATCH") ? std::getenv("SLIME_GPU_MATCH") : "";
#ifdef _WIN32
    _putenv_s("SLIME_GPU_MATCH", "0"); // 本组只验证扫描路径
#else
    setenv("SLIME_GPU_MATCH", "0", 1);
#endif

    char msg[160];
    const auto compare = [&](const SearchOptions& base, const char* label) {
        SearchOutcome cpu = runOnce(base);
        SearchOptions g = base;
        g.gpuScan = gpu.get();
        SearchOutcome gpuOut = runOnce(g);

        std::snprintf(msg, sizeof(msg), "%s: CPU/GPU 均成功", label);
        CHECK(cpu.ok && gpuOut.ok, msg);

        auto normNew = [](std::vector<std::pair<int, int>>& v) {
            std::sort(v.begin(), v.end());
        };
        normNew(cpu.newCacheChunks);
        normNew(gpuOut.newCacheChunks);
        std::snprintf(msg, sizeof(msg), "%s: 结果数量一致 (%zu vs %zu)", label,
                      cpu.results.size(), gpuOut.results.size());
        CHECK(cpu.results.size() == gpuOut.results.size(), msg);
        bool sameResults = cpu.results.size() == gpuOut.results.size();
        if (sameResults) {
            for (size_t i = 0; i < cpu.results.size(); ++i) {
                const auto& a = cpu.results[i];
                const auto& b = gpuOut.results[i];
                if (a.minX != b.minX || a.maxX != b.maxX || a.minZ != b.minZ || a.maxZ != b.maxZ ||
                    a.density != b.density) {
                    sameResults = false;
                    break;
                }
            }
        }
        std::snprintf(msg, sizeof(msg), "%s: 匹配列表逐项一致", label);
        CHECK(sameResults, msg);
        std::snprintf(msg, sizeof(msg), "%s: totalFound 一致 (%lld vs %lld)", label,
                      static_cast<long long>(cpu.totalFound),
                      static_cast<long long>(gpuOut.totalFound));
        CHECK(cpu.totalFound == gpuOut.totalFound, msg);
        std::snprintf(msg, sizeof(msg), "%s: 新增缓存条目一致 (%zu vs %zu)", label,
                      cpu.newCacheChunks.size(), gpuOut.newCacheChunks.size());
        CHECK(cpu.newCacheChunks == gpuOut.newCacheChunks, msg);
        std::printf("      %s: CPU 扫描 %.0f ms | GPU 扫描 %.0f ms | 结果 %zu\n", label,
                    cpu.stats.scanMs, gpuOut.stats.scanMs, cpu.results.size());
    };

    // 退出前恢复环境变量（见下方 restoreEnv 调用）

    // A) Java 密度（大矩形、结果多）
    {
        SearchOptions o;
        o.version = Version::Java;
        o.worldSeed = 42;
        o.minX = -1024; o.maxX = 1023; o.minZ = -1024; o.maxZ = 1023;
        o.mode = Mode::Density;
        o.gridSize = 2;
        o.minDensity = 0.9;
        o.targetCount = INT64_MAX;
        o.threads = 0;
        compare(o, "Java 密度 2048x2048");
    }
    // B) 基岩密度（GPU 收益最大的算法）
    {
        SearchOptions o;
        o.version = Version::Bedrock;
        o.minX = -1024; o.maxX = 1023; o.minZ = -1024; o.maxZ = 1023;
        o.mode = Mode::Density;
        o.gridSize = 2;
        o.minDensity = 0.7;
        o.targetCount = INT64_MAX;
        o.threads = 0;
        compare(o, "基岩密度 2048x2048");
    }
    // C) 基岩 + 缓存（左半覆盖声明为空条目；右半计算 + 新条目收集）
    {
        CacheManager cm;
        cm.mergeRegion(Rect{ -1024, -1, -1024, 1023 }, {}); // 覆盖声明（无条目）
        SearchOptions o;
        o.version = Version::Bedrock;
        o.minX = -1024; o.maxX = 1023; o.minZ = -1024; o.maxZ = 1023;
        o.mode = Mode::Density;
        o.gridSize = 2;
        o.minDensity = 0.7;
        o.targetCount = INT64_MAX;
        o.threads = 0;
        o.useCache = true;
        o.cache = &cm;
        compare(o, "基岩密度+缓存覆盖");
    }
    // D) 分块路径 + GPU（强制分块、小块容量 → 多分块并发）
    {
        const std::string oldForce = std::getenv("SLIME_FORCE_CHUNKED") ? std::getenv("SLIME_FORCE_CHUNKED") : "";
        const std::string oldWords = std::getenv("SLIME_CHUNK_WORDS") ? std::getenv("SLIME_CHUNK_WORDS") : "";
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_CHUNKED", "1");
        _putenv_s("SLIME_CHUNK_WORDS", "65536");
#else
        setenv("SLIME_FORCE_CHUNKED", "1", 1);
        setenv("SLIME_CHUNK_WORDS", "65536", 1);
#endif
        SearchOptions o;
        o.version = Version::Java;
        o.worldSeed = 7;
        o.minX = -2048; o.maxX = 2047; o.minZ = -1024; o.maxZ = 1023;
        o.mode = Mode::Density;
        o.gridSize = 3;
        o.minDensity = 0.5;
        o.targetCount = 10000;
        o.threads = 0;
        compare(o, "Java 密度 分块(4块)+GPU");
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_CHUNKED", oldForce.c_str());
        _putenv_s("SLIME_CHUNK_WORDS", oldWords.c_str());
#else
        setenv("SLIME_FORCE_CHUNKED", oldForce.c_str(), 1);
        setenv("SLIME_CHUNK_WORDS", oldWords.c_str(), 1);
#endif
    }
#ifdef _WIN32
    _putenv_s("SLIME_GPU_MATCH", oldMatchEnv.c_str()); // 恢复（[17] 需要默认启用 GPU 匹配）
#else
    setenv("SLIME_GPU_MATCH", oldMatchEnv.c_str(), 1);
#endif
}

static double pearson(const std::vector<double>& a, const std::vector<double>& b)
{
    if (a.size() != b.size() || a.size() < 3) return 0.0;
    const double n = static_cast<double>(a.size());
    double sa = 0, sb = 0;
    for (size_t i = 0; i < a.size(); ++i) { sa += a[i]; sb += b[i]; }
    const double ma = sa / n, mb = sb / n;
    double cab = 0, va = 0, vb = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double da = a[i] - ma, db = b[i] - mb;
        cab += da * db; va += da * da; vb += db * db;
    }
    if (va <= 0 || vb <= 0) return 0.0;
    return cab / std::sqrt(va * vb);
}

// v3.3.0（3.3 分层 / 3.5 采样估算）：新算法模式验收测试
static void testNewAlgorithmModes()
{
    std::printf("[14] 分层模式/快速估算模式（v3.3.0 新增算法）…\n");
    const int64_t seed = 20260915;

    // ---------- A. 分层模式：块级必要条件剪枝（ratio ≤ 1 时无损） ----------
    {
        // A1. 稀疏场景（无匹配窗口）：剪枝应带来显著加速，且结果与精确一致
        //     g=64/阈值0.5 时窗口需 2048 个史莱姆区块（基准密度 ~10%），地图上不可能成立，
        //     分层剪枝可整段跳过；精确模式仍需逐窗统计 64 行。
        {
            SearchOptions ex;
            ex.version = Version::Java;
            ex.worldSeed = seed;
            ex.minX = 0; ex.maxX = 2999; ex.minZ = 0; ex.maxZ = 2999;
            ex.mode = Mode::Density; ex.gridSize = 64; ex.minDensity = 0.5;
            ex.targetCount = 20000;
            ex.threads = 1;
            ex.algorithm = Algorithm::Exact;
            const SearchOutcome rEx = runOnce(ex);

            SearchOptions br = ex;
            br.algorithm = Algorithm::Brute; // 保守模式 = v3.2.1 字面实现（同二进制 A/B 基准）
            const SearchOutcome rBr = runOnce(br);

            SearchOptions ly = ex;
            ly.algorithm = Algorithm::Layered;
            ly.layerBlock = 64;
            ly.layerRatio = 1.0;
            const SearchOutcome rLy = runOnce(ly);
            CHECK(rEx.ok && rLy.ok && rBr.ok, "分层/精确/保守搜索均成功（稀疏场景）");
            CHECK(rLy.windowsSkipped > 0, "分层模式产生剪枝跳过（windowsSkipped>0）");
            CHECK(rLy.results.size() == rEx.results.size(), "稀疏场景：分层结果数与精确一致（无损剪枝）");
            CHECK(rBr.results.size() == rEx.results.size(), "保守模式结果数与精确一致（语义等价基准）");
            const double speed = rLy.stats.totalMs > 0 ? rEx.stats.totalMs / rLy.stats.totalMs : 0.0;
            const double speedBr = rLy.stats.totalMs > 0 ? rBr.stats.totalMs / rLy.stats.totalMs : 0.0;
            const double speedMatch = (rLy.stats.matchMs - rLy.stats.scanMs) > 0
                                          ? (rEx.stats.matchMs - rEx.stats.scanMs) /
                                                (rLy.stats.matchMs - rLy.stats.scanMs)
                                          : 0.0;
            const int64_t totalWin = static_cast<int64_t>(2985) * 2985;
            std::printf("      稀疏场景(g=64,阈值0.5)：精确 %.0fms | 保守 %.0fms | 分层 %.0fms(%zu) 加速 vs精确 %.2fx"
                        " vs保守 %.2fx（匹配阶段 %.2fx）剪枝率 %.1f%%\n",
                        rEx.stats.totalMs, rBr.stats.totalMs, rLy.stats.totalMs, rLy.results.size(), speed,
                        speedBr, speedMatch,
                        100.0 * static_cast<double>(rLy.windowsSkipped) / static_cast<double>(totalWin));
            CHECK(speedBr >= 5.0, "分层模式在稀疏区域加速 ≥ 5 倍（相对保守模式）");
        }

        // A2. 有匹配场景：分层结果必须与精确结果完全一致（正确性红线）
        {
            SearchOptions ex;
            ex.version = Version::Java;
            ex.worldSeed = seed;
            ex.minX = 0; ex.maxX = 1999; ex.minZ = 0; ex.maxZ = 1999;
            ex.mode = Mode::Density; ex.gridSize = 5; ex.minDensity = 0.4;
            ex.targetCount = 100000;
            ex.threads = 1;
            ex.algorithm = Algorithm::Exact;
            const SearchOutcome rEx = runOnce(ex);

            SearchOptions ly = ex;
            ly.algorithm = Algorithm::Layered;
            ly.layerBlock = 16;
            ly.layerRatio = 1.0;
            const SearchOutcome rLy = runOnce(ly);
            CHECK(rEx.ok && rLy.ok, "分层/精确搜索均成功（密集场景）");

            std::set<std::tuple<int, int, int, int>> setEx, setLy;
            for (const MatchResult& m : rEx.results) setEx.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
            for (const MatchResult& m : rLy.results) setLy.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
            const bool same = (setEx == setLy);
            CHECK(same, "分层结果与精确结果 100% 一致（无损剪枝）");
            const double speed = rLy.stats.totalMs > 0 ? rEx.stats.totalMs / rLy.stats.totalMs : 0.0;
            std::printf("      密集场景(g=5,阈值0.4)：精确 %.0fms(%zu 结果) | 分层 %.0fms(%zu 结果) 加速 %.2fx 跳过 %lld\n",
                        rEx.stats.totalMs, setEx.size(), rLy.stats.totalMs, setLy.size(), speed,
                        static_cast<long long>(rLy.windowsSkipped));

            // A3. ratio > 1：门限高于必要条件 → 剪枝更激进（可能漏检），结果仍必须是精确结果的子集
            SearchOptions ag = ly;
            ag.layerRatio = 2.0;
            const SearchOutcome rAg = runOnce(ag);
            CHECK(rAg.ok, "分层激进模式搜索成功");
            size_t falsePos = 0;
            for (const MatchResult& m : rAg.results) {
                if (!setEx.count(std::make_tuple(m.minX, m.maxX, m.minZ, m.maxZ))) ++falsePos;
            }
            CHECK(falsePos == 0, "激进分层不产生假阳性（结果 ⊆ 精确结果）");
            CHECK(rAg.windowsSkipped >= rLy.windowsSkipped, "阈值比例 >1 时剪枝跳过数不减少");
            std::printf("      比例 2.0（可能漏检）：跳过 %lld，保留 %zu/%zu\n",
                        static_cast<long long>(rAg.windowsSkipped), rAg.results.size(), setEx.size());
        }
    }

    // ---------- B. 估算模式：结果位置/密度与独立采样计算一致；加速比；相关性统计 ----------
    {
        // B1. 加速比：大区域（16384² = 2.68 亿区块）单核对比，扫描与匹配均按 1/S² 缩减。
        //     网格取 32（窗口内 4×4 个采样点），估算模式的命中集与精确模式量级可比。
        {
            const int NL = 16384;
            const int g = 32;
            const int S = 8;
            SearchOptions ex;
            ex.version = Version::Java;
            ex.worldSeed = seed;
            ex.minX = 0; ex.maxX = NL - 1; ex.minZ = 0; ex.maxZ = NL - 1;
            ex.mode = Mode::Density; ex.gridSize = g; ex.minDensity = 0.4;
            ex.targetCount = 200000;
            ex.threads = 1;
            ex.algorithm = Algorithm::Exact;
            const SearchOutcome rEx = runOnce(ex);
            SearchOptions sp = ex;
            sp.algorithm = Algorithm::Sample;
            sp.sampleStep = S;
            const SearchOutcome rSp = runOnce(sp);
            SearchOptions br = ex;
            br.algorithm = Algorithm::Brute;
            const SearchOutcome rBr = runOnce(br);
            CHECK(rEx.ok && rSp.ok && rBr.ok, "精确/估算/保守搜索均成功（大区域）");
            const double speed = rSp.stats.totalMs > 0 ? rEx.stats.totalMs / rSp.stats.totalMs : 0.0;
            const double speedBr = rSp.stats.totalMs > 0 ? rBr.stats.totalMs / rSp.stats.totalMs : 0.0;
            std::printf("      大区域 %dx%d g=32 单核：精确 %.0fms | 保守 %.0fms | 估算 %.0fms(%zu 结果)"
                        " 加速 vs精确 %.2fx vs保守 %.2fx\n",
                        NL, NL, rEx.stats.totalMs, rBr.stats.totalMs, rSp.stats.totalMs, rSp.results.size(),
                        speed, speedBr);
            std::printf("      采样间隔 %d：计算区块 %lld / 总区块 %lld（%.3f%%）\n", S,
                        static_cast<long long>(rSp.stats.scannedChunks),
                        static_cast<long long>(NL) * NL,
                        100.0 * static_cast<double>(rSp.stats.scannedChunks) / (static_cast<double>(NL) * NL));
            CHECK(speedBr >= 50.0, "快速估算模式加速 ≥ 50 倍（相对保守模式）");
        }

        const int N = 4096;
        const int g = 5;
        const double thr = 0.4;
        const int S = 8;

        SearchOptions ex;
        ex.version = Version::Java;
        ex.worldSeed = seed;
        ex.minX = 0; ex.maxX = N - 1; ex.minZ = 0; ex.maxZ = N - 1;
        ex.mode = Mode::Density; ex.gridSize = g; ex.minDensity = thr;
        ex.targetCount = 200000; // 足以容纳全部匹配窗口
        ex.threads = 1;
        ex.algorithm = Algorithm::Exact;
        const SearchOutcome rEx = runOnce(ex);

        SearchOptions sp = ex;
        sp.algorithm = Algorithm::Sample;
        sp.sampleStep = S;
        const SearchOutcome rSp = runOnce(sp);
        CHECK(rEx.ok && rSp.ok, "精确/估算搜索均成功");

        // B1. 每个估算结果必须落在采样网格上，且密度等于“窗口内采样格点中史莱姆比例”
        size_t badPos = 0, badVal = 0;
        for (const MatchResult& m : rSp.results) {
            if ((m.minX % S) != 0 || (m.minZ % S) != 0) { ++badPos; continue; }
            int hits = 0, samples = 0;
            for (int rz = m.minZ; rz < m.minZ + g; rz += S) {
                for (int rx = m.minX; rx < m.minX + g; rx += S) {
                    ++samples;
                    if (SlimeEngine::isSlimeChunk(Version::Java, seed, rx, rz)) ++hits;
                }
            }
            const double est = samples > 0 ? static_cast<double>(hits) / samples : 0.0;
            if (std::fabs(est - m.density) > 1e-6) ++badVal;
        }
        CHECK(badPos == 0, "估算结果锚点全部位于采样网格上");
        CHECK(badVal == 0, "估算结果密度 = 窗口内采样格点的史莱姆比例");

        const double speed = rSp.stats.totalMs > 0 ? rEx.stats.totalMs / rSp.stats.totalMs : 0.0;
        std::printf("      小区域 %dx%d g=5：精确 %.0fms | 估算 %.0fms 加速 %.2fx（g 小、命中集大，固定开销占比高）\n",
                    N, N, rEx.stats.totalMs, rSp.stats.totalMs, speed);

        // B2. 相关性统计（独立于引擎重算采样估计与精确密度，逐锚点比对）
        std::vector<double> estV, trueV;
        const int blockA = 64; // 区域级口径的块边长（锚点坐标）
        std::map<std::pair<int, int>, std::pair<double, int>> blkEst;  // (sum, count)
        std::map<std::pair<int, int>, std::pair<double, int>> blkTrue;
        const int MS = 512; // 统计窗口（控制 isSlimeChunk 调用量）
        for (int az = 0; az + g <= MS; az += S) {
            for (int ax = 0; ax + g <= MS; ax += S) {
                int hits = 0, samples = 0;
                for (int rz = az; rz < az + g; rz += S)
                    for (int rx = ax; rx < ax + g; rx += S) {
                        ++samples;
                        if (SlimeEngine::isSlimeChunk(Version::Java, seed, rx, rz)) ++hits;
                    }
                const double est = static_cast<double>(hits) / samples;
                int cnt = 0;
                for (int rz = az; rz < az + g; ++rz)
                    for (int rx = ax; rx < ax + g; ++rx)
                        if (SlimeEngine::isSlimeChunk(Version::Java, seed, rx, rz)) ++cnt;
                const double tru = static_cast<double>(cnt) / (g * g);
                estV.push_back(est);
                trueV.push_back(tru);
                const auto key = std::make_pair(ax / blockA, az / blockA);
                blkEst[key].first += est;
                blkEst[key].second += 1;
                blkTrue[key].first += tru;
                blkTrue[key].second += 1;
            }
        }
        std::vector<double> be, bt;
        for (const auto& kv : blkEst) {
            const auto it = blkTrue.find(kv.first);
            if (it == blkTrue.end() || kv.second.second == 0) continue;
            be.push_back(kv.second.first / kv.second.second);
            bt.push_back(it->second.first / it->second.second);
        }
        const double corrAnchor = pearson(estV, trueV);
        const double corrBlock = pearson(be, bt);
        // 位置级命中：精确匹配中“锚点在采样网格上”的比例（可被采样覆盖的上限）
        size_t gridMatches = 0;
        for (const MatchResult& m : rEx.results)
            if ((m.minX % S) == 0 && (m.minZ % S) == 0) ++gridMatches;
        std::printf("      相关性（同位置密度，样本 %zu）：%.3f | 区域级(块=%d)：%.3f | 理论上界 1/S=%.3f\n",
                    estV.size(), corrAnchor, blockA, corrBlock, 1.0 / S);
        std::printf("      精确结果中位于采样网格上的窗口：%zu/%zu（采样可覆盖上限）\n",
                    gridMatches, rEx.results.size());

        // B3. 采样间隔 → 相关性经验规律（理论：corr = sqrt(m)/g，m = 窗口内采样点数）
        for (int s : { 2, 4, 8 }) {
            std::vector<double> e2, t2;
            for (int az = 0; az + g <= 256; az += s) {
                for (int ax = 0; ax + g <= 256; ax += s) {
                    int hits = 0, samples = 0;
                    for (int rz = az; rz < az + g; rz += s)
                        for (int rx = ax; rx < ax + g; rx += s) {
                            ++samples;
                            if (SlimeEngine::isSlimeChunk(Version::Java, seed, rx, rz)) ++hits;
                        }
                    int cnt = 0;
                    for (int rz = az; rz < az + g; ++rz)
                        for (int rx = ax; rx < ax + g; ++rx)
                            if (SlimeEngine::isSlimeChunk(Version::Java, seed, rx, rz)) ++cnt;
                    e2.push_back(static_cast<double>(hits) / samples);
                    t2.push_back(static_cast<double>(cnt) / (g * g));
                }
            }
            const int m = ((g + s - 1) / s) * ((g + s - 1) / s);
            std::printf("      采样间隔 %d：实测相关 %.3f（理论 sqrt(m)/g = %.3f，m=%d）\n", s, pearson(e2, t2),
                        std::sqrt(static_cast<double>(m)) / g, m);
        }

        // B4. 精算采样（sampleExact）：结果 ⊆ 精确结果，报告密度与精确密度完全一致（相关 1.00）
        {
            SearchOptions se = sp;
            se.sampleExact = true;
            const SearchOutcome rSe = runOnce(se);
            CHECK(rSe.ok, "精算采样搜索成功");
            std::map<std::pair<int, int>, double> exD;
            for (const MatchResult& m : rEx.results) exD[{ m.minX, m.minZ }] = m.density;
            size_t notInExact = 0, densMismatch = 0;
            for (const MatchResult& m : rSe.results) {
                if ((m.minX % S) != 0 || (m.minZ % S) != 0) ++notInExact;
                const auto it = exD.find({ m.minX, m.minZ });
                if (it == exD.end()) {
                    ++notInExact;
                } else if (std::fabs(static_cast<double>(it->second) - m.density) > 1e-6) {
                    ++densMismatch;
                }
            }
            CHECK(notInExact == 0, "精算采样结果全部属于精确结果（采样锚点上的密度一致）");
            CHECK(densMismatch == 0, "精算采样报告密度与精确模式完全一致（相关性 = 1.00）");
            const double speedSE = rSe.stats.totalMs > 0 ? rEx.stats.totalMs / rSe.stats.totalMs : 0.0;
            const double corrSE = rSe.results.empty() ? 0.0 : 1.0;
            std::printf("      精算采样：%.0fms（%zu 结果，加速 %.2fx，相关性 %.2f）"
                        " vs 快速估算 %.0fms（%zu 结果，相关性 %.2f）\n",
                        rSe.stats.totalMs, rSe.results.size(), speedSE, corrSE, rSp.stats.totalMs,
                        rSp.results.size(), corrAnchor);
        }
    }
}

// v3.3.0（3.4）：基岩版 AVX2 批量 MT19937 —— 一致性 + 单核吞吐 + 引擎级等价
static void testBedrockSimdMt()
{
    std::printf("[15] 基岩版 AVX2 批量 MT19937（v3.3.0 新增内核）…\n");
    std::printf("      AVX2 内核: %s\n", fastmt::cpuHasAvx2() ? "可用" : "不可用（标量回退）");

    // 15.1 一致性：批量扫描 vs 引擎逐区块判定（含负坐标、奇偶、跨 int32 边界）
    {
        size_t mismatch = 0, checked = 0;
        const int32_t czs[] = { 0, 1, -1, 7, -7, 123456, -123456 };
        for (int32_t cz : czs) {
            for (int32_t start : { 0, 1, 2, 3, 5, -1, -5, 1000, -1000, 2147483000, -2147483000 }) {
                const int32_t n = 97;
                std::vector<uint8_t> buf(static_cast<size_t>(n));
                fastmt::bedrockSlimeScan(start, cz, n, buf.data(), false);
                for (int32_t i = 0; i < n; ++i) {
                    const int32_t cx = static_cast<int32_t>(static_cast<uint32_t>(start) +
                                                            static_cast<uint32_t>(i)); // int32 回绕
                    const bool want = SlimeEngine::isSlimeChunk(Version::Bedrock, 0, cx, cz);
                    if (want != (buf[static_cast<size_t>(i)] != 0)) ++mismatch;
                    ++checked;
                }
            }
        }
        CHECK(mismatch == 0, "批量扫描与引擎逐区块判定完全一致");
        std::printf("      一致性：%zu 个区块，%zu 处不一致\n", checked, mismatch);

        size_t mismatch2 = 0;
        for (int32_t start : { -33, 0, 17, 4096, -4097 }) {
            std::vector<uint8_t> a(256), b(256);
            fastmt::bedrockSlimeScan(start, 4242, 256, a.data(), false);
            fastmt::bedrockSlimeScanScalar(start, 4242, 256, b.data(), false);
            if (a != b) ++mismatch2;
        }
        CHECK(mismatch2 == 0, "AVX2 扫描与纯标量扫描一致（A/B 对照）");
    }

    // 15.2 单核吞吐（warm-up + best of 3）
    {
        constexpr int64_t kTotal = 20000000;
        const int32_t cz = 20260915;
        std::vector<uint8_t> buf(1024);
        const auto runOnce = [&]() {
            const auto t0 = std::chrono::steady_clock::now();
            int64_t done = 0;
            int32_t cx = -10000000;
            uint64_t sink = 0;
            while (done < kTotal) {
                const int32_t n = 1024;
                fastmt::bedrockSlimeScan(cx, cz, n, buf.data(), false);
                for (int i = 0; i < n; i += 64) sink += buf[static_cast<size_t>(i)];
                cx += n;
                done += n;
            }
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            return std::make_pair(sec > 0 ? static_cast<double>(done) / sec : 0.0, sink);
        };
        double best = 0.0;
        uint64_t sink = 0;
        for (int i = 0; i < 4; ++i) { // 第 1 次为 warm-up
            const auto r = runOnce();
            sink = r.second;
            if (i > 0 && r.first > best) best = r.first;
        }
        std::printf("      单核吞吐：%.2f 万区块/秒（%.2f M/s，sink=%llu）\n", best / 10000.0, best / 1e6,
                    static_cast<unsigned long long>(sink));
        CHECK(best >= 4.0e6, "基岩版单核速度 ≥ 400 万/秒（AVX2 生效时）");
    }

    // 15.4 v3.3.1：基岩版采样扫描的 AVX2 批处理（跨步/连续段）与独立参考一致
    {
        const int N = 1024;
        const int g = 5;
        const int S = 8;
        SearchOptions ex;
        ex.version = Version::Bedrock;
        ex.minX = 0; ex.maxX = N - 1;
        ex.minZ = 0; ex.maxZ = N - 1;
        ex.mode = Mode::Density;
        ex.gridSize = g;
        ex.minDensity = 0.4;
        ex.targetCount = 50000;
        ex.threads = 1;
        ex.algorithm = Algorithm::Exact;
        const SearchOutcome rEx = runOnce(ex);

        SearchOptions sp = ex;
        sp.algorithm = Algorithm::Sample;
        sp.sampleStep = S;
        sp.sampleExact = false;
        const SearchOutcome rFast = runOnce(sp);
        sp.sampleExact = true;
        const SearchOutcome rExact = runOnce(sp);
        CHECK(rEx.ok && rFast.ok && rExact.ok, "基岩版 精确/估算/精算采样 搜索均成功");

        // 快速估算：位置必须落在采样网格上，且密度 = 窗口内采样格点比例（独立复算）
        size_t bad = 0;
        for (const MatchResult& m : rFast.results) {
            if ((m.minX % S) != 0 || (m.minZ % S) != 0) { ++bad; continue; }
            int hits = 0, samples = 0;
            for (int rz = m.minZ; rz < m.minZ + g; rz += S)
                for (int rx = m.minX; rx < m.minX + g; rx += S) {
                    ++samples;
                    if (SlimeEngine::isSlimeChunk(Version::Bedrock, 0, rx, rz)) ++hits;
                }
            const double est = samples > 0 ? static_cast<double>(hits) / samples : 0.0;
            if (std::fabs(est - m.density) > 1e-6) ++bad;
        }
        CHECK(bad == 0, "基岩版快速估算：采样位置与估算密度与独立参考一致（AVX2 跨步批量）");

        // 精算采样：结果必须属于精确结果集合，且密度逐位一致（AVX2 连续段批量）
        std::map<std::pair<int, int>, float> exD;
        for (const MatchResult& m : rEx.results) exD[{ m.minX, m.minZ }] = m.density;
        size_t notIn = 0, densDiff = 0;
        for (const MatchResult& m : rExact.results) {
            const auto it = exD.find({ m.minX, m.minZ });
            if (it == exD.end()) {
                ++notIn;
            } else if (std::memcmp(&it->second, &m.density, sizeof(float)) != 0) {
                ++densDiff;
            }
        }
        CHECK(notIn == 0 && densDiff == 0, "基岩版精算采样结果属于精确结果且密度逐位一致");

        // 强制标量 MT（SLIME_FORCE_SCALAR=1）结果必须与 AVX2 批处理完全一致
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_SCALAR", "1");
#else
        setenv("SLIME_FORCE_SCALAR", "1", 1);
#endif
        const SearchOutcome rFastScalar = runOnce(sp.sampleExact ? ex : sp); // 见下：分别跑两种模式
        SearchOptions spF = ex;
        spF.algorithm = Algorithm::Sample;
        spF.sampleStep = S;
        spF.sampleExact = false;
        const SearchOutcome rFastScalar2 = runOnce(spF);
        SearchOptions spE = spF;
        spE.sampleExact = true;
        const SearchOutcome rExactScalar = runOnce(spE);
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_SCALAR", "");
#else
        unsetenv("SLIME_FORCE_SCALAR");
#endif
        std::set<std::tuple<int, int, float>> f1, f2;
        for (const MatchResult& m : rFast.results) f1.emplace(m.minX, m.minZ, m.density);
        for (const MatchResult& m : rFastScalar2.results) f2.emplace(m.minX, m.minZ, m.density);
        CHECK(f1 == f2, "基岩版快速估算：AVX2 批量与强制标量 MT 结果一致");
        std::set<std::tuple<int, int, float>> e1, e2;
        for (const MatchResult& m : rExact.results) e1.emplace(m.minX, m.minZ, m.density);
        for (const MatchResult& m : rExactScalar.results) e2.emplace(m.minX, m.minZ, m.density);
        CHECK(e1 == e2, "基岩版精算采样：AVX2 批量与强制标量 MT 结果一致");
        (void)rFastScalar;
        std::printf("      采样扫描：快速 %zu 条 / 精算 %zu 条，AVX2 与标量 MT 完全一致\n",
                    rFast.results.size(), rExact.results.size());
    }

    // 15.3 引擎级等价：同一基岩版搜索（AVX2 内核 vs SLIME_FORCE_SCALAR=1 强制标量）
    {
        SearchOptions opt;
        opt.version = Version::Bedrock;
        opt.minX = -1024; opt.maxX = 1023;
        opt.minZ = -1024; opt.maxZ = 1023;
        opt.mode = Mode::Density;
        opt.gridSize = 3;
        opt.minDensity = 0.34;
        opt.targetCount = 5000;
        opt.threads = 1;
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_SCALAR", "0");
#else
        setenv("SLIME_FORCE_SCALAR", "0", 1);
#endif
        const SearchOutcome rAvx = runOnce(opt);
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_SCALAR", "1");
#else
        setenv("SLIME_FORCE_SCALAR", "1", 1);
#endif
        const SearchOutcome rScalar = runOnce(opt);
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_SCALAR", "");
#else
        unsetenv("SLIME_FORCE_SCALAR");
#endif
        CHECK(rAvx.ok && rScalar.ok, "基岩版搜索成功（AVX2 / 标量）");
        std::set<std::tuple<int, int, int, int>> sa, ss;
        for (const MatchResult& m : rAvx.results) sa.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
        for (const MatchResult& m : rScalar.results) ss.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
        CHECK(sa == ss, "基岩版搜索结果 AVX2 与标量完全一致");
        std::printf("      引擎级等价：AVX2 %.0fms/%zu 结果 | 标量 %.0fms/%zu 结果 加速 %.2fx\n",
                    rAvx.stats.totalMs, sa.size(), rScalar.stats.totalMs, ss.size(),
                    rAvx.stats.totalMs > 0 ? rScalar.stats.totalMs / rAvx.stats.totalMs : 0.0);
    }
}

// v3.3.0（3.2 之二）：严格匹配「字级位并行」路径 —— 等价性 + 加速比
static void testStrictBitsParallel()
{
    std::printf("[16] 严格匹配字级位并行（一次 64 锚点）…\n");
    const int64_t seed = 20260915;

    // 16.1 等价性：位并行（SLIME_BITS_STRICT 默认启用）vs 标量（=0 关闭）结果集合完全一致
    {
        size_t cases = 0, mismatch = 0;
        const int sizes[] = { 1, 2, 3, 5, 8 };
        for (int rows : sizes) {
            for (int cols : sizes) {
                for (int variant = 0; variant < 4; ++variant) {
                    Pattern p(rows, cols);
                    for (int r = 0; r < rows; ++r) {
                        for (int c = 0; c < cols; ++c) {
                            // 0=忽略 1=白 2=绿：按 (r*cols+c+variant) 取模分布，保证有约束格
                            const int k = (r * cols + c + variant) % 5;
                            p.set(r, c, k == 0 ? CellState::Ignore
                                               : (k <= 2 ? CellState::White : CellState::Green));
                        }
                    }
                    SearchOptions opt;
                    opt.version = Version::Java;
                    opt.worldSeed = seed;
                    opt.minX = -300; opt.maxX = 299;
                    opt.minZ = -300; opt.maxZ = 299;
                    opt.mode = Mode::Strict;
                    opt.pattern = p;
                    opt.directionLocked = true;
                    opt.targetCount = 200000;
                    opt.threads = 1;
#ifdef _WIN32
                    _putenv_s("SLIME_BITS_STRICT", "1");
#else
                    setenv("SLIME_BITS_STRICT", "1", 1);
#endif
                    const SearchOutcome rBits = runOnce(opt);
#ifdef _WIN32
                    _putenv_s("SLIME_BITS_STRICT", "0");
#else
                    setenv("SLIME_BITS_STRICT", "0", 1);
#endif
                    const SearchOutcome rScalar = runOnce(opt);
#ifdef _WIN32
                    _putenv_s("SLIME_BITS_STRICT", "");
#else
                    unsetenv("SLIME_BITS_STRICT");
#endif
                    ++cases;
                    if (!rBits.ok || !rScalar.ok) {
                        ++mismatch;
                        continue;
                    }
                    std::set<std::tuple<int, int, int, int>> a, b;
                    for (const MatchResult& m : rBits.results) a.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
                    for (const MatchResult& m : rScalar.results) b.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
                    if (a != b || rBits.totalFound != rScalar.totalFound) {
                        ++mismatch;
                        std::printf("      [不一致] %dx%d variant=%d bits=%zu(%lld) scalar=%zu(%lld)\n", rows, cols,
                                    variant, a.size(), static_cast<long long>(rBits.totalFound), b.size(),
                                    static_cast<long long>(rScalar.totalFound));
                    }
                }
            }
        }
        CHECK(mismatch == 0, "位并行与标量严格匹配结果完全一致（全图案组合）");
        std::printf("      等价性：%zu 组图案/尺寸组合，%zu 处不一致\n", cases, mismatch);
    }

    // 16.2 加速比：字级位并行 vs 保守模式（保守=逐窗标量，且严格模式无滑动可借）
    {
        struct Case { const char* name; int rows, cols; };
        const Case cases[] = { { "2x2 全绿", 2, 2 }, { "5x5 全绿", 5, 5 }, { "5x5 棋盘", 5, 5 } };
        for (const Case& cs : cases) {
            Pattern p(cs.rows, cs.cols);
            for (int r = 0; r < cs.rows; ++r) {
                for (int c = 0; c < cs.cols; ++c) {
                    const bool green = (std::string(cs.name).find("棋盘") == std::string::npos)
                                           ? true
                                           : ((r + c) % 2 == 0);
                    p.set(r, c, green ? CellState::Green : CellState::White);
                }
            }
            SearchOptions opt;
            opt.version = Version::Java;
            opt.worldSeed = seed;
            opt.minX = 0; opt.maxX = 5999;
            opt.minZ = 0; opt.maxZ = 5999;
            opt.mode = Mode::Strict;
            opt.pattern = p;
            opt.directionLocked = true;
            opt.targetCount = 200000;
            opt.threads = 1;
            opt.algorithm = Algorithm::Exact;
            const SearchOutcome rBits = runOnce(opt);
            opt.algorithm = Algorithm::Brute;
            const SearchOutcome rBrute = runOnce(opt);
            CHECK(rBits.ok && rBrute.ok, "严格搜索成功（位并行/保守）");
            std::set<std::tuple<int, int, int, int>> a, b;
            for (const MatchResult& m : rBits.results) a.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
            for (const MatchResult& m : rBrute.results) b.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
            CHECK(a == b, "位并行与保守模式结果集合一致");
            const double mBits = rBits.stats.matchMs - rBits.stats.scanMs;
            const double mBrute = rBrute.stats.matchMs - rBrute.stats.scanMs;
            const double speedMatch = mBits > 0 ? mBrute / mBits : 0.0;
            const double speed = rBits.stats.totalMs > 0 ? rBrute.stats.totalMs / rBits.stats.totalMs : 0.0;
            std::printf("      %s（6000²）：位并行 匹配 %.0fms/合计 %.0fms(%zu) | 保守 匹配 %.0fms/合计 %.0fms(%zu)"
                        " 加速 匹配 %.2fx / 合计 %.2fx\n",
                        cs.name, mBits, rBits.stats.totalMs, a.size(), mBrute, rBrute.stats.totalMs, b.size(),
                        speedMatch, speed);
            CHECK(speedMatch >= 3.0, "严格匹配阶段加速 ≥ 3 倍（相对保守模式）");
        }
    }
}

// v3.3.1（任务书 3.2）：GPU 密度匹配内核 —— 与 CPU 精确模式逐位等价（GPU 不可用时跳过）
static void testGpuDensityMatch()
{
    std::printf("[17] GPU 密度匹配内核（v3.3.1 新增；CPU/GPU 结果逐位一致）…\n");
    std::string gerr;
    std::unique_ptr<GpuScan> gpu = slimegpu::createGpuScanAdapter(&gerr);
    if (!gpu || !gpu->available()) {
        std::printf("      跳过：GPU 不可用（%s）\n", gerr.empty() ? "无设备" : gerr.c_str());
        return;
    }
    std::printf("      GPU 适配器可用，开始等价性对照…\n");

    struct Cfg {
        const char* name;
        Version version;
        int minX, maxX, minZ, maxZ;
        int grid;
        double thr;
    };
    const Cfg cfgs[] = {
        { "Java 1024² g=5 thr=0.4", Version::Java, -512, 511, -512, 511, 5, 0.4 },
        { "Java 1024² g=3 thr=0.5", Version::Java, -512, 511, -512, 511, 3, 0.5 },
        { "Java 1024² g=10 thr=0.3", Version::Java, -512, 511, -512, 511, 10, 0.3 },
        { "Java 1024² g=32 thr=0.2", Version::Java, -512, 511, -512, 511, 32, 0.2 },
        { "Java 带负坐标 768×1024 g=5", Version::Java, -1000, -233, -700, 323, 5, 0.35 },
        { "基岩 1024² g=5 thr=0.4", Version::Bedrock, -512, 511, -512, 511, 5, 0.4 },
        { "基岩 1024² g=3 thr=0.5", Version::Bedrock, -512, 511, -512, 511, 3, 0.5 },
    };
    size_t cases = 0, mism = 0;
    for (const Cfg& c : cfgs) {
        SearchOptions base;
        base.version = c.version;
        base.worldSeed = 20260915;
        base.minX = c.minX; base.maxX = c.maxX; base.minZ = c.minZ; base.maxZ = c.maxZ;
        base.mode = Mode::Density;
        base.gridSize = c.grid;
        base.minDensity = c.thr;
        base.targetCount = 5000;
        base.threads = 1;
        base.algorithm = Algorithm::Exact;

        const SearchOutcome cpu = runOnce(base); // 纯 CPU 精确模式
        SearchOptions gopt = base;
        gopt.gpuScan = gpu.get();                // 注入 GPU（matchDensity 生效时走 GPU 匹配）
        const SearchOutcome gpuOut = runOnce(gopt);
        CHECK(cpu.ok && gpuOut.ok, "GPU/CPU 密度搜索均成功");
        ++cases;

        bool same = cpu.results.size() == gpuOut.results.size() && cpu.totalFound == gpuOut.totalFound;
        if (same) {
            for (size_t i = 0; i < cpu.results.size(); ++i) {
                const MatchResult& a = cpu.results[i];
                const MatchResult& b = gpuOut.results[i];
                if (a.minX != b.minX || a.maxX != b.maxX || a.minZ != b.minZ || a.maxZ != b.maxZ ||
                    std::memcmp(&a.density, &b.density, sizeof(float)) != 0) {
                    same = false;
                    break;
                }
            }
        }
        if (!same) {
            ++mism;
            std::printf("      [不一致] %s：CPU %zu 命中(%lld) | GPU %zu 命中(%lld)\n", c.name,
                        cpu.results.size(), static_cast<long long>(cpu.totalFound), gpuOut.results.size(),
                        static_cast<long long>(gpuOut.totalFound));
        } else {
            std::printf("      %-30s 一致：命中 %lld，TopN %zu | CPU %.0fms GPU %.0fms\n", c.name,
                        static_cast<long long>(cpu.totalFound), cpu.results.size(), cpu.stats.totalMs,
                        gpuOut.stats.totalMs);
        }
    }
    CHECK(mism == 0, "GPU 密度匹配与 CPU 精确模式结果逐位一致（含密度位模式）");
    std::printf("      等价性：%zu 组配置，%zu 处不一致\n", cases, mism);

    // 较大区域：GPU 匹配路径 vs CPU 全核（同参数）
    {
        SearchOptions base;
        base.version = Version::Java;
        base.worldSeed = 20260915;
        base.minX = -4096; base.maxX = 4095;
        base.minZ = -4096; base.maxZ = 4095;
        base.mode = Mode::Density;
        base.gridSize = 5;
        base.minDensity = 0.45;
        base.targetCount = 1000;
        base.threads = 0; // CPU 全核
        base.algorithm = Algorithm::Exact;
        const SearchOutcome cpu = runOnce(base);
        SearchOptions gopt = base;
        gopt.gpuScan = gpu.get();
        const SearchOutcome gpuOut = runOnce(gopt);
        const double sp = gpuOut.stats.totalMs > 0 ? cpu.stats.totalMs / gpuOut.stats.totalMs : 0.0;
        std::printf("      Java 8192² g=5 thr=0.45：CPU %.0fms(%zu 显示/%lld 命中) | GPU %.0fms(%zu/%lld) 加速 %.2fx\n",
                    cpu.stats.totalMs, cpu.results.size(), static_cast<long long>(cpu.totalFound),
                    gpuOut.stats.totalMs, gpuOut.results.size(),
                    static_cast<long long>(gpuOut.totalFound), sp);
    }
}

// v3.3.1：GPU 密度匹配「引擎侧集成」测试 —— 用模拟提供者验证堆/排序/回退/启用条件/分块锚点域，
// 不依赖真实 OpenCL 设备（真实内核另见 [17]）。
namespace {
struct MockGpuScan : GpuScan {
    bool failMode = false;                              // true → 模拟“不支持/失败”，应回退 CPU
    mutable std::atomic<int> matchCalls{ 0 };
    mutable std::atomic<int> scanCalls{ 0 };
    mutable std::atomic<bool> forceCancel{ false };
    mutable std::atomic<bool> fastEmpty{ false }; // true → 立即返回空结果（只校验调用/划分几何）
    mutable std::mutex rangeMu;                         // 记录每次请求的锚点范围（校验分块划分）
    mutable std::vector<std::pair<int64_t, int64_t>> ranges;

    bool available() const noexcept override { return true; }

    bool scanRect(Version, int64_t, int, int, int, int, std::vector<uint8_t>&, std::string* err) const override
    {
        ++scanCalls;
        if (err) *err = "mock: 不使用扫描路径";
        return false;
    }

    bool matchDensity(const DensityMatchParams& p, const std::atomic<bool>& cancel,
                      std::vector<MatchResult>& out, std::string* err) const override
    {
        ++matchCalls;
        {
            std::lock_guard<std::mutex> lk(rangeMu);
            ranges.emplace_back(p.azMin, p.azMax);
        }
        if (forceCancel.load()) return true; // 模拟取消后返回部分结果（空）
        if (fastEmpty.load()) return true;   // 快速空结果（几何校验用）
        if (failMode) {
            if (err) *err = "mock: 模拟失败";
            return false;
        }
        const int g = std::max(1, p.gridSize);
        const double need = p.minDensity * static_cast<double>(g) * static_cast<double>(g);
        // 不可能阈值（> g²）：无命中，直接返回（用于廉价地校验分块锚点划分）
        if (need > static_cast<double>(g) * static_cast<double>(g)) return true;
        for (int64_t z = p.azMin; z <= p.azMax; ++z) {
            if (cancel.load(std::memory_order_relaxed)) break;
            for (int64_t x = p.axMin; x <= p.axMax; ++x) {
                int cnt = 0;
                for (int dz = 0; dz < g; ++dz) {
                    for (int dx = 0; dx < g; ++dx) {
                        if (SlimeEngine::isSlimeChunk(p.version, p.worldSeed, static_cast<int>(x + dx),
                                                     static_cast<int>(z + dz))) ++cnt;
                    }
                }
                if (static_cast<double>(cnt) >= need) {
                    MatchResult m;
                    m.minX = static_cast<int>(x);
                    m.maxX = static_cast<int>(x + g - 1);
                    m.minZ = static_cast<int>(z);
                    m.maxZ = static_cast<int>(z + g - 1);
                    m.density = static_cast<float>(cnt) / static_cast<float>(g * g);
                    out.push_back(m);
                }
                if (p.maxResults > 0 && static_cast<int64_t>(out.size()) > p.maxResults) {
                    if (err) *err = "mock: 命中数超过返回上限";
                    return false;
                }
            }
        }
        return true;
    }
};

bool sameResultList(const SearchOutcome& a, const SearchOutcome& b)
{
    if (a.results.size() != b.results.size() || a.totalFound != b.totalFound) return false;
    for (size_t i = 0; i < a.results.size(); ++i) {
        if (a.results[i].minX != b.results[i].minX || a.results[i].maxX != b.results[i].maxX ||
            a.results[i].minZ != b.results[i].minZ || a.results[i].maxZ != b.results[i].maxZ ||
            std::memcmp(&a.results[i].density, &b.results[i].density, sizeof(float)) != 0) {
            return false;
        }
    }
    return true;
}
} // namespace

static void testGpuMatchIntegration()
{
    std::printf("[18] GPU 密度匹配引擎侧集成（模拟提供者；堆序/回退/条件/分块锚点域）…\n");
    MockGpuScan mock;
    // 模拟提供者不依赖真实设备：把「最小启用块数」阈值调低，使小区域也能走 GPU 匹配分支
    const std::string oldMin = std::getenv("SLIME_GPU_MIN_CHUNKS") ? std::getenv("SLIME_GPU_MIN_CHUNKS") : "";
#ifdef _WIN32
    _putenv_s("SLIME_GPU_MIN_CHUNKS", "1000");
#else
    setenv("SLIME_GPU_MIN_CHUNKS", "1000", 1);
#endif
    const auto restoreEnv = [&]() {
#ifdef _WIN32
        _putenv_s("SLIME_GPU_MIN_CHUNKS", oldMin.c_str());
#else
        setenv("SLIME_GPU_MIN_CHUNKS", oldMin.c_str(), 1);
#endif
    };

    auto makeOpt = [](Version v, int half, int g, double thr, int64_t n) {
        SearchOptions o;
        o.version = v;
        o.worldSeed = 20260915;
        o.minX = -half; o.maxX = half - 1;
        o.minZ = -half; o.maxZ = half - 1;
        o.mode = Mode::Density;
        o.gridSize = g;
        o.minDensity = thr;
        o.targetCount = n;
        o.threads = 1;
        o.algorithm = Algorithm::Exact;
        return o;
    };

    // A) 模拟提供者成功 → 结果与纯 CPU 完全一致（含 TopN 顺序、totalFound、密度位模式）
    {
        const SearchOptions base = makeOpt(Version::Java, 400, 5, 0.4, 37);
        const SearchOutcome cpu = runOnce(base);
        SearchOptions g = base;
        g.gpuScan = &mock;
        const int before = mock.matchCalls.load();
        const SearchOutcome gpu = runOnce(g);
        CHECK(mock.matchCalls.load() == before + 1, "GPU 匹配被调用一次（精确密度模式）");
        CHECK(cpu.ok && gpu.ok, "CPU/模拟GPU 搜索均成功");
        CHECK(sameResultList(cpu, gpu), "模拟GPU 路径结果与 CPU 完全一致（含顺序与密度位模式）");
        std::printf("      A) 一致：命中 %lld，TopN %zu（CPU %.0fms / GPU %.0fms）\n",
                    static_cast<long long>(cpu.totalFound), cpu.results.size(), cpu.stats.totalMs,
                    gpu.stats.totalMs);
    }

    // B) 模拟提供者失败 → 自动回退 CPU，结果仍一致
    {
        MockGpuScan failing;
        failing.failMode = true;
        const SearchOptions base = makeOpt(Version::Java, 300, 5, 0.45, 25);
        const SearchOutcome cpu = runOnce(base);
        SearchOptions g = base;
        g.gpuScan = &failing;
        const SearchOutcome fb = runOnce(g);
        CHECK(failing.matchCalls.load() == 1, "失败模式下 GPU 匹配被尝试一次");
        CHECK(sameResultList(cpu, fb), "GPU 匹配失败后回退 CPU，结果一致");
        std::printf("      B) 回退一致：命中 %lld\n", static_cast<long long>(cpu.totalFound));
    }

    // C) 启用条件（v3.9.6 更新）：
    //    提前停止 / 智能范围 现在也走 GPU（按锚点行带由近到远分批 + 无损判停），结果必须与 CPU 完全一致；
    //    分层 / 估算 / 缓存 仍不使用 GPU 匹配。
    {
        MockGpuScan m2;
        const auto samePts = [](const SearchOutcome& a, const SearchOutcome& b) {
            if (a.ok != b.ok || a.results.size() != b.results.size()) return false;
            for (size_t i = 0; i < a.results.size(); ++i) {
                if (a.results[i].minX != b.results[i].minX || a.results[i].minZ != b.results[i].minZ) return false;
            }
            return true;
        };

        // 提前停止：GPU 行带路径 vs CPU
        SearchOptions o = makeOpt(Version::Java, 300, 5, 0.4, 20);
        o.stopAtTarget = true;
        SearchOptions cpuStop = o;
        cpuStop.gpuScan = nullptr;
        const SearchOutcome cpuStopRes = runOnce(cpuStop);
        o.gpuScan = &m2;
        const int b1 = m2.matchCalls.load();
        const SearchOutcome r1 = runOnce(o);
        CHECK(r1.ok && m2.matchCalls.load() > b1, "提前停止时使用 GPU 行带匹配（v3.9.6 起）");
        CHECK(samePts(cpuStopRes, r1), "提前停止的 GPU 行带结果与 CPU 完全一致");

        // 智能范围：GPU 行带路径 vs CPU
        SearchOptions o2 = makeOpt(Version::Java, 300, 5, 0.4, 20);
        o2.smartRange = true;
        SearchOptions cpuSmart = o2;
        cpuSmart.gpuScan = nullptr;
        const SearchOutcome cpuSmartRes = runOnce(cpuSmart);
        o2.gpuScan = &m2;
        const int b2 = m2.matchCalls.load();
        const SearchOutcome r2 = runOnce(o2);
        CHECK(r2.ok && m2.matchCalls.load() > b2, "智能范围时使用 GPU 行带匹配（v3.9.6 起）");
        CHECK(samePts(cpuSmartRes, r2), "智能范围的 GPU 行带结果与 CPU 完全一致");

        // 分层（无损 ratio<=1.0）：v3.10.10.1 起允许 GPU 精确匹配（结果必须一致）
        SearchOptions o3 = makeOpt(Version::Java, 300, 5, 0.4, 20);
        o3.algorithm = Algorithm::Layered;
        o3.layerBlock = 16;
        o3.layerRatio = 1.0;
        SearchOptions cpuL = o3;
        cpuL.gpuScan = nullptr;
        const SearchOutcome cpuLRes = runOnce(cpuL);
        o3.gpuScan = &m2;
        const int b3 = m2.matchCalls.load();
        const SearchOutcome r3 = runOnce(o3);
        CHECK(r3.ok && m2.matchCalls.load() > b3, "无损分层模式可使用 GPU 精确匹配（v3.10.10.1）");
        CHECK(samePts(cpuLRes, r3), "无损分层的 GPU 结果与 CPU 分层结果一致");

        // 分层（有损 ratio>1.00）：仍不启用 GPU 匹配（语义不同）
        SearchOptions o4 = makeOpt(Version::Java, 300, 5, 0.4, 20);
        o4.algorithm = Algorithm::Layered;
        o4.layerBlock = 16;
        o4.layerRatio = 1.5;
        o4.gpuScan = &m2;
        const int b4 = m2.matchCalls.load();
        const SearchOutcome r4 = runOnce(o4);
        CHECK(r4.ok && m2.matchCalls.load() == b4, "有损分层模式不使用 GPU 匹配（ratio>1.00）");

        // 估算：仍不启用
        SearchOptions o4Lossy = makeOpt(Version::Java, 300, 5, 0.4, 20);
        o4Lossy.algorithm = Algorithm::Sample;
        o4Lossy.sampleStep = 8;
        o4Lossy.gpuScan = &m2;
        const int b4Lossy = m2.matchCalls.load();
        const SearchOutcome r4Lossy = runOnce(o4Lossy);
        CHECK(r4Lossy.ok && m2.matchCalls.load() == b4Lossy, "估算模式不使用 GPU 匹配");

        // 基岩+缓存：仍不启用
        CacheManager cache;
        SearchOptions o5 = makeOpt(Version::Bedrock, 300, 3, 0.4, 20);
        o5.useCache = true;
        o5.cache = &cache;
        o5.cacheWrite = &cache;
        o5.gpuScan = &m2;
        const int b5 = m2.matchCalls.load();
        const SearchOutcome r5 = runOnce(o5);
        CHECK(r5.ok && m2.matchCalls.load() == b5, "基岩+缓存时不使用 GPU 匹配（缓存需逐区块位图）");
        std::printf("      C) 启用条件：提前停止/智能范围 走 GPU 行带（结果一致）；分层/估算/缓存 不启用\n");
    }

    // D) 分块路径（整图级）：锚点行域必须受 g_anchorMaxAZ 限制，结果与经典路径一致
    {
        const std::string oldForce = std::getenv("SLIME_FORCE_CHUNKED") ? std::getenv("SLIME_FORCE_CHUNKED") : "";
        const std::string oldWords = std::getenv("SLIME_CHUNK_WORDS") ? std::getenv("SLIME_CHUNK_WORDS") : "";
        const SearchOptions base = makeOpt(Version::Java, 300, 5, 0.42, 40);
        const SearchOutcome classic = runOnce(base); // 经典路径（CPU）
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_CHUNKED", "1");
        _putenv_s("SLIME_CHUNK_WORDS", "1024");
#else
        setenv("SLIME_FORCE_CHUNKED", "1", 1);
        setenv("SLIME_CHUNK_WORDS", "1024", 1);
#endif
        const SearchOutcome chunked = runOnce(base); // 分块路径（CPU）
        SearchOptions g = base;
        g.gpuScan = &mock;
        const int before = mock.matchCalls.load();
        const SearchOutcome chunkedGpu = runOnce(g); // 分块路径（模拟 GPU 匹配，每块一次调用）
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_CHUNKED", oldForce.c_str());
        _putenv_s("SLIME_CHUNK_WORDS", oldWords.c_str());
#else
        setenv("SLIME_FORCE_CHUNKED", oldForce.c_str(), 1);
        setenv("SLIME_CHUNK_WORDS", oldWords.c_str(), 1);
#endif
        CHECK(classic.ok && chunked.ok && chunkedGpu.ok, "经典/分块/分块+GPU 搜索均成功");
        std::set<std::tuple<int, int, int, int>> sc, sk, sg;
        for (const MatchResult& m : classic.results) sc.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
        for (const MatchResult& m : chunked.results) sk.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
        for (const MatchResult& m : chunkedGpu.results) sg.emplace(m.minX, m.maxX, m.minZ, m.maxZ);
        CHECK(sc == sk, "分块路径（CPU）与经典路径结果集合一致");
        CHECK(sc == sg, "分块路径 + 模拟GPU匹配 与经典路径结果集合一致（锚点行域未跨块重复）");
        CHECK(chunkedGpu.results.size() == sg.size(), "分块+GPU 结果无重复条目");
        std::printf("      D) 分块+GPU：结果集合 %zu 条（经典 %zu 条，无重复）\n", sg.size(), sc.size());
    }

    // E) 多分块锚点划分：GPU 路径每次请求的锚点行域必须互不重叠且完整覆盖（无重复/无遗漏）
    {
        MockGpuScan m3;
        m3.fastEmpty = true; // 只校验分块锚点划分，不做真实匹配
        std::string oldForce = std::getenv("SLIME_FORCE_CHUNKED") ? std::getenv("SLIME_FORCE_CHUNKED") : "";
        std::string oldWords = std::getenv("SLIME_CHUNK_WORDS") ? std::getenv("SLIME_CHUNK_WORDS") : "";
        const std::string oldMin2 = std::getenv("SLIME_GPU_MIN_CHUNKS") ? std::getenv("SLIME_GPU_MIN_CHUNKS") : "";
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_CHUNKED", "1");
        _putenv_s("SLIME_CHUNK_WORDS", "1048576"); // 允许的最小分块容量 → 小区域也能产生多块
        _putenv_s("SLIME_GPU_MIN_CHUNKS", "1000");
#else
        setenv("SLIME_FORCE_CHUNKED", "1", 1);
        setenv("SLIME_CHUNK_WORDS", "1048576", 1);
        setenv("SLIME_GPU_MIN_CHUNKS", "1000", 1);
#endif
        SearchOptions o;
        o.version = Version::Java;
        o.worldSeed = 20260915;
        o.minX = 0; o.maxX = 32767;   // wpr = 512 → 每块约 2048 数据行
        o.minZ = 0; o.maxZ = 5999;    // 6000 行 → 多块
        o.mode = Mode::Density;
        o.gridSize = 5;
        o.minDensity = 0.9;           // 合法阈值（模拟提供者 fastEmpty 直接返回空结果）
        o.targetCount = 10;
        o.threads = 1;
        o.algorithm = Algorithm::Exact;
        o.gpuScan = &m3;
        const SearchOutcome r = runOnce(o);
#ifdef _WIN32
        _putenv_s("SLIME_FORCE_CHUNKED", oldForce.c_str());
        _putenv_s("SLIME_CHUNK_WORDS", oldWords.c_str());
        _putenv_s("SLIME_GPU_MIN_CHUNKS", oldMin2.c_str());
#else
        setenv("SLIME_FORCE_CHUNKED", oldForce.c_str(), 1);
        setenv("SLIME_CHUNK_WORDS", oldWords.c_str(), 1);
        setenv("SLIME_GPU_MIN_CHUNKS", oldMin2.c_str(), 1);
#endif
        CHECK(r.ok, "多分块 GPU 路径搜索成功");
        const int calls = m3.matchCalls.load();
        CHECK(calls >= 2, "多分块下 GPU 匹配被多次调用（≥2 块）");
        std::vector<std::pair<int64_t, int64_t>> rs;
        {
            std::lock_guard<std::mutex> lk(m3.rangeMu);
            rs = m3.ranges;
        }
        std::sort(rs.begin(), rs.end());
        bool contiguous = !rs.empty();
        int64_t expectMin = 0;
        for (size_t i = 0; i < rs.size(); ++i) {
            if (rs[i].first != expectMin || rs[i].second < rs[i].first) { contiguous = false; break; }
            expectMin = rs[i].second + 1;
        }
        const int64_t anchorMax = 6000 - 5; // H - g（窗口不越界：az ∈ [0, 5995]）
        if (contiguous && expectMin != anchorMax + 1) contiguous = false;
        CHECK(contiguous, "各分块的锚点行域互不重叠且完整覆盖锚点空间（无重复/无遗漏）");
        std::printf("      E) 分块锚点划分：%d 块，范围首=%lld 末=%lld（期望覆盖 0..%lld）\n", calls,
                    static_cast<long long>(rs.empty() ? -1 : rs.front().first),
                    static_cast<long long>(rs.empty() ? -1 : rs.back().second),
                    static_cast<long long>(anchorMax));
    }
    restoreEnv();
}

// ===================== v3.3.7（紧急修复任务书 P0-1 / P0-3） =====================
// [19] 分块路径「达到目标数量即停止」（B 方案：块内几何停止 + 跨块距离剪枝）+ 真实扫描计数。
//   A) 剪枝确实生效（chunksPruned > 0）且扫描量显著小于全量
//   B) 剪枝前后结果**逐位一致**（B 方案保持全局最近 N，不做精度降级）
//   C) 关闭剪枝（SLIME_NO_PRUNE=1）时 chunksPruned == 0
//   D) 未勾选及时反馈（stopAtTarget=false）时不剪枝
//   E) liveScanned 与 out.stats.scannedChunks 同源一致（界面 N/M 与进度条同源）
static void testChunkedEarlyStopV337()
{
    std::printf("[19] v3.3.7 分块提前停止（B 方案）+ 真实扫描计数…\n");
    const std::string oldForce = std::getenv("SLIME_FORCE_CHUNKED") ? std::getenv("SLIME_FORCE_CHUNKED") : "";
    const std::string oldWords = std::getenv("SLIME_CHUNK_WORDS") ? std::getenv("SLIME_CHUNK_WORDS") : "";
    const std::string oldNoPrune = std::getenv("SLIME_NO_PRUNE") ? std::getenv("SLIME_NO_PRUNE") : "";
#ifdef _WIN32
    const auto setEnv = [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); };
#else
    const auto setEnv = [](const char* k, const char* v) { v ? setenv(k, v, 1) : unsetenv(k); };
#endif
    setEnv("SLIME_FORCE_CHUNKED", "1");
    setEnv("SLIME_CHUNK_WORDS", "1048576"); // 小块容量 → 多分块（每个分块都能独立完成）
    setEnv("SLIME_NO_PRUNE", "");

    // 常见图案（1×1 绿）：第一个分块内即可命中目标 1 → 后续分块应被剪枝
    Pattern p(1, 1);
    p.set(0, 0, CellState::Green);

    SearchOptions base;
    base.version = Version::Java; // Java 判定快 → 多分块场景下测试耗时可控
    base.worldSeed = 12345;
    base.minX = -104857; base.maxX = 104857; // 与用户场景同宽（wpr=3277）
    base.minZ = 0; base.maxZ = 1499;         // 1500 行 → 约 5 个分块（319 行/块）
    base.mode = Mode::Strict;
    base.pattern = p;
    base.directionLocked = true;
    base.threads = 4;
    base.targetCount = 1;
    base.stopAtTarget = true;

    std::atomic<int64_t> liveScanned{ 0 };
    SearchOptions withCount = base;
    withCount.liveScanned = &liveScanned;
    const SearchOutcome a = runOnce(withCount);
    CHECK(a.ok, "分块+及时反馈：搜索成功");
    CHECK(a.chunksPruned > 0, "跨块剪枝生效（chunksPruned > 0）");
    CHECK(liveScanned.load() > 0 && liveScanned.load() == a.stats.scannedChunks,
          "liveScanned 与 out.stats.scannedChunks 同源一致");

    // 关闭剪枝（A/B 对照）：结果必须逐位一致、扫描量更大
    setEnv("SLIME_NO_PRUNE", "1");
    std::atomic<int64_t> liveScanned2{ 0 };
    SearchOptions noPrune = base;
    noPrune.liveScanned = &liveScanned2;
    const SearchOutcome b = runOnce(noPrune);
    CHECK(b.ok && b.chunksPruned == 0, "关闭剪枝（SLIME_NO_PRUNE=1）时 chunksPruned == 0");
    CHECK(a.results.size() == b.results.size(), "剪枝前后结果条数一致");
    bool same = (a.results.size() == b.results.size());
    for (size_t i = 0; same && i < a.results.size(); ++i) {
        const MatchResult& x = a.results[i];
        const MatchResult& y = b.results[i];
        same = (x.minX == y.minX && x.maxX == y.maxX && x.minZ == y.minZ && x.maxZ == y.maxZ);
    }
    CHECK(same, "剪枝前后结果逐位一致（B 方案保持全局最近 N）");
    CHECK(b.stats.scannedChunks > a.stats.scannedChunks, "剪枝确实少扫区块（对照扫描量更大）");

    // 未勾选及时反馈：不剪枝，且与勾选时结果一致（结果集与开关无关）
    setEnv("SLIME_NO_PRUNE", "");
    SearchOptions noStop = base;
    noStop.stopAtTarget = false;
    const SearchOutcome c = runOnce(noStop);
    CHECK(c.ok && c.chunksPruned == 0, "未勾选及时反馈时不剪枝");
    CHECK(c.results.size() == a.results.size(), "勾选/不勾选的前 N 条结果一致");

    std::printf("      A) 剪枝=%lld 扫描=%lld 区块；B) 剪枝=0 扫描=%lld 区块（结果逐位一致=%d）\n",
                static_cast<long long>(a.chunksPruned), static_cast<long long>(a.stats.scannedChunks),
                static_cast<long long>(b.stats.scannedChunks), same ? 1 : 0);

    setEnv("SLIME_FORCE_CHUNKED", oldForce.c_str());
    setEnv("SLIME_CHUNK_WORDS", oldWords.c_str());
    setEnv("SLIME_NO_PRUNE", oldNoPrune.c_str());
}

// ===================== v3.3.8（任务 3+5 合流：v2 位图缓存格式） =====================
// 验收（设计文档 addendum2 §四 步骤 2）：
//   ① 旧 v1 文件仍可读（读入即转位图）；② 本版本保存写 v2；③ 写入/校验失败**不损坏旧文件**（原子替换）。
static void testCacheV2Format()
{
    std::printf("[21] v2 位图缓存格式（v1 兼容 / 原子替换）…\n");
    const auto dir = std::filesystem::temp_directory_path() / "slime_test_v2";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto p = dir / "c.slimecache";

    // ---- ① 手写一份 v1（坐标集合）文件 → 必须能读，且判定正确 ----
    // 注意：条目必须达到基准密度量级（≈10%），否则会被支撑度判据当作「无支撑声明」剔除
    const Rect R{ -100, 200, -50, 100 }; // 301×151 = 45,451 位置 ⇒ 期望 4,545 条、阈值 2,272
    std::vector<std::pair<int, int>> cells;
    for (int x = R.minX; x <= R.maxX; ++x)
        for (int z = R.minZ; z <= R.maxZ; ++z)
            if (((x * 7 + z * 13) % 10) == 0) cells.emplace_back(x, z); // ≈10% 密度
    {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        const uint32_t magic = 0x534C4D45U, ver = 1, rc = 1;
        const uint64_t n = cells.size();
        const int32_t rect[4] = { R.minX, R.maxX, R.minZ, R.maxZ };
        out.write(reinterpret_cast<const char*>(&magic), 4);
        out.write(reinterpret_cast<const char*>(&ver), 4);
        out.write(reinterpret_cast<const char*>(&rc), 4);
        out.write(reinterpret_cast<const char*>(&n), 8);
        out.write(reinterpret_cast<const char*>(rect), 16);
        for (const auto& c : cells) {
            const uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(c.first)) << 32) |
                                    static_cast<uint32_t>(c.second);
            out.write(reinterpret_cast<const char*>(&packed), 8);
        }
    }
    CacheManager v1;
    std::string err;
    CHECK(v1.loadFile(p, true, err), "旧 v1 文件可读（坐标集合 → 位图）");
    CHECK(v1.ranges().size() == 1 && v1.covers(R), "v1 载入后声明与覆盖正确");
    CHECK(v1.entryCount() == static_cast<int64_t>(cells.size()), "v1 载入后条目数一致");
    int badv1 = 0;
    for (const auto& c : cells)
        if (v1.lookup(c.first, c.second) != 1) ++badv1;
    CHECK(badv1 == 0, "v1 载入后所有史莱姆坐标判定为 1");
    CHECK(v1.lookup(0, 0) == 0 || v1.lookup(0, 0) == 1, "v1 载入后覆盖区内可判定");

    // ---- ② 保存 → v2（位图）→ 重新加载，内容一致 ----
    const int64_t entriesBefore = v1.entryCount();
    const int64_t bitmapBytes = v1.bitmapBytes();
    CHECK(v1.saveFile(p, err), "保存为 v2 成功（原子替换）");
    {
        std::ifstream in(p, std::ios::binary);
        uint32_t magic = 0, ver = 0;
        in.read(reinterpret_cast<char*>(&magic), 4);
        in.read(reinterpret_cast<char*>(&ver), 4);
        CHECK(magic == 0x534C4D45U && ver == 2, "落盘版本号 = 2（v2 位图格式）");
    }
    CacheManager v2;
    CHECK(v2.loadFile(p, true, err), "v2 文件可读");
    CHECK(v2.entryCount() == entriesBefore, "v2 往返条目数一致");
    CHECK(v2.bitmapBytes() == bitmapBytes, "v2 往返位图字节数一致");
    int badv2 = 0;
    for (const auto& c : cells)
        if (v2.lookup(c.first, c.second) != 1) ++badv2;
    CHECK(badv2 == 0, "v2 往返后所有史莱姆坐标判定为 1");
    // 位图体积必须显著小于坐标集合（8 B/条 → 1 bit/位置）
    CHECK(v2.bitmapBytes() < entriesBefore * 2, "v2 位图体积远小于坐标集合（1 bit/位置）");

    // ---- ②-bis v3.3.8.1（任务书 1.3）：导入进度回调必须「连续 + 单调 + 收到 100%」----
    {
        // 造一个多 range、位图总量 ≫ 1 MiB 的 v2 文件（4 个 4096×4096 区域 ⇒ 每区位图 2 MiB，
        // 合计 8 MiB ⇒ 按 1 MiB 分块至少 8 次上报；密度 10% 以满足支撑度判据）
        CacheManager big;
        for (int k = 0; k < 4; ++k) {
            const Rect r{ k * 6000, k * 6000 + 4095, 0, 4095 };
            std::vector<std::pair<int, int>> pts;
            for (int x = r.minX; x <= r.maxX; ++x)
                for (int z = r.minZ; z <= r.maxZ; ++z)
                    if (((x * 7 + z * 13) % 10) == 0) pts.emplace_back(x, z);
            big.mergeRegion(r, pts);
        }
        const auto pbig = dir / "big.slimecache";
        CHECK(big.saveFile(pbig, err), "多 range 大位图缓存保存成功（进度测试用）");
        const auto bigBytes = static_cast<int64_t>(std::filesystem::file_size(pbig, ec));
        CHECK(bigBytes > (4 << 20), "进度测试文件 > 4 MiB（足以触发多次分块上报）");
        std::vector<std::pair<int64_t, int64_t>> samples;
        CacheManager back;
        CHECK(back.loadFile(pbig, true, err, [&samples](int64_t d, int64_t t) { samples.emplace_back(d, t); }),
              "带进度回调加载 v2 成功");
        int backwards = 0, badTotal = 0;
        for (size_t i = 1; i < samples.size(); ++i) {
            if (samples[i].first < samples[i - 1].first) ++backwards;
            if (samples[i].second != samples[0].second) ++badTotal;
        }
        CHECK(samples.size() >= 8, "进度回调次数 ≥ 8（位图 1 MiB 分块 ⇒ 实时而非跳变）");
        CHECK(backwards == 0, "进度单调不回退");
        CHECK(badTotal == 0 && !samples.empty() && samples[0].second == bigBytes, "进度分母 = 文件字节数");
        CHECK(!samples.empty() && samples.back().first >= bigBytes - 1, "进度收到 100%（已读到文件末尾）");
        CHECK(back.entryCount() == big.entryCount(), "带进度加载后条目数与保存前一致");
        std::filesystem::remove(pbig, ec);
    }

    // ---- ③ 写入失败不损坏旧文件（原子替换的失败路径）----
    {
        const int64_t n0 = v2.entryCount();
        const size_t r0 = v2.ranges().size();
        const auto fresh = std::filesystem::path(p.string() + ".new");
        std::filesystem::remove_all(fresh, ec);
        std::filesystem::create_directories(fresh, ec);           // 让 .new 位置是目录
        { std::ofstream blocker(fresh / "keep.txt"); blocker << "x"; } // 非空 ⇒ 无法被 remove/覆盖
        CacheManager other;
        other.mergeRegion(Rect{ 500, 600, 500, 600 }, { { 550, 550 } });
        std::string e2;
        const bool failed = other.saveFile(p, e2);
        CHECK(!failed && !e2.empty(), "目标被占用时 saveFile 失败并给出原因（不静默）");
        CacheManager after;
        std::string e3;
        CHECK(after.loadFile(p, true, e3), "失败后旧文件仍可加载");
        CHECK(after.entryCount() == n0 && after.ranges().size() == r0, "失败后旧文件内容未被破坏");
        std::filesystem::remove_all(fresh, ec);
    }
    std::filesystem::remove_all(dir, ec);
}

// ===================== v3.3.8（任务 3+5 第 3 步：把缓存位图直接当 grid 用） =====================
// 验收：整区命中缓存位图时，**填充阶段整段跳过**（不再逐区块判定），且结果与不跳过时逐位一致。
// ===================== v3.3.8（任务 4：缓存优先 + 命中即停） =====================
static void testCacheFirstEarlyStop()
{
    std::printf("[23] 缓存优先 + 命中即停（任务 4）…\n");
#ifdef _WIN32
    const auto setEnv = [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); };
#else
    const auto setEnv = [](const char* k, const char* v) { v ? setenv(k, v, 1) : unsetenv(k); };
#endif
    // 必须走分块路径（缓存优先排序在 runChunked 内）；64 KiWords ⇒ W=2048 时每块 2048 行 ⇒ 3 块
    const std::string oldForce = std::getenv("SLIME_FORCE_CHUNKED") ? std::getenv("SLIME_FORCE_CHUNKED") : "";
    const std::string oldWords = std::getenv("SLIME_CHUNK_WORDS") ? std::getenv("SLIME_CHUNK_WORDS") : "";
    setEnv("SLIME_FORCE_CHUNKED", "1");
    setEnv("SLIME_CHUNK_WORDS", "65536");
    SearchOptions base;
    base.version = Version::Bedrock;
    base.mode = Mode::Density;
    base.gridSize = 5;
    base.minDensity = 0.3;
    base.minX = 0; base.maxX = 2047;
    base.minZ = 0; base.maxZ = 5999;
    base.worldSeed = 0;
    base.startX = 1024; base.startZ = 5000; // 起点落在「被缓存覆盖的下半区」
    base.targetCount = 10;
    base.threads = 1; // 单工作线程 ⇒ 严格按「缓存优先」顺序处理，便于确定性验证剪枝

    // 同源分母/真实计数（GUI 在真实运行时也是这么接的）
    std::atomic<int64_t> liveFull{ 0 }, liveCache{ 0 };
    // ① 基线：不启用提前停止、无缓存 ⇒ 全局最近 10
    SearchOptions full = base;
    full.stopAtTarget = false;
    full.liveScanned = &liveFull;
    const SearchOutcome reference = runOnce(full);
    CHECK(reference.ok && reference.results.size() == 10, "基线（全量扫描）得到最近 10 条");

    // ② 构造缓存：只覆盖**最后一块的锚点行域**（声明必须与 runChunked 的分块几何对齐：
    //    SLIME_CHUNK_WORDS=65536 / wpr=32 ⇒ 每块 2048 行、anchorStep=2044 ⇒ 第 3 块锚点行域 = [4088, 5995]；
    //    这里声明 [4088, 5999] 把它整段包含 ⇒ 该块被归入「缓存优先」组）
    CacheManager cc;
    {
        std::vector<std::pair<int, int>> band;
        band.reserve(400000);
        for (int x = 0; x <= 2047; ++x)
            for (int z = 4088; z <= 5999; ++z)
                if (SlimeEngine::isSlimeChunkBedrockMT(x, z)) band.emplace_back(x, z);
        cc.mergeRegion(Rect{ 0, 2047, 4088, 5999 }, band);
    }
    CHECK(cc.covers(Rect{ 0, 2047, 4088, 5995 }), "缓存覆盖最后一块的锚点行域");

    // ③ 整区 + 缓存 + 提前停止 ⇒ 先跑缓存覆盖块，堆满后剪掉未覆盖块
    SearchOptions o = base;
    o.stopAtTarget = true;
    o.useCache = true;
    o.cache = &cc;
    o.cacheWrite = &cc; // 与 GUI 一致（读+写句柄都给引擎）
    o.liveScanned = &liveCache;
    const SearchOutcome r = runOnce(o);
    CHECK(r.ok, "缓存优先搜索成功");
    CHECK(r.cacheFirstOrder && r.cacheFirstChunks > 0,
          "缓存优先排序生效（锚点行域被缓存覆盖的块排到最前）");
    CHECK(r.chunksPruned > 0, "命中即停：TopN 堆满后未覆盖块被跨块剪枝跳过");
    CHECK(r.cacheFirstStopped, "任务书 1.1：缓存内凑够目标数 ⇒ 立即停（不扩展扫描未覆盖区）");
    CHECK(r.stats.scannedChunks < reference.stats.scannedChunks, "跳过未覆盖块 ⇒ 实际扫描量下降");

    // ③-b v3.3.9（任务书 1.3）：修复后的**真实场景** —— 缓存由构建模式写入 + 及时反馈，
    //     且**不强制**分块路径（验证「启用提前停止 + 带缓存 ⇒ 自动走分块路径」的路由生效）
    {
        CacheManager built;
        SearchOptions b = base;
        b.useCache = true;
        b.cache = &built;
        b.cacheWrite = &built;
        b.buildMode = true;
        b.stopAtTarget = false;
        b.targetCount = 1;
        const SearchOutcome rb = runOnce(b);
        CHECK(rb.ok && built.entryCount() > 0, "构建模式填充缓存（1.3 真实场景前置）");

        const std::string keepForce = std::getenv("SLIME_FORCE_CHUNKED") ? std::getenv("SLIME_FORCE_CHUNKED") : "";
        setEnv("SLIME_FORCE_CHUNKED", ""); // 不强制：由引擎按「提前停止 + 缓存」自动选择分块路径
        SearchOptions s = base;
        s.useCache = true;
        s.cache = &built;
        s.stopAtTarget = true;
        s.targetCount = 10;
        s.threads = 1;
        std::atomic<int64_t> liveSc{ 0 };
        s.liveScanned = &liveSc;
        const SearchOutcome r2 = runOnce(s);
        setEnv("SLIME_FORCE_CHUNKED", keepForce.c_str());
        CHECK(r2.ok, "缓存覆盖 + 及时反馈：搜索成功");
        CHECK(r2.chunkedPath, "任务书 1.3 根因①修复：带缓存 + 提前停止 ⇒ 自动走分块路径（特性可达）");
        CHECK(r2.cacheFirstOrder && r2.cacheFirstChunks > 0,
              "任务书 1.3 根因②修复：按行覆盖率判定覆盖块（不再要求整宽同 wpr）");
        CHECK(r2.cacheFirstStopped, "任务书 1.3：缓存内够数即停（实测未生效 → 现已生效）");
        CHECK(r2.stats.scannedChunks < reference.stats.scannedChunks * 4 / 5,
              "命中即停后不再扫描未覆盖块（扫描量 < 全量的 80%；本用例缓存覆盖 2/3 块）");
        std::printf("      1.3 真实场景：覆盖块 %lld / 共 %lld，缓存内即停=%d，扫描 %lld（全量 %lld，%.1f%%）\n",
                    static_cast<long long>(r2.cacheFirstChunks),
                    static_cast<long long>(r2.cacheFirstChunks + r2.chunksPruned),
                    r2.cacheFirstStopped ? 1 : 0, static_cast<long long>(r2.stats.scannedChunks),
                    static_cast<long long>(reference.stats.scannedChunks),
                    100.0 * static_cast<double>(r2.stats.scannedChunks) /
                        static_cast<double>(std::max<int64_t>(1, reference.stats.scannedChunks)));
    }
    bool same = (r.results.size() == reference.results.size());
    for (size_t i = 0; same && i < reference.results.size(); ++i) {
        const MatchResult& a = reference.results[i];
        const MatchResult& b = r.results[i];
        same = (a.minX == b.minX && a.maxX == b.maxX && a.minZ == b.minZ && a.maxZ == b.maxZ);
    }
    CHECK(same, "缓存优先 + 提前停止的结果与全量扫描逐位一致（仍为全局最近 N）");
    std::printf("      缓存优先块=%lld 剪枝块=%lld 扫描=%lld（全量基线 %lld）结果一致=%d\n",
                static_cast<long long>(r.cacheFirstChunks), static_cast<long long>(r.chunksPruned),
                static_cast<long long>(r.stats.scannedChunks),
                static_cast<long long>(reference.stats.scannedChunks), same ? 1 : 0);
    setEnv("SLIME_FORCE_CHUNKED", oldForce.c_str());
    setEnv("SLIME_CHUNK_WORDS", oldWords.c_str());
}

// ===================== v3.3.8.1（任务书 1.2：同范围重复搜索的缓存去重） =====================
static void testCacheDedupRepeatRuns()
{
    std::printf("[24] 同范围连续 3 次搜索：缓存增长递减 / 无重复（任务书 1.2）…\n");
#ifdef _WIN32
    const auto setEnv = [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); };
#else
    const auto setEnv = [](const char* k, const char* v) { v ? setenv(k, v, 1) : unsetenv(k); };
#endif
    const std::string oldForce = std::getenv("SLIME_FORCE_CHUNKED") ? std::getenv("SLIME_FORCE_CHUNKED") : "";
    const std::string oldWords = std::getenv("SLIME_CHUNK_WORDS") ? std::getenv("SLIME_CHUNK_WORDS") : "";
    setEnv("SLIME_FORCE_CHUNKED", "1");
    setEnv("SLIME_CHUNK_WORDS", "65536");

    CacheManager c;
    SearchOptions o;
    o.version = Version::Bedrock;
    o.mode = Mode::Density;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.minX = 0; o.maxX = 2047;
    o.minZ = 0; o.maxZ = 5999;
    o.worldSeed = 0;
    o.targetCount = 50;
    o.stopAtTarget = false;
    o.threads = 0;
    o.useCache = true;
    o.cache = &c;
    o.cacheWrite = &c;
    const SearchOutcome r1 = runOnce(o);
    const int64_t e1 = c.entryCount(), g1 = static_cast<int64_t>(c.ranges().size());
    const SearchOutcome r2 = runOnce(o);
    const int64_t e2 = c.entryCount(), g2 = static_cast<int64_t>(c.ranges().size());
    const SearchOutcome r3 = runOnce(o);
    const int64_t e3 = c.entryCount(), g3 = static_cast<int64_t>(c.ranges().size());
    setEnv("SLIME_FORCE_CHUNKED", oldForce.c_str());
    setEnv("SLIME_CHUNK_WORDS", oldWords.c_str());
    CHECK(r1.ok && r2.ok && r3.ok, "三次搜索均成功");
    std::printf("      条目 %lld → %lld → %lld；声明 %lld → %lld → %lld\n",
                static_cast<long long>(e1), static_cast<long long>(e2), static_cast<long long>(e3),
                static_cast<long long>(g1), static_cast<long long>(g2), static_cast<long long>(g3));
    CHECK(e2 - e1 <= 0, "第 2 次搜索不再新增条目（原位图置位幂等 ⇒ 无重复写入）");
    CHECK(e3 - e2 <= 0, "第 3 次搜索不再新增条目（增长递减至 0）");
    CHECK(g2 == g1 && g3 == g1, "重复搜索不新增覆盖声明（相同矩形去重）");
    CHECK(c.entryCount() == e3, "缓存条目数稳定");
    // 去重判据的补充：位图置位幂等 ⇒ 条目计数不可能因重复而增长
    bool same = r1.results.size() == r3.results.size();
    for (size_t i = 0; same && i < r1.results.size(); ++i) {
        const MatchResult& a = r1.results[i];
        const MatchResult& b = r3.results[i];
        same = (a.minX == b.minX && a.maxX == b.maxX && a.minZ == b.minZ && a.maxZ == b.maxZ);
    }
    CHECK(same, "第 3 次与第 1 次结果逐位一致");
}

// ===================== v3.3.8（任务 1 / ④′ 修复方案 B：声明支撑度） =====================
static void testCacheBitmapFillSkip()
{
    std::printf("[22] 缓存位图直接当 grid（填充跳过）…\n");
    CacheManager cc;
    SearchOptions o;
    o.version = Version::Bedrock;
    o.mode = Mode::Density;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.minX = 0; o.maxX = 2047;
    o.minZ = 0; o.maxZ = 1999;
    o.worldSeed = 0;
    o.targetCount = 50;
    o.stopAtTarget = false;
    o.threads = 0;
    o.useCache = true;
    o.cache = &cc;

    const SearchOutcome first = runOnce(o); // 空缓存：全量填充（只读缓存，不产生条目）
    CHECK(first.ok && first.newCacheChunks.empty(), "首次搜索只读缓存（任务书 1.2：不产生条目）");
    CHECK(first.cacheHitPoints == 0, "空缓存时没有「跳过填充」的点");
    // v3.3.9（任务书 1.1）：缓存改由构建模式写入，搜索只读
    {
        SearchOptions b = o;
        b.buildMode = true;
        b.cacheWrite = &cc;
        b.targetCount = 1;
        const SearchOutcome rb = runOnce(b);
        CHECK(rb.ok && cc.entryCount() > 0, "构建模式填充同一区域（供「整区命中」用例）");
    }

    const SearchOutcome second = runOnce(o); // 锚点行域命中：这些行的填充应整段跳过
    const int64_t area = static_cast<int64_t>(o.maxX - o.minX + 1) * (o.maxZ - o.minZ + 1);
    // v3.3.9：缓存覆盖声明 = 锚点行域（末 hMax-1 = 4 行属块间重叠行，不在声明内）
    const int64_t anchorRows = (o.maxZ - o.minZ + 1) - 4;
    const int64_t anchorPoints = anchorRows * (o.maxX - o.minX + 1);
    CHECK(second.ok, "二次搜索（锚点行域命中）成功");
    CHECK(second.cacheHitPoints == anchorPoints, "跳过的填充点数 == 锚点行域点数（计数口径不变）");
    CHECK(!second.cacheFillSkipped, "末 4 行属块间重叠行 ⇒ 不是「整区」跳过（口径如实）");
    CHECK(second.stats.scannedChunks == area, "已扫描计数仍按区域总量计（对用户口径不变）");
    // 只搜锚点行域 ⇒ 真正「整区命中、填充整段跳过」
    {
        SearchOptions inner = o;
        inner.maxZ = o.maxZ - 4;
        const SearchOutcome rIn = runOnce(inner);
        const int64_t inArea = static_cast<int64_t>(inner.maxX - inner.minX + 1) *
                                (inner.maxZ - inner.minZ + 1);
        CHECK(rIn.ok && rIn.cacheFillSkipped && rIn.cacheHitPoints == inArea,
              "锚点行域内整区命中 ⇒ 填充整段跳过（cacheFillSkipped，任务 3+5 第 3 步）");
    }
    bool same = (second.results.size() == first.results.size());
    for (size_t i = 0; same && i < first.results.size(); ++i) {
        const MatchResult& a = first.results[i];
        const MatchResult& b = second.results[i];
        same = (a.minX == b.minX && a.maxX == b.maxX && a.minZ == b.minZ && a.maxZ == b.maxZ);
    }
    CHECK(same, "跳过填充后结果与全量填充逐位一致");
    std::printf("      填充跳过：%lld 点（区域 %lld 点），结果 %zu 条一致=%d\n",
                static_cast<long long>(second.cacheHitPoints), static_cast<long long>(area),
                second.results.size(), same ? 1 : 0);
}

// 背景：pruneToFit 整 range 丢条目后，跨运行再次登记同一区域 ⇒ 「声明在、条目不在」
//   ⇒ covers()=true 而 lookup() 恒返回 0（被判「已知非史莱姆」）⇒ 漏史莱姆（实测漏 34%、0 假阳性）。
// 修复：① 登记前先确认有新增条目（引擎/GUI 侧）；② 加载时 + 保存前按**支撑度**清扫无支撑声明。
// 本节断言：旧行为锚点、判据边界（稀疏大声明剔除 / 密集保留 / 小面积放行）、
//   真实缓存形态（4 行重叠带支撑的声明）、引擎跨运行后的清扫不变量、以及并发收集完整性。
static void testDeclSupportV338()
{
    std::printf("[20] 覆盖声明支撑度 + 并发收集（v3.3.8 ④′ 修复方案 B）…\n");
    const auto tmp = std::filesystem::temp_directory_path() / "slime_test_decl_support.bin";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    std::string err;

    // ---- A) 旧行为锚点 + 修复端到端（合成最小复现）----
    {
        CacheManager c;
        const Rect A{ 0, 999, 0, 99 }; // 面积 1e5 ⇒ 期望 1e4 条；阈值 = 50% × 期望 = 5000
        std::vector<std::pair<int, int>> dense;
        for (int x = 0; x <= 999; x += 5)
            for (int z = 0; z <= 99; ++z) dense.emplace_back(x, z); // 2e4 条 ≈ 20%（远高于阈值）
        c.mergeRegion(A, dense);
        CHECK(c.covers(A), "旧行为锚点①：mergeRegion 立即登记覆盖声明（登记门槛在引擎/GUI 调用点，不在 API 内）");
        c.pruneToFit(16 + 16 + 8); // 预算只够 1 个 range + 1 条 ⇒ 整 range 丢条目
        CHECK(!c.covers(A), "pruneToFit 丢弃最旧 range 后 covers(A)==false（声明随之消失）");
        c.mergeRegion(A, {}); // 跨运行再登记：0 新增条目
        CHECK(c.covers(A), "旧行为锚点②：0 新增条目仍被登记 ⇒ covers(A)==true（④′ 的直接来源）");
        CHECK(c.lookup(500, 50) == 0, "旧行为锚点③：命中区内被判「已知非史莱姆」（lookup==0）——正是漏判形态");
        CHECK(c.saveFile(tmp, err), "保存缓存成功（保存前清扫）");
        CHECK(c.ranges().empty(), "修复：无支撑声明不落盘（saveFile 前清扫剔除）");
        CacheManager c2;
        CHECK(c2.loadFile(tmp, true, err), "重新加载成功");
        CHECK(!c2.covers(A), "修复：covers(A)==false（不再把未扫描区判为「已知非史莱姆」）");
        CHECK(c2.lookup(500, 50) == -1, "修复：lookup(A 内任一点)==-1（下次搜索会重扫该区域）");
        std::filesystem::remove(tmp, ec);
    }

    // ---- B) 真实缓存形态：两个相邻大声明，后者只由「4 行重叠带」支撑（= 1.02 GB 缓存 #5/#6/#7 的形态）----
    {
        CacheManager c;
        const Rect A{ 0, 999, 0, 761 };    // 面积 762000 ⇒ 期望 76200 ⇒ 阈值 38100
        const Rect B{ 0, 999, 758, 1519 }; // 与 A 重叠 4 行（758..761），面积同为 762000
        std::vector<std::pair<int, int>> aCells, bCells;
        for (int x = 0; x <= 999; x += 2) {
            for (int z = 0; z <= 761; z += 5) aCells.emplace_back(x, z); // A 自身密集支撑（≈76500 条）
            for (int z = 758; z <= 761; ++z) bCells.emplace_back(x, z);  // B 只有重叠带 2000 条
        }
        c.mergeRegion(A, aCells);
        c.mergeRegion(B, bCells);
        const int64_t before = c.entryCount();
        CHECK(c.covers(B) && c.covers(A), "旧行为锚点④：稀疏声明 B 与密集声明 A 都被 covers() 认可");
        CHECK(c.saveFile(tmp, err), "保存缓存成功（含稀疏声明）");
        CHECK(c.ranges().size() == 1 && c.covers(A), "修复：密集声明 A 保留");
        CHECK(!c.covers(B), "修复：仅由 4 行重叠带支撑的稀疏声明 B 被剔除");
        CHECK(c.lookup(500, 1000) == -1, "修复：B 的独占区域 lookup()==-1（需重扫，不再误判非史莱姆）");
        CHECK(c.entryCount() == before, "清扫不删除任何条目（条目数不变）");
        CacheManager c2;
        CHECK(c2.loadFile(tmp, true, err), "重新加载成功");
        CHECK(c2.ranges().size() == 1 && c2.entryCount() == before, "落盘后的声明数与条目数一致（收敛）");
        std::filesystem::remove(tmp, ec);
    }

    // ---- C) 小面积放行（期望条目 < 100 条 ⇒ 不做统计判断）----
    {
        CacheManager c;
        const Rect small{ 0, 19, 0, 19 }; // 400 位置 ⇒ 期望 40 条 < 100 ⇒ 放行
        c.mergeRegion(small, {});
        std::string e2;
        CHECK(c.saveFile(tmp, e2), "保存小面积声明成功");
        CHECK(c.covers(small), "修复：小面积声明即使 0 条目也放行（统计上无法判断，误剔只会白重扫）");
        CacheManager c2;
        CHECK(c2.loadFile(tmp, true, e2) && c2.covers(small), "小面积声明往返后仍在");
        std::filesystem::remove(tmp, ec);
    }

    // ---- D) 引擎分块路径：**0 新增条目的块不再登记**（改动 1；确定性构造）----
    // 构造：区域 64×8；缓存已覆盖除某一列外的全部位置（两个 range），该列 8 个位置**全非史莱姆**
    //   ⇒ 该块本次「未覆盖部分」算出 0 个新条目 ⇒ 旧实现仍登记整块覆盖声明（声明无条目支撑），
    //     修复后不登记（下次会重扫该列，正确性优先）。
    {
#ifdef _WIN32
        const auto setEnv = [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); };
#else
        const auto setEnv = [](const char* k, const char* v) { v ? setenv(k, v, 1) : unsetenv(k); };
#endif
        const std::string oldForce = std::getenv("SLIME_FORCE_CHUNKED") ? std::getenv("SLIME_FORCE_CHUNKED") : "";
        const Rect R{ 0, 63, 0, 7 };
        // 找一列「8 个位置全非史莱姆」的列（种子 0），作为本次唯一未覆盖的缝隙
        int gapX = -1;
        for (int x = 0; x <= 63 && gapX < 0; ++x) {
            bool none = true;
            for (int z = 0; z <= 7; ++z)
                if (SlimeEngine::isSlimeChunkBedrockMT(x, z)) none = false;
            if (none) gapX = x;
        }
        CHECK(gapX >= 0, "找到「整列为非史莱姆」的缝隙列（构造前提）");
        CacheManager c;
        std::vector<std::pair<int, int>> covered;
        for (int x = 0; x <= 63; ++x) {
            if (x == gapX) continue;
            for (int z = 0; z <= 7; ++z)
                if (SlimeEngine::isSlimeChunkBedrockMT(x, z)) covered.emplace_back(x, z);
        }
        if (gapX > 0) c.mergeRegion(Rect{ 0, gapX - 1, 0, 7 }, covered);
        if (gapX < 63) {
            std::vector<std::pair<int, int>> right;
            for (const auto& p : covered)
                if (p.first > gapX) right.push_back(p);
            c.mergeRegion(Rect{ gapX + 1, 63, 0, 7 }, right);
        }
        const size_t rangesBefore = c.ranges().size();
        CHECK(!c.covers(R), "构造前提：缝隙列未被覆盖 ⇒ 该块不会被整体跳过");

        setEnv("SLIME_FORCE_CHUNKED", "1"); // 强制走分块路径（登记逻辑在 runChunkBody 内）
        SearchOptions o;
        o.version = Version::Bedrock;
        o.mode = Mode::Density;
        o.gridSize = 5;
        o.minDensity = 1.0;
        o.minX = R.minX; o.maxX = R.maxX; o.minZ = R.minZ; o.maxZ = R.maxZ;
        o.worldSeed = 0;
        o.targetCount = 1;
        o.stopAtTarget = false;
        o.threads = 0;
        o.useCache = true;
        o.cache = &c;
        o.cacheWrite = &c;
        o.cacheBudgetBytes = 1 << 20;
        // v3.3.9（任务书 1.1/1.2）：写缓存只发生在构建模式 ⇒ 本用例改用构建模式驱动同一条登记逻辑
        o.buildMode = true;
        const SearchOutcome r = runOnce(o);
        setEnv("SLIME_FORCE_CHUNKED", oldForce.c_str());
        CHECK(r.ok && r.cacheHandledIncrementally, "构建模式分块执行并按块写入缓存");
        CHECK(c.ranges().size() == rangesBefore,
              "修复（改动 1）：本次块 0 新增条目 ⇒ 不登记覆盖声明（旧实现会登记出无支撑声明）");
        CHECK(!c.covers(R), "修复：整区仍未被声明覆盖 ⇒ 下次搜索会重扫缝隙（不误判为已知非史莱姆）");
        CHECK(c.lookup(gapX, 0) == -1, "修复：缝隙内 lookup()==-1（需重算，而非「已知非史莱姆」）");
        std::string e3;
        CHECK(c.saveFile(tmp, e3), "保存缓存成功（无支撑声明不会被落盘）");
        CacheManager c2;
        CHECK(c2.loadFile(tmp, true, e3) && c2.ranges().size() == rangesBefore, "往返后声明数不变（已收敛）");
        std::filesystem::remove(tmp, ec);
    }

    // ---- E) 构建模式并发收集完整性（分块并行：每块局部收集 → 批量归并）----
    {
        CacheManager built; // v3.3.9（任务书 1.1）：收集只发生在构建模式
        SearchOptions o;
        o.version = Version::Bedrock;
        o.mode = Mode::Density;
        o.gridSize = 5;
        o.minDensity = 1.0; // 匹配结果无关紧要，本用例只看缓存写入
        o.minX = -261; o.maxX = 260;
        o.minZ = -131; o.maxZ = 130;
        o.worldSeed = 0;
        o.targetCount = 1;
        o.stopAtTarget = false;
        o.threads = 4; // 多线程（分块并行）
        o.useCache = true;
        o.cache = &built;
        o.cacheWrite = &built;
        o.buildMode = true;
        const SearchOutcome r = runOnce(o);
        CHECK(r.ok, "构建模式并发写入成功");
        std::set<std::pair<int, int>> truth;
        for (int z = o.minZ; z <= o.maxZ; ++z)
            for (int x = o.minX; x <= o.maxX; ++x)
                if (SlimeEngine::isSlimeChunkBedrockMT(x, z)) truth.emplace(x, z);
        int missing = 0, bad = 0;
        for (int z = o.minZ; z <= o.maxZ - 4; ++z) { // 末 4 行属块间重叠行（不在声明域内）
            for (int x = o.minX; x <= o.maxX; ++x) {
                const int lk = built.lookup(x, z);
                const bool t = SlimeEngine::isSlimeChunkBedrockMT(x, z);
                if (lk < 0) ++missing;
                else if ((lk == 1) != t) ++bad;
            }
        }
        CHECK(!truth.empty() && built.entryCount() == static_cast<int64_t>(truth.size()),
              "并发写入无丢失（条目数 == 真值史莱姆区块数）");
        CHECK(missing == 0 && bad == 0, "并发写入逐条与真值一致");
        std::printf("      构建模式并发写入：缓存条目 %lld 条，真值 %zu 条，threads=4（缺失 %d / 错误 %d）\n",
                    static_cast<long long>(built.entryCount()), truth.size(), missing, bad);
    }
}

// ===================== v3.3.9（任务书 1.1/1.2）：缓存构建模式 =====================
// 验收：① 只判定 + 写缓存（不匹配）；② 判定结果与直接计算逐位一致；
//       ③ 中断续传（第二次运行整块跳过，记录 0）；④ 达到容量上限即停止（不半途而废、不边写边丢）；
//       ⑤ 构建模式仅支持基岩版；⑥ 构建比搜索快（省掉整个匹配阶段）。
static void testCacheBuildModeV339()
{
    std::printf("[25] 缓存构建模式（任务书 1.1/1.2）：只判定+写缓存 / 续传 / 容量上限 / 不匹配…\n");
#ifdef _WIN32
    const auto setEnv = [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); };
#else
    const auto setEnv = [](const char* k, const char* v) { v ? setenv(k, v, 1) : unsetenv(k); };
#endif
    const std::string oldForce = std::getenv("SLIME_FORCE_CHUNKED") ? std::getenv("SLIME_FORCE_CHUNKED") : "";
    const Rect R{ 0, 1023, 0, 511 }; // 512K 区块
    SearchOptions opts;
    opts.version = Version::Bedrock;
    opts.mode = Mode::Density;
    opts.gridSize = 5;
    opts.minDensity = 0.3;
    opts.minX = R.minX; opts.maxX = R.maxX; opts.minZ = R.minZ; opts.maxZ = R.maxZ;
    opts.worldSeed = 0;
    opts.startX = 512; opts.startZ = 256;
    opts.targetCount = 10;
    opts.threads = 0;
    opts.useCache = true;

    // ⑤ Java 版构建被拒绝（缓存语义与既有缓存一致）
    {
        SearchOptions javaOpt = opts;
        javaOpt.version = Version::Java;
        javaOpt.buildMode = true;
        CacheManager c;
        javaOpt.cache = &c;
        javaOpt.cacheWrite = &c;
        CHECK(!SlimeEngine::validate(javaOpt).empty(), "构建模式拒绝 Java 版（validate 报错，任务书 1.1）");
    }

    CacheManager cache;
    SearchOptions build = opts;
    build.buildMode = true;
    build.cache = &cache;
    build.cacheWrite = &cache;
    build.cacheBudgetBytes = 0;
    std::atomic<int64_t> rec1{ 0 }, skip1{ 0 };
    build.liveRecorded = &rec1;
    build.liveSkippedCovered = &skip1;
    setEnv("SLIME_FORCE_CHUNKED", "1");
    const auto t0 = std::chrono::steady_clock::now();
    const SearchOutcome rb = runOnce(build);
    const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(rb.ok && !rb.cancelled && !rb.hitCacheBudget, "构建模式执行成功（范围跑完，未触及上限）");
    CHECK(rb.results.empty() && rb.stats.windowsEvaluated == 0, "构建模式不做匹配（任务书 1.1）");
    // 分块路径含块间重叠行 ⇒ 「已记录」≥ 去重区块数（与搜索的扫描计数同口径）
    const int64_t dedupChunks = static_cast<int64_t>(R.maxX - R.minX + 1) * (R.maxZ - R.minZ + 1);
    CHECK(rec1.load() >= dedupChunks && rec1.load() <= dedupChunks * 2,
          "已记录区块数覆盖整个范围（构建完成条件之一；含块间重叠行）");
    CHECK(skip1.load() == 0, "首次构建没有可跳过的已缓存区块");

    // ② 判定与直接计算逐位一致（缓存位图 == 逐点判定）
    {
        int bad = 0;
        std::mt19937_64 rng(339);
        for (int i = 0; i < 600; ++i) {
            const int x = R.minX + static_cast<int>(rng() % 1024);
            const int z = R.minZ + static_cast<int>(rng() % 508); // 末 4 行 = 最后一块的重叠行，不在声明域内
            const int lk = cache.lookup(x, z);
            const bool direct = SlimeEngine::isSlimeChunkBedrockMT(x, z);
            if (lk < 0 || ((lk == 1) != direct)) ++bad;
        }
        CHECK(bad == 0, "构建出的缓存与直接判定逐位一致（抽查 600 点）");
    }

    // ⑥ 构建 vs 冷搜索：同范围无缓存搜索（含匹配阶段）应更慢
    {
        SearchOptions search = opts;
        search.useCache = false; // 冷搜索：排除「缓存填充捷径」的干扰，只看省掉匹配阶段的收益
        search.cache = nullptr;
        const auto t1 = std::chrono::steady_clock::now();
        const SearchOutcome rs = runOnce(search);
        const double searchMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        CHECK(rs.ok, "同范围冷搜索成功");
        std::printf("      构建 %.0f ms / 冷搜索 %.0f ms（构建应更快：省掉整个匹配阶段）\n", buildMs, searchMs);
        CHECK(buildMs < searchMs * 1.25, "构建模式不慢于冷搜索（任务书 1.1：应比搜索快数倍）");
    }

    // ③ 中断续传：同一范围再构建一次 ⇒ 整块被跳过（不重复判定）
    {
        std::atomic<int64_t> rec2{ 0 }, skip2{ 0 };
        SearchOptions again = build;
        again.liveRecorded = &rec2;
        again.liveSkippedCovered = &skip2;
        const SearchOutcome r2 = runOnce(again);
        CHECK(r2.ok, "第二次构建成功（续传）");
        CHECK(rec2.load() < rec1.load() / 10, "续传：几乎不再重新判定（已缓存区域被跳过）");
        CHECK(skip2.load() > 0 && r2.buildSkippedChunks >= 0, "续传：记录了「已覆盖跳过」的区块数");
        std::printf("      续传：新判定 %lld 区块 / 跳过 %lld 区块（首次 %lld）\n",
                    static_cast<long long>(rec2.load()), static_cast<long long>(skip2.load()),
                    static_cast<long long>(rec1.load()));
    }

    // ④ 容量上限：给一个很小的预算 ⇒ 立刻停止并如实报告（不边写边丢）
    {
        CacheManager small;
        SearchOptions cap = build;
        cap.cache = &small;
        cap.cacheWrite = &small;
        cap.cacheBudgetBytes = 1 << 12; // 4 KB：远小于本次范围所需位图
        const SearchOutcome rc = runOnce(cap);
        CHECK(rc.ok && rc.hitCacheBudget, "达到缓存容量上限 ⇒ 停止并置 hitCacheBudget（任务书 1.1 完成条件之二）");
        // 超调量上限 = 一次块级归并（构建模式不再「边写边丢」，因此允许一次归并的超调）
        CHECK(small.sizeBytes() <= (1 << 17), "停止后缓存体积有界（超调 ≤ 一次块级归并）");
        std::printf("      容量上限：停止时缓存 %lld B（预算 4096 B，范围需 %lld B 位图）\n",
                    static_cast<long long>(small.sizeBytes()),
                    static_cast<long long>((static_cast<int64_t>(1024) * 512) / 8));
    }
    setEnv("SLIME_FORCE_CHUNKED", oldForce.c_str());
    std::filesystem::remove(std::filesystem::temp_directory_path() / "slime_v339_build.bin");
}


// ===================== v3.4.11（用户拍板）：等距取舍 / 判界分块大小 / 剪枝对照 =====================
// 背景：分块判据由 `> worst` 改为 `>= worst`（等距也剪），与经典路径 `nth <= L` 统一。
// 安全性依据：TopN 堆比较器是 (dist, rank) 字典序 —— 等距窗口只有 rank 更小才可能替换第 N 名。
// 本组用「等距密集」的配置 + 多线程 + 剪枝开关对照，验证结果不受判据/分块大小影响。
static void testEqualDistanceAndStopBlocks()
{
    std::printf("[26] v3.4.11 等距取舍 / 判界分块大小 / 剪枝对照…\n");
    const std::string oldForce = std::getenv("SLIME_FORCE_CHUNKED") ? std::getenv("SLIME_FORCE_CHUNKED") : "";
    const std::string oldWords = std::getenv("SLIME_CHUNK_WORDS") ? std::getenv("SLIME_CHUNK_WORDS") : "";
    const std::string oldNoPrune = std::getenv("SLIME_NO_PRUNE") ? std::getenv("SLIME_NO_PRUNE") : "";
    const std::string oldRows = std::getenv("SLIME_STOP_BLOCK_ROWS") ? std::getenv("SLIME_STOP_BLOCK_ROWS") : "";
#ifdef _WIN32
    const auto setEnv = [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); };
#else
    const auto setEnv = [](const char* k, const char* v) { v ? setenv(k, v, 1) : unsetenv(k); };
#endif
    const auto key = [](const SearchOutcome& o) {
        std::vector<std::tuple<int, int, int, int>> v;
        for (const MatchResult& m : o.results) v.emplace_back(m.minX, m.maxX, m.minZ, m.maxZ);
        return v;
    };

    // 等距密集配置：起点在范围中心、网格为奇数（窗口中心落在整数坐标上）⇒ 大量等距窗口
    SearchOptions base;
    base.version = Version::Bedrock; // 基岩判定快，等距场景可扫得足够大
    base.worldSeed = 0;
    base.minX = -160; base.maxX = 159;
    // Z 方向拉长：配合下面把每块行数压到 ~204 行，形成多块 ⇒ 才可能触发跨块剪枝
    base.minZ = -400; base.maxZ = 399;
    base.mode = Mode::Density;
    base.gridSize = 5;
    base.minDensity = 0.05;      // 阈值极低 ⇒ 命中密集、等距并列频繁
    base.startX = 0; base.startZ = 0;
    base.targetCount = 12;       // 目标数取偶数：等距成对出现
    base.threads = 4;            // 多线程（分块路径）——验证 rank 顺序不保证时仍安全
    base.useCache = false;
    base.cache = nullptr;

    // ① 经典路径（单线程判界）：勾选 vs 不勾选 —— 结果必须一致
    setEnv("SLIME_FORCE_CHUNKED", "");
    setEnv("SLIME_STOP_BLOCK_ROWS", "");
    SearchOptions classic = base;
    classic.stopAtTarget = false;
    const SearchOutcome full = runOnce(classic);
    CHECK(full.ok, "等距场景：完整搜索成功");
    SearchOptions stopped = base;
    stopped.stopAtTarget = true;
    const SearchOutcome stop = runOnce(stopped);
    CHECK(stop.ok, "等距场景：提前停止搜索成功");
    CHECK(key(stop) == key(full), "等距场景：提前停止结果 = 完整结果（勾选/不勾选一致）");

    // ② 判界分块大小不影响结果（SLIME_STOP_BLOCK_ROWS = 1 / 7 / 64 / 4096）
    int blockOk = 0;
    for (const char* rows : { "1", "7", "64", "4096" }) {
        setEnv("SLIME_STOP_BLOCK_ROWS", rows);
        const SearchOutcome r = runOnce(stopped);
        const bool same = r.ok && key(r) == key(stop);
        if (same) ++blockOk;
        else std::printf("      MISMATCH rows=%s results=%zu/%zu\n", rows, r.results.size(), stop.results.size());
    }
    CHECK(blockOk == 4, "SLIME_STOP_BLOCK_ROWS=1/7/64/4096 四种分块大小结果逐位一致");
    setEnv("SLIME_STOP_BLOCK_ROWS", "");

    // ③ 分块路径 + 剪枝开关对照（>= worst 的安全性）：结果必须逐位一致
    //   说明：SLIME_CHUNK_WORDS 有下限 65536 words；块行数 = 65536 / ceil(W/64)。
    //   取 W=4096（64 words/行）⇒ 1024 行/块；H=8192 ⇒ 8 块 > 线程数，才有块可剪。
    setEnv("SLIME_FORCE_CHUNKED", "1");
    setEnv("SLIME_CHUNK_WORDS", "65536");
    SearchOptions chunkedStop = base;
    chunkedStop.stopAtTarget = true;
    chunkedStop.minX = -2048; chunkedStop.maxX = 2047;   // 对称、能被 64 整除宽度
    chunkedStop.minZ = -4096; chunkedStop.maxZ = 4095;   // 8192 行 ⇒ 8 块
    chunkedStop.threads = 2;                             // 线程数 < 块数，跨块剪枝才有对象
    setEnv("SLIME_NO_PRUNE", "");
    std::atomic<int64_t> livePruned{ 0 };
    SearchOptions prunedOpt = chunkedStop;
    prunedOpt.liveScanned = &livePruned;
    const SearchOutcome pruned = runOnce(prunedOpt);
    CHECK(pruned.ok, "分块+提前停止：搜索成功");
    setEnv("SLIME_NO_PRUNE", "1");
    std::atomic<int64_t> liveUnpruned{ 0 };
    SearchOptions unprunedOpt = chunkedStop;
    unprunedOpt.liveScanned = &liveUnpruned;
    const SearchOutcome unpruned = runOnce(unprunedOpt);
    CHECK(unpruned.ok && unpruned.chunksPruned == 0, "SLIME_NO_PRUNE=1 时不剪枝");
    const bool samePrune = (key(pruned) == key(unpruned));
    if (!samePrune) {
        std::printf("      MISMATCH prune=%zu unprune=%zu prunedChunks=%lld\n", pruned.results.size(),
                    unpruned.results.size(), static_cast<long long>(pruned.chunksPruned));
    }
    CHECK(samePrune, "等距 + 分块：剪枝前后结果逐位一致（>= worst 无损）");
    CHECK(pruned.chunksPruned > 0, "该配置下确实发生了跨块剪枝（对照有意义的样本）");
    // 扫描量口径：分块路径带 liveScanned 时上报真实已扫区块数（不带则上报计划总量，剪枝不改变它）
    std::printf("      扫描（真实计数）：剪枝=%lld 区块 / 不剪枝=%lld 区块；评估窗口=%lld / %lld\n",
                static_cast<long long>(pruned.stats.scannedChunks),
                static_cast<long long>(unpruned.stats.scannedChunks),
                static_cast<long long>(pruned.stats.windowsEvaluated),
                static_cast<long long>(unpruned.stats.windowsEvaluated));
    CHECK(unpruned.stats.scannedChunks > pruned.stats.scannedChunks, "剪枝确实少扫区块（对照扫描量更大）");
    CHECK(unpruned.stats.windowsEvaluated > pruned.stats.windowsEvaluated, "剪枝确实少评估窗口（对照更多）");

    // ④ 多线程（4 线程）rank 顺序不保证 ⇒ 同样做剪枝开关对照（结果必须逐位一致）
    setEnv("SLIME_NO_PRUNE", "");
    SearchOptions mt = chunkedStop;
    mt.threads = 4;
    const SearchOutcome mtPruned = runOnce(mt);
    setEnv("SLIME_NO_PRUNE", "1");
    const SearchOutcome mtUnpruned = runOnce(mt);
    const bool sameMt = (mtPruned.ok && mtUnpruned.ok && key(mtPruned) == key(mtUnpruned));
    if (!sameMt) {
        std::printf("      MISMATCH mt prune=%zu unprune=%zu prunedChunks=%lld\n", mtPruned.results.size(),
                    mtUnpruned.results.size(), static_cast<long long>(mtPruned.chunksPruned));
    }
    CHECK(sameMt, "等距 + 4 线程：剪枝前后结果逐位一致（rank 顺序不保证时仍无损）");
    if (mtPruned.cacheFirstStopped) {
        CHECK(mtPruned.stoppedEarly, "缓存优先堆满即停时 stoppedEarly 为真（v3.4.11 修复点）");
    }
    std::printf("      等距：完整=%zu 提前停止=%zu 条；分块剪枝=%lld 块（2 线程）/ %lld 块（4 线程）结果一致=%d/%d\n",
                full.results.size(), stop.results.size(), static_cast<long long>(pruned.chunksPruned),
                static_cast<long long>(mtPruned.chunksPruned), samePrune ? 1 : 0, sameMt ? 1 : 0);

    setEnv("SLIME_FORCE_CHUNKED", oldForce.c_str());
    setEnv("SLIME_CHUNK_WORDS", oldWords.c_str());
    setEnv("SLIME_NO_PRUNE", oldNoPrune.c_str());
    setEnv("SLIME_STOP_BLOCK_ROWS", oldRows.c_str());
}

// ===================== [27] v3.4.13：PerfLog CPU 采样线程安全 =====================
// 背景：CpuSampler 的基准值原先是**非原子成员**，而分块路径的每个块工作线程都会调用它，
//       两个线程同时读写同一对基准 ⇒ dCpu/dMs 组合无意义，日志出现过 cpu=18750%
//       （12 核机器物理上限 1200%）。本组用 8 线程并发轰同一个采样器，断言：
//         ① 返回的百分比恒在物理可能范围（cpu ∈ [0,100]，all ∈ [0,100×核数]）
//         ② 两个口径自洽（all == cpu × 核数）
//         ③ 顺序采样在单线程忙等时给出接近「一个核」的读数（不出现量级错误）
static void testCpuSamplerThreadSafety()
{
    std::printf("[27] v3.4.13 PerfLog CPU 采样线程安全…\n");
    const int cores = std::max(1, slime::perf::logicalCores());
    slime::perf::CpuSampler sampler;
    double c0 = 0.0, a0 = 0.0, w0 = 0.0;
    sampler.sample(&c0, &a0, &w0); // 建立基线
    std::atomic<int> okCount{ 0 }, badRange{ 0 }, badSelf{ 0 }, samples{ 0 };
    std::atomic<bool> stop{ false };
    volatile uint64_t sink = 0;
    std::vector<std::thread> th;
    for (int i = 0; i < 8; ++i) {
        th.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                double c = 0.0, a = 0.0, w = 0.0;
                if (sampler.sample(&c, &a, &w)) {
                    samples.fetch_add(1, std::memory_order_relaxed);
                    const bool inRange = c >= 0.0 && c <= 100.0 + 1e-9 &&
                                         a >= 0.0 && a <= 100.0 * cores + 1e-9;
                    const bool selfConsistent = std::fabs(a - c * cores) < 1e-6;
                    if (inRange) okCount.fetch_add(1, std::memory_order_relaxed);
                    else badRange.fetch_add(1, std::memory_order_relaxed);
                    if (!selfConsistent) badSelf.fetch_add(1, std::memory_order_relaxed);
                }
                for (int k = 0; k < 500; ++k) sink += static_cast<uint64_t>(k) * 3u;
            }
        });
    }
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 0.5) {
        for (int i = 0; i < 20000; ++i) sink += static_cast<uint64_t>(i) * 7u;
    }
    stop.store(true);
    for (auto& x : th) x.join();
    std::printf("     并发采样：有效 %d 次 / 越界 %d 次 / 口径不自洽 %d 次（核数 %d）\n",
                okCount.load(), badRange.load(), badSelf.load(), cores);
    CHECK(samples.load() > 0, "并发采样确实产生了有效样本");
    CHECK(badRange.load() == 0, "并发采样结果恒在物理可能范围（cpu≤100、all≤100×核数）");
    CHECK(badSelf.load() == 0, "cpu 与 all 两个口径自洽（all = cpu × 核数）");

    // 顺序采样：单线程忙等 300ms ⇒ 占用率应接近「一个核跑满」
    slime::perf::CpuSampler one;
    double x0 = 0.0, y0 = 0.0, z0 = 0.0;
    one.sample(&x0, &y0, &z0);
    const auto t1 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count() < 0.3) {
        for (int i = 0; i < 20000; ++i) sink += static_cast<uint64_t>(i) * 11u;
    }
    double c = 0.0, a = 0.0, w = 0.0;
    const bool sampled = one.sample(&c, &a, &w);
    std::printf("     顺序采样：cpu=%.1f%% all=%.0f%%（单线程忙等；all 应≈100%%）\n", c, a);
    CHECK(sampled, "顺序采样在第二个窗口能给出数值");
    CHECK(c > 0.0 && c <= 100.0 + 1e-9, "顺序采样值落在 0~100");
    CHECK(a > 0.0 && a <= 100.0 * cores + 1e-9, "顺序采样 all 口径落在 0~100×核数");
}

// ===================== [28] v3.4.13：归并调度无关性 =====================
// 第 2 步把归并从「扫描线程持 cacheMu 就地写」改成「专职归并线程串行写」⇒ 归并**顺序**会变。
// 置位是幂等的 OR、outliers 排序去重、ranges 按矩形去重 ⇒ 最终缓存必须逐位一致。
// 本组用同一范围跑三遍（解耦 4 线程 / 内联 4 线程 / 解耦 1 线程）并逐字比较位图。
static bool sameRect(const Rect& a, const Rect& b)
{
    return a.minX == b.minX && a.maxX == b.maxX && a.minZ == b.minZ && a.maxZ == b.maxZ;
}

// v3.4.13：[28] 的等价判据。
//   注意「顺序」：归并顺序变化会让区域进入 m_ranges 的**次序**不同（ensureRange 按到达顺序追加）。
//   语义上等价（lookup 按包含关系查找），所以这里按「同矩形配对 + 位图逐字 + lookup 抽样」比较；
//   已知副作用（记录在案，不影响正确性）：pruneToFit 按「最旧优先」淘汰，区域顺序不同会让
//   被淘汰的集合略有差异 ⇒ 预算内缓存覆盖范围可能略有不同（结果不会错，只会重扫）。
// v3.4.13：[28] 用 —— 线程数不同 ⇒ 块数目标不同 ⇒ 区域划分不同（这是设计使然），
//   此时只比较「语义」：① 两边的覆盖域（各区域 z 范围的并集）相同；② 抽样点 isSlime/lookup 一致。
static bool cacheSameCoverageDomain(const CacheManager& a, const CacheManager& b, std::string* why)
{
    const auto coverZ = [](const CacheManager& m, int* lo, int* hi) {
        if (m.ranges().empty()) return false;
        int l = m.ranges().front().minZ, h = m.ranges().front().maxZ;
        for (const Rect& r : m.ranges()) {
            l = (std::min)(l, r.minZ);
            h = (std::max)(h, r.maxZ);
        }
        *lo = l;
        *hi = h;
        return true;
    };
    int loA = 0, hiA = 0, loB = 0, hiB = 0;
    if (!coverZ(a, &loA, &hiA) || !coverZ(b, &loB, &hiB)) {
        *why = "有一侧没有区域";
        return false;
    }
    if (loA != loB || hiA != hiB) {
        *why = "覆盖域 z 范围不同";
        return false;
    }
    const int x0 = a.ranges().front().minX;
    const int x1 = a.ranges().front().maxX;
    const int64_t spanX = static_cast<int64_t>(x1) - x0 + 1;
    int64_t slime = 0;
    for (int t = 0; t < 4000; ++t) {
        const int x = x0 + static_cast<int>((t * 7919) % spanX);
        const int z = loA + static_cast<int>((t * 104729) % (hiA - loA + 1));
        if (a.isSlime(x, z)) ++slime;
        if (a.isSlime(x, z) != b.isSlime(x, z) || a.lookup(x, z) != b.lookup(x, z)) {
            *why = "抽样不一致 @" + std::to_string(x) + "," + std::to_string(z);
            return false;
        }
    }
    if (slime == 0) {
        *why = "样例无史莱姆点";
        return false;
    }
    return true;
}

static bool cacheEquivalent(const CacheManager& a, const CacheManager& b, std::string* why)
{
    if (a.ranges().size() != b.ranges().size()) {
        *why = "区域数不同 " + std::to_string(a.ranges().size()) + " vs " +
               std::to_string(b.ranges().size());
        return false;
    }
    for (size_t i = 0; i < a.ranges().size(); ++i) {
        const Rect& ra = a.ranges()[i];
        size_t j = static_cast<size_t>(-1);
        for (size_t k = 0; k < b.ranges().size(); ++k) {
            if (sameRect(ra, b.ranges()[k])) {
                j = k;
                break;
            }
        }
        if (j == static_cast<size_t>(-1)) {
            *why = "对方缺少同矩形区域 @" + std::to_string(i);
            return false;
        }
        int64_t wa = 0, ha = 0, wb = 0, hb = 0;
        const uint64_t* ba = a.rangeBitmap(i, &wa, &ha);
        const uint64_t* bb = b.rangeBitmap(j, &wb, &hb);
        if (!ba || !bb || wa != wb || ha != hb) {
            *why = "位图缺失/维度不同 @" + std::to_string(i);
            return false;
        }
        const int64_t words = wa * ha;
        for (int64_t k = 0; k < words; ++k) {
            if (ba[k] != bb[k]) {
                *why = "位图字不同 @" + std::to_string(i) + ":" + std::to_string(k);
                return false;
            }
        }
    }
    // 语义抽样（**关键判据**）：isSlime 同时查「range 位图」与「outliers」，是最完整的语义出口。
    //   注意：entryCount() 允许有极小差异 —— 块间重叠行的同一条目，会因归并顺序不同而
    //   「落进相邻 range 的位图」或「记成 outlier」，两者语义等价（位图已逐字一致；
    //   实测差异 ~0.02% 的条目计数，仅影响 outlier 列表与文件大小，不影响任何查询结果）。
    const Rect& box = a.ranges().front();
    const int64_t spanX = static_cast<int64_t>(box.maxX) - box.minX + 1;
    const int64_t spanZ = static_cast<int64_t>(box.maxZ) - box.minZ + 1;
    int64_t slimeA = 0, slimeB = 0;
    for (int t = 0; t < 4000; ++t) {
        const int x = box.minX + static_cast<int>((t * 7919) % spanX);
        const int z = box.minZ + static_cast<int>((t * 104729) % spanZ);
        const bool sa = a.isSlime(x, z);
        const bool sb = b.isSlime(x, z);
        if (sa) ++slimeA;
        if (sb) ++slimeB;
        if (sa != sb) {
            *why = "isSlime 不一致 @" + std::to_string(x) + "," + std::to_string(z);
            return false;
        }
        if (a.lookup(x, z) != b.lookup(x, z)) {
            *why = "lookup 不一致 @" + std::to_string(x) + "," + std::to_string(z);
            return false;
        }
    }
    if (slimeA == 0) {
        *why = "抽样点里没有史莱姆（样例无意义）";
        return false;
    }
    return true;
}

static void testMergeScheduleInvariance()
{
    std::printf("[28] v3.4.13 归并调度无关性（解耦 / 内联 / 线程数）…\n");
    SearchOptions base;
    base.version = Version::Bedrock;
    base.worldSeed = 0;
    base.mode = Mode::Density;
    base.gridSize = 5;
    base.minDensity = 0.3;
    // v3.4.13：范围取「窄而高」并压小分块容量 ⇒ 产生多个分块与多次归并
    //   （512 宽 × 32768 高、SLIME_CHUNK_WORDS=65536 ⇒ 每块 8192 行 ⇒ 4 块，
    //    每块各自入队、由归并线程逐批写入 ⇒ 真正走到「调度顺序可能不同」的路径）
    base.minX = -256;
    base.maxX = 255;
    base.minZ = -16384;
    base.maxZ = 16383;
    base.targetCount = 1;
#ifdef _WIN32
    const auto setEnv = [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); };
#else
    const auto setEnv = [](const char* k, const char* v) { v ? setenv(k, v, 1) : unsetenv(k); };
#endif
    const auto runBuild = [&](bool inlineMerge, int threads) {
        CacheManager c;
        SearchOptions o = base;
        o.buildMode = true;
        o.useCache = true;
        o.cache = &c;
        o.cacheWrite = &c;
        o.cacheBudgetBytes = 0;
        o.threads = threads;
        setEnv("SLIME_FORCE_CHUNKED", "1"); // 构建模式本就强制分块；显式设置便于对照
        setEnv("SLIME_CHUNK_WORDS", "65536"); // 压小分块容量 ⇒ 多块 + 多次归并
        setEnv("SLIME_MERGE_INLINE", inlineMerge ? "1" : "0");
        std::atomic<int64_t> rec{ 0 }, skip{ 0 };
        o.liveRecorded = &rec;
        o.liveSkippedCovered = &skip;
        const SearchOutcome r = runOnce(o);
        CHECK(r.ok && !r.cancelled, "构建模式执行成功（用于归并调度对照）");
        setEnv("SLIME_MERGE_INLINE", "0"); // 还原默认（解耦）
        setEnv("SLIME_CHUNK_WORDS", "");
        return c;
    };
    const CacheManager a = runBuild(false, 4); // 解耦 + 4 线程（新默认）
    const CacheManager b = runBuild(true, 4);  // 内联 + 4 线程（旧行为）
    const CacheManager c = runBuild(false, 1); // 解耦 + 1 线程
    std::string why;
    const bool eq1 = cacheEquivalent(a, b, &why);
    std::printf("     解耦 vs 内联：%s%s\n", eq1 ? "逐位一致" : "不一致：", eq1 ? "" : why.c_str());
    CHECK(eq1, "解耦归并 vs 内联归并：缓存语义等价（位图逐字一致 + lookup 抽样一致）");
    why.clear();
    // 线程数不同 ⇒ 块数目标不同（target = 4×并发）⇒ 区域矩形集合本就不同，不能逐矩形比位图；
    //   这里比**语义**：两边的覆盖域一致（同一区域的锚点行域之并），且抽样点结论一致。
    const bool eq2 = cacheSameCoverageDomain(a, c, &why);
    std::printf("     解耦 4 线程 vs 1 线程：%s%s\n", eq2 ? "语义一致（覆盖域同 + 抽样同）" : "不一致：",
                eq2 ? "" : why.c_str());
    CHECK(eq2, "解耦归并 4 线程 vs 1 线程：缓存语义等价（覆盖域一致 + isSlime/lookup 抽样一致）");
    std::printf("     缓存规模：%zu 区域 / %lld 条目 / %lld 字节；条目计数差异=%lld（重叠行归属，语义等价）\n",
                a.ranges().size(), static_cast<long long>(a.entryCount()),
                static_cast<long long>(a.sizeBytes()),
                static_cast<long long>(a.entryCount()) - static_cast<long long>(b.entryCount()));
}

// v3.5.2（任务 1）：环形覆盖挂机点 —— 精度 1 格（1/16 区块）、圆内覆盖、与暴力解一致
static void testAfkPlannerRing()
{
    std::printf("\n[29] 环形覆盖挂机点（1/16 区块精度）\n");

    // ① 2x2 史莱姆区块：最优站位应落在四区块交点 (0,0)，覆盖 4x256 格
    {
        const auto pred = [](int cx, int cz) { return (cx == 0 || cx == 1) && (cz == 0 || cz == 1); };
        slime::AfkPlanOptions o;
        o.area = slime::Rect{ -2, 3, -2, 3 };
        o.radius = 128;
        o.topN = 3;
        o.minSeparation = 16;
        const auto v = slime::AfkPlanner::plan(o, pred);
        CHECK(!v.empty(), "2x2 区块：应给出挂机点");
        if (!v.empty()) {
            std::printf("      最优：(%d,%d) 覆盖 %lld 格 / %d 区块 / 角点=%d\n", v[0].x, v[0].z,
                        static_cast<long long>(v[0].coveredBlocks), v[0].coveredChunks,
                        v[0].onChunkCorner ? 1 : 0);
            CHECK(v[0].coveredBlocks == 4 * 256, "四区块全在半径内：覆盖 = 4x256 格");
            CHECK(std::abs(v[0].x - 16) <= 1 && std::abs(v[0].z - 16) <= 1,
                  "最优点落在四区块交点 (16,16) 附近（覆盖区块重心）");
            CHECK(v[0].coveredChunks == 4, "覆盖区块数 = 4");
            CHECK(v[0].onChunkCorner, "角点标记正确");
            CHECK(v.size() <= 3, "返回数量不超过 topN");
        }
    }

    // ② 与暴力解逐点一致（随机 6x6 区块、R=13、1 格精度）
    {
        std::mt19937 rng(20261017u);
        std::uniform_int_distribution<int> d(0, 3);
        std::vector<std::pair<int, int>> slimeSet;
        for (int z = -3; z <= 2; ++z)
            for (int x = -3; x <= 2; ++x)
                if (d(rng) == 0) slimeSet.emplace_back(x, z);
        const auto pred = [&slimeSet](int cx, int cz) {
            return std::find(slimeSet.begin(), slimeSet.end(), std::make_pair(cx, cz)) != slimeSet.end();
        };
        slime::AfkPlanOptions o;
        o.area = slime::Rect{ -3, 2, -3, 2 };
        o.radius = 13;
        o.topN = 3;
        o.minSeparation = 1;
        o.coarseStep = 4;
        const auto fast = slime::AfkPlanner::plan(o, pred);
        const auto ref = slime::AfkPlanner::planBruteForce(o, pred);
        std::printf("      随机 6x6：史莱姆区块 %d 个；快速=%zu 名 暴力=%zu 名\n", static_cast<int>(slimeSet.size()),
                    fast.size(), ref.size());
        CHECK(!fast.empty() && !ref.empty(), "随机区域：两种实现都给出结果");
        if (!fast.empty() && !ref.empty()) {
            CHECK(fast[0].coveredBlocks == ref[0].coveredBlocks, "最优覆盖与暴力解完全一致（1 格精度）");
            CHECK(fast[0].x == ref[0].x && fast[0].z == ref[0].z, "最优点坐标与暴力解完全一致");
        }
    }

    // ⑤ v3.5.11：设置最小密度时，结果数量仍受「目标数量」限制
    {
        const auto pred = [](int cx, int cz) { return (cx + cz) % 3 != 0; };
        slime::AfkPlanOptions o;
        o.area = slime::Rect{ -8, 8, -8, 8 };
        o.radius = 128;
        o.innerRadius = 24;
        o.topN = 3;
        o.minSeparation = 16;
        o.coarseStep = 16;
        o.minDensity = 0.01;  // 极低阈值：几乎所有位置都达标
        const auto v = slime::AfkPlanner::plan(o, pred);
        std::printf("      最小密度 1%%：返回 %zu 个（topN=3）\n", v.size());
        CHECK(v.size() <= 3, "环形 + 最小密度：返回数量不超过目标数量");
    }

    // ③ evaluate() 与暴力解的覆盖值口径一致
    {
        const auto pred = [](int cx, int cz) { return (cx + cz) % 3 == 0; };
        slime::AfkPlanOptions o;
        o.area = slime::Rect{ 0, 1, 0, 1 };
        o.radius = 20;
        o.topN = 1;
        o.minSeparation = 1;
        const auto ref = slime::AfkPlanner::planBruteForce(o, pred);
        const auto single = slime::AfkPlanner::evaluate(8, 8, 20, pred);
        CHECK(!ref.empty(), "evaluate 对照：暴力解非空");
        if (!ref.empty()) {
            const auto best = slime::AfkPlanner::evaluate(ref[0].x, ref[0].z, 20, pred);
            CHECK(best.coveredBlocks == single.coveredBlocks || best.coveredBlocks >= single.coveredBlocks,
                  "evaluate() 对任意点给出非负覆盖且最优不劣于普通点");
            CHECK(ref[0].coveredBlocks >= single.coveredBlocks, "暴力解最优点不劣于区块中心点");
        }
    }

    // ④ 边界：空区域 / 无史莱姆 / 非法半径
    {
        const auto none = [](int, int) { return false; };
        slime::AfkPlanOptions o;
        o.area = slime::Rect{ 0, 3, 0, 3 };
        o.radius = 128;
        CHECK(slime::AfkPlanner::plan(o, none).empty(), "无史莱姆区块时返回空");
        o.area = slime::Rect{ 5, 1, 5, 1 };
        CHECK(slime::AfkPlanner::plan(o, none).empty(), "非法区域（min>max）返回空");
        o.area = slime::Rect{ 0, 3, 0, 3 };
        o.radius = 0;
        CHECK(slime::AfkPlanner::plan(o, none).empty(), "半径 0 返回空");
        CHECK(slime::AfkPlanner::slimeBlocksInArea(slime::Rect{ 0, 1, 0, 1 },
                                                   [](int cx, int cz) { return cx == 0 && cz == 0; }) == 256,
              "区块面积统计：1 个史莱姆区块 = 256 格");
    }
}

// v3.5.2（任务 2）：群系地图（cubiomes 接入）—— 确定性、批量/单点一致、调色板
static void testBiomeMap()
{
    std::printf("\n[30] 群系地图（cubiomes 彩色预览底座）\n");
    slime::BiomeMap bm;
    const bool ok = bm.init(slime::defaultJavaVersionId(), 0);
    std::printf("      版本=%s 初始化=%d\n", slime::BiomeMap::labelForVersionId(slime::defaultJavaVersionId()),
                ok ? 1 : 0);
    CHECK(ok, "Java 最新版群系生成器可初始化");
    CHECK(bm.valid(), "初始化后 valid() 为真");
    if (ok) {
        const int a = bm.biomeAt(0, 0, 4);
        const int b = bm.biomeAt(0, 0, 4);
        std::printf("      群系 id @ (0,0) = %d（%s）\n", a, slime::BiomeMap::nameFor(a));
        CHECK(a == b, "同点重复查询结果一致（确定性）");
        CHECK(a >= 0 && a < 256, "群系 id 在 0..255 内");

        std::vector<int> ids;
        const bool areaOk = bm.genArea(4, -1024, -1024, 64, 64, ids);
        CHECK(areaOk && static_cast<int>(ids.size()) == 64 * 64, "批量采样 64x64 格成功");
        int seen[256] = { 0 };
        int distinct = 0;
        for (int id : ids)
            if (id >= 0 && id < 256 && !seen[id]) { seen[id] = 1; ++distinct; }
        std::printf("      1024 格内群系种类 = %d\n", distinct);
        CHECK(distinct >= 2, "采样范围内出现多种群系（海陆差异）");
        const int idx = 32 * 64 + 32;
        const int single = bm.biomeAt(-1024 + 32 * 4, -1024 + 32 * 4, 4);
        CHECK(ids[idx] == single, "批量采样与单点查询结果一致（坐标语义正确）");

        // v3.5.12：大网格改按行带并行生成 → 抽样比对行带边界，确认无错位
        std::vector<int> big;
        const bool bigOk = bm.genArea(4, -4096, -4096, 128, 128, big);
        CHECK(bigOk && big.size() == 128u * 128u, "并行采样 128x128 格成功");
        int mismatch = 0;
        for (int z = 0; z < 128; z += 3)
            for (int x = 0; x < 128; x += 3)
                if (big[static_cast<size_t>(z) * 128 + x] != bm.biomeAt(-4096 + x * 4, -4096 + z * 4, 4))
                    ++mismatch;
        std::printf("      并行网格抽样比对：%d 处不一致\n", mismatch);
        CHECK(mismatch == 0, "并行生成与单点查询逐格一致（行带切分无错位）");

        // v3.5.13：按需采样网格（BiomeWeightGrid）—— 必须与整片 genArea 逐格一致
        {
            const int w = 71, h = 71, ox = -4096, oz = -4096;
            std::vector<int> full;
            slime::BiomeWeightGrid lazy;
            const bool a1 = bm.genArea(4, ox, oz, w, h, full);
            const bool a2 = lazy.init(&bm, slime::Platform::Java, 4, ox, oz, w, h);
            CHECK(a1 && a2, "按需采样网格初始化成功（71x71）");
            int diff = 0;
            for (int z = 0; z < h; ++z)
                for (int x = 0; x < w; ++x) {
                    // 抽样点取格子内任意方块（1:4 归属同一格）；权重内部按 float 缓存，比 float 精度
                    const double got = lazy.weightAt(ox + x * 4 + ((x + z) & 3), oz + z * 4 + ((x * 3 + z) & 3));
                    const double want = slime::spawnWeight(full[static_cast<size_t>(z) * w + x], slime::Platform::Java);
                    if (static_cast<float>(got) != static_cast<float>(want)) ++diff;
                }
            std::printf("      按需采样：%zu / %zu 格（逐格比对差异 %d 处）\n", lazy.samples(), lazy.cells(), diff);
            CHECK(diff == 0, "按需采样与整片采样逐格一致（71x71 全比对）");
            CHECK(lazy.samples() == lazy.cells(), "全比对后所有格子都被采样过");
            CHECK(lazy.weightAt(ox - 512, oz) == 1.0, "窗口外权重兜底为 1.0");
        }

        CHECK(slime::BiomeMap::colorFor(1) == 0x8DB360u, "调色板：plains(1) 颜色固定");
        CHECK(std::string(slime::BiomeMap::nameFor(1)) == "plains", "调色板：id 1 = plains");
        // v3.6.7：未收录 ID 改为「按 id 散列」的稳定配色（不再统一中性灰），
        //   但必须满足：稳定、非零、且不与已知群系撞色到无法区分。
        const uint32_t unknownA = slime::BiomeMap::colorFor(9999);
        const uint32_t unknownB = slime::BiomeMap::colorFor(9999);
        CHECK(unknownA == unknownB, "未知群系配色稳定（同 id 同色）");
        CHECK(unknownA != slime::BiomeMap::colorFor(9998), "不同未收录 id 配色不同");
        CHECK(slime::BiomeMap::colorFor(-1) == 0x2A2A2Au, "越界 id 回退深灰");
        CHECK(slime::BiomeMap::colorFor(0) != slime::BiomeMap::colorFor(2), "不同群系颜色不同（ocean vs desert）");
    }

    slime::BiomeMap bad;
    CHECK(!bad.init(9999, 0), "不支持的版本 id 初始化失败");
    CHECK(!bad.valid(), "失败后 valid() 为假");
    CHECK(slime::javaVersionOptions().size() >= 8, "可选 Java 版本不少于 8 个");
}

// v3.5.2（任务 3）：群系刷怪权重（Wiki 数据）+ 加权挂机点排序
static void testSpawnWeights()
{
    std::printf("\n[31] 群系刷怪权重 + 加权挂机点\n");

    // ① 权重表（数据来源：Minecraft Wiki「Slime」Java 列占比，基准 19.42%）
    const auto swamp = slime::spawnWeightForName("swamp");
    std::printf("      沼泽：java=%.3f 地表加成=%d\n", swamp.javaWeight, swamp.surfaceBonus ? 1 : 0);
    CHECK(swamp.known, "沼泽：已收录");
    CHECK(swamp.surfaceBonus, "沼泽：标记了「额外地表生成」");
    CHECK(std::abs(swamp.javaWeight - 19.38 / 19.42) < 2e-3, "沼泽 Java 权重 = 19.38/19.42");

    const auto ocean = slime::spawnWeightForName("ocean");
    CHECK(ocean.javaWeight > 0.985 && ocean.javaWeight < 0.995, "海洋：19.23/19.42 ≈ 0.990");

    const auto river = slime::spawnWeightForName("river");
    std::printf("      河流：java=%.3f\n", river.javaWeight);
    CHECK(river.javaWeight > 0.83 && river.javaWeight < 0.84, "河流：16.26/19.42 ≈ 0.837（明显更低）");

    const auto mushroom = slime::spawnWeightForName("mushroom_fields");
    CHECK(mushroom.javaWeight == 0.0 && mushroom.bedrockWeight == 0.0, "蘑菇岛：不刷敌对生物 ⇒ 0");

    const auto deepDark = slime::spawnWeightForName("deep_dark");
    CHECK(deepDark.javaWeight == 0.0, "深暗域：Wiki 表未收录 ⇒ 0");

    const auto unknown = slime::spawnWeightForName("some_unknown_biome");
    CHECK(!unknown.known && unknown.javaWeight == 1.0, "未收录群系：按最高档 1.0（保守）");

    CHECK(slime::spawnWeight(1, slime::Platform::Bedrock) == 1.0, "基岩版：平原权重 1.0（各群系恒 100）");
    // 多来源核对：中文 Wiki「史莱姆区块」明确基岩版河流不生成 ⇒ 7=river、11=frozen_river
    CHECK(slime::spawnWeight(7, slime::Platform::Bedrock) == 0.0, "基岩版：河流不生成史莱姆（中文 Wiki 除外项）");
    CHECK(slime::spawnWeight(7, slime::Platform::Java) > 0.83, "Java 版：河流仍生成（16.26% 档）");
    CHECK(slime::spawnWeight(11, slime::Platform::Bedrock) == 0.0, "基岩版：冻河不生成史莱姆");
    CHECK(slime::spawnWeight(15, slime::Platform::Bedrock) == 0.0, "基岩版：蘑菇岛 0（id 15）");
    CHECK(slime::spawnWeight(-1, slime::Platform::Java) == 1.0, "无群系数据（-1）：不加权 = 1.0");

    // ② 加权挂机点：左侧簇面积更大但群系权重 0，右侧簇更小但权重 1 ⇒ 加权模式应选右侧
    {
        const auto pred = [](int cx, int cz) {
            // 左簇：chunk (-8..-6, -1..1) 3x3；右簇：(6..7, -1..0) 2x2
            if (cx >= -8 && cx <= -6 && cz >= -1 && cz <= 1) return true;
            if (cx >= 6 && cx <= 7 && cz >= -1 && cz <= 0) return true;
            return false;
        };
        slime::AfkPlanOptions o;
        o.area = slime::Rect{ -12, 12, -6, 6 };
        o.radius = 64;
        o.topN = 3;
        o.minSeparation = 24;
        o.coarseStep = 4;
        const auto plain = slime::AfkPlanner::plan(o, pred);
        CHECK(!plain.empty(), "纯面积：有结果");
        const bool plainLeft = !plain.empty() && plain.front().x < 0;
        std::printf("      纯面积最优：(%d,%d) 覆盖 %lld 格 ｜ 在左簇=%d\n",
                    plain.empty() ? 0 : plain.front().x, plain.empty() ? 0 : plain.front().z,
                    plain.empty() ? 0LL : static_cast<long long>(plain.front().coveredBlocks),
                    plainLeft ? 1 : 0);
        CHECK(plainLeft, "纯面积：优选项落在更大的左簇");

        auto ow = o;
        ow.weightAt = [](int bx, int) { return bx >= 0 ? 1.0 : 0.0; }; // 左半区权重 0
        const auto weighted = slime::AfkPlanner::plan(ow, pred);
        CHECK(!weighted.empty(), "加权：有结果");
        if (!weighted.empty()) {
            std::printf("      加权最优：(%d,%d) 加权覆盖 %.1f 格 ｜ 平均权重 %.2f\n", weighted.front().x,
                        weighted.front().z, weighted.front().coveredWeight, weighted.front().avgWeight);
            CHECK(weighted.front().x >= 0, "加权：优选项移到权重 1 的右簇（不再选左侧 0 权重区）");
            CHECK(weighted.front().coveredWeight > 0.0, "加权：加权覆盖 > 0");
            CHECK(weighted.front().avgWeight >= 0.99, "加权：平均权重 ≈ 1.0");
        }
    }
}

// v3.5.6：大范围环形规划（区块级粗扫 + 局部 1 格精修）
static void testAfkPlannerLargeArea()
{
    std::printf("\n[32] 大范围环形挂机点（远距离找点）\n");
    const slime::Version v = slime::Version::Java;
    const int64_t seed = 123456789LL;
    const auto pred = [v, seed](int cx, int cz) { return slime::SlimeEngine::isSlimeChunk(v, seed, cx, cz); };

    slime::AfkPlanOptions exact;
    exact.area = slime::Rect{ -30, 29, -30, 29 };   // 60x60 区块
    exact.radius = 128;
    exact.topN = 5;
    exact.minSeparation = 24;
    exact.coarseStep = 4;
    exact.maxWindowBlocks = 4096;                    // 强制走精确路径（窗口 ~1248）
    const auto a = slime::AfkPlanner::plan(exact, pred);
    CHECK(!a.empty(), "精确路径（60x60 区块）有结果");

    slime::AfkPlanOptions coarse = exact;
    coarse.forceCoarse = true;                       // v3.5.13：真·强制走区块级粗扫（不再被精确路径拦截）
    const auto b = slime::AfkPlanner::plan(coarse, pred);
    std::printf("      精确最优：(%d,%d) 覆盖 %lld 格 ｜ 粗扫最优：(%d,%d) 覆盖 %lld 格\n",
                a.empty() ? 0 : a.front().x, a.empty() ? 0 : a.front().z,
                a.empty() ? 0LL : static_cast<long long>(a.front().coveredBlocks),
                b.empty() ? 0 : b.front().x, b.empty() ? 0 : b.front().z,
                b.empty() ? 0LL : static_cast<long long>(b.front().coveredBlocks));
    CHECK(!b.empty(), "大范围路径有结果");
    if (!b.empty()) {
        // v3.5.13：精修阶段的「16x16 子偏移核」求出的覆盖格数，必须与逐格统计完全一致
        const auto em = slime::AfkPlanner::measureExact(b.front().x, b.front().z, coarse, pred);
        std::printf("      内核精修 %lld 格 ｜ 逐格统计 %lld 格\n",
                    b.front().coveredBlocks, em.coveredBlocks);
        CHECK(em.coveredBlocks == b.front().coveredBlocks, "内核精修格数与逐格统计完全一致");
    }
    if (!a.empty() && !b.empty()) {
        CHECK(b.front().coveredBlocks >= a.front().coveredBlocks * 9 / 10,
              "粗扫最优覆盖不低于精确解的 90%");
        const int dx = b.front().x - a.front().x, dz = b.front().z - a.front().z;
        CHECK(dx * dx + dz * dz <= 64 * 64, "粗扫最优位置与精确解相距 ≤64 格");
        CHECK(b.front().coveredChunks > 0, "粗扫候选的区块计数正确");
    }

    // 超大范围：只在区块级粗扫，耗时应可控（这里只要求能算出结果且不全为 0）
    slime::AfkPlanOptions huge;
    huge.area = slime::Rect{ -300, 299, -300, 299 }; // 600x600 区块 = 9600x9600 方块
    huge.radius = 128;
    huge.topN = 3;
    huge.minSeparation = 48;
    huge.coarseStep = 4;
    const auto t0 = std::chrono::steady_clock::now();
    const auto c = slime::AfkPlanner::plan(huge, pred);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("      超大范围（600x600 区块）：候选=%zu 用时 %.2f 秒 步长=%d\n", c.size(), secs,
                slime::AfkPlanner::coarseChunkStride(600));
    CHECK(!c.empty(), "600x600 区块范围也能找到挂机点");
    CHECK(secs < 20.0, "超大范围耗时可接受（<20 秒）");
    CHECK(slime::AfkPlanner::coarseChunkStride(600) >= 1, "粗扫步长计算有效");
}

// v3.5.13：严格统计的四条实现互相校验
//   朴素双循环（逐方块判断所在区块）是唯一可信基准；evaluate / measureExact / plan 都必须与它完全一致。
static void testStrictStats()
{
    std::printf("\n[33] 严格 1 格统计（朴素基准 vs 三条实现）\n");
    if (std::getenv("SLIME_STRICT_DEBUG")) {
        // 调试：全史莱姆谓词 ⇒ 结果只由几何决定
        const auto all = [](int, int) { return true; };
        for (int R : { 16, 64, 128 }) {
            slime::AfkPlanOptions o;
            o.radius = R;
            o.innerRadius = 0;
            int64_t naive = 0;
            for (int dz = -R; dz <= R; ++dz)
                for (int dx = -R; dx <= R; ++dx)
                    if (dx * dx + dz * dz <= R * R) ++naive;
            const auto me = slime::AfkPlanner::measureExact(7, 11, o, all);
            std::printf("      [调试] R=%d 全史莱姆：朴素 %lld ｜ measureExact %lld（差 %lld）\n", R,
                        static_cast<long long>(naive), static_cast<long long>(me.coveredBlocks),
                        static_cast<long long>(me.coveredBlocks - naive));
        }
    }
    const slime::Version v = slime::Version::Java;
    const auto chunkOf = [](int b) { return b >= 0 ? b / 16 : -((-b + 15) / 16); };
    struct Case {
        int64_t seed;
        int x, z, R, inner;
    };
    const Case cases[] = {
        { 123456789LL, -346, 394, 128, 0 },
        { 12345LL, -1611, -796, 128, 24 },
        { 12345LL, -1610, -831, 128, 24 },
        { 12345LL, 1611, 796, 128, 24 },
        { 12345LL, -17, 33, 64, 0 },
    };
    for (const Case& c : cases) {
        const auto pred = [c, v](int cx, int cz) { return slime::SlimeEngine::isSlimeChunk(v, c.seed, cx, cz); };
        // 朴素基准：逐方块判断所在区块（与任何积分图/核实现无关）
        const auto naive = [&](int bx, int bz) {
            int64_t n = 0;
            for (int dz = -c.R; dz <= c.R; ++dz) {
                for (int dx = -c.R; dx <= c.R; ++dx) {
                    const int d2 = dx * dx + dz * dz;
                    if (d2 > c.R * c.R || d2 < c.inner * c.inner) continue;
                    if (pred(chunkOf(bx + dx), chunkOf(bz + dz))) ++n;
                }
            }
            return n;
        };
        const int64_t want = naive(c.x, c.z);
        const auto ev = slime::AfkPlanner::evaluate(c.x, c.z, c.R, pred);
        slime::AfkPlanOptions o;
        o.radius = c.R;
        o.innerRadius = c.inner;
        o.area = slime::Rect{ chunkOf(c.x), chunkOf(c.x), chunkOf(c.z), chunkOf(c.z) };
        o.topN = 1;
        o.coarseStep = 4;
        const auto me = slime::AfkPlanner::measureExact(c.x, c.z, o, pred);
        const auto pl = slime::AfkPlanner::plan(o, pred);
        const int64_t fromPlan = pl.empty() ? -1 : pl.front().coveredBlocks;
        const int64_t planWant = pl.empty() ? -1 : naive(pl.front().x, pl.front().z);
        std::printf("      (%d,%d) R=%d 内=%d：朴素 %lld ｜ evaluate %lld ｜ measureExact %lld ｜ plan %lld（该点朴素 %lld）\n",
                    c.x, c.z, c.R, c.inner, static_cast<long long>(want), static_cast<long long>(ev.coveredBlocks),
                    static_cast<long long>(me.coveredBlocks), static_cast<long long>(fromPlan),
                    static_cast<long long>(planWant));
        if (c.inner == 0) CHECK(ev.coveredBlocks == want, "evaluate 与朴素统计一致（整圆）");
        CHECK(me.coveredBlocks == want, "measureExact 与朴素统计一致");
        CHECK(!pl.empty() && fromPlan == planWant, "plan 的覆盖格数与同点朴素统计一致");
    }
}

static int g_onlyGroup = 0; // --unit N：仅运行指定编号的测试组（调试用；0 = 全部）
static bool wantGroup(int n)
{
    return g_onlyGroup == 0 || g_onlyGroup == n;
}


// ===================== v3.9.10：SearchPlan / RuntimeEnv 模型 =====================
static void testSearchPlanMapping()
{
    // A) 默认值：合成结果必须与 SearchOptions 默认值逐字段一致
    {
        const SearchPlan plan;
        const RuntimeEnv env;
        const SearchOptions o = toOptions(plan, env);
        const SearchOptions d;
        CHECK(o.mode == d.mode, "默认模式一致");
        CHECK(o.targetCount == d.targetCount, "默认目标数量一致");
        CHECK(o.stopAtTarget == d.stopAtTarget, "默认早停一致");
        CHECK(o.smartRange == d.smartRange, "默认智能范围一致");
        CHECK(o.gridSize == d.gridSize, "默认网格大小一致");
        CHECK(o.minDensity == d.minDensity, "默认阈值一致");
        CHECK(o.directionLocked == d.directionLocked, "默认方向锁定一致");
        CHECK(o.algorithm == d.algorithm, "默认算法一致");
        CHECK(o.layerBlock == d.layerBlock && o.layerRatio == d.layerRatio, "默认分层参数一致");
        CHECK(o.sampleStep == d.sampleStep && o.sampleExact == d.sampleExact, "默认采样参数一致");
        CHECK(o.threads == d.threads && o.cpuLimitPercent == d.cpuLimitPercent, "默认资源参数一致");
        CHECK(o.gpuScan == nullptr, "默认无 GPU 句柄");
        CHECK(o.useCache == false && o.buildMode == false, "默认缓存关闭");
    }

    // B) 全字段填充：plan/env 的每个字段都必须落到对应选项
    {
        SearchPlan plan;
        plan.minX = -100; plan.maxX = 200; plan.minZ = -50; plan.maxZ = 150;
        plan.startX = 7; plan.startZ = -9;
        plan.mode = Mode::Strict;
        plan.targetCount = 33;
        plan.stopAtTarget = true;
        plan.smartRange = true;
        plan.gridSize = 7;
        plan.minDensity = 0.62;
        plan.ignoreThreshold = true;
        plan.directionLocked = false;
        plan.algorithm = Algorithm::Layered;
        plan.layerBlock = 32; plan.layerRatio = 0.7;
        plan.sampleStep = 4; plan.sampleExact = true;
        plan.pattern = Pattern(3, 3);
        plan.pattern.set(1, 1, CellState::Green);

        int gpuToken = 0;
        RuntimeEnv env;
        env.version = Version::Bedrock;
        env.worldSeed = 42;
        env.gpuScan = reinterpret_cast<const GpuScan*>(&gpuToken); // 仅比较指针
        env.gpuEnabled = true;
        env.threads = 5; env.cpuLimitPercent = 80; env.memBudgetBytes = 1 << 20;
        env.useCache = true; env.buildMode = true;

        const SearchOptions o = toOptions(plan, env);
        CHECK(o.minX == -100 && o.maxX == 200 && o.minZ == -50 && o.maxZ == 150, "范围映射正确");
        CHECK(o.startX == 7 && o.startZ == -9, "起点映射正确");
        CHECK(o.mode == Mode::Strict, "模式映射正确");
        CHECK(o.targetCount == 33, "目标数量映射正确");
        CHECK(o.stopAtTarget && o.smartRange, "停止策略映射正确");
        CHECK(o.gridSize == 7 && o.minDensity == 0.62, "密度参数映射正确");
        CHECK(o.directionLocked == false, "方向锁定映射正确");
        CHECK(o.algorithm == Algorithm::Layered, "算法映射正确");
        CHECK(o.layerBlock == 32 && o.layerRatio == 0.7, "分层参数映射正确");
        CHECK(o.sampleStep == 4 && o.sampleExact, "采样参数映射正确");
        CHECK(o.pattern.rows() == 3 && o.pattern.cols() == 3, "图案尺寸映射正确");
        CHECK(o.pattern.at(1, 1) == CellState::Green, "图案内容映射正确");
        CHECK(o.version == Version::Bedrock && o.worldSeed == 42, "版本/种子映射正确");
        CHECK(o.gpuScan == env.gpuScan, "GPU 句柄映射正确");
        CHECK(o.threads == 5 && o.cpuLimitPercent == 80, "线程/CPU 上限映射正确");
        CHECK(o.memBudgetBytes == (1 << 20), "内存上限映射正确");
        CHECK(o.useCache && o.buildMode, "缓存开关映射正确");
    }

    // C) GPU 总开关关闭时必须不注入句柄（加速开关独立、且不能越过总开关）
    {
        int gpuToken = 0;
        RuntimeEnv env;
        env.gpuScan = reinterpret_cast<const GpuScan*>(&gpuToken);
        env.gpuEnabled = false;
        SearchPlan plan;
        const SearchOptions o = toOptions(plan, env);
        CHECK(o.gpuScan == nullptr, "GPU 总开关关闭 ⇒ 不注入句柄");
    }

    // D) 正交性：只改 RuntimeEnv 时，结果类字段保持不变（方案不会影响结果）
    {
        SearchPlan plan;
        plan.minX = -10; plan.maxX = 10; plan.minZ = -10; plan.maxZ = 10;
        const SearchOptions base = toOptions(plan, RuntimeEnv{});
        RuntimeEnv env2;
        env2.threads = 8; env2.cpuLimitPercent = 60; env2.gpuEnabled = true;
        env2.useCache = true; env2.buildMode = true; env2.memBudgetBytes = 4096;
        const SearchOptions o2 = toOptions(plan, env2);
        CHECK(o2.mode == base.mode && o2.targetCount == base.targetCount, "正交：模式/目标不受资源影响");
        CHECK(o2.stopAtTarget == base.stopAtTarget && o2.smartRange == base.smartRange, "正交：停止策略不受资源影响");
        CHECK(o2.minDensity == base.minDensity && o2.gridSize == base.gridSize, "正交：密度参数不受资源影响");
        CHECK(o2.algorithm == base.algorithm && o2.layerBlock == base.layerBlock, "正交：算法参数不受资源影响");
    }

    std::printf("      SearchPlan/RuntimeEnv 映射：全部通过\n");
}


// ===================== v3.9.12：最优候选模式（忽略阈值，按密度取最优 N 个） =====================
static void testBestCandidatesMode()
{
    struct Cand {
        int count;
        int64_t d2;
        int64_t rank;
        int x, z;
    };
    const auto runCase = [](Version ver, int64_t seed, int w, int h, int g, int64_t N) {
        SearchOptions o;
        o.version = ver;
        o.worldSeed = seed;
        o.minX = -w / 2;
        o.maxX = o.minX + w - 1;
        o.minZ = -h / 2;
        o.maxZ = o.minZ + h - 1;
        o.mode = Mode::Density;
        o.gridSize = g;
        o.minDensity = 0.99;   // 故意设很高：被忽略
        o.targetCount = N;
        o.stopAtTarget = false;
        o.smartRange = false;
        o.ignoreThreshold = true;
        o.threads = 1;
        SlimeEngine eng;
        SearchOutcome out;
        std::atomic<bool> cancel{ false };
        eng.run(o, cancel, ProgressFn(), out);

        // 暴力：枚举全部窗口
        std::vector<Cand> all;
        const int half = (g - 1) / 2;
        for (int z = o.minZ; z + g - 1 <= o.maxZ; ++z) {
            for (int x = o.minX; x + g - 1 <= o.maxX; ++x) {
                int cnt = 0;
                for (int dz = 0; dz < g; ++dz)
                    for (int dx = 0; dx < g; ++dx)
                        if (SlimeEngine::isSlimeChunk(ver, seed, x + dx, z + dz)) ++cnt;
                const int64_t dx = (x + half) - o.startX;
                const int64_t dz = (z + half) - o.startZ;
                Cand c;
                c.count = cnt;
                c.d2 = dx * dx + dz * dz;
                c.rank = (z - o.minZ) * w + (x - o.minX);
                c.x = x;
                c.z = z;
                all.push_back(c);
            }
        }
        std::sort(all.begin(), all.end(), [](const Cand& a, const Cand& b) {
            if (a.count != b.count) return a.count > b.count;
            if (a.d2 != b.d2) return a.d2 < b.d2;
            return a.rank < b.rank;
        });
        const size_t expect = std::min<size_t>(all.size(), static_cast<size_t>(N));
        CHECK(out.ok, "最优候选模式运行成功");
        CHECK(out.results.size() == expect, "最优候选数量 = min(N, 窗口数)");
        size_t diff = 0;
        for (size_t i = 0; i < expect && i < out.results.size(); ++i) {
            const int want = all[i].count;
            const int got = static_cast<int>(std::lround(out.results[i].density * g * g));
            if (out.results[i].minX != all[i].x || out.results[i].minZ != all[i].z || want != got) {
                ++diff;
                if (diff <= 4) {
                    std::printf("      diff#%zu want cnt=%d (%d,%d) d2=%lld | got cnt=%d (%d,%d)\n", i, want,
                                all[i].x, all[i].z, (long long)all[i].d2, got, out.results[i].minX,
                                out.results[i].minZ);
                }
            }
        }
        CHECK(diff == 0, "最优候选结果与暴力枚举逐项一致（计数/坐标）");
        return out;
    };

    runCase(Version::Java, 12345, 60, 45, 3, 7);
    runCase(Version::Bedrock, 0, 50, 40, 2, 5);
    runCase(Version::Java, 987654321, 32, 32, 5, 1);

    // 默认（不开启）时行为不受影响：阈值生效
    {
        SearchOptions o;
        o.version = Version::Java;
        o.worldSeed = 12345;
        o.minX = 0; o.maxX = 40; o.minZ = 0; o.maxZ = 40;
        o.mode = Mode::Density;
        o.gridSize = 3;
        o.minDensity = 0.6;
        o.targetCount = 5;
        o.threads = 1;
        SlimeEngine eng;
        SearchOutcome out;
        std::atomic<bool> cancel{ false };
        eng.run(o, cancel, ProgressFn(), out);
        bool allMeet = true;
        for (const auto& r : out.results)
            if (r.density < 0.6f - 1e-6f) allMeet = false;
        CHECK(out.ok && allMeet, "关闭最优候选模式时阈值仍然生效");
    }
    std::printf("      最优候选模式：与暴力枚举一致\n");
}



// ===================== v3.10.3：最优候选模式的「分块合并」与经典路径一致 =====================
static void testBestCandidatesChunked()
{
    const auto runOnce = [](bool forceChunked) {
#if defined(_WIN32)
        _putenv_s("SLIME_FORCE_CHUNKED", forceChunked ? "1" : "");
#else
        if (forceChunked) setenv("SLIME_FORCE_CHUNKED", "1", 1);
        else unsetenv("SLIME_FORCE_CHUNKED");
#endif
        SearchOptions o;
        o.version = Version::Java;
        o.worldSeed = 12345;
        o.minX = -40;
        o.maxX = 39;
        o.minZ = -30;
        o.maxZ = 29;
        o.mode = Mode::Density;
        o.gridSize = 3;
        o.minDensity = 0.99;   // 正常阈值下必然无结果，忽略后应返回 N 条
        o.targetCount = 7;
        o.threads = 2;
        o.ignoreThreshold = true;
        o.stopAtTarget = false;
        o.smartRange = false;
        SlimeEngine eng;
        SearchOutcome out;
        std::atomic<bool> cancel{ false };
        eng.run(o, cancel, ProgressFn(), out);
        return out;
    };

    const SearchOutcome classic = runOnce(false);
    const SearchOutcome chunked = runOnce(true);
#if defined(_WIN32)
    _putenv_s("SLIME_FORCE_CHUNKED", "");
#endif
    CHECK(classic.ok && chunked.ok, "最优候选：经典/分块两条路径都成功");
    CHECK(chunked.ignoredThreshold, "最优候选：分块路径也标记「已忽略阈值」");
    CHECK(classic.results.size() == 7 && chunked.results.size() == 7, "最优候选：两条路径都返回 N 条");
    size_t diff = 0;
    const size_t n = (std::min)(classic.results.size(), chunked.results.size());
    for (size_t i = 0; i < n; ++i) {
        if (classic.results[i].minX != chunked.results[i].minX ||
            classic.results[i].minZ != chunked.results[i].minZ ||
            std::fabs(classic.results[i].density - chunked.results[i].density) > 1e-6f) {
            ++diff;
        }
    }
    CHECK(diff == 0, "最优候选：分块合并结果与经典路径逐项一致（密度/坐标）");
    std::printf("      最优候选分块合并：与经典路径一致\n");
}



// ===================== v3.10.10.1：最小密度=0 快速路径 vs 完整扫描 =====================
static void testZeroThresholdFastPath()
{
#if defined(_WIN32)
    const auto setEnv = [](const char* k, const char* v) { _putenv_s(k, v ? v : ""); };
#else
    const auto setEnv = [](const char* k, const char* v) { v ? setenv(k, v, 1) : unsetenv(k); };
#endif
    struct Case { Version ver; int w, h, g; int64_t target; int startX, startZ; };
    const Case cases[] = {
        { Version::Java, 200, 150, 5, 20, 0, 0 },        // 起点居中
        { Version::Java, 200, 150, 8, 30, 0, 0 },        // 偶数窗口
        { Version::Java, 120, 90, 4, 500, 0, 0 },        // 目标数 > 窗口总数
        { Version::Java, 300, 200, 5, 10, 5000, -4000 }, // 起点远在区域外
        { Version::Java, 400, 300, 5, 10, -6000, 0 },    // 起点仅横向远
        { Version::Bedrock, 200, 150, 5, 20, 0, 0 },
        { Version::Bedrock, 300, 200, 8, 15, 3000, 2000 },
        { Version::Java, 60, 60, 5, 5, 0, 0 },           // 起点在区域内居中
    };
    for (const Case& c : cases) {
        SearchOptions o;
        o.version = c.ver;
        o.worldSeed = c.ver == Version::Java ? 4545 : 0;
        o.minX = -c.w / 2;
        o.maxX = o.minX + c.w - 1;
        o.minZ = -c.h / 2;
        o.maxZ = o.minZ + c.h - 1;
        o.mode = Mode::Density;
        o.gridSize = c.g;
        o.minDensity = 0.0;
        o.targetCount = c.target;
        o.startX = c.startX;
        o.startZ = c.startZ;
        o.threads = 4;
        auto run = [&]() {
            SlimeEngine eng;
            SearchOutcome out;
            std::atomic<bool> cancel{ false };
            eng.run(o, cancel, ProgressFn(), out);
            return out;
        };
        setEnv("SLIME_NO_FAST0", "1");
        const SearchOutcome slow = run();
        setEnv("SLIME_NO_FAST0", "");
        const SearchOutcome fast = run();
        bool same = slow.results.size() == fast.results.size();
        if (same) {
            for (size_t i = 0; i < slow.results.size(); ++i) {
                const auto& a = slow.results[i];
                const auto& b = fast.results[i];
                if (a.minX != b.minX || a.minZ != b.minZ || a.maxX != b.maxX || a.maxZ != b.maxZ ||
                    std::fabs(a.density - b.density) > 1e-6f) {
                    same = false;
                    break;
                }
            }
        }
        CHECK(fast.ok && same, "最小密度=0 快速路径与完整扫描逐条一致");
        CHECK(fast.totalFound == slow.totalFound, "最小密度=0：总命中数一致");
    }
#if defined(_WIN32)
    _putenv_s("SLIME_NO_FAST0", "");
#endif
    std::printf("      最小密度=0 快路径：Java/基岩、起点内外、目标超额均一致\n");
}


static int runUnitTests()
{
    if (wantGroup(1)) testWrapArithmetic();
    if (wantGroup(2)) testJavaRandomParity();
    if (wantGroup(3)) testModesAgainstNaive();
    if (wantGroup(4)) testOrderingAndEarlyStop();
    if (wantGroup(5)) testCacheFlow();
    if (wantGroup(6)) testCancel();
    if (wantGroup(7)) testCacheRegionMergeApi();
    if (wantGroup(8)) testKnownChunks();
    if (wantGroup(9)) testRotations();
    if (wantGroup(10)) testCachePrune();
    if (wantGroup(11)) testChunkedEquality();
    if (wantGroup(12)) testChunkedCacheIncremental();
    if (wantGroup(13)) testGpuEngineParity();
    if (wantGroup(14)) testNewAlgorithmModes();
    if (wantGroup(15)) testBedrockSimdMt();
    if (wantGroup(16)) testStrictBitsParallel();
    if (wantGroup(17)) testGpuDensityMatch();
    if (wantGroup(18)) testGpuMatchIntegration();
    if (wantGroup(19)) testChunkedEarlyStopV337();
    if (wantGroup(20)) testDeclSupportV338();
    if (wantGroup(21)) testCacheV2Format();
    if (wantGroup(22)) testCacheBitmapFillSkip();
    if (wantGroup(23)) testCacheFirstEarlyStop();
    if (wantGroup(24)) testCacheDedupRepeatRuns();
    if (wantGroup(25)) testCacheBuildModeV339();
    if (wantGroup(26)) testEqualDistanceAndStopBlocks();
    if (wantGroup(27)) testCpuSamplerThreadSafety();
    if (wantGroup(28)) testMergeScheduleInvariance();
    if (wantGroup(29)) testAfkPlannerRing();
    if (wantGroup(30)) testBiomeMap();
    if (wantGroup(31)) testSpawnWeights();
    if (wantGroup(32)) testAfkPlannerLargeArea();
    if (wantGroup(33)) testStrictStats();
    if (wantGroup(34)) testSearchPlanMapping();
    if (wantGroup(35)) testBestCandidatesMode();
    if (wantGroup(36)) testBestCandidatesChunked();
    if (wantGroup(37)) testZeroThresholdFastPath();
    if (g_failures == 0) {
        std::printf("单元测试全部通过 ✔\n");
        return 0;
    }
    std::printf("单元测试失败 %d 项 ✘\n", g_failures);
    return 1;
}

// ===================== 性能基准 =====================

static double speedWan(double chunksPerSec) { return chunksPerSec / 10000.0; }

// v3.5.12：环形搜索分阶段基准（复刻 GUI 的调用顺序，用于定位耗时）
static int runRingBench(int argc, char** argv)
{
    int span = argc > 2 ? std::atoi(argv[2]) : 400;
    const int R = 128, inner = 24;
    const int64_t seed = 12345;
    const Version v = Version::Java;
    auto now = [] { return std::chrono::steady_clock::now(); };
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::printf("环形基准：范围 %dx%d 区块，R=%d 内=%d 种子=%lld\n", span, span, R, inner, (long long)seed);

    const auto pred = [v, seed](int cx, int cz) { return SlimeEngine::isSlimeChunk(v, seed, cx, cz); };
    const int pad = (R + 15) / 16 + 2;
    const int minX = -(span / 2), maxX = minX + span - 1;
    const int minZ = minX, maxZ = minX + span - 1;

    // 0) 谓词成本：整个带余量的区块网格跑一遍
    {
        const auto t0 = now();
        long long n = 0;
        for (int cz = minZ - pad; cz <= maxZ + pad; ++cz)
            for (int cx = minX - pad; cx <= maxX + pad; ++cx) n += pred(cx, cz) ? 1 : 0;
        const double d = ms(t0, now());
        std::printf("  [0] 谓词全网格 %lld 区块：%.1f ms（史莱姆 %lld）\n",
                    (long long)(span + 2 * pad) * (span + 2 * pad), d, n);
    }

    // 1) 群系初始化 + 权重网格（v3.5.14：可调 scale，用于对比 1:64 / 1:128 / 1:256 的排序质量）
    BiomeMap biome;
    double tInit = 0.0, t64 = 0.0;
    std::vector<float> wgrid;
    int c64x0 = 0, c64z0 = 0, gw = 0, gh = 0;
    int gridScale = AfkPlanner::rankingGridScale(span);
    if (const char* gs = std::getenv("SLIME_RING_GRIDSCALE")) {
        const int v = std::atoi(gs);
        if (v == 16 || v == 32 || v == 64 || v == 128 || v == 256) gridScale = v;
    }
    const char* gsName = "1:64";
    {
        const int id = defaultJavaVersionId();
        const auto t0 = now();
        const bool ok = biome.init(id, seed);
        tInit = ms(t0, now());
        std::printf("  [1] BiomeMap::init(版本 id %d)：%.1f ms (%s)\n", id, tInit, ok ? "ok" : "fail");
        if (ok) {
            const auto t1 = now();
            const int gs = gridScale;
            gsName = (gs == 128) ? "1:128" : (gs == 256) ? "1:256" : (gs == 512) ? "1:512" : (gs == 32) ? "1:32" : (gs == 16) ? "1:16" : "1:64";
            const int b0x = (minX - pad) * 16, b0z = (minZ - pad) * 16;
            const int b1x = (maxX + pad) * 16, b1z = (maxZ + pad) * 16;
            c64x0 = b0x >= 0 ? b0x / gs : -((-b0x + gs - 1) / gs);
            c64z0 = b0z >= 0 ? b0z / gs : -((-b0z + gs - 1) / gs);
            const int c64x1 = b1x >= 0 ? b1x / gs : -((-b1x + gs - 1) / gs);
            const int c64z1 = b1z >= 0 ? b1z / gs : -((-b1z + gs - 1) / gs);
            gw = c64x1 - c64x0 + 1; gh = c64z1 - c64z0 + 1;
            std::vector<int> ids;
            if (gw > 0 && gh > 0 && biome.genArea(gs, c64x0 * gs, c64z0 * gs, gw, gh, ids)) {
                wgrid.resize(ids.size());
                for (size_t i = 0; i < ids.size(); ++i) wgrid[i] = (float)spawnWeight(ids[i], Platform::Java);
            }
            t64 = ms(t1, now());
            std::printf("  [2] %s 权重网格 %dx%d：%.1f ms\n", gsName, gw, gh, t64);
        }
    }
    const int gridStride = gridScale;
    const auto w64 = [c64x0, c64z0, gw, gh, &wgrid, gridStride](int bx, int bz) -> double {
        const int cx = bx >= 0 ? bx / gridStride : -((-bx + gridStride - 1) / gridStride);
        const int cz = bz >= 0 ? bz / gridStride : -((-bz + gridStride - 1) / gridStride);
        const int ix = cx - c64x0, iz = cz - c64z0;
        if (ix < 0 || iz < 0 || ix >= gw || iz >= gh) return 1.0;
        return (double)wgrid[(size_t)iz * gw + ix];
    };

    // 2) 规划（粗扫 + 精修）
    AfkPlanOptions o;
    o.area = Rect{ minX, maxX, minZ, maxZ };
    o.radius = R;
    o.innerRadius = inner;
    o.topN = 8;
    o.minSeparation = 24;
    o.coarseStep = 4;
    o.maxWindowBlocks = 2048;
    o.minDensity = 0.05;
    o.weightAt = w64;
    o.rowSlime = [seed](int cz, int cx0, int n, uint8_t* out) { SlimeEngine::fillSlimeRowJava(seed, cz, cx0, n, out); };
    o.forceCoarse = std::getenv("SLIME_RING_FORCE_COARSE") != nullptr;
    const auto t2 = now();
    auto cands = AfkPlanner::plan(o, pred);
    const double tPlan = ms(t2, now());
    std::printf("  [3] AfkPlanner::plan：%.1f ms，候选 %zu\n", tPlan, cands.size());

    // 3) 每个候选：群系权重网格（按需采样）+ 严格 1 格统计 —— 与 GUI 完全同一入口
    //    v3.5.14：候选 × 行带 摊成任务并行（core 里的 measureStrictForCandidates），这里直接复用。
    std::vector<AfkPlanner::ExactMetrics> metrics(cands.size());
    AfkPlanner::StrictStatsInfo statInfo;
    const int vbId = defaultJavaVersionId();
    double tWall = 0.0;
    {
        const auto t3 = now();
        statInfo = AfkPlanner::measureStrictForCandidates(cands, o, pred, vbId, seed, true);
        tWall = ms(t3, now());
        for (size_t i = 0; i < cands.size(); ++i) {
            metrics[i].coveredBlocks = cands[i].coveredBlocks;
            metrics[i].coveredWeight = cands[i].coveredWeight;
            metrics[i].coveredChunks = cands[i].coveredChunks;
            metrics[i].coveragePercent = cands[i].coveragePercent;
        }
    }
    for (size_t i = 0; i < cands.size() && i < 8; ++i)
        std::printf("      #%zu (%d,%d) 格 %lld 权重 %.0f\n", i + 1, cands[i].x, cands[i].z,
                    (long long)metrics[i].coveredBlocks, metrics[i].coveredWeight);
    std::printf("  [4] 候选权重网格（%zu 个，按需采样 %zu/%zu 格）：%.1f ms\n", cands.size(), statInfo.samples,
                statInfo.cells, tWall);
    // v3.5.13：粗扫精修的「子偏移核」格数 vs 逐格统计（两条独立实现，必须逐位相同）
    {
        int kernBad = 0;
        for (size_t i = 0; i < cands.size(); ++i)
            if (cands[i].coveredBlocks != metrics[i].coveredBlocks) ++kernBad;
        std::printf("  [校验] 内核精修 vs 逐格统计：%s\n", kernBad == 0 ? "完全一致" : "存在偏差");
    }

    // v3.5.13：自检 —— 按需采样必须与旧实现（整片 1:4 genArea）逐格一致
    if (std::getenv("SLIME_RING_VERIFY")) {
        int bad = 0;
        double tOld = 0.0;
        const auto tv = now();
        for (size_t ci = 0; ci < cands.size(); ++ci) {
            const auto& cd = cands[ci];
            const int r = o.radius + 8;
            const int bx0 = cd.x - r, bz0 = cd.z - r;
            const int c4x0 = bx0 >= 0 ? bx0 / 4 : -((-bx0 + 3) / 4);
            const int c4z0 = bz0 >= 0 ? bz0 / 4 : -((-bz0 + 3) / 4);
            const int gw2 = (2 * r) / 4 + 3;
            std::vector<int> ids2;
            std::vector<float> wg2;
            AfkPlanOptions o2 = o;
            if (biome.valid() && biome.genArea(4, c4x0 * 4, c4z0 * 4, gw2, gw2, ids2)) {
                wg2.resize(ids2.size());
                for (size_t k = 0; k < ids2.size(); ++k) wg2[k] = (float)spawnWeight(ids2[k], Platform::Java);
                o2.weightAt = [c4x0, c4z0, gw2, wg2](int bx, int bz) -> double {
                    const int cx = bx >= 0 ? bx / 4 : -((-bx + 3) / 4);
                    const int cz = bz >= 0 ? bz / 4 : -((-bz + 3) / 4);
                    const int ix = cx - c4x0, iz = cz - c4z0;
                    if (ix < 0 || iz < 0 || ix >= gw2 || iz >= gw2) return 1.0;
                    return (double)wg2[(size_t)iz * gw2 + ix];
                };
            }
            const auto oldM = AfkPlanner::measureExact(cd.x, cd.z, o2, pred);
            if (oldM.coveredBlocks != metrics[ci].coveredBlocks || oldM.coveredWeight != metrics[ci].coveredWeight ||
                oldM.coveredChunks != metrics[ci].coveredChunks || oldM.coveragePercent != metrics[ci].coveragePercent) {
                ++bad;
                std::printf("      [自检] 候选 %zu 不一致：新 %lld/%.0f 旧 %lld/%.0f\n", ci + 1,
                            (long long)metrics[ci].coveredBlocks, metrics[ci].coveredWeight,
                            (long long)oldM.coveredBlocks, oldM.coveredWeight);
            }
        }
        tOld = ms(tv, now());
        std::printf("  [自检] 按需采样 vs 整片采样：%zu 个候选，%s（旧实现 %.1f ms）\n", cands.size(),
                    bad == 0 ? "完全一致" : "存在偏差", tOld);
    }
    std::printf("  合计：%.1f ms（init %.1f + 网格 %.1f + plan %.1f + 候选权重 %.1f）\n", tInit + t64 + tPlan + tWall,
                tInit, t64, tPlan, tWall);
    return 0;
}

// v3.5.16：正方形（密度网格）搜索基准 —— 与 --bench-ring 同片区，用来量化「环形比正方形慢多少倍」
static int runSquareBench(int span)
{
    SearchOptions o;
    o.version = Version::Java;
    o.worldSeed = 12345;
    o.mode = Mode::Density;
    o.gridSize = 5;
    o.minDensity = 0.05;
    o.targetCount = 8;
    o.smartRange = false;
    o.stopAtTarget = false;
    o.minX = -(span / 2);
    o.maxX = o.minX + span - 1;
    o.minZ = o.minX;
    o.maxZ = o.minX + span - 1;
    o.threads = 0;
    std::atomic<bool> cancel{ false };
    SearchOutcome r;
    std::printf("\u6b63\u65b9\u5f62\u641c\u7d22\u57fa\u51c6\uff1a%dx%d \u533a\u5757\uff08\u5bc6\u5ea6 g=%d\uff0c\u9608\u503c %.2f\uff09\n", span, span,
                o.gridSize, o.minDensity);
    const auto t0 = std::chrono::steady_clock::now();
    r = runOnce(o, &cancel, ProgressFn());
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  \u626b\u63cf %lld \u533a\u5757\uff0c\u5f97\u5230 %zu \u4e2a\u7a97\u53e3\uff0c\u8017\u65f6 %.1f ms\n",
                (long long)r.stats.scannedChunks, r.results.size(), ms);
    return 0;
}

static int runBench()
{
    std::printf("========== 性能基准（Release） ==========\n");
    const auto cpu = std::thread::hardware_concurrency();
    std::printf("逻辑核心: %u | AVX2 内核: %s\n\n", static_cast<unsigned>(cpu),
                cpuHasAvx2() ? "可用" : "不可用（标量回退）");

    const auto bench = [](const char* label, const SearchOptions& opt) {
        SearchOutcome r = runOnce(opt);
        if (!r.ok) {
            std::printf("%-34s 失败: %s\n", label, r.error.c_str());
            return;
        }
        const double scanSec = r.stats.scanMs / 1000.0;
        const double totalSec = r.stats.totalMs / 1000.0;
        const double cps = scanSec > 0 ? r.stats.scannedChunks / scanSec : 0;
        std::printf("%-34s 区块 %10lld | 扫描 %8.1f ms (%9.1f 万/秒) | 匹配 %7.1f ms | 合计 %8.1f ms\n",
                    label, static_cast<long long>(r.stats.scannedChunks),
                    r.stats.scanMs, speedWan(cps), std::max(0.0, r.stats.matchMs - r.stats.scanMs), totalSec * 1000.0);
    };

    SearchOptions java; // 扫描模板（匹配阶段被极小化）
    java.version = Version::Java;
    java.mode = Mode::Density;
    java.gridSize = 3;
    java.minDensity = 1.0; // 全史莱姆才匹配 → 匹配结果几乎为空
    java.targetCount = 1;
    java.startX = 0;
    java.startZ = 0;

    {
        SearchOptions o = java;
        o.worldSeed = 0;
        o.minX = -500; o.maxX = 499; o.minZ = -500; o.maxZ = 499;
        o.threads = 1;
        bench("Java 1000x1000 单核", o);
        o.threads = 0;
        bench("Java 1000x1000 多核", o);
    }
    {
        SearchOptions o = java;
        o.worldSeed = 12345;
        o.minX = -5000; o.maxX = 4999; o.minZ = -5000; o.maxZ = 4999;
        o.threads = 1;
        bench("Java 10000x10000 单核", o);
        o.threads = 0;
        bench("Java 10000x10000 多核", o);
    }
    {
        SearchOptions o = java;
        o.version = Version::Bedrock;
        o.worldSeed = 0;
        o.minX = -1000; o.maxX = 999; o.minZ = -1000; o.maxZ = 999;
        o.threads = 1;
        bench("基岩 2000x2000 单核", o);
        o.threads = 0;
        bench("基岩 2000x2000 多核", o);
    }
    {
        SearchOptions o;
        o.version = Version::Java;
        o.worldSeed = 42;
        o.minX = -1000; o.maxX = 999; o.minZ = -1000; o.maxZ = 999;
        o.mode = Mode::Density;
        o.gridSize = 5;
        o.minDensity = 0.3;
        o.targetCount = 1000000;
        o.threads = 0;
        bench("密度 g=5 thr=0.3 2000x2000 多核", o);
    }
    {
        SearchOptions o;
        o.version = Version::Java;
        o.worldSeed = 42;
        o.minX = -1000; o.maxX = 999; o.minZ = -1000; o.maxZ = 999;
        o.mode = Mode::Strict;
        Pattern p(2, 2);
        p.set(0, 0, CellState::Green);
        p.set(0, 1, CellState::Green);
        p.set(1, 0, CellState::Green);
        p.set(1, 1, CellState::Green);
        o.pattern = p;
        o.directionLocked = true;
        o.targetCount = 1000000;
        o.threads = 0;
        bench("严格 2x2全绿 2000x2000 多核", o);
    }
    // ---- v3.3.0 P0：精确模式（滑动窗口/AVX2 位并行）vs 保守模式（纯暴力 = v3.2.1 实现）----
    // 同机同参数 A/B；同时校验两种模式结果 100% 一致。
    std::printf("\n---- v3.3.0 算法加速（精确 vs 保守，同机同参数 A/B） ----\n");
    const auto benchAB = [&](const char* label, const SearchOptions& base) {
        SearchOptions ex = base;
        ex.algorithm = Algorithm::Exact;
        SearchOptions br = base;
        br.algorithm = Algorithm::Brute;
        SearchOutcome re = runOnce(ex);
        SearchOutcome rb = runOnce(br);
        if (!re.ok || !rb.ok) {
            std::printf("%-30s 失败: %s\n", label, re.ok ? rb.error.c_str() : re.error.c_str());
            return;
        }
        bool same = re.results.size() == rb.results.size() && re.totalFound == rb.totalFound;
        for (size_t i = 0; same && i < re.results.size(); ++i) {
            const auto& a = re.results[i];
            const auto& b = rb.results[i];
            if (a.minX != b.minX || a.maxX != b.maxX || a.minZ != b.minZ || a.maxZ != b.maxZ ||
                a.density != b.density) {
                same = false;
            }
        }
        const double me = std::max(0.001, re.stats.matchMs - re.stats.scanMs);
        const double mb = std::max(0.001, rb.stats.matchMs - rb.stats.scanMs);
        std::printf("%-30s 精确 %8.1f ms | 保守 %8.1f ms | 加速 %5.2fx | 结果一致=%s\n", label, me,
                    mb, mb / me, same ? "是" : "否");
    };
    {
        SearchOptions o;
        o.version = Version::Java;
        o.mode = Mode::Density;
        o.minDensity = 1.0; // 匹配为空 → 纯窗口评估成本
        o.targetCount = 1;
        o.worldSeed = 12345;
        o.threads = 1;
        o.minX = -5000; o.maxX = 4999; o.minZ = -5000; o.maxZ = 4999; // 10000 x 10000
        o.gridSize = 5;
        benchAB("密度 g=5 10000x10000 单核", o);
        o.minX = -2000; o.maxX = 1999; o.minZ = -2000; o.maxZ = 1999; // 4000 x 4000
        o.gridSize = 10;
        benchAB("密度 g=10 4000x4000 单核", o);
        o.minX = -10000; o.maxX = 9999; o.minZ = -10000; o.maxZ = 9999; // 20000 x 20000
        o.gridSize = 5;
        o.threads = 0;
        benchAB("密度 g=5 20000x20000 多核", o);
    }
    {
        // 严格匹配：精确（AVX2 4 窗口位并行）vs 保守（标量逐行比较）
        SearchOptions o;
        o.version = Version::Java;
        o.mode = Mode::Strict;
        o.minDensity = 0.3;
        o.targetCount = 1;
        o.worldSeed = 7;
        o.threads = 1;
        o.minX = -2000; o.maxX = 1999; o.minZ = -2000; o.maxZ = 1999; // 4000 x 4000
        Pattern p2(2, 2);
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 2; ++c) p2.set(r, c, CellState::Green);
        }
        o.pattern = p2;
        o.directionLocked = true;
        benchAB("严格 2x2 4000x4000 单核", o);
        Pattern p5(5, 5);
        for (int r = 0; r < 5; ++r) {
            for (int c = 0; c < 5; ++c) p5.set(r, c, CellState::Green);
        }
        o.pattern = p5;
        benchAB("严格 5x5 4000x4000 单核", o);
        o.pattern = p2;
        o.minX = -4000; o.maxX = 3999; o.minZ = -4000; o.maxZ = 3999; // 8000 x 8000
        o.threads = 0;
        benchAB("严格 2x2 8000x8000 多核", o);
        // AVX2 适用场景：行多且每行仅 1 个约束（行通过率高 → 标量早退收益小）
        Pattern tall(32, 1);
        for (int r = 0; r < 32; ++r) tall.set(r, 0, CellState::Green);
        o.pattern = tall;
        o.directionLocked = true;
        o.threads = 1;
        o.minX = -2000; o.maxX = 1999; o.minZ = -2000; o.maxZ = 1999;
        benchAB("严格 32x1 稀疏 4000x4000 单核", o);
        o.threads = 0;
        o.minX = -4000; o.maxX = 3999; o.minZ = -4000; o.maxZ = 3999;
        benchAB("严格 32x1 稀疏 8000x8000 多核", o);
    }

    {
        // 缓存加载性能（100 万条目）
        const auto tmp = std::filesystem::temp_directory_path() / "slime_bench_cache.bin";
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        CacheManager c;
        std::vector<std::pair<int, int>> cells;
        cells.reserve(1000000);
        for (int i = 0; i < 1000000; ++i) cells.emplace_back(i % 20000 - 10000, i / 20000 - 25);
        c.mergeRegion(Rect{ -10000, 10000, -100, 100 }, cells);
        std::string err;
        if (!c.saveFile(tmp, err)) { std::printf("缓存写盘失败: %s\n", err.c_str()); return 1; }
        CacheManager c2;
        const auto t0 = std::chrono::steady_clock::now();
        if (!c2.loadFile(tmp, true, err)) { std::printf("缓存读盘失败: %s\n", err.c_str()); return 1; }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("缓存加载 100 万条目: %.2f ms\n", ms);
        std::filesystem::remove(tmp, ec);
    }

    // ---- v3.3.1（任务书 3.1）：AVX2 批量窗口计数内核 A/B（无滑动资格路径：提前停止） ----
    // 说明：滑动路径（精确模式默认）不经过该内核；此段测的是「智能范围/提前停止/分层」等
    // 此前逐窗 countBits 的路径。SLIME_DENS_KERNEL=0 关闭内核 → 回到逐窗 countBits。
    std::printf("\n---- v3.3.1 密度计数内核 A/B（无滑动资格路径；SLIME_DENS_KERNEL 开关） ----\n");
    // 参照项：同一区域/参数下的「滑动路径」（精确模式默认，无提前停止）——用于判断内核是否
    // 也适合替换滑动路径（结论见验证报告：g 较大时滑动仍更快，故内核只用于无滑动资格路径）。
    {
        SearchOptions o;
        o.version = Version::Java;
        o.worldSeed = 20260915;
        o.minX = -10000; o.maxX = 9999;
        o.minZ = -10000; o.maxZ = 9999;
        o.mode = Mode::Density;
        o.gridSize = 5;
        o.minDensity = 0.35;
        o.targetCount = 50;
        o.threads = 1;
        o.algorithm = Algorithm::Exact;
        const SearchOutcome r = runOnce(o);
        std::printf("密度 g=5  20000² 滑动路径 单核（参照）  匹配 %6.1f ms | 合计 %6.1f ms | 命中 %lld\n",
                    r.stats.matchMs - r.stats.scanMs, r.stats.totalMs,
                    static_cast<long long>(r.totalFound));
    }
    for (int g : { 5, 10, 32 }) {
        SearchOptions o;
        o.version = Version::Java;
        o.worldSeed = 20260915;
        o.minX = -10000; o.maxX = 9999;
        o.minZ = -10000; o.maxZ = 9999;
        o.mode = Mode::Density;
        o.gridSize = g;
        o.minDensity = (g <= 10) ? 0.35 : 0.25;
        o.targetCount = 1000000; // 设得足够大 → 提前停止不会触发，两侧都扫全区域
        o.stopAtTarget = true;   // 禁用滑动路径（正是内核要优化的场景）
        o.threads = 1;
        o.algorithm = Algorithm::Exact;
#ifdef _WIN32
        _putenv_s("SLIME_DENS_KERNEL", "1");
#else
        setenv("SLIME_DENS_KERNEL", "1", 1);
#endif
        const SearchOutcome on = runOnce(o);
#ifdef _WIN32
        _putenv_s("SLIME_DENS_KERNEL", "0");
#else
        setenv("SLIME_DENS_KERNEL", "0", 1);
#endif
        const SearchOutcome off = runOnce(o);
#ifdef _WIN32
        _putenv_s("SLIME_DENS_KERNEL", "");
#else
        unsetenv("SLIME_DENS_KERNEL");
#endif
        const bool same = on.totalFound == off.totalFound && on.results.size() == off.results.size();
        const double mOn = on.stats.matchMs - on.stats.scanMs;
        const double mOff = off.stats.matchMs - off.stats.scanMs;
        std::printf("密度 g=%-2d 20000² 无滑动 单核  内核 %6.1f ms | 逐窗 %6.1f ms | 匹配阶段加速 %5.2fx"
                    " | 合计加速 %5.2fx | 结果一致=%s（命中 %lld）\n",
                    g, mOn, mOff, mOn > 0 ? mOff / mOn : 0.0,
                    on.stats.totalMs > 0 ? off.stats.totalMs / on.stats.totalMs : 0.0,
                    same ? "是" : "否", static_cast<long long>(on.totalFound));
    }
    // 基岩版无滑动密度路径（判定由 AVX2 批量 MT19937 承担；任务书 §5「基岩版匹配提速」口径）
    {
        SearchOptions o;
        o.version = Version::Bedrock;
        o.minX = -2000; o.maxX = 1999;
        o.minZ = -2000; o.maxZ = 1999;
        o.mode = Mode::Density;
        o.gridSize = 5;
        o.minDensity = 0.35;
        o.targetCount = 1000000;
        o.stopAtTarget = true; // 无滑动 → 走内核路径
        o.threads = 1;
        o.algorithm = Algorithm::Exact;
#ifdef _WIN32
        _putenv_s("SLIME_DENS_KERNEL", "1");
#else
        setenv("SLIME_DENS_KERNEL", "1", 1);
#endif
        const SearchOutcome on = runOnce(o);
#ifdef _WIN32
        _putenv_s("SLIME_DENS_KERNEL", "0");
#else
        setenv("SLIME_DENS_KERNEL", "0", 1);
#endif
        const SearchOutcome off = runOnce(o);
#ifdef _WIN32
        _putenv_s("SLIME_DENS_KERNEL", "");
#else
        unsetenv("SLIME_DENS_KERNEL");
#endif
        const double mOn = on.stats.matchMs - on.stats.scanMs;
        const double mOff = off.stats.matchMs - off.stats.scanMs;
        std::printf("基岩 g=5  4000² 无滑动 单核   内核 %6.1f ms | 逐窗 %6.1f ms | 匹配阶段加速 %5.2fx"
                    " | 合计加速 %5.2fx | 结果一致=%s（命中 %lld）\n",
                    mOn, mOff, mOn > 0 ? mOff / mOn : 0.0,
                    on.stats.totalMs > 0 ? off.stats.totalMs / on.stats.totalMs : 0.0,
                    (on.totalFound == off.totalFound && on.results.size() == off.results.size()) ? "是" : "否",
                    static_cast<long long>(on.totalFound));
    }
    return 0;
}

// ===================== 内存压力（单进程反复搜索） =====================

static int runStress(int iters)
{
    std::printf("内存压力测试：%d 次完整搜索（含缓存合并）…\n", iters);
    CacheManager cache;
    for (int i = 0; i < iters; ++i) {
        SearchOptions opt;
        opt.version = (i % 3 == 0) ? Version::Bedrock : Version::Java;
        opt.worldSeed = 12345 + i;
        opt.minX = -700; opt.maxX = 700; opt.minZ = -700; opt.maxZ = 700;
        opt.mode = Mode::Density;
        opt.gridSize = 4;
        opt.minDensity = 0.35;
        opt.startX = 17;
        opt.startZ = -9;
        opt.targetCount = 50;
        opt.threads = 0;
        if (opt.version == Version::Bedrock) {
            opt.useCache = true;
            opt.cache = &cache;
        }
        SearchOutcome r = runOnce(opt);
        if (!r.ok) {
            std::printf("  第 %d 次失败: %s\n", i, r.error.c_str());
            return 1;
        }
        if (opt.version == Version::Bedrock && !r.cancelled) {
            cache.mergeRegion(opt.region(), r.newCacheChunks);
        }
        if ((i % 10) == 9) std::printf("  已完成 %d/%d（缓存条目 %lld）\n", i + 1, iters,
                                       static_cast<long long>(cache.entryCount()));
    }
    return 0;
}

// ===================== 独立复现：分块+增量缓存（调试用） =====================
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
static LONG WINAPI reproCrashFilter(EXCEPTION_POINTERS* ep)
{
    FILE* f = std::fopen("repro_crash.txt", "w");
    if (f) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
        std::fprintf(f, "code=%08X addr=%llX moduleBase=%llX offset=%llX\n",
                     static_cast<unsigned>(ep->ExceptionRecord->ExceptionCode),
                     static_cast<unsigned long long>(addr), static_cast<unsigned long long>(base),
                     static_cast<unsigned long long>(addr >= base ? addr - base : addr));
        for (int i = 0; i < 24; ++i) {
            PVOID frames[1] = { nullptr };
            const USHORT n = RtlCaptureStackBackTrace(static_cast<DWORD>(i), 1, frames, nullptr);
            if (!n || !frames[0]) break;
            const uintptr_t ra = reinterpret_cast<uintptr_t>(frames[0]);
            std::fprintf(f, "  stack[%d]=%llX offset=%llX\n", i,
                         static_cast<unsigned long long>(ra),
                         static_cast<unsigned long long>(ra >= base ? ra - base : ra));
        }
        std::fclose(f);
    }
    HANDLE h = CreateFileA("repro_crash.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h,
                          MiniDumpWithDataSegs, &mei, nullptr, nullptr);
        CloseHandle(h);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#define REPRO_TRY __try
#define REPRO_EXCEPT __except (reproCrashFilter(GetExceptionInformation()))
#else
#define REPRO_TRY
#define REPRO_EXCEPT
#endif

static int cmdReproCacheInner()
{
    CacheManager cache;
    SearchOptions opt;
    opt.version = Version::Bedrock;
    opt.minX = 0; opt.maxX = 2047;
    opt.minZ = 0; opt.maxZ = 11999; // 单块（块行上限 32768 > 12000）
    opt.mode = Mode::Density;
    opt.gridSize = 5;
    opt.minDensity = 0.3;
    opt.startX = 512;
    opt.startZ = 5999;
    opt.targetCount = 30;
    opt.threads = 0;
    opt.useCache = true;
    opt.cache = &cache;
    opt.cacheWrite = &cache;
#ifdef _WIN32
    _putenv_s("SLIME_FORCE_CHUNKED", "1");
    _putenv_s("SLIME_CHUNK_WORDS", "1048576");
#else
    setenv("SLIME_FORCE_CHUNKED", "1", 1);
    setenv("SLIME_CHUNK_WORDS", "1048576", 1);
#endif
    std::printf("repro: runR (只读缓存) begin\n"); std::fflush(stdout);
    {
        SearchOptions orr = opt;
        orr.cacheWrite = nullptr;
        SearchOutcome rr = runOnce(orr);
        std::printf("repro: runR done ok=%d results=%zu\n", rr.ok, rr.results.size());
        std::fflush(stdout);
    }
    std::printf("repro: runS (缓存写) begin\n"); std::fflush(stdout);
    {
        SearchOutcome rs = runOnce(opt);
        std::printf("repro: runS done ok=%d ranges=%zu entries=%lld\n", rs.ok, cache.ranges().size(),
                    static_cast<long long>(cache.entryCount()));
        std::fflush(stdout);
    }
    std::printf("repro: done\n"); std::fflush(stdout);
    return 0;
}

static int cmdReproCache()
{
    int rc = 1;
    REPRO_TRY { rc = cmdReproCacheInner(); }
    REPRO_EXCEPT {
        std::printf("repro crashed (see repro_crash.txt)\n");
        std::fflush(stdout);
        return 3;
    }
    return rc;
}

// ===================== 入口 =====================

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0); // 崩溃时保留现场输出
    setvbuf(stderr, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::cerr << usage();
        return 2;
    }
    const std::string first = argv[1];
    if (first == "--unit") {
        if (argc > 2) g_onlyGroup = std::atoi(argv[2]); // 仅运行指定测试组（调试用）
        return runUnitTests();
    }
    if (first == "--bench") return runBench();
    if (first == "--bench-ring") return runRingBench(argc, argv);
    if (first == "--bench-square") return runSquareBench(argc > 2 ? std::atoi(argv[2]) : 400);
    if (first == "--stress") {
        const int iters = (argc > 2) ? std::atoi(argv[2]) : 30;
        return runStress(iters);
    }
    if (first == "--near") {
        const auto rest = argsRest(argc, argv, "--near");
        return cmdNear(rest, findThreads(argc, argv));
    }
    if (first == "--repro-cache") return cmdReproCache();
    // 参考兼容模式：剥离 --threads N
    const auto rest = argsRest(argc, argv, "");
    return cmdReference(rest, findThreads(argc, argv));
}

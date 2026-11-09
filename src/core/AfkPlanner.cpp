// v3.5.2（任务 1）：环形覆盖挂机点规划 —— 实现（算法说明见 AfkPlanner.h 头注释）
#include "core/AfkPlanner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <vector>
#include <limits>

#include "core/BiomeMap.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace slime {
namespace {

// v3.5.14：规划器自己的线程数 —— 不再依赖 OpenMP 的 ICV。
//   引擎搜索会调用 omp_set_num_threads()，之后同一线程里所有「匿名并行区」都会沿用那个值；
//   实测自检里跑过一次单线程搜索后，环形搜索的粗扫/精修/抛光会整体退化成单线程（慢 3 倍）。
inline bool slimeProfileEnabled() noexcept
{
    static const bool on = std::getenv("SLIME_PROFILE") != nullptr;
    return on;
}

inline int plannerThreads() noexcept
{
#ifdef _OPENMP
    static const int envThreads = [] {
        if (const char* e = std::getenv("SLIME_PLANNER_THREADS")) {
            const int v = std::atoi(e);
            if (v > 0) return (std::min)(64, v);
        }
        return 0;
    }();
    if (envThreads > 0) return envThreads;
    return (std::max)(1, (std::min)(64, omp_get_num_procs()));
#else
    return 1;
#endif
}

// 权重千分比（1.0 → 1000），全程用整数比较，避免内层循环里成千上万次 lround
constexpr int32_t kWeightScale = 1000;

// 方块坐标 → 区块坐标（向下取整，负数正确）
inline int floorDiv16(int v) noexcept
{
    return v >= 0 ? (v >> 4) : -((-v + 15) >> 4);
}

// 区块矩形 → 方块矩形（含边界）
inline void chunkRectToBlocks(const Rect& r, int& bx0, int& bz0, int& bx1, int& bz1) noexcept
{
    bx0 = r.minX * 16;
    bz0 = r.minZ * 16;
    bx1 = (r.maxX + 1) * 16 - 1;
    bz1 = (r.maxZ + 1) * 16 - 1;
}

// 占用图 + 积分图（方块分辨率）
struct Occupancy {
    int x0 = 0, z0 = 0;      // 窗口左上角（方块坐标）
    int w = 0, h = 0;        // 窗口尺寸（方块）
    std::vector<int32_t> sum; // (w+1)*(h+1) 前缀和；sum[(z)*(w+1)+x]

    inline int32_t at(int x, int z) const noexcept
    {
        // 越界按 0 处理（窗口外没有史莱姆信息）
        const int ix = x - x0 + 1;
        const int iz = z - z0 + 1;
        if (ix < 1 || iz < 1 || ix > w || iz > h) return 0;
        return sum[static_cast<size_t>(iz) * (w + 1) + ix];
    }
    // [ax..bx] × [az..bz] 的和（含边界，自动裁剪到窗口）
    inline int64_t rect(int ax, int az, int bx, int bz) const noexcept
    {
        ax = (std::max)(ax, x0);
        bx = (std::min)(bx, x0 + w - 1);
        az = (std::max)(az, z0);
        bz = (std::min)(bz, z0 + h - 1);
        if (ax > bx || az > bz) return 0;
        // 二维前缀和：S(bx,bz) - S(ax-1,bz) - S(bx,az-1) + S(ax-1,az-1)
        const int32_t d = at(bx, bz);
        const int32_t b = at(ax - 1, bz);
        const int32_t c = at(bx, az - 1);
        const int32_t a = at(ax - 1, az - 1);
        return static_cast<int64_t>(d) - b - c + a;
    }
};

// 加权占用（千分比）+ 前缀和
struct Weighted {
    int x0 = 0, z0 = 0, w = 0, h = 0;
    std::vector<int32_t> sum;
    inline int32_t at(int x, int z) const noexcept
    {
        const int ix = x - x0 + 1;
        const int iz = z - z0 + 1;
        if (ix < 1 || iz < 1 || ix > w || iz > h) return 0;
        return sum[static_cast<size_t>(iz) * (w + 1) + ix];
    }
    inline int64_t rect(int ax, int az, int bx, int bz) const noexcept
    {
        ax = (std::max)(ax, x0);
        bx = (std::min)(bx, x0 + w - 1);
        az = (std::max)(az, z0);
        bz = (std::min)(bz, z0 + h - 1);
        if (ax > bx || az > bz) return 0;
        return static_cast<int64_t>(at(bx, bz)) - at(ax - 1, bz) - at(bx, az - 1) + at(ax - 1, az - 1);
    }
};

// 圆内史莱姆格数：逐行用积分图一次求和（r2 = 半径平方，含 d² == r2 的边界）
int64_t discSumR2(const Occupancy& occ, int cx, int cz, int64_t r2) noexcept
{
    int64_t total = 0;
    if (r2 < 0) return 0;
    const int r = static_cast<int>(std::sqrt(static_cast<double>(r2)));
    for (int dz = -r; dz <= r; ++dz) {
        const int64_t rem = r2 - static_cast<int64_t>(dz) * dz;
        if (rem < 0) continue;
        int w = static_cast<int>(std::sqrt(static_cast<double>(rem)));
        // 修正浮点误差：确保 (w+1)² + dz² > r2
        while (static_cast<int64_t>(w + 1) * (w + 1) + static_cast<int64_t>(dz) * dz <= r2) ++w;
        while (w > 0 && static_cast<int64_t>(w) * w + static_cast<int64_t>(dz) * dz > r2) --w;
        total += occ.rect(cx - w, cz + dz, cx + w, cz + dz);
    }
    return total;
}

// v3.5.13：一次行循环同时给出「格数」和「加权格数」—— 精确路径的粗扫/精修本来就两样都要，
//           以前是各自完整跑一遍行循环（窗口 1024 时约 2×1000 次矩形求和），现在合成一遍。
// v3.6.21：磁盘「每行半宽」只依赖半径，预算成表 ——
//   粗扫对 25 万个候选逐个重算 sqrt（每次约 300 行）曾是环形规划的主要开销。
struct DiskRows {
    int r = 0;
    std::vector<int> w;   // w[dz + r] = 该行半宽
    bool valid() const noexcept { return r > 0 && !w.empty(); }
};

DiskRows makeDiskRowsR2(int64_t r2) noexcept
{
    DiskRows d;
    if (r2 < 0) return d;
    const int r = static_cast<int>(std::sqrt(static_cast<double>(r2)));
    d.r = r;
    d.w.assign(static_cast<size_t>(2 * r + 1), 0);
    for (int dz = -r; dz <= r; ++dz) {
        const int64_t rem = r2 - static_cast<int64_t>(dz) * dz;
        if (rem < 0) continue;
        int w = static_cast<int>(std::sqrt(static_cast<double>(rem)));
        while (static_cast<int64_t>(w + 1) * (w + 1) + static_cast<int64_t>(dz) * dz <= r2) ++w;
        while (w > 0 && static_cast<int64_t>(w) * w + static_cast<int64_t>(dz) * dz > r2) --w;
        d.w[static_cast<size_t>(dz + r)] = w;
    }
    return d;
}

void annulusRows(const Occupancy& occ, const Weighted* wt, int cx, int cz, const DiskRows& outerRows,
                 const DiskRows* innerRows, int64_t& blocks, int64_t& wsum) noexcept
{
    blocks = 0;
    wsum = 0;
    const auto pass = [&](const DiskRows& d, int sign) {
        for (int dz = -d.r; dz <= d.r; ++dz) {
            const int w = d.w[static_cast<size_t>(dz + d.r)];
            const int z = cz + dz;
            blocks += sign * occ.rect(cx - w, z, cx + w, z);
            if (wt) wsum += sign * wt->rect(cx - w, z, cx + w, z);
        }
    };
    pass(outerRows, +1);
    if (innerRows && innerRows->valid()) pass(*innerRows, -1);
}

void annulusBoth(const Occupancy& occ, const Weighted* wt, int cx, int cz, int inner, int outer,
                 int64_t& blocks, int64_t& wsum) noexcept
{
    const DiskRows o = makeDiskRowsR2(static_cast<int64_t>(outer) * outer);
    if (inner > 0) {
        const DiskRows in = makeDiskRowsR2(static_cast<int64_t>(inner) * inner - 1);
        annulusRows(occ, wt, cx, cz, o, &in, blocks, wsum);
    } else {
        annulusRows(occ, wt, cx, cz, o, nullptr, blocks, wsum);
    }
}

// 圆内触及的史莱姆区块数 + 这些区块的几何重心（区块中心求平均）
// v3.5.8：区块矩形与环带 [inner, outer] 相交则计数（并累计重心）
int countChunksInAnnulus(int cx, int cz, int inner, int outer, const SlimeChunkPredicate& isSlime,
                         double* gx, double* gz)
{
    int n = 0;
    double sx = 0.0, sz = 0.0;
    const int c0x = floorDiv16(cx - outer) - 1, c1x = floorDiv16(cx + outer) + 1;
    const int c0z = floorDiv16(cz - outer) - 1, c1z = floorDiv16(cz + outer) + 1;
    const double inner2 = static_cast<double>(inner) * inner;
    const double outer2 = static_cast<double>(outer) * outer;
    for (int gz2 = c0z; gz2 <= c1z; ++gz2) {
        for (int gx2 = c0x; gx2 <= c1x; ++gx2) {
            if (!isSlime(gx2, gz2)) continue;
            // 区块矩形 [x0,x1]×[z0,z1] 到中心的最短距离（0 表示中心在矩形内）
            const double x0 = gx2 * 16.0, x1 = x0 + 16.0, z0 = gz2 * 16.0, z1 = z0 + 16.0;
            const double nx = (std::max)(x0, (std::min)(static_cast<double>(cx), x1));
            const double nz = (std::max)(z0, (std::min)(static_cast<double>(cz), z1));
            const double dx = nx - cx, dz = nz - cz;
            const double dmin2 = dx * dx + dz * dz;
            // 矩形最远角距离
            const double fx = (std::abs(x0 - cx) > std::abs(x1 - cx)) ? x0 : x1;
            const double fz = (std::abs(z0 - cz) > std::abs(z1 - cz)) ? z0 : z1;
            const double fdx = fx - cx, fdz = fz - cz;
            const double dmax2 = fdx * fdx + fdz * fdz;
            if (dmin2 <= outer2 && dmax2 >= inner2) {
                ++n;
                sx += gx2 * 16.0 + 8.0;
                sz += gz2 * 16.0 + 8.0;
            }
        }
    }
    if (gx) *gx = (n > 0) ? sx / n : static_cast<double>(cx);
    if (gz) *gz = (n > 0) ? sz / n : static_cast<double>(cz);
    return n;
}

int countChunksInDisc(int cx, int cz, int r, const SlimeChunkPredicate& isSlime,
                      double* outCentroidX = nullptr, double* outCentroidZ = nullptr) noexcept
{
    const int c0x = floorDiv16(cx - r), c1x = floorDiv16(cx + r);
    const int c0z = floorDiv16(cz - r), c1z = floorDiv16(cz + r);
    const int64_t r2 = static_cast<int64_t>(r) * r;
    int n = 0;
    double sx = 0.0, sz = 0.0;
    for (int chz = c0z; chz <= c1z; ++chz) {
        for (int chx = c0x; chx <= c1x; ++chx) {
            if (!isSlime(chx, chz)) continue;
            const int bx0 = chx * 16, bx1 = bx0 + 15;
            const int bz0 = chz * 16, bz1 = bz0 + 15;
            const int dx = (cx < bx0) ? (bx0 - cx) : (cx > bx1 ? cx - bx1 : 0);
            const int dz = (cz < bz0) ? (bz0 - cz) : (cz > bz1 ? cz - bz1 : 0);
            if (static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dz) * dz <= r2) {
                ++n;
                sx += bx0 + 7.5;
                sz += bz0 + 7.5;
            }
        }
    }
    if (outCentroidX) *outCentroidX = n ? sx / n : static_cast<double>(cx);
    if (outCentroidZ) *outCentroidZ = n ? sz / n : static_cast<double>(cz);
    return n;
}

AfkCandidate makeCandidate(int x, int z, int r, int64_t covered, const SlimeChunkPredicate& isSlime,
                          int inner = 0)
{
    AfkCandidate c;
    c.x = x;
    c.z = z;
    c.coveredBlocks = covered;
    double gx = 0.0, gz = 0.0;
    c.coveredChunks = (inner > 0) ? countChunksInAnnulus(x, z, inner, r, isSlime, &gx, &gz)
                                  : countChunksInDisc(x, z, r, isSlime, &gx, &gz);
    c.centroidX = gx;
    c.centroidZ = gz;
    const double ddx = static_cast<double>(x) - gx;
    const double ddz = static_cast<double>(z) - gz;
    c.centerDist = std::sqrt(ddx * ddx + ddz * ddz);
    c.chunkX = floorDiv16(x);
    c.chunkZ = floorDiv16(z);
    c.onChunkCorner = (x % 16 == 0) && (z % 16 == 0);
    // v3.5.8：环带面积 = π(R² − r²)
    const double disc = 3.14159265358979323846 *
                        (static_cast<double>(r) * r - static_cast<double>(inner) * inner);
    c.coveragePercent = disc > 0.0 ? (static_cast<double>(covered) / disc) * 100.0 : 0.0;
    return c;
}

// v3.5.25：已知「覆盖格数 / 覆盖区块数 / 重心」时直接构造候选 —— 不再重新逐区块调用谓词。
//   refine/抛光阶段每个候选原本要再跑一遍 countChunksInAnnulus（289 次间接调用），
//   120 个候选 ≈ 3.5 万次 → 这是精修阶段真正的大头。
AfkCandidate makeCandidateKnown(int x, int z, int r, int64_t covered, int coveredChunks, double gx, double gz,
                                int inner = 0)
{
    AfkCandidate c;
    c.x = x;
    c.z = z;
    c.coveredBlocks = covered;
    c.coveredChunks = coveredChunks;
    c.centroidX = gx;
    c.centroidZ = gz;
    const double ddx = static_cast<double>(x) - gx;
    const double ddz = static_cast<double>(z) - gz;
    c.centerDist = std::sqrt(ddx * ddx + ddz * ddz);
    c.chunkX = floorDiv16(x);
    c.chunkZ = floorDiv16(z);
    c.onChunkCorner = (x % 16 == 0) && (z % 16 == 0);
    const double disc = 3.14159265358979323846 *
                        (static_cast<double>(r) * r - static_cast<double>(inner) * inner);
    c.coveragePercent = disc > 0.0 ? (static_cast<double>(covered) / disc) * 100.0 : 0.0;
    return c;
}

// 从候选列表里挑选「互不相邻」的前 N 个（按覆盖降序、间距约束）
std::vector<AfkCandidate> pickSpread(std::vector<AfkCandidate>& all, int topN, int minSep)
{
    std::sort(all.begin(), all.end(), [](const AfkCandidate& a, const AfkCandidate& b) {
        // v3.5.7：有加权值时按加权覆盖排（建农场时群系效率更关键），否则按面积
        const double aw = (a.coveredWeight > 0.0) ? a.coveredWeight : static_cast<double>(a.coveredBlocks);
        const double bw = (b.coveredWeight > 0.0) ? b.coveredWeight : static_cast<double>(b.coveredBlocks);
        if (std::abs(aw - bw) > 1e-9) return aw > bw;
        // 覆盖面积打平：优先取更靠近「覆盖区块重心」的点（对称、便于建造，且唯一可解释）
        if (std::abs(a.centerDist - b.centerDist) > 1e-9) return a.centerDist < b.centerDist;
        if (a.x != b.x) return a.x < b.x;
        return a.z < b.z;
    });
    std::vector<AfkCandidate> out;
    const int sep2 = minSep * minSep;
    for (const AfkCandidate& c : all) {
        bool far = true;
        for (const AfkCandidate& k : out) {
            const int dx = c.x - k.x, dz = c.z - k.z;
            if (dx * dx + dz * dz < sep2) { far = false; break; }
        }
        if (!far) continue;
        out.push_back(c);
        if (static_cast<int>(out.size()) >= topN) break;
    }
    return out;
}

// v3.5.14：严格 1 格统计的「行带」版本 —— dz ∈ [dzFrom, dzTo]。
//   各带结果相加 == 整圆/整环的结果（区块计数只在 countChunks 的那一带算一次）。
AfkPlanner::ExactMetrics measureRowsImpl(int blockX, int blockZ, const AfkPlanOptions& opt,
                                         const SlimeChunkPredicate& isSlime, int dzFrom, int dzTo,
                                         bool countChunks)
{
    AfkPlanner::ExactMetrics m;
    if (!isSlime || opt.radius <= 0) return m;
    const int inner = (std::max)(0, opt.innerRadius);
    const int64_t r2 = static_cast<int64_t>(inner) * inner;
    const int64_t R2 = static_cast<int64_t>(opt.radius) * opt.radius;
    const int r = opt.radius;
    dzFrom = (std::max)(dzFrom, -r);
    dzTo = (std::min)(dzTo, r);
    // 逐行：用圆方程求该行的 x 范围，再按「内圆外」跳过中间段
    for (int dz = dzFrom; dz <= dzTo; ++dz) {
        const int64_t remOut = R2 - static_cast<int64_t>(dz) * dz;
        if (remOut < 0) continue;
        int halfOut = static_cast<int>(std::sqrt(static_cast<double>(remOut)));
        while (static_cast<int64_t>(halfOut + 1) * (halfOut + 1) + static_cast<int64_t>(dz) * dz <= R2) ++halfOut;
        while (halfOut > 0 && static_cast<int64_t>(halfOut) * halfOut + static_cast<int64_t>(dz) * dz > R2) --halfOut;
        // 内圆：d² < inner² 的格子要排除 ⇒ 用 (inner²-1) 作为「可排除」的上界
        const int64_t remIn = r2 - 1 - static_cast<int64_t>(dz) * dz;
        int halfIn = -1;
        if (r2 > 0 && remIn >= 0) {
            halfIn = static_cast<int>(std::sqrt(static_cast<double>(remIn)));
            while (static_cast<int64_t>(halfIn + 1) * (halfIn + 1) + static_cast<int64_t>(dz) * dz <= r2 - 1) ++halfIn;
            while (halfIn > 0 && static_cast<int64_t>(halfIn) * halfIn + static_cast<int64_t>(dz) * dz > r2 - 1) --halfIn;
        }
        const int z = blockZ + dz;
        const int chunkZ = z >= 0 ? z / 16 : -((-z + 15) / 16);
        const auto scanSpan = [&](int x0, int x1) {
            if (x0 > x1) return;
            int x = blockX + x0;              // 绝对方块坐标（区块归属必须用绝对值算）
            const int xEnd = blockX + x1;
            while (x <= xEnd) {
                const int chunkX = x >= 0 ? x / 16 : -((-x + 15) / 16);
                const int spanEnd = (std::min)(xEnd, chunkX * 16 + 15);
                if (isSlime(chunkX, chunkZ)) {
                    const int count = spanEnd - x + 1;
                    m.coveredBlocks += count;
                    if (opt.weightAt) {
                        for (int bx = x; bx <= spanEnd; ++bx) {
                            const double wv = opt.weightAt(bx, z);
                            if (wv > 0.0) m.coveredWeight += wv;
                        }
                    } else {
                        m.coveredWeight += count;
                    }
                }
                x = spanEnd + 1;
            }
        };
        scanSpan(-halfOut, -(std::max)(1, halfIn + 1));
        scanSpan((std::max)(0, halfIn + 1), halfOut);
    }
    if (!countChunks) return m;
    // 触及的区块数（用于展示）
    double gx = 0.0, gz = 0.0;
    m.coveredChunks = countChunksInAnnulus(blockX, blockZ, inner, opt.radius, isSlime, &gx, &gz);
    const double area = 3.14159265358979323846 *
                        (static_cast<double>(opt.radius) * opt.radius - static_cast<double>(inner) * inner);
    m.coveragePercent = area > 0.0 ? (static_cast<double>(m.coveredBlocks) / area) * 100.0 : 0.0;
    return m;
}

} // namespace

std::vector<AfkCandidate> AfkPlanner::plan(const AfkPlanOptions& opt, const SlimeChunkPredicate& isSlime)
{
    std::vector<AfkCandidate> none;
    if (!isSlime || !opt.area.valid() || opt.radius <= 0 || opt.topN <= 0) return none;

    int bx0, bz0, bx1, bz1;
    chunkRectToBlocks(opt.area, bx0, bz0, bx1, bz1);
    // v3.5.17：这里原来是 int 乘法 —— 320000x320000 直接溢出成负数，超大范围会静默返回 0 个候选
    if (static_cast<long long>(bx1 - bx0 + 1) * static_cast<long long>(bz1 - bz0 + 1) <= 0) return none;

    // 计算窗口 = 候选范围 + 半径 + 1 区块（圆会越界，窗口外按 0 计）
    const int pad = opt.radius + 16;
    const int w = (bx1 + pad) - (bx0 - pad) + 1;
    const int h = (bz1 + pad) - (bz0 - pad) + 1;
    const bool weightedIn = static_cast<bool>(opt.weightAt);
    // v3.5.13：加权模式一律走「区块级粗扫 + 1 格核精修 + 前 12 名抛光」：
    //   实测（span 12~48 区块）候选位置与精确路径完全相同或更好，而速度快 2~8 倍。
    //   顺带修掉旧逻辑的漏洞：加权模式的块级前缀和窗口上限只有 1024 方块，
    //   窗口 1025~2048 的中等范围（≈46~110 区块见方）以前会静默返回 0 个站位。
    const int exactLimit = weightedIn ? 0 : opt.maxWindowBlocks;
    if (opt.forceCoarse || w <= 0 || h <= 0 || w > exactLimit || h > exactLimit) {
        // v3.5.6：范围很大时改走「区块级粗扫 + 局部精修」（半径仍是固定 R）
        return planChunkCoarse(opt, isSlime);
    }
    return planExactBlockLevel(opt, isSlime);
}

// 块级精确路径：方块分辨率前缀和 + 粗扫 + ±step 精修（窗口上限见下）
std::vector<AfkCandidate> AfkPlanner::planExactBlockLevel(const AfkPlanOptions& opt,
                                                          const SlimeChunkPredicate& isSlime)
{
    std::vector<AfkCandidate> none;
    if (!isSlime || !opt.area.valid() || opt.radius <= 0 || opt.topN <= 0) return none;
    int bx0, bz0, bx1, bz1;
    chunkRectToBlocks(opt.area, bx0, bz0, bx1, bz1);
    if ((bx1 - bx0 + 1) * (bz1 - bz0 + 1) <= 0) return none;

    const int pad = opt.radius + 16;
    const int wx0 = bx0 - pad, wz0 = bz0 - pad;
    const int wx1 = bx1 + pad, wz1 = bz1 + pad;
    const int w = wx1 - wx0 + 1;
    const int h = wz1 - wz0 + 1;
    if (w <= 0 || h <= 0 || w > (std::max)(opt.maxWindowBlocks, 2048) ||
        h > (std::max)(opt.maxWindowBlocks, 2048))
        return none;
    const bool weightedIn = static_cast<bool>(opt.weightAt);

    // 1) 占用图（区块 → 16x16 格）
    std::vector<uint8_t> occv(static_cast<size_t>(w) * h, 0);
    const int c0x = floorDiv16(wx0), c1x = floorDiv16(wx1);
    const int c0z = floorDiv16(wz0), c1z = floorDiv16(wz1);
    for (int chz = c0z; chz <= c1z; ++chz) {
        for (int chx = c0x; chx <= c1x; ++chx) {
            if (!isSlime(chx, chz)) continue;
            const int sx = (std::max)(chx * 16, wx0);
            const int ex = (std::min)(chx * 16 + 15, wx1);
            const int sz = (std::max)(chz * 16, wz0);
            const int ez = (std::min)(chz * 16 + 15, wz1);
            for (int z = sz; z <= ez; ++z) {
                uint8_t* row = occv.data() + static_cast<size_t>(z - wz0) * w + (sx - wx0);
                std::fill(row, row + (ex - sx + 1), static_cast<uint8_t>(1));
            }
        }
    }

    // 2) 二维前缀和
    Occupancy occ;
    occ.x0 = wx0;
    occ.z0 = wz0;
    occ.w = w;
    occ.h = h;
    occ.sum.assign(static_cast<size_t>(w + 1) * (h + 1), 0);
    for (int z = 0; z < h; ++z) {
        int32_t rowAcc = 0;
        const uint8_t* src = occv.data() + static_cast<size_t>(z) * w;
        int32_t* cur = occ.sum.data() + static_cast<size_t>(z + 1) * (w + 1);
        const int32_t* prev = occ.sum.data() + static_cast<size_t>(z) * (w + 1);
        for (int x = 0; x < w; ++x) {
            rowAcc += src[x];
            cur[x + 1] = prev[x + 1] + rowAcc;
        }
    }

    // 2b) v3.5.2（任务 3）：加权模式 —— 再建一张千分比前缀和（窗口上限 1024 方块）
    const bool weighted = weightedIn;
    Weighted wt;
    if (weighted) {
        if (w > 1024 || h > 1024) {
            // 理论上到不了这里（上面的 exactLimit 已把超限窗口交给粗扫路径），保底不乱算
            std::fprintf(stderr, "[环形] 加权模式窗口过大（%dx%d，上限 1024）\n", w, h);
            return none;
        }
        wt.x0 = wx0;
        wt.z0 = wz0;
        wt.w = w;
        wt.h = h;
        wt.sum.assign(static_cast<size_t>(w + 1) * (h + 1), 0);
        for (int z = 0; z < h; ++z) {
            int32_t rowAcc = 0;
            const uint8_t* src = occv.data() + static_cast<size_t>(z) * w;
            int32_t* cur = wt.sum.data() + static_cast<size_t>(z + 1) * (w + 1);
            const int32_t* prev = wt.sum.data() + static_cast<size_t>(z) * (w + 1);
            const int bz = wz0 + z;
            for (int x = 0; x < w; ++x) {
                if (src[x]) {
                    const double wv = opt.weightAt(wx0 + x, bz);
                    const int milli = static_cast<int>(std::lround((std::max)(0.0, (std::min)(4.0, wv)) * 1000.0));
                    rowAcc += milli;
                }
                cur[x + 1] = prev[x + 1] + rowAcc;
            }
        }
    }

    // 3) 粗扫（coarseStep 格）→ 取局部最优若干
    const int step = (std::max)(1, opt.coarseStep);
    const int64_t r2 = static_cast<int64_t>(opt.radius) * opt.radius;
    struct Coarse {
        int x, z;
        int64_t cov;   // 排序用分数：加权模式为「千分比加权格数」，否则为格数
        int64_t blocks; // 未加权格数（展示用）
    };
    std::vector<Coarse> coarse;
    coarse.reserve(static_cast<size_t>((bx1 - bx0) / step + 1) * ((bz1 - bz0) / step + 1));
    // v3.6.21：轮廓表只建一次，粗扫里复用（原来每个候选都重算 sqrt）
    const DiskRows coarseOuterRows = makeDiskRowsR2(static_cast<int64_t>(opt.radius) * opt.radius);
    const DiskRows coarseInnerRows =
        (opt.innerRadius > 0) ? makeDiskRowsR2(static_cast<int64_t>(opt.innerRadius) * opt.innerRadius - 1)
                              : DiskRows{};
    const DiskRows* coarseInnerPtr = (opt.innerRadius > 0 && coarseInnerRows.valid()) ? &coarseInnerRows : nullptr;
    // v3.7.6：中心数够多且非加权时，先尝试 GPU 批量算覆盖（结果与 CPU 逐项一致）；
    //   GPU 不可用 / 失败 / 数据规模太小 ⇒ 保持原有 CPU 路径（不替代 CPU 计算）。
    bool coarseDoneByGpu = false;
    const int64_t anchorCols = static_cast<int64_t>(bx1 - bx0) / step + 1;
    const int64_t anchorRows = static_cast<int64_t>(bz1 - bz0) / step + 1;
    const int64_t anchorCount = anchorCols * anchorRows;
    if (!weighted && opt.gpuScan != nullptr && anchorCount >= 20000) {
        slime::AnnulusCoverageRequest req;
        req.pref2d = occ.sum.data();   // 直接用已建好的二维前缀和（省掉一次 O(w*h) 预处理）
        req.w = w;
        req.h = h;
        req.originX = wx0;
        req.originZ = wz0;
        req.step = step;
        req.nx = static_cast<int>(anchorCols);
        req.nz = static_cast<int>(anchorRows);
        req.firstX = bx0;
        req.firstZ = bz0;
        req.innerRadius = opt.innerRadius;
        req.outerRadius = opt.radius;
        static thread_local std::vector<int> gpuCov;
        std::string gpuErr;
        const auto tGpu0 = std::chrono::steady_clock::now();
        const bool gpuOk = opt.gpuScan->annulusCoverage(req, gpuCov, &gpuErr) &&
                           gpuCov.size() == static_cast<size_t>(anchorCount);
        const double gpuCallMs = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - tGpu0)
                                     .count();
        if (std::getenv("SLIME_DEBUG")) {
            std::fprintf(stderr, "[环形] GPU 覆盖调用耗时 %.2f ms（%lld 中心）\n", gpuCallMs,
                         static_cast<long long>(anchorCount));
        }
        if (!gpuOk && std::getenv("SLIME_DEBUG")) {
            std::fprintf(stderr, "[环形] GPU 覆盖不可用（回退 CPU）：%s\n", gpuErr.c_str());
        }
        if (gpuOk) {
            coarseDoneByGpu = true;
            if (std::getenv("SLIME_DEBUG")) {
                std::fprintf(stderr, "[环形] 粗扫改用 GPU：%lld 个中心（%dx%d）\n",
                             static_cast<long long>(anchorCount), w, h);
            }
            for (int iz = 0; iz < req.nz; ++iz) {
                for (int ix = 0; ix < req.nx; ++ix) {
                    const int64_t blocks = gpuCov[static_cast<size_t>(iz) * req.nx + ix];
                    if (blocks <= 0) continue;
                    coarse.push_back({ bx0 + ix * step, bz0 + iz * step, blocks, blocks });
                }
            }
        }
    }
    if (!coarseDoneByGpu) {
        for (int z = bz0; z <= bz1; z += step) {
            for (int x = bx0; x <= bx1; x += step) {
                int64_t blocks = 0, wsum = 0;
                annulusRows(occ, weighted ? &wt : nullptr, x, z, coarseOuterRows, coarseInnerPtr, blocks,
                            wsum);
                if (blocks <= 0) continue;
                const int64_t cov = weighted ? wsum : blocks;
                if (cov > 0) coarse.push_back({ x, z, cov, blocks });
            }
        }
    }
    if (coarse.empty()) return none;
    std::sort(coarse.begin(), coarse.end(), [](const Coarse& a, const Coarse& b) { return a.cov > b.cov; });

    // 4) 最大覆盖平台 + 其几何重心：面积打平时取重心附近（对称、便于建造；2x2 阵即四区块角点）
    const int64_t covMax = coarse.front().cov;
    double gx = 0.0, gz = 0.0;
    int plateauN = 0;
    for (const Coarse& p : coarse) {
        if (p.cov != covMax) continue;
        gx += p.x; gz += p.z; ++plateauN;
    }
    if (plateauN > 0) { gx /= plateauN; gz /= plateauN; }

    // 5) 精修顺序：平台内离重心近的先精修；平台外按覆盖降序
    std::vector<Coarse> order = coarse;
    std::sort(order.begin(), order.end(), [&](const Coarse& a, const Coarse& b) {
        const bool aTop = (a.cov == covMax), bTop = (b.cov == covMax);
        if (aTop != bTop) return aTop;
        if (aTop) {
            const double da = std::hypot(static_cast<double>(a.x) - gx, static_cast<double>(a.z) - gz);
            const double db = std::hypot(static_cast<double>(b.x) - gx, static_cast<double>(b.z) - gz);
            if (std::abs(da - db) > 1e-9) return da < db;
        } else if (a.cov != b.cov) {
            return a.cov > b.cov;
        }
        return a.x < b.x;
    });

    // 6) 1 格精修（±step 邻域）+ 非极大值抑制
    std::vector<AfkCandidate> refined;
    const int keep = (std::max)(opt.topN * 8, 24);
    for (const Coarse& c0 : order) {
        if (static_cast<int>(refined.size()) >= keep) break;
        bool tooClose = false;
        const int sep2 = (std::max)(step, opt.minSeparation) * (std::max)(step, opt.minSeparation);
        for (const AfkCandidate& k : refined) {
            const int dx = c0.x - k.x, dz = c0.z - k.z;
            if (dx * dx + dz * dz < sep2) { tooClose = true; break; }
        }
        if (tooClose) continue;
        int bestX = c0.x, bestZ = c0.z;
        int64_t bestCov = -1;
        int64_t bestBlocks = 0;
        for (int dz = -step; dz <= step; ++dz) {
            for (int dx = -step; dx <= step; ++dx) {
                const int x = c0.x + dx, z = c0.z + dz;
                if (x < bx0 || x > bx1 || z < bz0 || z > bz1) continue;
                int64_t blocks = 0, wsum = 0;
                annulusBoth(occ, weighted ? &wt : nullptr, x, z, opt.innerRadius, opt.radius, blocks, wsum);
                const int64_t cov = weighted ? wsum : blocks;
                if (cov > bestCov) { bestCov = cov; bestBlocks = blocks; bestX = x; bestZ = z; }
            }
        }
        if (bestCov <= 0 || bestBlocks <= 0) continue;
        AfkCandidate cand = makeCandidate(bestX, bestZ, opt.radius, bestBlocks, isSlime);
        cand.coveredWeight = weighted ? (static_cast<double>(bestCov) / 1000.0)
                                      : static_cast<double>(bestBlocks);
        cand.avgWeight = bestBlocks > 0 ? cand.coveredWeight / static_cast<double>(bestBlocks) : 0.0;
        refined.push_back(cand);
    }
    if (opt.minDensity > 0.0) {
        // v3.5.11：最小密度只做筛选，结果数量仍受「目标数量」约束（此前会返回全部达标项，
        // 小范围+低阈值时一次刷出几十个站位，与用户设置的 8 个不符）
        std::vector<AfkCandidate> filtered;
        for (const auto& cd : refined) {
            if (cd.coveragePercent / 100.0 >= opt.minDensity) filtered.push_back(cd);
        }
        return pickSpread(filtered, opt.topN, opt.minSeparation);
    }
    return pickSpread(refined, opt.topN, opt.minSeparation);
}

std::vector<AfkCandidate> AfkPlanner::planForSeed(const AfkPlanOptions& opt, Version version, int64_t worldSeed)
{
    return plan(opt, [version, worldSeed](int cx, int cz) {
        return SlimeEngine::isSlimeChunk(version, worldSeed, cx, cz);
    });
}

int64_t AfkPlanner::slimeBlocksInArea(const Rect& chunkArea, const SlimeChunkPredicate& isSlime)
{
    if (!isSlime || !chunkArea.valid()) return 0;
    int64_t n = 0;
    for (int z = chunkArea.minZ; z <= chunkArea.maxZ; ++z) {
        for (int x = chunkArea.minX; x <= chunkArea.maxX; ++x) {
            if (isSlime(x, z)) n += 256;
        }
    }
    return n;
}

AfkCandidate AfkPlanner::evaluate(int blockX, int blockZ, int radius, const SlimeChunkPredicate& isSlime)
{
    if (!isSlime || radius <= 0) return AfkCandidate{};
    int64_t cov = 0;
    const int64_t r2 = static_cast<int64_t>(radius) * radius;
    const int c0x = floorDiv16(blockX - radius), c1x = floorDiv16(blockX + radius);
    const int c0z = floorDiv16(blockZ - radius), c1z = floorDiv16(blockZ + radius);
    for (int chz = c0z; chz <= c1z; ++chz) {
        for (int chx = c0x; chx <= c1x; ++chx) {
            if (!isSlime(chx, chz)) continue;
            const int bx0 = chx * 16, bz0 = chz * 16;
            for (int z = bz0; z < bz0 + 16; ++z) {
                const int dz = z - blockZ;
                const int64_t rem = r2 - static_cast<int64_t>(dz) * dz;
                if (rem < 0) continue;
                int w = static_cast<int>(std::sqrt(static_cast<double>(rem)));
                while (static_cast<int64_t>(w + 1) * (w + 1) + static_cast<int64_t>(dz) * dz <= r2) ++w;
                while (w > 0 && static_cast<int64_t>(w) * w + static_cast<int64_t>(dz) * dz > r2) --w;
                const int lo = (std::max)(bx0, blockX - w);
                const int hi = (std::min)(bx0 + 15, blockX + w);
                if (hi >= lo) cov += (hi - lo + 1);
            }
        }
    }
    return makeCandidate(blockX, blockZ, radius, cov, isSlime);
}

int AfkPlanner::coarseChunkStride(int spanChunks)
{
    // 目标：粗扫候选数 ≤ 约 200 万（1.2M~2M 时约 0.3~0.6 秒）
    const double target = 2000000.0;
    const double area = (std::max)(1.0, static_cast<double>(spanChunks) * spanChunks);
    const int s = static_cast<int>(std::ceil(std::sqrt(area / target)));
    return (std::max)(1, s);
}

int AfkPlanner::threadPoolSize() noexcept
{
    return plannerThreads();
}

int AfkPlanner::rankingGridScale(int spanChunks) noexcept
{
    // 只影响排序：单次采样格数控制在约 2500 以内（1:64 约 100 区块内、1:128 约 200 区块内、
    // 1:256 约 400 区块内，更大的范围用 1:512）。
    //   注意不要为了省采样把 400 区块档压到 1:128 —— 实测那一档的最优点会明显变差（1:256 反而与 1:64 一致）。
    const double blocks = (std::max)(1.0, static_cast<double>(spanChunks) * 16.0);
    for (int scale : { 64, 128, 256, 512, 1024, 2048, 4096 }) {
        const double cells = blocks / scale + 2.0;
        if (cells * cells <= 2500.0) return scale;
    }
    return 4096;
}

// v3.5.12：按区块内 16x16 子偏移预计算「环带 ∩ 区块单元」的精确交叠格数。
// 与 measureExact 完全同一口径（整数格、中心 = 挂机点方块本身），因此精修结果就是 1 格精确值。
// 索引：[ (sz*16+sx) * kk * kk + (dj+k) * kk + (di+k) ]，kk = 2k+1，k = ceil(R/16)
// v3.5.25：转置布局 —— subKT[off * 256 + (sz*16+sx)]。
//   精修时对「一个史莱姆格」读 256 个亚格计数是**连续 512 字节**（旧布局要跨 256 个核各跳 1156 字节），
//   一个候选的核数据访存从 ~148 KB 降到 ~15 KB，能进 L1。
static const std::vector<int16_t>& subOffsetKernels(int R, int inner);   // 前向声明（转置版要用）

static const std::vector<int16_t>& subOffsetKernelsT(int R, int inner)
{
    static std::mutex mu;
    static std::unordered_map<int64_t, std::vector<int16_t>> cache;
    const int64_t key = static_cast<int64_t>(R) * 1000 + inner;
    std::lock_guard<std::mutex> lk(mu);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    const std::vector<int16_t>& src = subOffsetKernels(R, inner);
    const int k = (R + 15) / 16;
    const int kk = 2 * k + 1;
    const size_t nOff = static_cast<size_t>(kk) * kk;
    std::vector<int16_t> dst(nOff * 256, 0);
    for (int s = 0; s < 256; ++s) {
        const int16_t* kbase = src.data() + static_cast<size_t>(s) * nOff;
        for (size_t off = 0; off < nOff; ++off) dst[off * 256 + static_cast<size_t>(s)] = kbase[off];
    }
    auto res = cache.emplace(key, std::move(dst));
    return res.first->second;
}

static const std::vector<int16_t>& subOffsetKernels(int R, int inner)
{
    static std::mutex mu;
    static std::unordered_map<int64_t, std::vector<int16_t>> cache;
    const int64_t key = static_cast<int64_t>(R) * 1000 + inner;
    std::lock_guard<std::mutex> lk(mu);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    const int k = (R + 15) / 16;
    const int kk = 2 * k + 1;
    std::vector<int16_t> K(static_cast<size_t>(256) * kk * kk, 0);
    const int64_t R2 = static_cast<int64_t>(R) * R;
    const int64_t r2 = static_cast<int64_t>((std::max)(0, inner)) * (std::max)(0, inner);
    // v3.5.13：256 个子偏移核其实是同一张「方块级环带掩码」在不同位置的 16x16 盒和，
    //   所以先建掩码的积分图，之后每格只要 4 次查表（旧实现每格 256 次距离判定，
    //   单线程约 30ms → 现在约 2ms）。
    const int half = k * 16 + 15;               // 掩码覆盖的方块偏移范围 [-half, half]
    const int n = 2 * half + 1;
    std::vector<int32_t> integ(static_cast<size_t>(n + 1) * (n + 1), 0);
    for (int z = 0; z < n; ++z) {
        const int64_t dz = static_cast<int64_t>(z - half);
        const int64_t dz2 = dz * dz;
        int32_t rowSum = 0;
        const size_t row = static_cast<size_t>(z + 1) * (n + 1);
        const size_t prev = static_cast<size_t>(z) * (n + 1);
        for (int x = 0; x < n; ++x) {
            const int64_t dx = static_cast<int64_t>(x - half);
            const int64_t d2 = dx * dx + dz2;
            if (d2 <= R2 && d2 >= r2) ++rowSum;
            integ[row + static_cast<size_t>(x) + 1] = integ[prev + static_cast<size_t>(x) + 1] + rowSum;
        }
    }
    const int strideI = n + 1;
    const auto boxSum = [&integ, strideI, half](int x0, int z0) -> int32_t {
        const int a = x0 + half, b = z0 + half;   // 左上角（掩码坐标）
        const int c = a + 16, d = b + 16;
        return integ[static_cast<size_t>(d) * strideI + c] - integ[static_cast<size_t>(b) * strideI + c] -
               integ[static_cast<size_t>(d) * strideI + a] + integ[static_cast<size_t>(b) * strideI + a];
    };
    for (int sz = 0; sz < 16; ++sz) {
        for (int sx = 0; sx < 16; ++sx) {
            int16_t* kbase = K.data() + (static_cast<size_t>(sz) * 16 + sx) * kk * kk;
            for (int dj = -k; dj <= k; ++dj) {
                const int z0 = dj * 16 - sz;
                int16_t* krow = kbase + static_cast<size_t>(dj + k) * kk;
                for (int di = -k; di <= k; ++di) krow[di + k] = static_cast<int16_t>(boxSum(di * 16 - sx, z0));
            }
        }
    }
    auto ins = cache.emplace(key, std::move(K));
    return ins.first->second;
}

// v3.5.6：大范围环形规划 —— 区块级卷积粗扫 + 局部 1 格精修
// v3.5.16：范围不再卡 4096x4096（跟随「半径」设定值）；粗扫改成按 z 带流式处理 ——
//   峰值内存与范围大小无关（每带 (spanX+2k) × (bandRows+2k)），精修/抛光按需取局部窗口。
std::vector<AfkCandidate> AfkPlanner::planChunkCoarse(const AfkPlanOptions& opt, const SlimeChunkPredicate& isSlime)
{
    std::vector<AfkCandidate> none;
    if (!isSlime || !opt.area.valid() || opt.radius <= 0 || opt.topN <= 0) return none;
    const int ax0 = opt.area.minX, ax1 = opt.area.maxX;
    const int az0 = opt.area.minZ, az1 = opt.area.maxZ;
    const int spanX = ax1 - ax0 + 1, spanZ = az1 - az0 + 1;
    if (spanX <= 0 || spanZ <= 0) return none;
    // v3.5.19：范围跟随输入半径，不再卡 4 亿区块。
    //   快速模式（抽样）成本与面积无关；精确模式（逐区块）才与面积成正比，这里给一个物理上限防止跑飞。
    const long long totalCells = static_cast<long long>(spanX) * static_cast<long long>(spanZ);
    if (totalCells > 4000000000000LL) {   // 4 万亿区块（≈ 2,000,000 x 2,000,000）物理上限
        std::fprintf(stderr, "[环形] 范围超出物理上限（%dx%d 区块）\n", spanX, spanZ);
        return none;
    }

    // v3.5.29（用户反馈：大范围精确搜索跑久了崩溃）：精确模式是逐区块铺占用图，
    //   候选命中表会按「几乎每个位置都有分」增长（4 亿位置 × 16B ≈ 6 GB）→ OOM。
    //   这里给精确模式一个**可承受的规模上限**（400 万区块 ≈ 2000×2000），超过就明确拒绝；
    //   大范围请用「快速」模式（抽样，内存有界）。
    if (opt.scanStride == 1 && totalCells > 4000000LL) {
        std::fprintf(stderr, "[环形] 精确模式范围过大（%d x %d = %.1f 万区块，上限 400 万），已拒绝\n",
                     spanX, spanZ, static_cast<double>(totalCells) / 10000.0);
        return none;
    }

    const int R = opt.radius;
    // v3.5.13：窗口其实装得进块级精确路径时优先给精确解。注意这里只在「直接调用本函数」时生效 ——
    //   plan() 对加权模式已经直接选择本函数，所以不会再绕回精确路径（也就不可能递归）。
    {
        const int padB = R + 16;
        const long long wB = static_cast<long long>(spanX) * 16 + 2LL * padB;
        const long long hB = static_cast<long long>(spanZ) * 16 + 2LL * padB;
        const long long exactLimit = static_cast<bool>(opt.weightAt) ? 0 : 2048;
        if (!opt.forceCoarse && wB <= exactLimit && hB <= exactLimit) {
            AfkPlanOptions bigger = opt;
            bigger.maxWindowBlocks = static_cast<int>(exactLimit);
            return planExactBlockLevel(bigger, isSlime);
        }
    }
    const int k = (R + 15) / 16;                 // 半径跨越的区块数
    const int kk = 2 * k + 1;
    const bool weighted = static_cast<bool>(opt.weightAt);

    // 1) 核：区块单元（16x16）与环带的交叠格数；按 (R, inner, 子偏移) 缓存
    static std::mutex kernelMutex;
    static std::unordered_map<int64_t, std::vector<int32_t>> kernelCache;
    const int kSubOffsets[4][2] = { { 0, 0 }, { 8, 0 }, { 0, 8 }, { 8, 8 } };
    std::vector<int32_t> kernels[4];
    {
        std::lock_guard<std::mutex> lk(kernelMutex);
        for (int si = 0; si < 4; ++si) {
            const int64_t key = (static_cast<int64_t>(R) * 1000 + opt.innerRadius) * 100 + si;
            auto it = kernelCache.find(key);
            if (it != kernelCache.end()) {
                kernels[si] = it->second;
                continue;
            }
            std::vector<int32_t> K(static_cast<size_t>(kk) * kk, 0);
            const double R2 = static_cast<double>(R) * R;
            const double r2 = static_cast<double>(opt.innerRadius) * opt.innerRadius;
            const int sx = kSubOffsets[si][0], sz = kSubOffsets[si][1];
            for (int dj = -k; dj <= k; ++dj) {
                for (int di = -k; di <= k; ++di) {
                    int cnt = 0;
                    for (int bz = 0; bz < 16; ++bz) {
                        for (int bx = 0; bx < 16; ++bx) {
                            // 该格中心相对候选点（候选点在区块内的偏移 sx,sz）的位移
                            const double dx = di * 16.0 + bx - sx + 0.5;
                            const double dz = dj * 16.0 + bz - sz + 0.5;
                            const double d2 = dx * dx + dz * dz;
                            if (d2 <= R2 && d2 >= r2) ++cnt;
                        }
                    }
                    K[static_cast<size_t>(dj + k) * kk + (di + k)] = cnt;
                }
            }
            kernelCache.emplace(key, K);
            kernels[si] = K;
        }
    }
    // 4 套核之和 + 反向副本（粗扫排序用；反向让内层两边都正向连续读，便于向量化）
    std::vector<int32_t> kernSum(static_cast<size_t>(kk) * kk, 0);
    for (int kj = 0; kj < kk * kk; ++kj) {
        int32_t s = 0;
        for (int si = 0; si < 4; ++si) s += kernels[si][static_cast<size_t>(kj)];
        kernSum[static_cast<size_t>(kj)] = s;
    }
    std::vector<int32_t> kernRev(static_cast<size_t>(kk) * kk);
    for (int dj = 0; dj < kk; ++dj)
        for (int di = 0; di < kk; ++di)
            kernRev[static_cast<size_t>(dj) * kk + di] = kernSum[static_cast<size_t>(dj) * kk + (kk - 1 - di)];

    // 2) 粗扫：按 z 带流式（内存与范围无关）
    int stride = coarseChunkStride((std::max)(spanX, spanZ));
    // v3.5.20：快速模式（scanStride = 0）再套一层「候选点数预算」——大范围自动加步长，
    //   把粗扫候选压到 ~40 万以内（1 格核精修 + 3x3 抛光仍会把这些区域挖出来）。
    //   精确模式（scanStride = 1）不受影响，逐区块全扫。
    if (opt.scanStride == 0) {
        const long long budget = 400000;
        while (static_cast<long long>((spanX + stride - 1) / stride) *
                   static_cast<long long>((spanZ + stride - 1) / stride) > budget)
            ++stride;
    }
    if (opt.scanStride > 0) stride = opt.scanStride;   // v3.5.17：精确模式 = 1（逐区块）
    if (const char* se = std::getenv("SLIME_RING_STRIDE")) {
        const int v = std::atoi(se);
        if (v > 0) stride = v;
    }
    const int nX = (spanX + stride - 1) / stride;
    const int nZ = (spanZ + stride - 1) / stride;
    if (nX <= 0 || nZ <= 0) return none;
    const int ow = spanX + 2 * k;
    // 每带目标 ≈ 24MB（occ 1B + 权重 2B ⇒ 每格 3B），并对齐到 stride 的整数倍
    int bandRows = static_cast<int>((24LL << 20) / (static_cast<long long>(ow) * 3LL));
    bandRows = (std::max)(stride, (std::min)(8192, bandRows));
    bandRows = ((bandRows + stride - 1) / stride) * stride;

    struct Hit {
        int64_t score;
        int cx, cz;
    };
    std::vector<Hit> hits;
    // 列带：每带 sc ≤ 65536 ints（L2 内），同时吃满线程
    const int rowsPerBandCand = bandRows / stride + 1;
    int colBands = (std::max)(1, (std::min)(nX, plannerThreads()));
    {
        const long long maxPerBand = 65536;
        while (colBands < nX && static_cast<long long>(nX / colBands + 1) * rowsPerBandCand > maxPerBand) ++colBands;
        colBands = (std::min)(colBands, nX);
    }
    const int colW = (nX + colBands - 1) / colBands;
    const int colCount = (nX + colW - 1) / colW;
    const int nThreads = plannerThreads();
    const bool haveRowSlime = static_cast<bool>(opt.rowSlime);

    auto stageT0 = std::chrono::steady_clock::now();
    // v3.5.17：超大范围（>2 亿区块）走「等距抽样」—— 每个样本直接算 (2k+1)^2 区块窗口的核分，
    //   不铺占用图 ⇒ 内存与耗时都有界；随后仍走同一套 1 格核精修 + 抛光。精确模式（scanStride=1）不抽样。
    // v3.5.29：抽样阈值降到 2000 万区块 —— 再大时命中表会到 GB 级（内存有界优先）
    const bool useSampling = (opt.scanStride != 1) && (totalCells > 20LL * 1000 * 1000);
    if (useSampling) {
        const long long target = 400000;   // 目标样本数
        int ss = 1;
        while ((static_cast<long long>(spanX) / ss) * (static_cast<long long>(spanZ) / ss) > target) ++ss;
        const int nx = spanX / ss, nz = spanZ / ss;
        std::fprintf(stderr, "[环形] 超大范围抽样：%dx%d 区块，样本 %dx%d（步长 %d）\n", spanX, spanZ, nx, nz, ss);
        std::vector<Hit> all;
        all.reserve(static_cast<size_t>(nx) * nz);
#pragma omp parallel for schedule(dynamic, 1) num_threads(nThreads)
        for (int j = 0; j < nz; ++j) {
            std::vector<Hit> local;
            local.reserve(static_cast<size_t>(nx));
            const int cz = az0 + j * ss;
            for (int i = 0; i < nx; ++i) {
                const int cx = ax0 + i * ss;
                int64_t score = 0;   // 先按「未加权覆盖」排序，权重只对前若干名补算
                for (int dj = -k; dj <= k; ++dj) {
                    const int32_t* __restrict krow = kernSum.data() + static_cast<size_t>(dj + k) * kk;
                    for (int di = -k; di <= k; ++di) {
                        const int32_t w = krow[di + k];
                        if (w && isSlime(cx + di, cz + dj)) score += w;
                    }
                }
                if (score > 0) local.push_back(Hit{ score, cx, cz });
            }
#pragma omp critical
            all.insert(all.end(), local.begin(), local.end());
        }
        hits.swap(all);
        std::fprintf(stderr, "[环形] 抽样粗扫：候选 %zu（%.1f ms）\n", hits.size(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stageT0).count());
    } else
    for (int bz0 = az0; bz0 <= az1; bz0 += bandRows) {
        const int bz1 = (std::min)(az1, bz0 + bandRows - 1);
        const int oz0 = bz0 - k;
        const int oh = (bz1 - bz0 + 1) + 2 * k;
        // v3.6.9（计算逻辑优化）：本带只需要「史莱姆区块列表」+ 权重，
        //   occ 缓冲写了全程没人读 —— 去掉它省下每带约 24MB 的写入/清零流量。
        std::vector<int16_t> wq;   // 千分比权重（0..4000，int16 足够）
        if (weighted) wq.assign(static_cast<size_t>(ow) * oh, 0);
        // 2a) 占用 + 权重（按 32 行分带并行），同时收集本带的史莱姆区块
        const int strips = (oh + 31) / 32;
        std::vector<std::vector<std::pair<int, int>>> stripChunks(static_cast<size_t>(strips));
        const bool doParallel = oh >= 64;
        const int occThreads = doParallel ? nThreads : 1;
#pragma omp parallel for schedule(static) num_threads(occThreads) if (doParallel)
        for (int s = 0; s < (doParallel ? strips : 1); ++s) {
            const int j0 = (doParallel ? s * 32 : 0);
            const int j1 = (doParallel ? (std::min)(oh - 1, j0 + 31) : oh - 1);
            std::vector<uint8_t> rowBuf;
            if (haveRowSlime) rowBuf.assign(static_cast<size_t>(ow), 0);
            std::vector<std::pair<int, int>>& myChunks = stripChunks[static_cast<size_t>(doParallel ? s : 0)];
            for (int j = j0; j <= j1; ++j) {
                const int cz = oz0 + j;
                if (haveRowSlime) {
                    opt.rowSlime(cz, ax0 - k, ow, rowBuf.data());
                }
                for (int i = 0; i < ow; ++i) {
                    const int cx = ax0 - k + i;
                    const bool sl = haveRowSlime ? (rowBuf[static_cast<size_t>(i)] != 0) : isSlime(cx, cz);
                    if (!sl) continue;
                    const size_t idx = static_cast<size_t>(j) * ow + i;
                    if (weighted) {
                        const double wv = opt.weightAt(cx * 16 + 8, cz * 16 + 8);
                        wq[idx] = static_cast<int16_t>(
                            std::lround((std::max)(0.0, (std::min)(4.0, wv)) * kWeightScale));
                    }
                    myChunks.emplace_back(cx, cz);
                }
            }
        }
        // 2b) 合并本带区块并按列带分桶（每块最多进两个桶）
        std::vector<std::vector<std::pair<int, int>>> buckets(static_cast<size_t>(colCount));
        for (const auto& list : stripChunks) {
            for (const auto& c : list) {
                const int b0 = (std::max)(0, (std::min)(colCount - 1, (c.first - k - ax0) / (stride * colW)));
                const int b1 = (std::max)(0, (std::min)(colCount - 1, (c.first + k - ax0) / (stride * colW)));
                for (int b = b0; b <= b1; ++b) buckets[static_cast<size_t>(b)].push_back(c);
            }
        }
        // 2c) 散射（按列带并行）
        std::vector<std::vector<Hit>> bandHits(static_cast<size_t>(colCount));
#pragma omp parallel for schedule(dynamic, 1) num_threads(nThreads)
        for (int b = 0; b < colCount; ++b) {
            const int jx0 = b * colW;
            const int jx1 = (std::min)(nX, jx0 + colW) - 1;
            if (jx0 > jx1) continue;
            const int bnX = jx1 - jx0 + 1;
            const int bnZ = (bz1 - bz0) / stride + 1;
            std::vector<int32_t> sc(static_cast<size_t>(bnX) * bnZ, 0);
            const int cxBase = ax0 + jx0 * stride;
            const int cxMax = ax0 + jx1 * stride;
            if (stride == 1) {
                for (const auto& sk : buckets[static_cast<size_t>(b)]) {
                    const int scx = sk.first, scz = sk.second;
                    if (scx + k < cxBase || scx - k > cxMax) continue;
                    if (scz + k < bz0 || scz - k > bz1) continue;
                    const int32_t wm =
                        weighted ? wq[static_cast<size_t>(scz - oz0) * ow + (scx - ax0 + k)] : kWeightScale;
                    if (wm == 0) continue;
                    const int dj0 = (std::max)(-k, scz - bz1);
                    const int dj1 = (std::min)(k, scz - bz0);
                    const int di0 = (std::max)(-k, scx - cxMax);
                    const int di1 = (std::min)(k, scx - cxBase);
                    if (di0 > di1) continue;
                    for (int dj = dj0; dj <= dj1; ++dj) {
                        const int jz = scz - dj - bz0;
                        const int n = di1 - di0 + 1;
                        int32_t* __restrict dst = sc.data() + static_cast<size_t>(jz) * bnX + (scx - di1 - cxBase);
                        const int32_t* __restrict kp =
                            kernRev.data() + static_cast<size_t>(dj + k) * kk + (k - di1);
                        // v3.5.19：提示编译器向量化（int32 x4/8）；尾部由编译器处理
for (int t = 0; t < n; ++t) dst[t] += kp[t] * wm;
                    }
                }
            } else {
                for (const auto& sk : buckets[static_cast<size_t>(b)]) {
                    const int scx = sk.first, scz = sk.second;
                    if (scx + k < cxBase || scx - k > cxMax) continue;
                    if (scz + k < bz0 || scz - k > bz1) continue;
                    const int32_t wm =
                        weighted ? wq[static_cast<size_t>(scz - oz0) * ow + (scx - ax0 + k)] : kWeightScale;
                    if (wm == 0) continue;
                    for (int dj = -k; dj <= k; ++dj) {
                        const int cz = scz - dj;
                        if (cz < bz0 || cz > bz1) continue;
                        const int offZ = cz - bz0;
                        if (offZ % stride) continue;
                        const int jz = offZ / stride;
                        const int32_t* __restrict krow = kernSum.data() + static_cast<size_t>(dj + k) * kk;
                        for (int di = -k; di <= k; ++di) {
                            const int cx = scx - di;
                            if (cx < cxBase || cx > cxMax) continue;
                            const int offX = cx - ax0;
                            if (offX % stride) continue;
                            const int jx = offX / stride - jx0;
                            sc[static_cast<size_t>(jz) * bnX + jx] += krow[di + k] * wm;
                        }
                    }
                }
            }
            std::vector<Hit>& bh = bandHits[static_cast<size_t>(b)];
            for (int jz = 0; jz < bnZ; ++jz) {
                const int cz = bz0 + jz * stride;
                const int32_t* __restrict row = sc.data() + static_cast<size_t>(jz) * bnX;
                for (int jx = 0; jx < bnX; ++jx) {
                    if (row[jx] > 0) bh.push_back(Hit{ static_cast<int64_t>(row[jx]), ax0 + (jx0 + jx) * stride, cz });
                }
            }
        }
        for (const auto& bh : bandHits) hits.insert(hits.end(), bh.begin(), bh.end());
    }
    if (hits.empty()) return none;
    std::fprintf(stderr, "[环形] 粗扫阶段：候选 %zu（%.1f ms）\n", hits.size(),
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stageT0).count());

    // 3) 取前 M 个粗点
    // v3.5.9：小范围多精修几个（质量优先），大范围控制精修数量（速度优先）
    // v3.6.0（用户反馈：目标数量被卡在很小）：精修候选数按目标数量放大，
    //   否则 topN 再大也只能从 48 个粗点里挑，看起来像「最多 4 个」。
    size_t M = (static_cast<long long>(spanX) * spanZ <= 40000LL) ? 200 : 48;
    M = (std::max)(M, static_cast<size_t>((std::min)(4000, opt.topN * 8)));
    // v3.6.1（用户反馈：环形最多只出 4 个）：最小密度会把大部分候选筛掉，
    //   阈值 >0 时多精修一批，保证「目标数量」还有得挑。
    if (opt.minDensity > 0.0) M = (std::max)(M, static_cast<size_t>(256));
    M = (std::min)(M, static_cast<size_t>(4000));
    const size_t keep = (std::min)(M, hits.size());
    std::partial_sort(hits.begin(), hits.begin() + keep, hits.end(),
                      [](const Hit& a, const Hit& b) {
                          if (a.score != b.score) return a.score > b.score;
                          if (a.cx != b.cx) return a.cx < b.cx;
                          return a.cz < b.cz;
                      });

    // 4) 内核精修：把「区块内 256 个 1 格站位」里的最优解出来
    //    与 measureExact 完全同口径（16x16 子偏移核 = 环带 ∩ 区块单元的整数格数）。
    //    v3.5.16：占位/权重改成「按需取 (2k+1)² 局部窗口」—— 与范围大小无关，流式粗扫也能精修。
    const std::vector<int16_t>& subK = subOffsetKernels(R, opt.innerRadius);
    const std::vector<int16_t>& subKT = subOffsetKernelsT(R, opt.innerRadius);
    struct ChunkBest {
        int64_t blocks = 0;
        int64_t wsum = 0;
        int chunks = 0;      // v3.5.25：环带内的史莱姆区块数（由窗口直接算）
        double gx = 0.0;     // 覆盖区块重心（决定打平时的取点）
        double gz = 0.0;
        int x = 0, z = 0;
        bool ok = false;
    };
    const auto chunkWeightMilli = [&](int cx, int cz) -> int32_t {
        if (!weighted) return kWeightScale;
        const double wv = opt.weightAt(cx * 16 + 8, cz * 16 + 8);
        return static_cast<int32_t>(std::lround((std::max)(0.0, (std::min)(4.0, wv)) * kWeightScale));
    };
    // v3.5.25 诊断：refineChunk 内部三段耗时（SLIME_PROFILE=1 时打印）
    static std::atomic<long long> s_profWin{ 0 }, s_profAcc{ 0 }, s_profPick{ 0 }, s_profN{ 0 };
    const auto refineChunk = [&](int ccx, int ccz, ChunkBest& out) {
        const auto rt0 = std::chrono::steady_clock::now();
        out = ChunkBest{};
        std::vector<uint8_t> winOcc(static_cast<size_t>(kk) * kk, 0);
        std::vector<int32_t> winW(static_cast<size_t>(kk) * kk, 0);
        for (int dj = -k; dj <= k; ++dj) {
            for (int di = -k; di <= k; ++di) {
                const size_t idx = static_cast<size_t>(dj + k) * kk + (di + k);
                const bool sl = isSlime(ccx + di, ccz + dj);
                winOcc[idx] = sl ? 1 : 0;
                if (sl) winW[idx] = chunkWeightMilli(ccx + di, ccz + dj);
            }
        }
        // v3.5.25：把窗口里的史莱姆格收成稀疏表 —— 内层只遍历 ~11% 的格子，且去掉两个分支。
        //   数学上完全等价（cnt==0 或 orow==0 的项本来就贡献 0），结果逐位一致。
        int32_t spKoff[1024];
        int32_t spW[1024];
        int nsp = 0;
        for (int dj = -k; dj <= k; ++dj) {
            for (int di = -k; di <= k; ++di) {
                const size_t idx = static_cast<size_t>(dj + k) * kk + (di + k);
                if (winOcc[idx] && nsp < 1024) {
                    spKoff[nsp] = static_cast<int32_t>(idx);
                    spW[nsp] = winW[idx];
                    ++nsp;
                }
            }
        }
        // v3.5.25：改为「对每个史莱姆格，一次累加 256 个亚格」——核数据连续、命中 L1。
        //   与旧实现逐格求和完全等价（只是循环次序对调），结果逐位一致。
        int32_t accScore[256] = { 0 };
        int32_t accW[256] = { 0 };
        for (int t = 0; t < nsp; ++t) {
            const int16_t* __restrict krow = subKT.data() + static_cast<size_t>(spKoff[t]) * 256;
            if (weighted) {
                const int32_t w = spW[t];
                for (int sidx = 0; sidx < 256; ++sidx) {
                    const int32_t cnt = krow[sidx];
                    accScore[sidx] += cnt;
                    accW[sidx] += cnt * w;
                }
            } else {
                for (int sidx = 0; sidx < 256; ++sidx) accScore[sidx] += krow[sidx];
            }
        }
        const auto rt1 = std::chrono::steady_clock::now();
        int64_t bestCrit = -1;
        int bestD2 = 1000;
        int64_t bestBlocks = 0, bestW = 0;
        int bx = ccx * 16, bz = ccz * 16;
        for (int sz = 0; sz < 16; ++sz) {
            for (int sx = 0; sx < 16; ++sx) {
                const int sidx = sz * 16 + sx;
                const int64_t score = accScore[sidx];
                const int64_t wsum = weighted ? static_cast<int64_t>(accW[sidx]) : 0;
                const int64_t crit = weighted ? wsum : score;
                const int d2 = std::abs(sx - 8) + std::abs(sz - 8);
                if (crit > bestCrit || (crit == bestCrit && d2 < bestD2)) {
                    bestCrit = crit;
                    bestD2 = d2;
                    bestBlocks = score;
                    bestW = wsum;
                    bx = ccx * 16 + sx;
                    bz = ccz * 16 + sz;
                }
            }
        }
        if (slimeProfileEnabled()) {
            const auto rt2 = std::chrono::steady_clock::now();
            s_profWin.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(rt1 - rt0).count());
            s_profAcc.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(rt2 - rt1).count());
            s_profN.fetch_add(1);
        }
        if (bestBlocks <= 0) return false;
        // v3.5.25：槽内统计——用同一个窗口里已算好的占用信息数区块，不再走谓词
        {
            int nch = 0;
            double sx2 = 0.0, sz2 = 0.0;
            const int cx0 = (bx - R) / 16, cx1 = (bx + R) / 16;
            const int cz0 = (bz - R) / 16, cz1 = (bz + R) / 16;
            for (int cz = cz0; cz <= cz1; ++cz) {
                const int dj = cz - ccz;
                if (dj < -k || dj > k) continue;
                for (int cx = cx0; cx <= cx1; ++cx) {
                    const int di = cx - ccx;
                    if (di < -k || di > k) continue;
                    if (!winOcc[static_cast<size_t>(dj + k) * kk + (di + k)]) continue;
                    // 区块中心到挂机点的距离落在环带内才算覆盖
                    const double cxp = static_cast<double>(cx * 16 + 8) - bx;
                    const double czp = static_cast<double>(cz * 16 + 8) - bz;
                    const double d2 = cxp * cxp + czp * czp;
                    const double rr = static_cast<double>(R);
                    const double ri = static_cast<double>(opt.innerRadius);
                    if (d2 > rr * rr || d2 < ri * ri) continue;
                    ++nch;
                    sx2 += cxp;
                    sz2 += czp;
                }
            }
            out.chunks = nch;
            if (nch > 0) {
                out.gx = static_cast<double>(bx) + sx2 / nch;
                out.gz = static_cast<double>(bz) + sz2 / nch;
            } else {
                out.gx = static_cast<double>(bx);
                out.gz = static_cast<double>(bz);
            }
        }
        out.blocks = bestBlocks;
        out.wsum = bestW;
        out.x = bx;
        out.z = bz;
        out.ok = true;
        return true;
    };
    const auto toCandidate = [&](const ChunkBest& cb) {
        // v3.5.25：用 refineChunk 已算好的区块数与窗口重心，避免再跑一遍逐区块谓词
        AfkCandidate c = makeCandidateKnown(cb.x, cb.z, R, cb.blocks, cb.chunks, cb.gx, cb.gz, opt.innerRadius);
        if (weighted) {
            c.coveredWeight = static_cast<double>(cb.wsum) / kWeightScale;
            c.avgWeight = cb.blocks > 0 ? c.coveredWeight / static_cast<double>(cb.blocks) : 0.0;
        }
        return c;
    };
    std::vector<AfkCandidate> refined;
    refined.resize(keep);
    std::vector<uint8_t> refinedOk(keep, 0);
#pragma omp parallel for schedule(dynamic, 1) num_threads(nThreads)
    for (int i = 0; i < static_cast<int>(keep); ++i) {
        ChunkBest cb;
        if (!refineChunk(hits[static_cast<size_t>(i)].cx, hits[static_cast<size_t>(i)].cz, cb)) continue;
        refined[static_cast<size_t>(i)] = toCandidate(cb);
        refinedOk[static_cast<size_t>(i)] = 1;
    }
    std::fprintf(stderr, "[环形] 内核精修：%zu 个（%.1f ms）\n", refined.size(),
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stageT0).count());
    {
        size_t w = 0;
        for (size_t i = 0; i < keep; ++i) {
            if (!refinedOk[i]) continue;
            if (w != i) refined[w] = refined[i];
            ++w;
        }
        refined.resize(w);
    }
    if (refined.empty()) return none;
    // 5) 抛光：前 8 名在 3x3 区块窗口内跑同一套 1 格核精修（口径不变，成本远低于块级精确路径）
    {
        std::vector<size_t> orderIdx(refined.size());
        for (size_t i = 0; i < orderIdx.size(); ++i) orderIdx[i] = i;
        const auto scoreOf = [](const AfkCandidate& c) {
            return c.coveredWeight > 0.0 ? c.coveredWeight : static_cast<double>(c.coveredBlocks);
        };
        const size_t polishN = (std::min)(refined.size(), static_cast<size_t>(8));
        std::partial_sort(orderIdx.begin(), orderIdx.begin() + polishN, orderIdx.end(),
                          [&](size_t a, size_t b) { return scoreOf(refined[a]) > scoreOf(refined[b]); });
#pragma omp parallel for schedule(dynamic, 1) num_threads(nThreads)
        for (int i = 0; i < static_cast<int>(polishN); ++i) {
            AfkCandidate& base = refined[orderIdx[static_cast<size_t>(i)]];
            ChunkBest best;
            ChunkBest cur;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int ccx = base.chunkX + dx, ccz = base.chunkZ + dz;
                    if (ccx < opt.area.minX || ccx > opt.area.maxX) continue;
                    if (ccz < opt.area.minZ || ccz > opt.area.maxZ) continue;
                    if (!refineChunk(ccx, ccz, cur)) continue;
                    const int64_t crit = weighted ? cur.wsum : cur.blocks;
                    const int64_t curBestCrit = best.ok ? (weighted ? best.wsum : best.blocks) : -1;
                    if (crit > curBestCrit) best = cur;
                }
            }
            if (!best.ok) continue;
            const AfkCandidate c = toCandidate(best);
            if (scoreOf(c) > scoreOf(base)) base = c;
        }
    }
    if (slimeProfileEnabled())
        std::fprintf(stderr, "[剖面] refineChunk 调用 %lld 次：建窗 %.1f ms｜累加 %.1f ms\n",
                     (long long)s_profN.load(), s_profWin.load() / 1e6, s_profAcc.load() / 1e6);
    std::fprintf(stderr, "[环形] 精修阶段：%zu 个候选（%.1f ms）\n", refined.size(),
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stageT0).count());
    const int strideUsed = stride;
    if (strideUsed > 1) {
        std::fprintf(stderr, "[环形] 大范围模式：%dx%d 区块，粗扫步长 %d 区块，候选 %zu\n", spanX, spanZ,
                     strideUsed, refined.size());
    }
    if (opt.minDensity > 0.0) {
        // v3.5.11：最小密度只做筛选，结果数量仍受「目标数量」约束
        std::vector<AfkCandidate> filtered;
        for (const auto& cd : refined) {
            if (cd.coveragePercent / 100.0 >= opt.minDensity) filtered.push_back(cd);
        }
        return pickSpread(filtered, opt.topN, opt.minSeparation);
    }
    return pickSpread(refined, opt.topN, opt.minSeparation);
}

// v3.5.9：严格 1 格统计（面积 + 总权重都按格子精确计算）
// v3.5.13：修 x 轴区块坐标 —— 旧实现用「相对偏移」算 chunkX（z 轴却是绝对坐标），
//          导致统计按「把挂机点当成 x=0」的错位区块网格取样，数值与 plan()/内核精修不一致。
//          同时把圆/环边界改成整数判定（与 annulusSum、子偏移核完全同一口径：
//          包含 d² == R²、包含 d² == inner² 的格子）。
AfkPlanner::ExactMetrics AfkPlanner::measureExact(int blockX, int blockZ, const AfkPlanOptions& opt,
                                                  const SlimeChunkPredicate& isSlime)
{
    return measureRowsImpl(blockX, blockZ, opt, isSlime, -opt.radius, opt.radius, true);
}

// v3.5.14：只统计一段行（供 GUI 把 8 个候选 × 若干行带并行铺满所有核心）
AfkPlanner::ExactMetrics AfkPlanner::measureExactRows(int blockX, int blockZ, const AfkPlanOptions& opt,
                                                      const SlimeChunkPredicate& isSlime, int dzFrom,
                                                      int dzTo, bool countChunks)
{
    return measureRowsImpl(blockX, blockZ, opt, isSlime, dzFrom, dzTo, countChunks);
}

// v3.5.14：候选列表的「严格 1 格」重算 —— 候选 × 行带 摊成任务并行，每线程自带一个 BiomeMap。
AfkPlanner::StrictStatsInfo AfkPlanner::measureStrictForCandidates(std::vector<AfkCandidate>& cands,
                                                                  const AfkPlanOptions& opt,
                                                                  const SlimeChunkPredicate& isSlime,
                                                                  int javaVersionId, int64_t seed, bool useBiome)
{
    StrictStatsInfo info;
    const int nCands = static_cast<int>(cands.size());
    if (nCands <= 0) return info;
    const int bands = (std::max)(1, (std::min)(16, opt.statBands));
    const int rAll = opt.radius;
    const int rows = 2 * rAll + 1;
    std::vector<ExactMetrics> parts(static_cast<size_t>(nCands) * bands);
    std::vector<size_t> sampleCnt(parts.size(), 0), cellCnt(parts.size(), 0);
    const int poolThreads = plannerThreads();
#pragma omp parallel num_threads(poolThreads) if (nCands > 1)
    {
        std::unique_ptr<BiomeMap> localBiome;
        if (useBiome) {
            localBiome = std::make_unique<BiomeMap>();
            if (!localBiome->init(javaVersionId, seed)) localBiome.reset();
        }
#pragma omp for schedule(dynamic, 1)
        for (int t = 0; t < nCands * bands; ++t) {
            const int ci = t / bands, b = t % bands;
            const auto& cd = cands[static_cast<size_t>(ci)];
            AfkPlanOptions weightedOpt = opt;
            BiomeWeightGrid wgrid;
            if (localBiome) {
                const int r = opt.radius + 8;
                const int bx0 = cd.x - r, bz0 = cd.z - r;
                const int c4x0 = bx0 >= 0 ? bx0 / 4 : -((-bx0 + 3) / 4);
                const int c4z0 = bz0 >= 0 ? bz0 / 4 : -((-bz0 + 3) / 4);
                const int gw2 = (2 * r) / 4 + 3, gh2 = gw2;
                if (gw2 > 0 && wgrid.init(localBiome.get(), Platform::Java, 4, c4x0 * 4, c4z0 * 4, gw2, gh2))
                    weightedOpt.weightAt = [&wgrid](int bx, int bz) { return wgrid.weightAt(bx, bz); };
            }
            const int r0 = -rAll + static_cast<int>(static_cast<long long>(rows) * b / bands);
            const int r1 = -rAll + static_cast<int>(static_cast<long long>(rows) * (b + 1) / bands) - 1;
            parts[static_cast<size_t>(t)] = measureExactRows(cd.x, cd.z, weightedOpt, isSlime, r0, r1, b == 0);
            sampleCnt[static_cast<size_t>(t)] = wgrid.samples();
            cellCnt[static_cast<size_t>(t)] = wgrid.cells();
        }
    }
    for (size_t t = 0; t < parts.size(); ++t) {
        info.samples += sampleCnt[t];
        info.cells += cellCnt[t];
    }
    const double annulusArea =
        3.14159265358979323846 * (static_cast<double>(opt.radius) * opt.radius -
                                  static_cast<double>(opt.innerRadius) * opt.innerRadius);
    for (int ci = 0; ci < nCands; ++ci) {
        ExactMetrics em;
        for (int b = 0; b < bands; ++b) {
            const auto& p = parts[static_cast<size_t>(ci) * bands + b];
            em.coveredBlocks += p.coveredBlocks;
            em.coveredWeight += p.coveredWeight;
            em.coveredChunks += p.coveredChunks;   // 只有 b == 0 非零
        }
        em.coveragePercent =
            annulusArea > 0.0 ? (static_cast<double>(em.coveredBlocks) / annulusArea) * 100.0 : 0.0;
        auto& cd = cands[static_cast<size_t>(ci)];
        cd.coveredBlocks = em.coveredBlocks;
        cd.coveredWeight = em.coveredWeight;
        cd.coveredChunks = em.coveredChunks;
        cd.coveragePercent = em.coveragePercent;
        cd.avgWeight = em.coveredBlocks > 0 ? em.coveredWeight / static_cast<double>(em.coveredBlocks) : 0.0;
    }
    return info;
}

std::vector<AfkCandidate> AfkPlanner::planBruteForce(const AfkPlanOptions& opt, const SlimeChunkPredicate& isSlime)
{
    std::vector<AfkCandidate> all;
    if (!isSlime || !opt.area.valid() || opt.radius <= 0) return all;
    int bx0, bz0, bx1, bz1;
    chunkRectToBlocks(opt.area, bx0, bz0, bx1, bz1);
    for (int z = bz0; z <= bz1; ++z) {
        for (int x = bx0; x <= bx1; ++x) {
            const AfkCandidate c = evaluate(x, z, opt.radius, isSlime);
            if (c.coveredBlocks > 0) all.push_back(c);
        }
    }
    return pickSpread(all, opt.topN, opt.minSeparation);
}

} // namespace slime

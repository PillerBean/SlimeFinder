// CachePrescan.cpp - 缓存位图正方形密度预筛（实现）
//
// 算法（按 z 带、按行流式，不复制整区域位图）：
//   1. 对每个 band（top-left z ∈ [az0, az1]）维护 regionW 个「列和」colSum[x]：
//        colSum[x] = Σ_{z ∈ [z, z+g)} isSlime(x, z)
//      首行窗口用 g 行累加建立，之后每下移一行「加 z+g-1 行、减 z-1 行」；
//   2. 每行对 colSum 做水平滑动窗口（O(1)/窗口）得到窗口密度 count，
//      只把 count ≥ thrCount 的窗口放进本带 topPerBand 的小根堆（堆顶 = 最差）；
//   3. 全部带汇总后全局排序 + minSeparation 网格哈希去重 + maxSeeds 截断
//      （与 src/gpu/GpuScanAdapter.cpp prescanDensitySeeds 222-249 行逐行对应）。
//
// 复杂度：O(带数 × (bandRows × 行宽)) 次窗口判定、约 2 次位测试/窗口，
//   峰值内存 O(行宽 + topPerBand + 带数×topPerBand)。
#include "core/CachePrescan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

#include "core/CacheManager.h"

namespace slime {
namespace {

struct Cand {
    int count = 0;
    int cx = 0; // 窗口中心（区块坐标）
    int cz = 0;
};

// 确定性「更优」次序：count 降序；count 相同 cx 升序、cz 升序。
// GPU 每带的 partial_sort 只比较 count（等价 count 的取舍由实现决定）；
// CPU 版固定为这个全序 —— 同一输入输出稳定，单测可与独立暴力实现逐种子比对。
inline bool better(const Cand& a, const Cand& b) noexcept
{
    if (a.count != b.count) return a.count > b.count;
    if (a.cx != b.cx) return a.cx < b.cx;
    return a.cz < b.cz;
}

inline bool overlaps(const Rect& a, const Rect& b) noexcept
{
    return a.minX <= b.maxX && a.maxX >= b.minX && a.minZ <= b.maxZ && a.maxZ >= b.minZ;
}

// 从位图行把小端位序的 [col0, col0+width) 加到/减到 colSum（delta = +1 / -1）。
// 每 64 个区块一次字读取，内部逐位展开（约 1 次位测试/区块）。
void accumulateRowWords(const uint64_t* rowWords, int64_t col0, int64_t width, int delta,
                        int* colSum) noexcept
{
    int64_t x = 0;
    while (x < width) {
        const int64_t col = col0 + x;
        const int bit = static_cast<int>(col & 63);
        const uint64_t word = rowWords[col >> 6];
        const int64_t n = (std::min)(static_cast<int64_t>(64 - bit), width - x);
        int* dst = colSum + x;
        for (int64_t k = 0; k < n; ++k) {
            dst[k] += delta * static_cast<int>((word >> (bit + k)) & 1ULL);
        }
        x += n;
    }
}

// 多 range 重叠（更早 range 遮蔽区域）时的确定性慢路径。
// CacheManager 文档语义是「ranges 顺序中第一个包含该点的 range」，但其 lookup() 内部的
// thread_local 快查只要「上次命中的 range」包含该点就直接返回 —— 两个 range 重叠时结果
// 会依赖访问顺序。这里按 ranges 顺序自己读位图，得到顺序无关的确定值。
// 正常缓存（单 range 完整覆盖）不会走到这里。
struct RangeReader {
    Rect rect;
    const uint64_t* words = nullptr;
    int64_t wordsPerRow = 0;
};

void accumulateRowFirstContaining(const std::vector<RangeReader>& readers, int64_t minX, int64_t width,
                                  int64_t z, int delta, int* colSum) noexcept
{
    const int zi = static_cast<int>(z);
    for (int64_t x = 0; x < width; ++x) {
        const int xi = static_cast<int>(minX + x);
        int hit = 0;
        for (const RangeReader& r : readers) {
            if (!r.rect.contains(xi, zi)) continue;
            if (r.words && r.wordsPerRow > 0) {
                const int64_t col = static_cast<int64_t>(xi) - r.rect.minX;
                const int64_t row = z - r.rect.minZ;
                const int64_t bitIndex = row * (r.wordsPerRow * 64) + col;
                hit = static_cast<int>((r.words[bitIndex >> 6] >> (bitIndex & 63)) & 1ULL);
            }
            break; // 第一个包含该点的 range 拥有该点
        }
        if (hit) colSum[x] += delta;
    }
}

} // namespace

bool cachePrescanSeeds(const CacheManager& cache, const DensityPrescanParams& p,
                       const std::atomic<bool>* cancel,
                       std::vector<DensitySeed>& out, std::string* err)
{
    out.clear();
    const int g = p.gridSize;
    if (g < 1 || g > 15) {
        if (err) *err = "正方形预筛窗口只支持 1..15 区块";
        return false;
    }
    // 窗口左上角的合法范围（与 GPU prescanDensitySeeds 一致）。
    const int64_t axMin = p.minX;
    const int64_t axMax = static_cast<int64_t>(p.maxX) - g + 1;
    const int64_t azMin = p.minZ;
    const int64_t azMax = static_cast<int64_t>(p.maxZ) - g + 1;
    if (axMax < axMin || azMax < azMin) {
        if (err) *err = "预筛范围小于窗口";
        return false;
    }

    // 前置条件：缓存必须由某个 range 完整覆盖整个搜索区域（调用方负责；不满足回退 GPU）。
    const Rect region{ p.minX, p.maxX, p.minZ, p.maxZ };
    if (!cache.covers(region)) {
        if (err) *err = "缓存未完整覆盖";
        return false;
    }
    if (cancel && cancel->load(std::memory_order_relaxed)) {
        if (err) *err = "已取消";
        return false;
    }

    // ---- 定位覆盖区域的 range 与位图 ----
    const std::vector<Rect>& ranges = cache.ranges();
    size_t covIdx = ranges.size();
    for (size_t i = 0; i < ranges.size(); ++i) {
        if (ranges[i].contains(region)) {
            covIdx = i;
            break;
        }
    }
    if (covIdx >= ranges.size()) { // covers() 与 ranges() 不一致：保守按未覆盖处理
        if (err) *err = "缓存未完整覆盖";
        return false;
    }
    int64_t wordsPerRow = 0;
    int64_t bitmapRows = 0;
    const uint64_t* words = cache.rangeBitmap(covIdx, &wordsPerRow, &bitmapRows);
    const Rect covRect = ranges[covIdx];
    const int64_t regionW = static_cast<int64_t>(region.maxX) - region.minX + 1;
    const int64_t regionH = static_cast<int64_t>(region.maxZ) - region.minZ + 1;
    if (!words || wordsPerRow <= 0 || bitmapRows < regionH || wordsPerRow * 64 < regionW) {
        if (err) *err = "缓存位图不可用";
        return false;
    }
    // CacheManager 对重叠 range 取「第一个包含该点的 range」：更早的 range 若与区域
    // 重叠，区域内的真实值可能记在它的位图里 ⇒ 走确定性慢路径（按 ranges 顺序读位图）。
    bool shadowed = false;
    for (size_t i = 0; i < covIdx && !shadowed; ++i) {
        if (overlaps(ranges[i], region)) shadowed = true;
    }
    std::vector<RangeReader> overlapReaders;
    if (shadowed) {
        overlapReaders.reserve(covIdx + 1);
        for (size_t i = 0; i <= covIdx; ++i) {
            RangeReader rr;
            rr.rect = ranges[i];
            rr.words = cache.rangeBitmap(i, &rr.wordsPerRow);
            overlapReaders.push_back(rr); // 位图缺失的 range 视作全 0（与空 range 相同）
        }
    }
    const int64_t col0 = static_cast<int64_t>(region.minX) - covRect.minX;

    // ---- 阈值 / 分带参数（与 GPU 一致）----
    double thr = std::clamp(p.minDensity, 0.0, 1.0);
    if (!(thr >= 0.0)) thr = 0.0; // NaN 防御：视作阈值 0
    const int thrCount =
        (std::max)(1, static_cast<int>(std::ceil(thr * static_cast<double>(g) * g - 1e-9)));
    const int64_t bandRows = (std::max)(64, p.bandRows);
    const int topPerBand = (std::max)(1, p.topPerBand);
    const int64_t topLeftCols = regionW - g + 1;

    std::vector<Cand> cands;
    {
        const int64_t bandCount = (azMax - azMin) / bandRows + 1;
        // reserve 只是优化：按「每带 topPerBand」估算，再封顶 1M 条防病态参数爆内存。
        const int64_t reserveCount = (std::min)(
            static_cast<int64_t>(topPerBand) * bandCount, static_cast<int64_t>(1) << 20);
        cands.reserve(static_cast<size_t>((std::max)(static_cast<int64_t>(1), reserveCount)));
    }
    std::vector<int> colSum(static_cast<size_t>(regionW), 0);

    for (int64_t az0 = azMin; az0 <= azMax; az0 += bandRows) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            if (err) *err = "已取消";
            return false;
        }
        const int64_t az1 = (std::min)(azMax, az0 + bandRows - 1);
        // 列和：先建立 z = az0 的 g 行窗口，随后每行滚动更新。
        std::fill(colSum.begin(), colSum.end(), 0);
        const auto accumulate = [&](int64_t z, int delta) {
            if (shadowed) {
                accumulateRowFirstContaining(overlapReaders, region.minX, regionW, z, delta,
                                             colSum.data());
            } else {
                accumulateRowWords(words + (z - covRect.minZ) * wordsPerRow, col0, regionW, delta,
                                   colSum.data());
            }
        };
        for (int64_t z = az0; z < az0 + g; ++z) accumulate(z, +1);

        // 本带 topPerBand：小根堆，堆顶 = 按确定性次序最差的一条。
        // priority_queue 的 comp 语义是「小于」：让「更好」= 更小，堆顶即最差。
        const auto worseFirst = [](const Cand& a, const Cand& b) { return better(a, b); };
        std::priority_queue<Cand, std::vector<Cand>, decltype(worseFirst)> heap(worseFirst);
        for (int64_t z = az0; z <= az1; ++z) {
            if (z > az0) {
                accumulate(z + g - 1, +1); // 新进入窗口的行
                accumulate(z - 1, -1);     // 离开窗口的行
            }
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                if (err) *err = "已取消";
                return false;
            }
            const int cz = static_cast<int>(z + g / 2);
            int acc = 0;
            for (int64_t dx = 0; dx < g; ++dx) acc += colSum[static_cast<size_t>(dx)];
            for (int64_t x = 0; x < topLeftCols; ++x) {
                if (x > 0) {
                    acc += colSum[static_cast<size_t>(x + g - 1)] - colSum[static_cast<size_t>(x - 1)];
                }
                if (acc < thrCount) continue;
                const Cand c{ acc, static_cast<int>(axMin + x) + g / 2, cz };
                if (static_cast<int>(heap.size()) < topPerBand) {
                    heap.push(c);
                } else if (better(c, heap.top())) {
                    heap.pop();
                    heap.push(c);
                }
            }
        }
        while (!heap.empty()) {
            cands.push_back(heap.top());
            heap.pop();
        }
    }

    if (cands.empty()) {
        if (err) *err = "正方形预筛没有候选";
        return false;
    }

    // ---- 全局排序 + minSeparation 网格哈希去重 + maxSeeds 截断 ----
    // 与 src/gpu/GpuScanAdapter.cpp prescanDensitySeeds 的 217-249 行一致。
    std::sort(cands.begin(), cands.end(), better);
    const int sep = (std::max)(1, p.minSeparation);
    const int maxSeeds = (std::max)(1, p.maxSeeds);
    const long long cellSize = sep;
    std::unordered_map<int64_t, std::vector<int>> occupied;
    out.reserve(static_cast<size_t>((std::min)(maxSeeds, static_cast<int>(cands.size()))));
    for (const Cand& c : cands) {
        if (static_cast<int>(out.size()) >= maxSeeds) break;
        const int gx = static_cast<int>(std::floor(static_cast<double>(c.cx) / cellSize));
        const int gz = static_cast<int>(std::floor(static_cast<double>(c.cz) / cellSize));
        bool far = true;
        for (int dz = -1; dz <= 1 && far; ++dz) {
            for (int dx = -1; dx <= 1 && far; ++dx) {
                const int64_t key = (static_cast<int64_t>(gx + dx) << 32) ^
                                    static_cast<uint32_t>(gz + dz);
                auto it = occupied.find(key);
                if (it == occupied.end()) continue;
                for (int idx : it->second) {
                    const int ddx = out[static_cast<size_t>(idx)].cx - c.cx;
                    const int ddz = out[static_cast<size_t>(idx)].cz - c.cz;
                    if (ddx * ddx + ddz * ddz < sep * sep) {
                        far = false;
                        break;
                    }
                }
            }
        }
        if (!far) continue;
        out.push_back(DensitySeed{ c.cx, c.cz, c.count });
        const int64_t key = (static_cast<int64_t>(gx) << 32) ^ static_cast<uint32_t>(gz);
        occupied[key].push_back(static_cast<int>(out.size() - 1));
    }
    if (out.empty()) {
        if (err) *err = "正方形预筛没有分散种子";
        return false;
    }
    return true;
}

} // namespace slime

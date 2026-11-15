#include "core/next/FusedGpu.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace slime::next {

namespace {

void addResult(const slime::SearchOptions& opt, const int64_t azMin, const int64_t anchorRows,
               const slime::MatchResult& m, ResultCollector& collector)
{
    const int64_t w = static_cast<int64_t>(m.maxX) - m.minX + 1;
    const int64_t h = static_cast<int64_t>(m.maxZ) - m.minZ + 1;
    const int64_t off2x = 2 * static_cast<int64_t>(m.minX) + (w - 1) - 2 * opt.startX;
    const int64_t off2z = 2 * static_cast<int64_t>(m.minZ) + (h - 1) - 2 * opt.startZ;
    Candidate c;
    c.result = m;
    c.d2 = off2x * off2x + off2z * off2z;
    c.rank = (static_cast<int64_t>(m.minX) - opt.minX) * anchorRows +
             (static_cast<int64_t>(m.minZ) - azMin);
    c.density = m.density;
    collector.add(c);
}

int64_t bandRowsFor(int64_t totalRows)
{
    // 常见小范围保持单次 GPU 调用；2048² 这类范围切 2 段，
    // 给 GUI 一次中间进度/取消点，同时把 dispatch 开销控制在低位。
    if (totalRows <= 1024) return totalRows;
    if (totalRows <= 8192) return (totalRows + 1) / 2;
    return (std::max)(int64_t{ 2048 }, (totalRows + 3) / 4);
}

} // namespace

bool FusedGpuPipeline::runPattern(const slime::SearchOptions& opt, const slime::GpuScan* gpu,
                                 const std::atomic<bool>& cancel, ResultCollector& collector,
                                 FusedGpuStats& stats, std::string* err,
                                 const slime::ProgressFn& progress) const
{
    if (!gpu || !gpu->available()) {
        if (err) *err = "GPU 不可用";
        return false;
    }
    if (opt.mode != slime::Mode::Strict || !opt.directionLocked || opt.useCache || opt.buildMode ||
        opt.stopAtTarget || opt.smartRange || opt.ignoreThreshold) {
        if (err) *err = "FusedGpuPattern 只支持方向锁定、无缓存/停止的严格图案";
        return false;
    }
    const int rows = opt.pattern.rows();
    const int cols = opt.pattern.cols();
    if (rows < 1 || cols < 1 || rows > 32 || cols > 32) {
        if (err) *err = "图案尺寸超出 GPU 流式限制";
        return false;
    }
    const int64_t axMin = opt.minX;
    const int64_t axMax = static_cast<int64_t>(opt.maxX) - cols + 1;
    const int64_t azMin = opt.minZ;
    const int64_t azMax = static_cast<int64_t>(opt.maxZ) - rows + 1;
    if (axMax < axMin || azMax < azMin) {
        stats = FusedGpuStats{};
        if (progress) progress(1.0, false, 0.0, 0.0);
        return true;
    }
    std::vector<uint32_t> enable(static_cast<size_t>(rows), 0);
    std::vector<uint32_t> green(static_cast<size_t>(rows), 0);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const auto s = opt.pattern.at(r, c);
            if (s == slime::CellState::Ignore) continue;
            enable[static_cast<size_t>(r)] |= 1u << c;
            if (s == slime::CellState::Green) green[static_cast<size_t>(r)] |= 1u << c;
        }
    }
    const int64_t totalRows = azMax - azMin + 1;
    const int64_t step = bandRowsFor(totalRows);
    const auto t0 = std::chrono::steady_clock::now();
    int64_t totalHits = 0;
    if (progress) progress(0.0, true, 0.0, 0.0);
    for (int64_t b0 = azMin; b0 <= azMax; b0 += step) {
        if (cancel.load(std::memory_order_relaxed)) return false;
        const int64_t b1 = (std::min)(azMax, b0 + step - 1);
        slime::PatternMatchParams p;
        p.version = opt.version;
        p.worldSeed = opt.worldSeed;
        p.axMin = axMin;
        p.axMax = axMax;
        p.azMin = b0;
        p.azMax = b1;
        p.rows = rows;
        p.cols = cols;
        p.enableMasks = enable.data();
        p.greenMasks = green.data();
        p.maxResults = 4000000;
        std::vector<slime::MatchResult> hits;
        if (!gpu->matchPattern(p, cancel, hits, err)) return false;
        for (const auto& m : hits) addResult(opt, azMin, totalRows, m, collector);
        totalHits += static_cast<int64_t>(hits.size());
        if (progress) {
            progress(static_cast<double>(b1 - azMin + 1) / static_cast<double>(totalRows), true, 0.0, 0.0);
        }
    }
    stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    stats.windows = totalRows;
    stats.totalFound = totalHits;
    if (progress) progress(1.0, false, 0.0, 0.0);
    return true;
}

bool FusedGpuPipeline::runDensity(const slime::SearchOptions& opt, const slime::GpuScan* gpu,
                                  const std::atomic<bool>& cancel, ResultCollector& collector,
                                  FusedGpuStats& stats, std::string* err,
                                  const slime::ProgressFn& progress) const
{
    if (!gpu || !gpu->available()) {
        if (err) *err = "GPU 不可用";
        return false;
    }
    if (opt.mode != slime::Mode::Density || opt.algorithm != slime::Algorithm::Exact || opt.useCache ||
        opt.buildMode || opt.stopAtTarget || opt.smartRange || opt.ignoreThreshold) {
        if (err) *err = "FusedGpuDensity 只支持无缓存/停止的精确密度";
        return false;
    }
    const int g = opt.gridSize;
    if (g < 1 || g > 512) {
        if (err) *err = "密度窗口非法";
        return false;
    }
    const int64_t axMin = opt.minX;
    const int64_t axMax = static_cast<int64_t>(opt.maxX) - g + 1;
    const int64_t azMin = opt.minZ;
    const int64_t azMax = static_cast<int64_t>(opt.maxZ) - g + 1;
    if (axMax < axMin || azMax < azMin) {
        stats = FusedGpuStats{};
        if (progress) progress(1.0, false, 0.0, 0.0);
        return true;
    }
    const int64_t totalRows = azMax - azMin + 1;
    const int64_t step = bandRowsFor(totalRows);
    const auto t0 = std::chrono::steady_clock::now();
    int64_t totalFound = 0;
    if (progress) progress(0.0, true, 0.0, 0.0);
    for (int64_t b0 = azMin; b0 <= azMax; b0 += step) {
        if (cancel.load(std::memory_order_relaxed)) return false;
        const int64_t b1 = (std::min)(azMax, b0 + step - 1);
        slime::DensityMatchParams p;
        p.version = opt.version;
        p.worldSeed = opt.worldSeed;
        p.axMin = axMin;
        p.axMax = axMax;
        p.azMin = b0;
        p.azMax = b1;
        p.gridSize = g;
        p.minDensity = opt.minDensity;
        p.maxResults = 4000000;
        p.startX = opt.startX;
        p.startZ = opt.startZ;
        p.keepCandidates = 0;
        int64_t bandTotal = 0;
        p.outTotalFound = &bandTotal;
        std::vector<slime::MatchResult> hits;
        if (!gpu->matchDensity(p, cancel, hits, err)) return false;
        for (const auto& m : hits) addResult(opt, azMin, totalRows, m, collector);
        totalFound += bandTotal;
        if (progress) {
            progress(static_cast<double>(b1 - azMin + 1) / static_cast<double>(totalRows), true, 0.0, 0.0);
        }
    }
    stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    stats.windows = totalFound;
    stats.totalFound = totalFound;
    if (progress) progress(1.0, false, 0.0, 0.0);
    return true;
}

} // namespace slime::next

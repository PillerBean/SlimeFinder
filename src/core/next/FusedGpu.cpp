#include "core/next/FusedGpu.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace slime::next {

namespace {

bool addResult(const slime::SearchOptions& opt, const int64_t azMin, const int64_t anchorRows,
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
    return true;
}

} // namespace

bool FusedGpuPipeline::runPattern(const slime::SearchOptions& opt, const slime::GpuScan* gpu,
                                 const std::atomic<bool>& cancel, ResultCollector& collector,
                                 FusedGpuStats& stats, std::string* err) const
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
    slime::PatternMatchParams p;
    p.version = opt.version;
    p.worldSeed = opt.worldSeed;
    p.axMin = axMin;
    p.axMax = axMax;
    p.azMin = azMin;
    p.azMax = azMax;
    p.rows = rows;
    p.cols = cols;
    p.enableMasks = enable.data();
    p.greenMasks = green.data();
    p.maxResults = 4000000;

    std::vector<slime::MatchResult> hits;
    const auto t0 = std::chrono::steady_clock::now();
    if (!gpu->matchPattern(p, cancel, hits, err)) return false;
    const auto t1 = std::chrono::steady_clock::now();
    const int64_t anchorRows = azMax - azMin + 1;
    for (const auto& m : hits) addResult(opt, azMin, anchorRows, m, collector);
    stats.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats.windows = static_cast<int64_t>(hits.size());
    stats.totalFound = static_cast<int64_t>(hits.size());
    return true;
}

bool FusedGpuPipeline::runDensity(const slime::SearchOptions& opt, const slime::GpuScan* gpu,
                                  const std::atomic<bool>& cancel, ResultCollector& collector,
                                  FusedGpuStats& stats, std::string* err) const
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
        return true;
    }
    slime::DensityMatchParams p;
    p.version = opt.version;
    p.worldSeed = opt.worldSeed;
    p.axMin = axMin;
    p.axMax = axMax;
    p.azMin = azMin;
    p.azMax = azMax;
    p.gridSize = g;
    p.minDensity = opt.minDensity;
    p.maxResults = 4000000;
    p.startX = opt.startX;
    p.startZ = opt.startZ;
    p.keepCandidates = 0;
    int64_t total = 0;
    p.outTotalFound = &total;

    std::vector<slime::MatchResult> hits;
    const auto t0 = std::chrono::steady_clock::now();
    if (!gpu->matchDensity(p, cancel, hits, err)) return false;
    const auto t1 = std::chrono::steady_clock::now();
    const int64_t anchorRows = azMax - azMin + 1;
    for (const auto& m : hits) addResult(opt, azMin, anchorRows, m, collector);
    stats.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats.windows = total;
    stats.totalFound = total;
    return true;
}

} // namespace slime::next

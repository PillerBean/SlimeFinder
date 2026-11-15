#include "core/next/Matcher.h"

#include "core/FastMatch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace slime::next {

namespace {

struct AnchorDims {
    int64_t axMin = 0, axMax = -1, azMin = 0, azMax = -1;
};

AnchorDims anchorDims(const Bitmap& bitmap, int cols, int rows)
{
    AnchorDims d;
    d.axMin = bitmap.x0();
    d.azMin = bitmap.z0();
    d.axMax = static_cast<int64_t>(bitmap.x0()) + bitmap.w() - cols;
    d.azMax = static_cast<int64_t>(bitmap.z0()) + bitmap.h() - rows;
    return d;
}

} // namespace

bool CpuExactMatcher::run(const Bitmap& bitmap, const slime::SearchOptions& opt,
                          const std::atomic<bool>& cancel, ResultCollector& collector,
                          MatchStats& stats, std::string* err, const slime::ProgressFn& progress, const MatchLimits& limits) const
{
    if (opt.smartRange || opt.stopAtTarget || opt.ignoreThreshold) {
        if (err) *err = "Phase2 CPU matcher 暂不支持 smart/stop/best";
        return false;
    }
    if (opt.mode == slime::Mode::Strict && !opt.directionLocked) {
        if (err) *err = "Phase2 CPU matcher 暂不支持旋转图案";
        return false;
    }
    if (opt.mode == slime::Mode::Density && opt.algorithm != slime::Algorithm::Exact) {
        if (err) *err = "Phase2 CPU matcher 只支持精确密度";
        return false;
    }
#ifdef _OPENMP
    if (opt.threads > 0) omp_set_num_threads(opt.threads);
#endif
    if (progress) progress(0.0, false, 0.0, 0.0);
    const auto t0 = std::chrono::steady_clock::now();
    std::atomic<int64_t> windows{ 0 };

    if (opt.mode == slime::Mode::Strict) {
        const int patRows = opt.pattern.rows();
        const int cols = opt.pattern.cols();
        if (patRows <= 0 || cols <= 0) {
            if (err) *err = "图案为空";
            return false;
        }
        if (cols > 64) {
            if (err) *err = "Phase2 matcher 暂不支持超过 64 列的图案";
            return false;
        }
        int active = 0;
        std::vector<uint64_t> enf(static_cast<size_t>(patRows), 0ULL);
        std::vector<uint64_t> grn(static_cast<size_t>(patRows), 0ULL);
        for (int r = 0; r < patRows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const auto state = opt.pattern.at(r, c);
                if (state == slime::CellState::Ignore) continue;
                ++active;
                enf[static_cast<size_t>(r)] |= 1ULL << c;
                if (state == slime::CellState::Green) grn[static_cast<size_t>(r)] |= 1ULL << c;
            }
        }
        if (active == 0) {
            stats.ms = 0.0;
            stats.windows = 0;
            stats.totalFound = 0;
            return true;
        }
        AnchorDims d = anchorDims(bitmap, cols, patRows);
        if (d.axMax < d.axMin || d.azMax < d.azMin) {
            stats.ms = 0.0;
            stats.windows = 0;
            stats.totalFound = 0;
            return true;
        }
        if (limits.zLimited) {
            d.azMin = (std::max)(d.azMin, limits.azMin);
            d.azMax = (std::min)(d.azMax, limits.azMax);
            if (d.azMax < d.azMin) {
                stats.ms = 0.0; stats.windows = 0; stats.totalFound = 0; return true;
            }
        }
        if (limits.xLimited) {
            d.axMin = (std::max)(d.axMin, limits.axMin);
            d.axMax = (std::min)(d.axMax, limits.axMax);
            if (d.axMax < d.axMin) {
                stats.ms = 0.0; stats.windows = 0; stats.totalFound = 0; return true;
            }
        }
        const int64_t nAz = d.azMax - d.azMin + 1;
        const int64_t rankXMin = limits.zLimited ? limits.rankXMin : d.axMin;
        const int64_t rankZMin = limits.zLimited ? limits.rankZMin : d.azMin;
        const int64_t rankAzCount = limits.zLimited ? limits.rankAzCount : nAz;
        const size_t anchorCount = static_cast<size_t>(d.axMax - d.axMin + 1);
        const uint64_t colMask = (cols >= 64) ? ~0ULL : ((1ULL << cols) - 1ULL);
        const int reportEvery = (std::max)(1, static_cast<int>(nAz / 100));
        const auto emit = [&](int64_t ax, int64_t az) {
            const int64_t off2x = 2 * ax + (cols - 1) - 2 * static_cast<int64_t>(opt.startX);
            const int64_t off2z = 2 * az + (patRows - 1) - 2 * static_cast<int64_t>(opt.startZ);
            Candidate c;
            c.result.minX = static_cast<int>(ax);
            c.result.maxX = static_cast<int>(ax + cols - 1);
            c.result.minZ = static_cast<int>(az);
            c.result.maxZ = static_cast<int>(az + patRows - 1);
            c.result.density = 0.0f;
            c.d2 = off2x * off2x + off2z * off2z;
            c.rank = (ax - rankXMin) * rankAzCount + (az - rankZMin);
            collector.add(c);
        };

#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            std::vector<const uint64_t*> rowPtrs(static_cast<size_t>(patRows));
            std::vector<unsigned char> flags(anchorCount);
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 4)
#endif
            for (int64_t az = d.azMin; az <= d.azMax; ++az) {
                if (cancel.load(std::memory_order_relaxed)) continue;
                for (int r = 0; r < patRows; ++r) {
                    rowPtrs[static_cast<size_t>(r)] = bitmap.row(static_cast<int>(az + r));
                }
                slime::strictRowAvx2(rowPtrs.data(), enf.data(), grn.data(), patRows,
                                     bitmap.wordsPerRow(), bitmap.x0(), static_cast<int>(d.axMin),
                                     static_cast<int>(d.axMax), colMask, flags.data());
                windows.fetch_add(static_cast<int64_t>(anchorCount), std::memory_order_relaxed);
                for (size_t i = 0; i < anchorCount; ++i) {
                    if (flags[i]) emit(d.axMin + static_cast<int64_t>(i), az);
                }
                if (progress && (az - d.azMin) % reportEvery == 0) {
                    progress(static_cast<double>(az - d.azMin + 1) / static_cast<double>(nAz), false, 0.0,
                             0.0);
                }
            }
        }
    } else {
        const int g = opt.gridSize;
        if (g <= 0 || g > 512) {
            if (err) *err = "密度窗口非法";
            return false;
        }
        AnchorDims d = anchorDims(bitmap, g, g);
        if (d.axMax < d.axMin || d.azMax < d.azMin) {
            stats.ms = 0.0;
            stats.windows = 0;
            stats.totalFound = 0;
            return true;
        }
        if (limits.zLimited) {
            d.azMin = (std::max)(d.azMin, limits.azMin);
            d.azMax = (std::min)(d.azMax, limits.azMax);
            if (d.azMax < d.azMin) {
                stats.ms = 0.0;
                stats.windows = 0;
                stats.totalFound = 0;
                return true;
            }
        }
        if (limits.xLimited) {
            d.axMin = (std::max)(d.axMin, limits.axMin);
            d.axMax = (std::min)(d.axMax, limits.axMax);
            if (d.axMax < d.axMin) {
                stats.ms = 0.0;
                stats.windows = 0;
                stats.totalFound = 0;
                return true;
            }
        }
        const int64_t nAz = d.azMax - d.azMin + 1;
        const int64_t rankXMin = limits.zLimited ? limits.rankXMin : d.axMin;
        const int64_t rankZMin = limits.zLimited ? limits.rankZMin : d.azMin;
        const int64_t rankAzCount = limits.zLimited ? limits.rankAzCount : nAz;
        const size_t anchorCount = static_cast<size_t>(d.axMax - d.axMin + 1);
        const double area = static_cast<double>(g) * g;
        const int reportEvery = (std::max)(1, static_cast<int>(nAz / 100));
        const auto emit = [&](int64_t ax, int64_t az, int count) {
            const int64_t off2x = 2 * ax + (g - 1) - 2 * static_cast<int64_t>(opt.startX);
            const int64_t off2z = 2 * az + (g - 1) - 2 * static_cast<int64_t>(opt.startZ);
            Candidate c;
            c.result.minX = static_cast<int>(ax);
            c.result.maxX = static_cast<int>(ax + g - 1);
            c.result.minZ = static_cast<int>(az);
            c.result.maxZ = static_cast<int>(az + g - 1);
            c.result.density = static_cast<float>(count) / static_cast<float>(area);
            c.density = c.result.density;
            c.d2 = off2x * off2x + off2z * off2z;
            c.rank = (ax - rankXMin) * rankAzCount + (az - rankZMin);
            collector.add(c);
        };

#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            std::vector<const uint64_t*> rows(static_cast<size_t>(g));
            std::vector<uint32_t> counts(anchorCount);
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 4)
#endif
            for (int64_t az = d.azMin; az <= d.azMax; ++az) {
                if (cancel.load(std::memory_order_relaxed)) continue;
                for (int r = 0; r < g; ++r) rows[static_cast<size_t>(r)] = bitmap.row(static_cast<int>(az + r));
                if (!slime::densityWindowCountsAvx2(rows.data(), g, bitmap.wordsPerRow(),
                                                    d.axMin - bitmap.x0(), d.axMax - bitmap.x0(),
                                                    counts.data())) {
                    continue;
                }
                windows.fetch_add(static_cast<int64_t>(anchorCount), std::memory_order_relaxed);
                for (size_t i = 0; i < anchorCount; ++i) {
                    const int count = static_cast<int>(counts[i]);
                    if (slime::SlimeEngine::densityWindowMeets(count, g, opt.minDensity)) {
                        emit(d.axMin + static_cast<int64_t>(i), az, count);
                    }
                }
                if (progress && (az - d.azMin) % reportEvery == 0) {
                    progress(static_cast<double>(az - d.azMin + 1) / static_cast<double>(nAz), false, 0.0,
                             0.0);
                }
            }
        }
    }

    if (cancel.load(std::memory_order_relaxed)) {
        if (err) *err = "cancelled";
        return false;
    }
    if (progress) progress(1.0, false, 0.0, 0.0);
    stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    stats.windows = windows.load(std::memory_order_relaxed);
    stats.totalFound = collector.totalFound();
    return true;
}

} // namespace slime::next

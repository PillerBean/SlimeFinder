#include "core/next/Matcher.h"

#include "core/FastMatch.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <chrono>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace slime::next {

namespace {

uint64_t extractRowBits(const uint64_t* row, int wpr, int64_t bit) noexcept
{
    const int64_t wi = bit >> 6;
    if (wi < 0 || wi >= wpr) return 0;
    const int off = static_cast<int>(bit & 63);
    uint64_t v = row[wi] >> off;
    if (off != 0 && wi + 1 < wpr) v |= row[wi + 1] << (64 - off);
    return v;
}

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
        struct PatternCell { int row; int col; slime::CellState state; };
        std::vector<PatternCell> cells;
        cells.reserve(static_cast<size_t>(patRows) * cols);
        for (int r = 0; r < patRows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const auto state = opt.pattern.at(r, c);
                if (state == slime::CellState::Ignore) continue;
                ++active;
                cells.push_back(PatternCell{ r, c, state });
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
        const int reportEvery = (std::max)(1, static_cast<int>(nAz / 100));
        const auto emit = [&](ResultCollector& out, int64_t ax, int64_t az) {
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
            out.add(c);
        };

#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            std::vector<const uint64_t*> rowPtrs(static_cast<size_t>(patRows));
            ResultCollector local(CollectorKind::DistanceTopN, opt.targetCount);
            const int wpr = bitmap.wordsPerRow();
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 4)
#endif
            for (int64_t az = d.azMin; az <= d.azMax; ++az) {
                if (cancel.load(std::memory_order_relaxed)) continue;
                for (int r = 0; r < patRows; ++r) {
                    rowPtrs[static_cast<size_t>(r)] = bitmap.row(static_cast<int>(az + r));
                }
                for (int64_t blockStart = d.axMin; blockStart <= d.axMax; blockStart += 64) {
                    const int nValid = static_cast<int>((std::min)(int64_t{ 64 }, d.axMax - blockStart + 1));
                    const uint64_t validMask = nValid >= 64 ? ~0ULL : ((1ULL << nValid) - 1ULL);
                    uint64_t cand = ~0ULL;
                    for (const auto& cell : cells) {
                        const int64_t rel = (blockStart - bitmap.x0()) + cell.col;
                        uint64_t bits = extractRowBits(rowPtrs[static_cast<size_t>(cell.row)], wpr, rel);
                        bits = cell.state == slime::CellState::Green ? bits : ~bits;
                        cand &= bits;
                        if (!cand) break;
                    }
                    cand &= validMask;
                    while (cand) {
                        const int bit = std::countr_zero(cand);
                        cand &= cand - 1;
                        emit(local, blockStart + bit, az);
                    }
                }
                windows.fetch_add(static_cast<int64_t>(anchorCount), std::memory_order_relaxed);
                if (progress && (az - d.azMin) % reportEvery == 0) {
                    progress(static_cast<double>(az - d.azMin + 1) / static_cast<double>(nAz), false, 0.0,
                             0.0);
                }
            }
            collector.merge(local);
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
        const auto emit = [&](ResultCollector& out, int64_t ax, int64_t az, int count) {
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
            out.add(c);
        };

        const int w = bitmap.w();
        const int wpr = bitmap.wordsPerRow();
        const int64_t baseRel = d.axMin - bitmap.x0();
#ifdef _OPENMP
        const int64_t threads = (std::max)(int64_t{ 1 }, static_cast<int64_t>(omp_get_max_threads()));
        const int64_t wantedBlocks = nAz <= 1024
                                         ? threads
                                         : (nAz <= 4096 ? threads * 2 : threads * 4);
        const int64_t blocks = (std::min)(nAz, wantedBlocks);
#else
        const int64_t blocks = 1;
#endif
        const int64_t blockRows = (nAz + blocks - 1) / blocks;
        const int minCount = (std::max)(0, static_cast<int>(std::ceil(opt.minDensity * area - 1e-12)));
        const size_t maskWords = (anchorCount + 63) / 64;
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            // vertSumDiffAvx2 按 64 列整字写回，必须按 wpr*64 分配，不能只按 w。
            std::vector<int32_t> vert(static_cast<size_t>(wpr) * 64, 0);
            std::vector<int32_t> counts(anchorCount);
            std::vector<uint64_t> hitMask(maskWords);
            ResultCollector local(CollectorKind::DistanceTopN, opt.targetCount);
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 1)
#endif
            for (int64_t bi = 0; bi < blocks; ++bi) {
                const int64_t az0 = d.azMin + bi * blockRows;
                if (az0 > d.azMax) continue;
                const int64_t az1 = (std::min)(d.azMax, az0 + blockRows - 1);
                std::fill(vert.begin(), vert.end(), 0);
                for (int r = 0; r < g; ++r) {
                    slime::vertSumDiffAvx2(vert.data(), bitmap.row(static_cast<int>(az0 + r)), nullptr, wpr, w, 0);
                }
                for (int64_t az = az0; az <= az1; ++az) {
                    if (cancel.load(std::memory_order_relaxed)) break;
                    windows.fetch_add(static_cast<int64_t>(anchorCount), std::memory_order_relaxed);
                    bool handled = false;
                    if (minCount > 0 &&
                        slime::densityHitMaskAvx2(vert.data() + baseRel, static_cast<int64_t>(anchorCount), g,
                                                  minCount - 1, hitMask.data(),
                                                  static_cast<int64_t>(maskWords))) {
                        handled = true;
                        for (size_t wi = 0; wi < maskWords; ++wi) {
                            uint64_t m = hitMask[wi];
                            while (m) {
                                const int b = std::countr_zero(m);
                                m &= m - 1;
                                const int64_t i = static_cast<int64_t>(wi * 64 + b);
                                if (i >= static_cast<int64_t>(anchorCount)) break;
                                int count = 0;
                                for (int k = 0; k < g; ++k) {
                                    count += vert[static_cast<size_t>(baseRel + i + k)];
                                }
                                emit(local, d.axMin + i, az, count);
                            }
                        }
                    }
                    if (!handled) {
                        bool sumsOk = slime::densityWindowSumsAvx2(vert.data() + baseRel,
                                                                   static_cast<int64_t>(anchorCount), g,
                                                                   counts.data());
                        if (!sumsOk) {
                            int32_t sum = 0;
                            for (int x = 0; x < w; ++x) {
                                sum += vert[static_cast<size_t>(x)];
                                if (x >= g) sum -= vert[static_cast<size_t>(x - g)];
                                if (x >= g - 1) {
                                    const int64_t rel = x - g + 1;
                                    if (rel >= baseRel && rel < baseRel + static_cast<int64_t>(anchorCount)) {
                                        counts[static_cast<size_t>(rel - baseRel)] = sum;
                                    }
                                }
                            }
                        }
                        for (size_t i = 0; i < anchorCount; ++i) {
                            const int count = static_cast<int>(counts[i]);
                            if (slime::SlimeEngine::densityWindowMeets(count, g, opt.minDensity)) {
                                emit(local, d.axMin + static_cast<int64_t>(i), az, count);
                            }
                        }
                    }
                    if (az < az1) {
                        slime::vertSumDiffAvx2(vert.data(), bitmap.row(static_cast<int>(az + g)),
                                               bitmap.row(static_cast<int>(az)), wpr, w, 0);
                    }
                    if (progress && ((az - d.azMin) % reportEvery == 0 || az == az1)) {
                        progress(static_cast<double>(az - d.azMin + 1) / static_cast<double>(nAz), false, 0.0,
                                 0.0);
                    }
                }
            }
            collector.merge(local);
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

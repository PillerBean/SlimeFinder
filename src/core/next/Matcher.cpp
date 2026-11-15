#include "core/next/Matcher.h"

#include <algorithm>
#include <chrono>

namespace slime::next {

bool CpuExactMatcher::run(const Bitmap& bitmap, const slime::SearchOptions& opt,
                          const std::atomic<bool>& cancel, ResultCollector& collector,
                          MatchStats& stats, std::string* err) const
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

    const auto t0 = std::chrono::steady_clock::now();
    int64_t windows = 0;
    int64_t total = 0;

    if (opt.mode == slime::Mode::Strict) {
        const int rows = opt.pattern.rows();
        const int cols = opt.pattern.cols();
        if (rows <= 0 || cols <= 0) {
            if (err) *err = "图案为空";
            return false;
        }
        int active = 0;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (opt.pattern.at(r, c) != slime::CellState::Ignore) ++active;
            }
        }
        if (active == 0) {
            stats.ms = 0.0;
            stats.windows = 0;
            stats.totalFound = 0;
            return true;
        }

        const int64_t axMin = bitmap.x0();
        const int64_t azMin = bitmap.z0();
        const int64_t axMax = static_cast<int64_t>(bitmap.x0()) + bitmap.w() - cols;
        const int64_t azMax = static_cast<int64_t>(bitmap.z0()) + bitmap.h() - rows;
        if (axMax < axMin || azMax < azMin) {
            stats.ms = 0.0;
            stats.windows = 0;
            stats.totalFound = 0;
            return true;
        }
        const int64_t nAz = azMax - azMin + 1;
        for (int64_t az = azMin; az <= azMax; ++az) {
            if (cancel.load(std::memory_order_relaxed)) return false;
            for (int64_t ax = axMin; ax <= axMax; ++ax) {
                ++windows;
                bool ok = true;
                for (int r = 0; r < rows && ok; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        const auto state = opt.pattern.at(r, c);
                        if (state == slime::CellState::Ignore) continue;
                        const bool slime = bitmap.isSlime(static_cast<int>(ax + c),
                                                          static_cast<int>(az + r));
                        if ((state == slime::CellState::Green) != slime) {
                            ok = false;
                            break;
                        }
                    }
                }
                if (!ok) continue;
                ++total;
                const int64_t off2x = 2 * ax + (cols - 1) - 2 * static_cast<int64_t>(opt.startX);
                const int64_t off2z = 2 * az + (rows - 1) - 2 * static_cast<int64_t>(opt.startZ);
                Candidate c;
                c.result.minX = static_cast<int>(ax);
                c.result.maxX = static_cast<int>(ax + cols - 1);
                c.result.minZ = static_cast<int>(az);
                c.result.maxZ = static_cast<int>(az + rows - 1);
                c.result.density = 0.0f;
                c.d2 = off2x * off2x + off2z * off2z;
                c.rank = (ax - axMin) * nAz + (az - azMin);
                collector.add(c);
            }
        }
    } else {
        const int g = opt.gridSize;
        if (g <= 0 || g > 512) {
            if (err) *err = "密度窗口非法";
            return false;
        }
        const int64_t axMin = bitmap.x0();
        const int64_t azMin = bitmap.z0();
        const int64_t axMax = static_cast<int64_t>(bitmap.x0()) + bitmap.w() - g;
        const int64_t azMax = static_cast<int64_t>(bitmap.z0()) + bitmap.h() - g;
        if (axMax < axMin || azMax < azMin) {
            stats.ms = 0.0;
            stats.windows = 0;
            stats.totalFound = 0;
            return true;
        }
        const int64_t nAz = azMax - azMin + 1;
        const double area = static_cast<double>(g) * g;
        for (int64_t az = azMin; az <= azMax; ++az) {
            if (cancel.load(std::memory_order_relaxed)) return false;
            for (int64_t ax = axMin; ax <= axMax; ++ax) {
                ++windows;
                int count = 0;
                for (int r = 0; r < g; ++r) {
                    for (int c = 0; c < g; ++c) {
                        if (bitmap.isSlime(static_cast<int>(ax + c), static_cast<int>(az + r))) ++count;
                    }
                }
                if (!slime::SlimeEngine::densityWindowMeets(count, g, opt.minDensity)) continue;
                ++total;
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
                c.rank = (ax - axMin) * nAz + (az - azMin);
                collector.add(c);
            }
        }
    }

    stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    stats.windows = windows;
    stats.totalFound = total;
    return true;
}

} // namespace slime::next

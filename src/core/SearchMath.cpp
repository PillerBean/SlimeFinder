#include "core/SearchMath.h"

#include <algorithm>
#include <cmath>

namespace slime {

double binomialTail(int n, double p, int k)
{
    if (n <= 0) return 0.0;
    if (k <= 0) return 1.0;
    if (k > n) return 0.0;
    if (p <= 0.0) return 0.0;
    if (p >= 1.0) return 1.0;

    const long double q = 1.0L - static_cast<long double>(p);
    const long double pp = static_cast<long double>(p);
    const long double mean = static_cast<long double>(n) * pp;

    auto logPmf = [&](int i) -> long double {
        return std::lgamma(static_cast<long double>(n) + 1.0L) -
               std::lgamma(static_cast<long double>(i) + 1.0L) -
               std::lgamma(static_cast<long double>(n - i) + 1.0L) +
               static_cast<long double>(i) * std::log(pp) +
               static_cast<long double>(n - i) * std::log(q);
    };

    // 上尾：从 k 开始，项随 i 增大而递减，通常在很少几项内收敛。
    if (static_cast<long double>(k) > mean) {
        long double term = std::exp(logPmf(k));
        long double sum = term;
        for (int i = k; i < n; ++i) {
            term *= (static_cast<long double>(n - i) / static_cast<long double>(i + 1)) * (pp / q);
            sum += term;
            if (term <= sum * 1e-18L) break;
        }
        return static_cast<double>(std::clamp(sum, 0.0L, 1.0L));
    }

    // 下尾较小：P(X <= k-1)，再用 1-下尾。
    long double term = std::pow(q, static_cast<long double>(n));
    long double sum = term;
    for (int i = 0; i < k - 1; ++i) {
        term *= (static_cast<long double>(n - i) / static_cast<long double>(i + 1)) * (pp / q);
        sum += term;
        if (term <= sum * 1e-18L) break;
    }
    return static_cast<double>(std::clamp(1.0L - sum, 0.0L, 1.0L));
}

HitProbability predictHits(const SearchOptions& opt)
{
    HitProbability r;
    const int64_t W = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t H = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    if (W <= 0 || H <= 0) return r;

    constexpr double pSlime = 0.1;
    if (opt.mode == Mode::Strict) {
        const int rows = opt.pattern.rows();
        const int cols = opt.pattern.cols();
        if (rows <= 0 || cols <= 0) return r;
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                const CellState st = opt.pattern.at(i, j);
                if (st == CellState::Green) ++r.green;
                else if (st == CellState::White) ++r.white;
            }
        }
        r.candidatePositions = (W - cols + 1) * (H - rows + 1);
        if (r.candidatePositions <= 0) return r;

        long double single = std::pow(static_cast<long double>(pSlime), r.green) *
                             std::pow(static_cast<long double>(1.0 - pSlime), r.white);
        if (!opt.directionLocked) {
            // 四方向联合的近似；同一位置只计数一次。
            single = 1.0L - std::pow(1.0L - single, 4.0L);
            r.directionUnion = true;
        }
        r.singleWindow = static_cast<double>(std::clamp(single, 0.0L, 1.0L));
    } else {
        const int g = (std::max)(1, opt.gridSize);
        const int64_t n = static_cast<int64_t>(g) * g;
        r.gridSize = g;
        r.minDensity = std::clamp(opt.minDensity, 0.0, 1.0);
        const int k = static_cast<int>(std::ceil(r.minDensity * static_cast<double>(n) - 1e-9));
        r.candidatePositions = (W - g + 1) * (H - g + 1);
        if (r.candidatePositions <= 0) return r;
        r.singleWindow = (k <= 0) ? 1.0 : (k > n ? 0.0 : binomialTail(static_cast<int>(n), pSlime, k));
    }

    r.expectedHits = static_cast<double>(r.candidatePositions) * r.singleWindow;
    if (r.singleWindow <= 0.0) {
        r.probabilityAtLeastOne = 0.0;
    } else if (r.singleWindow >= 1.0) {
        r.probabilityAtLeastOne = 1.0;
    } else {
        const long double logMiss = static_cast<long double>(r.candidatePositions) *
                                    std::log1p(-static_cast<long double>(r.singleWindow));
        r.probabilityAtLeastOne = static_cast<double>(std::clamp(1.0L - std::exp(logMiss), 0.0L, 1.0L));
    }
    r.ok = true;
    return r;
}

} // namespace slime

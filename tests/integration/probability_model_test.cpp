#include "core/SearchMath.h"

#include <cmath>
#include <cstdio>

int main()
{
    const double tail0 = slime::binomialTail(10, 0.1, 0);
    const double tail1 = slime::binomialTail(10, 0.1, 1);
    const double tail3 = slime::binomialTail(10, 0.1, 3);
    std::printf("tail0=%.12f tail1=%.12f tail3=%.12f\n", tail0, tail1, tail3);
    if (std::abs(tail0 - 1.0) > 1e-12) return 1;
    if (std::abs(tail1 - (1.0 - std::pow(0.9, 10))) > 1e-9) return 1;
    if (!(tail3 > 0.0 && tail3 < tail1)) return 1;

    slime::SearchOptions opt;
    opt.version = slime::Version::Java;
    opt.minX = 0;
    opt.maxX = 399;
    opt.minZ = 0;
    opt.maxZ = 399;
    opt.targetCount = 10;
    opt.mode = slime::Mode::Density;
    opt.gridSize = 5;
    opt.minDensity = 0.3;
    const auto density = slime::predictHits(opt);
    std::printf("density positions=%lld single=%.9f expected=%.3f atLeastOne=%.9f\n",
                static_cast<long long>(density.candidatePositions), density.singleWindow,
                density.expectedHits, density.probabilityAtLeastOne);
    if (!density.ok || density.candidatePositions != 396LL * 396LL) return 1;
    if (!(density.singleWindow > 0.0 && density.singleWindow < 0.01)) return 1;
    if (!(density.expectedHits > 0.0)) return 1;

    opt.mode = slime::Mode::Strict;
    opt.pattern = slime::Pattern(1, 1);
    opt.pattern.set(0, 0, slime::CellState::Green);
    opt.directionLocked = true;
    const auto locked = slime::predictHits(opt);
    std::printf("pattern locked positions=%lld single=%.9f expected=%.3f\n",
                static_cast<long long>(locked.candidatePositions), locked.singleWindow,
                locked.expectedHits);
    if (!locked.ok || std::abs(locked.singleWindow - 0.1) > 1e-12) return 1;
    if (locked.candidatePositions != 400LL * 400LL) return 1;

    opt.directionLocked = false;
    const auto unlocked = slime::predictHits(opt);
    std::printf("pattern union single=%.9f expected=%.3f\n", unlocked.singleWindow, unlocked.expectedHits);
    if (!(unlocked.singleWindow > locked.singleWindow && unlocked.singleWindow < 0.4)) return 1;

    std::printf("PROBABILITY-MODEL OK\n");
    return 0;
}

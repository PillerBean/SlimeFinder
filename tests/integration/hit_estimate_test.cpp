#include "core/SlimeEngine.h"

#include <cstdio>

int main()
{
    slime::SearchOptions opt;
    opt.version = slime::Version::Bedrock;
    opt.minX = -256;
    opt.maxX = 255;
    opt.minZ = -256;
    opt.maxZ = 255;
    opt.mode = slime::Mode::Strict;
    opt.algorithm = slime::Algorithm::Exact;
    opt.pattern = slime::Pattern(1, 1);
    opt.pattern.set(0, 0, slime::CellState::Green);
    opt.directionLocked = true;
    opt.targetCount = 50;
    const auto one = slime::SlimeEngine::estimateHits(opt);
    std::printf("one-cell candidates=%lld sampled=%lld hits=%lld estimated=%lld rate=%.3f ok=%d\n",
                static_cast<long long>(one.candidateWindows), static_cast<long long>(one.sampledWindows),
                static_cast<long long>(one.sampleHits), static_cast<long long>(one.estimatedHits),
                one.hitRate, one.ok ? 1 : 0);
    if (!one.ok || one.candidateWindows <= 0 || one.sampledWindows <= 0 || one.estimatedHits <= 0) return 1;

    opt.pattern.set(0, 0, slime::CellState::White);
    const auto none = slime::SlimeEngine::estimateHits(opt);
    std::printf("white-cell estimated=%lld rate=%.3f\n", static_cast<long long>(none.estimatedHits),
                none.hitRate);
    if (!none.ok || none.hitRate <= 0.0) return 1;

    opt.mode = slime::Mode::Density;
    opt.gridSize = 5;
    opt.minDensity = 0.3;
    const auto dense = slime::SlimeEngine::estimateHits(opt);
    std::printf("density candidates=%lld estimated=%lld rate=%.3f\n",
                static_cast<long long>(dense.candidateWindows), static_cast<long long>(dense.estimatedHits),
                dense.hitRate);
    if (!dense.ok || dense.estimatedHits < 0) return 1;
    std::printf("HIT-ESTIMATE OK\n");
    return 0;
}

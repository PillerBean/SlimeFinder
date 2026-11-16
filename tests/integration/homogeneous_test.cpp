#include "core/SlimeEngine.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <set>

int main()
{
    slime::SearchOptions opt;
    opt.version = slime::Version::Bedrock;
    opt.minX = -512;
    opt.maxX = 511;
    opt.minZ = -512;
    opt.maxZ = 511;
    opt.mode = slime::Mode::Density;
    opt.algorithm = slime::Algorithm::Exact;
    opt.gridSize = 5;
    opt.minDensity = 0.2;
    opt.targetCount = 10;
    opt.threads = 4;

    slime::SlimeEngine engine;
    opt.allowHomogeneousResults = true;
    slime::SearchOutcome a;
    engine.run(opt, std::atomic<bool>{}, slime::ProgressFn(), a);
    opt.allowHomogeneousResults = false;
    slime::SearchOutcome b;
    engine.run(opt, std::atomic<bool>{}, slime::ProgressFn(), b);
    std::set<int> qa, qb;
    for (const auto& r : a.results) qa.insert(static_cast<int>(std::lround(r.density * 1000.0)));
    for (const auto& r : b.results) qb.insert(static_cast<int>(std::lround(r.density * 1000.0)));
    std::printf("allow=%zu/%zu distinct=%zu deny=%zu/%zu distinct=%zu\n", a.results.size(), a.totalFound,
                qa.size(), b.results.size(), b.totalFound, qb.size());
    if (!a.ok || !b.ok) return 1;
    if (a.results.empty() || b.results.empty()) return 1;
    if (qb.size() != b.results.size()) return 1;
    
    std::printf("HOMOGENEOUS OK\n");
    return 0;
}

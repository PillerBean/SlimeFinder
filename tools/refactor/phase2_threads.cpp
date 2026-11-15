// Phase 2 多线程对拍：1/4/12 线程，新旧路径结果一致。
#include "core/next/Pipeline.h"
#include "core/SlimeEngine.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

bool same(const std::vector<slime::MatchResult>& a, const std::vector<slime::MatchResult>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].minX != b[i].minX || a[i].maxX != b[i].maxX ||
            a[i].minZ != b[i].minZ || a[i].maxZ != b[i].maxZ) return false;
    }
    return true;
}

} // namespace

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    const slime::Version versions[] = { slime::Version::Java, slime::Version::Bedrock };
    const slime::Mode modes[] = { slime::Mode::Strict, slime::Mode::Density };
    int failures = 0;
    for (auto version : versions) {
        for (auto mode : modes) {
            for (int threads : { 1, 4, 12 }) {
                slime::SearchOptions opt;
                opt.version = version;
                opt.worldSeed = 12345;
                opt.minX = -256;
                opt.maxX = 255;
                opt.minZ = -256;
                opt.maxZ = 255;
                opt.mode = mode;
                opt.algorithm = slime::Algorithm::Exact;
                opt.gridSize = 5;
                opt.minDensity = 0.3;
                opt.targetCount = 50;
                opt.threads = threads;
                opt.directionLocked = true;
                if (mode == slime::Mode::Strict) {
                    slime::Pattern p(2, 2);
                    for (int r = 0; r < 2; ++r)
                        for (int c = 0; c < 2; ++c) p.set(r, c, slime::CellState::Green);
                    opt.pattern = p;
                }
                std::atomic<bool> cancel{ false };
                slime::SearchOutcome oldOut;
                slime::SlimeEngine oldEngine;
                oldEngine.run(opt, cancel, slime::ProgressFn(), oldOut);
                slime::next::MaterializeResult newOut;
                slime::next::MaterializePipeline pipeline;
                std::string err;
                const bool newOk = pipeline.run(opt, cancel, newOut, &err);
                const bool sameRes = oldOut.ok && newOk && same(oldOut.results, newOut.results) &&
                                     oldOut.totalFound == newOut.totalFound;
                std::printf("%-8s %-8s t=%2d old=%6.2fms new=%6.2fms %s\n",
                            version == slime::Version::Java ? "Java" : "Bedrock",
                            mode == slime::Mode::Strict ? "pattern" : "density", threads,
                            oldOut.stats.totalMs, newOut.scan.ms + newOut.match.ms,
                            sameRes ? "SAME" : "DIFF");
                if (!sameRes) ++failures;
            }
        }
    }
    std::printf("PHASE2-THREADS %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

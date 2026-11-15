// Phase 2 对拍：MaterializePipeline vs 旧 SlimeEngine::run。
#include "core/next/Pipeline.h"
#include "core/SlimeEngine.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Case {
    slime::Version version;
    slime::Mode mode;
    const char* name;
};

bool sameResults(const std::vector<slime::MatchResult>& a,
                 const std::vector<slime::MatchResult>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].minX != b[i].minX || a[i].maxX != b[i].maxX ||
            a[i].minZ != b[i].minZ || a[i].maxZ != b[i].maxZ) {
            return false;
        }
        if (std::fabs(a[i].density - b[i].density) > 1e-6f) return false;
    }
    return true;
}

} // namespace

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    _putenv_s("OMP_NUM_THREADS", "1");
    const Case cases[] = {
        { slime::Version::Java, slime::Mode::Strict, "Java-pattern" },
        { slime::Version::Java, slime::Mode::Density, "Java-density" },
        { slime::Version::Bedrock, slime::Mode::Strict, "Bedrock-pattern" },
        { slime::Version::Bedrock, slime::Mode::Density, "Bedrock-density" },
    };

    int failures = 0;
    for (const Case& c : cases) {
        slime::SearchOptions opt;
        opt.version = c.version;
        opt.worldSeed = 12345;
        opt.minX = -256;
        opt.maxX = 255;
        opt.minZ = -256;
        opt.maxZ = 255;
        opt.mode = c.mode;
        opt.algorithm = slime::Algorithm::Exact;
        opt.gridSize = 5;
        opt.minDensity = 0.3;
        opt.targetCount = 50;
        opt.stopAtTarget = false;
        opt.smartRange = false;
        opt.threads = 1;
        opt.directionLocked = true;
        if (c.mode == slime::Mode::Strict) {
            slime::Pattern p(2, 2);
            for (int r = 0; r < 2; ++r)
                for (int col = 0; col < 2; ++col) p.set(r, col, slime::CellState::Green);
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
        const bool oldOk = oldOut.ok;

        const bool resultsOk = oldOk && newOk && sameResults(oldOut.results, newOut.results);
        const bool countOk = oldOk && newOk && oldOut.totalFound == newOut.totalFound;
        std::printf("%-16s old=%s(%zu/%lld %.2fms) new=%s(%zu/%lld %.2fms) results=%s count=%s\n",
                    c.name, oldOk ? "OK" : "FAIL", oldOut.results.size(),
                    static_cast<long long>(oldOut.totalFound), oldOut.stats.totalMs,
                    newOk ? "OK" : "FAIL", newOut.results.size(),
                    static_cast<long long>(newOut.totalFound),
                    newOut.scan.ms + newOut.match.ms,
                    resultsOk ? "SAME" : "DIFF", countOk ? "SAME" : "DIFF");
        if (!newOk) std::printf("  new error: %s\n", err.c_str());
        if (!resultsOk) {
            const size_t n = (std::min)(oldOut.results.size(), newOut.results.size());
            size_t shown = 0;
            for (size_t i = 0; i < n && shown < 5; ++i) {
                if (oldOut.results[i].minX != newOut.results[i].minX ||
                    oldOut.results[i].minZ != newOut.results[i].minZ) {
                    std::printf("  diff[%zu] old=(%d,%d) new=(%d,%d)\n", i,
                                oldOut.results[i].minX, oldOut.results[i].minZ,
                                newOut.results[i].minX, newOut.results[i].minZ);
                    ++shown;
                }
            }
        }
        if (!oldOk || !newOk || !resultsOk || !countOk) ++failures;
    }
    std::printf("PHASE2 %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

// Phase 14：字级图案 matcher 的多形状/白格/忽略格对拍。
#include "core/SlimeEngine.h"
#include "core/next/PlanExecutor.h"

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

slime::Pattern makePattern(int rows, int cols, int variant)
{
    slime::Pattern p(rows, cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int v = (r * 7 + c * 3 + variant * 5) % 7;
            slime::CellState s = slime::CellState::Ignore;
            if (v == 0 || v == 3) s = slime::CellState::Green;
            else if (v == 1) s = slime::CellState::White;
            p.set(r, c, s);
        }
    }
    return p;
}
} // namespace

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    _putenv_s("OMP_NUM_THREADS", "1");
    slime::next::MachineCaps caps;
    caps.gpuAvailable = false;
    caps.classicChunkLimit = int64_t{ 1 } << 40;

    int failures = 0;
    for (auto version : { slime::Version::Java, slime::Version::Bedrock }) {
        for (int variant = 0; variant < 7; ++variant) {
            slime::SearchOptions opt;
            opt.version = version;
            opt.worldSeed = 12345;
            opt.minX = -320;
            opt.maxX = 319;
            opt.minZ = -280;
            opt.maxZ = 279;
            opt.mode = slime::Mode::Strict;
            opt.algorithm = slime::Algorithm::Exact;
            opt.targetCount = 80;
            opt.threads = 4;
            opt.directionLocked = true;
            opt.pattern = makePattern(2 + variant % 3, 2 + variant % 5, variant);

            slime::SearchOptions oldOpt = opt;
            oldOpt.gpuScan = nullptr;
            std::atomic<bool> cancel{ false };
            slime::SearchOutcome oldOut;
            slime::SlimeEngine oldEngine;
            oldEngine.run(oldOpt, cancel, slime::ProgressFn(), oldOut);

            slime::next::PlannerInput pin;
            pin.options = opt;
            pin.caps = caps;
            const auto plan = slime::next::makeExecutionPlan(pin);
            slime::next::MaterializeResult newOut;
            std::string err;
            slime::next::PlanExecutor executor;
            const bool ok = executor.run(opt, plan, cancel, newOut, &err);
            const bool sameRes = oldOut.ok && ok && same(oldOut.results, newOut.results) &&
                                 oldOut.totalFound == newOut.totalFound;
            std::printf("%-8s variant=%d %dx%d old=%zu/%lld new=%zu/%lld %s\n",
                        version == slime::Version::Java ? "Java" : "Bedrock", variant, opt.pattern.rows(),
                        opt.pattern.cols(), oldOut.results.size(),
                        static_cast<long long>(oldOut.totalFound), newOut.results.size(),
                        static_cast<long long>(newOut.totalFound), sameRes ? "SAME" : "DIFF");
            if (!sameRes) ++failures;
        }
    }
    std::printf("PHASE14-PATTERN-VARIANTS %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL",
                failures);
    return failures == 0 ? 0 : 1;
}

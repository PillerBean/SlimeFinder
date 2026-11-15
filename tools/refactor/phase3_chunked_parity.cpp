// Phase 3 分块对拍：runChunked vs 旧 SlimeEngine::run。
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
    _putenv_s("OMP_NUM_THREADS", "1");
    int failures = 0;
    for (auto version : { slime::Version::Java, slime::Version::Bedrock }) {
        for (auto mode : { slime::Mode::Strict, slime::Mode::Density }) {
            for (int64_t chunkRows : { 37, 100 }) {
                slime::SearchOptions opt;
                opt.version = version;
                opt.worldSeed = 12345;
                opt.minX = -300;
                opt.maxX = 299;
                opt.minZ = -300;
                opt.maxZ = 299;
                opt.mode = mode;
                opt.algorithm = slime::Algorithm::Exact;
                opt.gridSize = 5;
                opt.minDensity = 0.3;
                opt.targetCount = 50;
                opt.threads = 1;
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
                const bool ok = pipeline.runChunked(opt, chunkRows, cancel, newOut, &err);
                const bool sameRes = oldOut.ok && ok && same(oldOut.results, newOut.results) &&
                                     oldOut.totalFound == newOut.totalFound;
                std::printf("%-8s %-8s chunk=%3lld old=%zu/%lld new=%zu/%lld %s\n",
                            version == slime::Version::Java ? "Java" : "Bedrock",
                            mode == slime::Mode::Strict ? "pattern" : "density",
                            static_cast<long long>(chunkRows), oldOut.results.size(),
                            static_cast<long long>(oldOut.totalFound), newOut.results.size(),
                            static_cast<long long>(newOut.totalFound), sameRes ? "SAME" : "DIFF");
                if (!sameRes) {
                    std::printf("  err=%s\n", err.c_str());
                    ++failures;
                }
            }
        }
    }
    std::printf("PHASE3-CHUNKED %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

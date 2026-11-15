// Phase 11：SearchTask feature flag 随机参数矩阵。
#include "core/SearchTask.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
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
    _putenv_s("SLIME_NEXT_PIPELINE", "1");
    std::string gpuErr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (gpu && !gpu->available()) gpu.reset();
    if (!gpu) {
        std::printf("GPU unavailable: %s\n", gpuErr.c_str());
        return 1;
    }

    uint64_t state = 0xD1B54A32D192ED03ULL;
    auto rnd = [&]() {
        state ^= state << 7;
        state ^= state >> 9;
        return state;
    };

    int cases = 0;
    int failures = 0;
    for (int cycle = 0; cycle < 4; ++cycle) {
        for (int i = 0; i < 6; ++i) {
            slime::SearchOptions opt;
            opt.version = (rnd() & 1) ? slime::Version::Java : slime::Version::Bedrock;
            opt.worldSeed = static_cast<int64_t>(rnd());
            opt.minX = -static_cast<int>(rnd() % 4000);
            opt.minZ = -static_cast<int>(rnd() % 4000);
            const int w = 128 + static_cast<int>(rnd() % 897);
            const int h = 128 + static_cast<int>(rnd() % 897);
            opt.maxX = opt.minX + w - 1;
            opt.maxZ = opt.minZ + h - 1;
            opt.mode = (rnd() & 1) ? slime::Mode::Strict : slime::Mode::Density;
            opt.algorithm = slime::Algorithm::Exact;
            opt.gridSize = 5;
            opt.minDensity = 0.3;
            opt.targetCount = 10 + static_cast<int>(rnd() % 80);
            opt.threads = 4;
            opt.directionLocked = true;
            opt.gpuScan = gpu.get();
            if (opt.mode == slime::Mode::Strict) {
                slime::Pattern p(2, 2);
                for (int r = 0; r < 2; ++r)
                    for (int c = 0; c < 2; ++c) p.set(r, c, slime::CellState::Green);
                opt.pattern = p;
            }

            slime::SearchOptions oldOpt = opt;
            oldOpt.gpuScan = nullptr;
            std::atomic<bool> cancel{ false };
            slime::SearchOutcome oldOut;
            slime::SlimeEngine oldEngine;
            oldEngine.run(oldOpt, cancel, slime::ProgressFn(), oldOut);

            slime::SearchOutcome newOut;
            std::atomic<bool> done{ false };
            std::atomic<int> progressCount{ 0 };
            std::mutex mu;
            double last = -1.0;
            bool monotonic = true;
            slime::SearchTask task;
            task.start(opt, [&](double f, bool, double, double) {
                std::lock_guard<std::mutex> lk(mu);
                if (f + 1e-9 < last) monotonic = false;
                last = f;
                ++progressCount;
            }, [&](const slime::SearchOutcome& o) {
                newOut = o;
                done.store(true, std::memory_order_release);
            });
            task.wait();
            ++cases;
            const bool ok = done.load() && oldOut.ok && newOut.ok && same(oldOut.results, newOut.results) &&
                            oldOut.totalFound == newOut.totalFound && monotonic &&
                            progressCount.load() >= 1 && last >= 0.999;
            if (!ok) {
                ++failures;
                std::printf("DIFF cycle=%d i=%d version=%d mode=%d area=%dx%d done=%d old=%zu/%lld new=%zu/%lld progress=%d last=%.3f\n",
                            cycle + 1, i, static_cast<int>(opt.version), static_cast<int>(opt.mode), w, h,
                            done.load() ? 1 : 0, oldOut.results.size(),
                            static_cast<long long>(oldOut.totalFound), newOut.results.size(),
                            static_cast<long long>(newOut.totalFound), progressCount.load(), last);
            }
        }
    }
    std::printf("PHASE11-SEARCHTASK-RANDOM cases=%d failures=%d %s\n", cases, failures,
                failures == 0 ? "ALL PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}

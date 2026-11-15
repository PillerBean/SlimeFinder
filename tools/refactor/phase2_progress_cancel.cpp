// Phase 2 进度/取消检查。
#include "core/next/Pipeline.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    int failures = 0;

    {
        slime::SearchOptions opt;
        opt.version = slime::Version::Java;
        opt.worldSeed = 12345;
        opt.minX = -512;
        opt.maxX = 511;
        opt.minZ = -512;
        opt.maxZ = 511;
        opt.mode = slime::Mode::Density;
        opt.algorithm = slime::Algorithm::Exact;
        opt.gridSize = 5;
        opt.minDensity = 0.3;
        opt.targetCount = 20;
        opt.threads = 4;

        std::atomic<bool> cancel{ false };
        std::mutex mu;
        bool sawScan = false;
        bool sawMatch = false;
        double last = -1.0;
        bool monotonic = true;
        slime::ProgressFn progress = [&](double f, bool scanning, double, double) {
            std::lock_guard<std::mutex> lk(mu);
            if (f + 1e-9 < last) monotonic = false;
            last = f;
            if (scanning && f < 0.75) sawScan = true;
            if (!scanning && f >= 0.75) sawMatch = true;
        };
        slime::next::MaterializeResult out;
        slime::next::MaterializePipeline pipeline;
        std::string err;
        const bool ok = pipeline.run(opt, cancel, out, &err, progress);
        std::printf("progress: ok=%d scan=%d match=%d final=%.3f monotonic=%d\n", ok ? 1 : 0,
                    sawScan ? 1 : 0, sawMatch ? 1 : 0, last, monotonic ? 1 : 0);
        if (!ok || !sawScan || !sawMatch || !monotonic || last < 0.999) ++failures;
    }

    {
        slime::SearchOptions opt;
        opt.version = slime::Version::Bedrock;
        opt.worldSeed = 12345;
        opt.minX = -1024;
        opt.maxX = 1023;
        opt.minZ = -1024;
        opt.maxZ = 1023;
        opt.mode = slime::Mode::Density;
        opt.algorithm = slime::Algorithm::Exact;
        opt.gridSize = 5;
        opt.minDensity = 0.3;
        opt.targetCount = 20;
        opt.threads = 4;

        std::atomic<bool> cancel{ true };
        slime::next::MaterializeResult out;
        slime::next::MaterializePipeline pipeline;
        std::string err;
        const bool ok = pipeline.run(opt, cancel, out, &err);
        std::printf("pre-cancel: ok=%d cancelled=%d results=%zu\n", ok ? 1 : 0, out.cancelled ? 1 : 0,
                    out.results.size());
        if (ok || !out.cancelled || !out.results.empty()) ++failures;
    }

    {
        slime::SearchOptions opt;
        opt.version = slime::Version::Bedrock;
        opt.worldSeed = 12345;
        opt.minX = -2048;
        opt.maxX = 2047;
        opt.minZ = -2048;
        opt.maxZ = 2047;
        opt.mode = slime::Mode::Strict;
        opt.algorithm = slime::Algorithm::Exact;
        opt.gridSize = 5;
        opt.targetCount = 20;
        opt.threads = 4;
        slime::Pattern p(2, 2);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) p.set(r, c, slime::CellState::Green);
        opt.pattern = p;

        std::atomic<bool> cancel{ false };
        slime::ProgressFn progress = [&](double f, bool, double, double) {
            if (f >= 0.2) cancel.store(true, std::memory_order_relaxed);
        };
        slime::next::MaterializeResult out;
        slime::next::MaterializePipeline pipeline;
        std::string err;
        const bool ok = pipeline.run(opt, cancel, out, &err, progress);
        std::printf("mid-cancel: ok=%d cancelled=%d results=%zu\n", ok ? 1 : 0, out.cancelled ? 1 : 0,
                    out.results.size());
        if (ok || !out.cancelled) ++failures;
    }

    std::printf("PROGRESS-CANCEL %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

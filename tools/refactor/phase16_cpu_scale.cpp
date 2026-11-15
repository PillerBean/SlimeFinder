// Phase 16：CPU 路径线程扩展与大范围。
#include "core/SlimeEngine.h"
#include "core/next/PlanExecutor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
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

slime::SearchOptions makeOpt(slime::Version v, slime::Mode m, int w, int h, int threads)
{
    slime::SearchOptions o;
    o.version = v;
    o.worldSeed = 12345;
    o.minX = -w / 2;
    o.maxX = o.minX + w - 1;
    o.minZ = -h / 2;
    o.maxZ = o.minZ + h - 1;
    o.mode = m;
    o.algorithm = slime::Algorithm::Exact;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.targetCount = 50;
    o.threads = threads;
    o.directionLocked = true;
    if (m == slime::Mode::Strict) {
        slime::Pattern p(2, 2);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) p.set(r, c, slime::CellState::Green);
        o.pattern = p;
    }
    return o;
}

struct Timed {
    double median = 0;
    size_t results = 0;
    int64_t total = 0;
    double scanMs = 0.0;
    double matchMs = 0.0;
    std::vector<slime::MatchResult> vec;
};

Timed runOld(const slime::SearchOptions& input, int samples)
{
    slime::SearchOptions opt = input;
    opt.gpuScan = nullptr;
    std::vector<double> t;
    slime::SearchOutcome last;
    std::atomic<bool> cancel{ false };
    for (int i = 0; i < samples + 1; ++i) {
        slime::SlimeEngine e;
        const auto a = std::chrono::steady_clock::now();
        e.run(opt, cancel, slime::ProgressFn(), last);
        const auto b = std::chrono::steady_clock::now();
        if (i > 0) t.push_back(std::chrono::duration<double, std::milli>(b - a).count());
    }
    std::sort(t.begin(), t.end());
    Timed r;
    r.median = t[t.size() / 2];
    r.results = last.results.size();
    r.total = last.totalFound;
    r.scanMs = last.stats.scanMs;
    r.matchMs = last.stats.matchMs;
    r.vec = last.results;
    return r;
}

Timed runNew(const slime::SearchOptions& opt, int samples)
{
    slime::next::MachineCaps caps;
    caps.gpuAvailable = false;
    caps.classicChunkLimit = int64_t{ 1 } << 40;
    slime::next::PlannerInput pin;
    pin.options = opt;
    pin.caps = caps;
    const auto plan = slime::next::makeExecutionPlan(pin);
    std::vector<double> t;
    slime::next::MaterializeResult last;
    for (int i = 0; i < samples + 1; ++i) {
        slime::next::PlanExecutor e;
        slime::next::MaterializeResult out;
        std::atomic<bool> cancel{ false };
        std::string err;
        const auto a = std::chrono::steady_clock::now();
        const bool ok = e.run(opt, plan, cancel, out, &err);
        const auto b = std::chrono::steady_clock::now();
        if (i > 0 && ok) t.push_back(std::chrono::duration<double, std::milli>(b - a).count());
        last = std::move(out);
    }
    std::sort(t.begin(), t.end());
    Timed r;
    r.median = t[t.size() / 2];
    r.results = last.results.size();
    r.total = last.totalFound;
    r.scanMs = last.scan.ms;
    r.matchMs = last.match.ms;
    r.vec = last.results;
    return r;
}

void one(const char* name, slime::Version v, slime::Mode m, int w, int h, int threads, int samples)
{
    auto opt = makeOpt(v, m, w, h, threads);
    const Timed o = runOld(opt, samples);
    const Timed n = runNew(opt, samples);
    const bool ok = same(o.vec, n.vec) && o.total == n.total;
    std::printf("%-22s t=%2d old=%7.2fms new=%7.2fms ratio=%5.2fx %s | old s/m %.2f/%.2f new s/m %.2f/%.2f\n",
                name, threads, o.median, n.median, n.median / o.median, ok ? "SAME" : "DIFF",
                o.scanMs, o.matchMs, n.scanMs, n.matchMs);
}
} // namespace

int main()
{
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    for (int t : { 1, 2, 4, 8, 12 }) {
        one("Java pattern 2048", slime::Version::Java, slime::Mode::Strict, 2048, 2048, t, 3);
        one("Java density 2048", slime::Version::Java, slime::Mode::Density, 2048, 2048, t, 3);
    }
    for (int t : { 1, 2, 4, 8 }) {
        one("Bedrock pattern 1024", slime::Version::Bedrock, slime::Mode::Strict, 1024, 1024, t, 3);
        one("Bedrock density 1024", slime::Version::Bedrock, slime::Mode::Density, 1024, 1024, t, 3);
    }
    one("Java pattern 4096", slime::Version::Java, slime::Mode::Strict, 4096, 4096, 8, 2);
    one("Java density 4096", slime::Version::Java, slime::Mode::Density, 4096, 4096, 8, 2);
    std::printf("PHASE16-CPU-SCALE DONE\n");
    return 0;
}

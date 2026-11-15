// Bench the flow layer: legacy SearchTask-style thread vs SearchJob-backed SearchTask.
#include "core/SearchTask.h"
#include "core/SlimeEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Timed {
    double ms = 0.0;
    size_t results = 0;
    int64_t total = 0;
};

slime::SearchOptions makeOptions(slime::Version version, slime::Mode mode, int side, int target)
{
    slime::SearchOptions o;
    o.version = version;
    o.worldSeed = 12345;
    o.minX = -side / 2;
    o.maxX = side / 2 - 1;
    o.minZ = -side / 2;
    o.maxZ = side / 2 - 1;
    o.mode = mode;
    o.algorithm = slime::Algorithm::Exact;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.targetCount = target;
    o.threads = 8;
    o.directionLocked = true;
    if (mode == slime::Mode::Strict) {
        slime::Pattern p(2, 2);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) p.set(r, c, slime::CellState::Green);
        o.pattern = p;
    }
    return o;
}

// Equivalent to the old SearchTask thread body: engine + atomic cancel + callbacks.
Timed runLegacy(const slime::SearchOptions& opt)
{
    std::atomic<bool> done{ false };
    slime::SearchOutcome result;
    const auto a = Clock::now();
    std::thread worker([&]() {
        slime::SlimeEngine engine;
        if (opt.cacheBandLoader) engine.runBanded(opt, done, slime::ProgressFn(), result);
        else engine.run(opt, done, slime::ProgressFn(), result);
        done.store(true, std::memory_order_release);
    });
    worker.join();
    const auto b = Clock::now();
    Timed t;
    t.ms = std::chrono::duration<double, std::milli>(b - a).count();
    t.results = result.results.size();
    t.total = result.totalFound;
    return t;
}

Timed runFlowSearch(const slime::SearchOptions& opt)
{
    slime::SearchTask task;
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    slime::SearchOutcome result;
    const auto a = Clock::now();
    task.start(opt, slime::ProgressFn(), [&](const slime::SearchOutcome& out) {
        {
            std::lock_guard<std::mutex> lk(mu);
            result = out;
            done = true;
        }
        cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&]() { return done; });
    }
    task.waitFor(5000);
    const auto b = Clock::now();
    Timed t;
    t.ms = std::chrono::duration<double, std::milli>(b - a).count();
    t.results = result.results.size();
    t.total = result.totalFound;
    return t;
}

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void benchCase(const char* name, const slime::SearchOptions& opt, int samples)
{
    (void)runFlowSearch(opt);
    (void)runLegacy(opt);
    std::vector<double> legacy;
    std::vector<double> flow;
    Timed lastLegacy;
    Timed lastFlow;
    for (int i = 0; i < samples; ++i) {
        // Alternate order so CPU/cache warm-up does not always favor the flow path.
        if ((i & 1) == 0) {
            lastLegacy = runLegacy(opt);
            legacy.push_back(lastLegacy.ms);
            lastFlow = runFlowSearch(opt);
            flow.push_back(lastFlow.ms);
        } else {
            lastFlow = runFlowSearch(opt);
            flow.push_back(lastFlow.ms);
            lastLegacy = runLegacy(opt);
            legacy.push_back(lastLegacy.ms);
        }
    }
    const double lm = median(legacy);
    const double fm = median(flow);
    const bool same = lastLegacy.results == lastFlow.results && lastLegacy.total == lastFlow.total;
    std::printf("%-22s legacy=%7.2fms flow=%7.2fms ratio=%5.2fx results=%zu total=%lld %s\n", name, lm, fm,
                fm / lm, lastFlow.results, static_cast<long long>(lastFlow.total), same ? "SAME" : "DIFF");
}

// Measure the public two-phase cancellation path: cancel call latency and time to finish.
void benchCancel(const slime::SearchOptions& opt)
{
    slime::SearchTask task;
    std::atomic<bool> done{ false };
    task.start(opt, slime::ProgressFn(), [&](const slime::SearchOutcome&) { done.store(true); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto c0 = Clock::now();
    task.cancel();
    const auto c1 = Clock::now();
    task.waitFor(10000);
    const auto c2 = Clock::now();
    const double cancelUs = std::chrono::duration<double, std::micro>(c1 - c0).count();
    const double finishMs = std::chrono::duration<double, std::milli>(c2 - c0).count();
    std::printf("cancel-after-30ms         call=%6.1fus finish=%7.2fms cancelled=%d done=%d\n", cancelUs,
                finishMs, task.lastReport().status == slime::SearchStatus::Cancelled, done.load() ? 1 : 0);
}

} // namespace

int main()
{
    _putenv_s("SLIME_GPU_MATCH", "0");
    _putenv_s("SLIME_GPU_PATTERN", "0");
    int samples = 15;
    if (const char* e = std::getenv("FLOW_BENCH_SAMPLES")) {
        const int v = std::atoi(e);
        if (v >= 3 && v <= 201) samples = v;
    }
    benchCase("Java pattern 2048", makeOptions(slime::Version::Java, slime::Mode::Strict, 2048, 50), samples);
    benchCase("Java density 2048", makeOptions(slime::Version::Java, slime::Mode::Density, 2048, 50), samples);
    benchCase("Bedrock density 1024", makeOptions(slime::Version::Bedrock, slime::Mode::Density, 1024, 50),
              samples);
    benchCase("Bedrock density 2048", makeOptions(slime::Version::Bedrock, slime::Mode::Density, 2048, 50),
              samples);
    benchCancel(makeOptions(slime::Version::Bedrock, slime::Mode::Density, 4096, 200));
    std::printf("FLOWBENCH DONE\n");
    return 0;
}

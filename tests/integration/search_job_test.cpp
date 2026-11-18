// SearchJob round-1 acceptance tests.
#include "core/SearchJob.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++g_fail;                                                                \
        }                                                                            \
    } while (0)

void sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool waitState(slime::SearchJob& job, slime::SearchJob::State want, int ms)
{
    for (int i = 0; i < ms; i += 2) {
        if (job.state() == want) return true;
        sleepMs(2);
    }
    return job.state() == want;
}

slime::SearchOutcome okOutcome(int64_t total, int results)
{
    slime::SearchOutcome out;
    out.ok = true;
    out.totalFound = total;
    for (int i = 0; i < results; ++i) {
        slime::MatchResult r;
        r.minX = i;
        r.maxX = i;
        r.minZ = 0;
        r.maxZ = 0;
        out.results.push_back(r);
    }
    return out;
}

void testSuccessAndProgress()
{
    slime::SearchJob job;
    std::mutex mu;
    std::vector<double> fractions;
    std::atomic<bool> done{ false };
    slime::SearchReport finalReport;

    const auto runner = [&](const slime::SearchOptions&, const std::atomic<bool>&,
                            const slime::ProgressFn& progress) {
        progress(0.20, true, 1000.0, 1.0);
        progress(0.10, true, 1000.0, 1.0);
        progress(0.80, false, 0.0, 0.0);
        return okOutcome(42, 2);
    };
    const bool started = job.start(
        slime::SearchOptions{}, [&](const slime::SearchProgress& p) {
            std::lock_guard<std::mutex> lk(mu);
            fractions.push_back(p.fraction);
        },
        [&](const slime::SearchReport& r) {
            finalReport = r;
            done.store(true);
        },
        runner);
    CHECK(started);
    CHECK(job.waitFor(5000));
    CHECK(job.state() == slime::SearchJob::State::Finished);
    CHECK(done.load());
    CHECK(finalReport.status == slime::SearchStatus::Ok);
    CHECK(finalReport.hitsTotal == 42);
    CHECK(finalReport.hitsReturned == 2);
    CHECK(finalReport.progress.phase == slime::SearchPhase::Done);
    CHECK(finalReport.progress.fraction == 1.0);
    CHECK(finalReport.progress.terminal);
    {
        std::lock_guard<std::mutex> lk(mu);
        CHECK(fractions.size() >= 4);
        for (size_t i = 1; i < fractions.size(); ++i) CHECK(fractions[i] >= fractions[i - 1]);
    }
}

void testTwoPhaseCancelAndBoundedWait()
{
    slime::SearchJob job;
    std::atomic<bool> started{ false };
    const auto runner = [&](const slime::SearchOptions&, const std::atomic<bool>& cancel,
                            const slime::ProgressFn&) {
        started.store(true);
        while (!cancel.load(std::memory_order_relaxed)) sleepMs(1);
        // Deliberately exceed the bound: cooperative cancel was seen, but finalization is slow.
        sleepMs(350);
        return okOutcome(9, 1);
    };
    CHECK(job.start(slime::SearchOptions{}, {}, {}, runner));
    CHECK(waitState(job, slime::SearchJob::State::Running, 1000));
    CHECK(started.load());
    CHECK(job.requestCancel());
    CHECK(job.state() == slime::SearchJob::State::Stopping);
    // stage two bound must NOT join forever
    CHECK(!job.waitFor(50));
    CHECK(job.state() == slime::SearchJob::State::Stopping);
    CHECK(job.waitFor(3000));
    CHECK(job.state() == slime::SearchJob::State::Finished);
    const auto report = job.report();
    CHECK(report.status == slime::SearchStatus::Cancelled);
    CHECK(report.hitsTotal == 0);
    CHECK(report.hitsReturned == 0);
    CHECK(report.outcome.results.empty());
    CHECK(report.progress.phase == slime::SearchPhase::Cancelled);
}

void testStartRejectedWhileActive()
{
    slime::SearchJob job;
    std::atomic<bool> release{ false };
    const auto runner = [&](const slime::SearchOptions&, const std::atomic<bool>&,
                            const slime::ProgressFn&) {
        while (!release.load()) sleepMs(1);
        return okOutcome(0, 0);
    };
    CHECK(job.start(slime::SearchOptions{}, {}, {}, runner));
    CHECK(waitState(job, slime::SearchJob::State::Running, 1000));
    CHECK(!job.start(slime::SearchOptions{}, {}, {}, runner));
    CHECK(job.requestCancel());
    release.store(true);
    CHECK(job.waitFor(3000));
    CHECK(job.state() == slime::SearchJob::State::Finished);
}

void testFailureAndRestart()
{
    slime::SearchJob job;
    CHECK(job.start(slime::SearchOptions{}, {}, {},
                    [](const slime::SearchOptions&, const std::atomic<bool>&,
                       const slime::ProgressFn&) -> slime::SearchOutcome {
                        throw std::runtime_error("boom");
                    }));
    CHECK(job.waitFor(3000));
    CHECK(job.report().status == slime::SearchStatus::Failed);
    CHECK(job.report().error.find("boom") != std::string::npos);
    CHECK(job.report().progress.phase == slime::SearchPhase::Failed);

    CHECK(job.start(slime::SearchOptions{}, {}, {},
                    [](const slime::SearchOptions&, const std::atomic<bool>&,
                       const slime::ProgressFn&) { return okOutcome(3, 1); }));
    CHECK(job.waitFor(3000));
    CHECK(job.report().status == slime::SearchStatus::Ok);
    CHECK(job.report().hitsTotal == 3);
}

void testCancelIdleRejected()
{
    slime::SearchJob job;
    CHECK(!job.requestCancel());
    CHECK(!job.active());
    CHECK(job.waitFor(0));
}

} // namespace

int main()
{
    testSuccessAndProgress();
    testTwoPhaseCancelAndBoundedWait();
    testStartRejectedWhileActive();
    testCancelIdleRejected();
    testFailureAndRestart();
    if (g_fail == 0) {
        std::printf("SEARCHJOB ALL PASS\n");
        return 0;
    }
    std::printf("SEARCHJOB FAIL %d\n", g_fail);
    return 1;
}

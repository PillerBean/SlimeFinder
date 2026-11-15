#include "core/SearchJob.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace slime {

const char* SearchJob::stateName(State state) noexcept
{
    switch (state) {
    case State::Idle: return "idle";
    case State::Starting: return "starting";
    case State::Running: return "running";
    case State::Stopping: return "stopping";
    case State::Finishing: return "finishing";
    case State::Finished: return "finished";
    }
    return "unknown";
}

struct SearchJob::Impl {
    mutable std::mutex mu;
    std::condition_variable cv;
    State state = State::Idle;
    std::atomic<bool> cancel{ false };
    uint64_t runId = 0;
    SearchReport report;
    SearchProgress lastProgress;
    std::thread worker;
};

SearchJob::SearchJob() : m_impl(std::make_shared<Impl>())
{
}

SearchJob::~SearchJob()
{
    if (!m_impl) return;
    requestCancel();
    if (m_impl->worker.joinable()) m_impl->worker.join();
}

bool SearchJob::start(const SearchOptions& options, SearchProgressSink progress, DoneFn done,
                      Runner runner)
{
    if (!runner) runner = defaultRunner();
    if (!runner) return false;

    std::thread previous;
    {
        std::lock_guard<std::mutex> lk(m_impl->mu);
        if (m_impl->state == State::Starting || m_impl->state == State::Running ||
            m_impl->state == State::Stopping || m_impl->state == State::Finishing) {
            return false;
        }
        if (m_impl->worker.joinable()) previous = std::move(m_impl->worker);
        m_impl->state = State::Starting;
        m_impl->cancel.store(false, std::memory_order_relaxed);
        ++m_impl->runId;
        m_impl->report = SearchReport{};
        const uint64_t runId = m_impl->runId;
        const auto shared = m_impl;
        try {
            m_impl->worker = std::thread([shared, options, progress = std::move(progress),
                                          done = std::move(done), runner = std::move(runner), runId]() mutable {
                {
                    std::lock_guard<std::mutex> lk(shared->mu);
                    if (shared->state == State::Starting) shared->state = State::Running;
                }

                ProgressContract contract([shared, progress](const SearchProgress& p) {
                    {
                        std::lock_guard<std::mutex> lk(shared->mu);
                        shared->lastProgress = p;
                    }
                    if (progress) progress(p);
                });
                contract.publish(0.0, SearchPhase::Preparing);
                const ProgressFn legacy = [&contract](double fraction, bool scanning, double cps, double eta) {
                    const SearchPhase phase = scanning ? SearchPhase::Scanning
                                                       : (fraction >= 0.999 ? SearchPhase::Finalizing
                                                                            : SearchPhase::Matching);
                    contract.publish(fraction, phase, cps, eta);
                };

                SearchOutcome outcome;
                try {
                    outcome = runner(options, shared->cancel, legacy);
                } catch (const std::exception& e) {
                    outcome = SearchOutcome{};
                    outcome.ok = false;
                    outcome.error = std::string("搜索线程异常: ") + e.what();
                } catch (...) {
                    outcome = SearchOutcome{};
                    outcome.ok = false;
                    outcome.error = "搜索线程未知异常";
                }

                const bool cancelRequested = shared->cancel.load(std::memory_order_relaxed);
                SearchReport report =
                    makeSearchReport(outcome, runId, cancelRequested, contract.last(), options);
                const SearchPhase terminal = report.status == SearchStatus::Ok
                                                 ? SearchPhase::Done
                                                 : (report.status == SearchStatus::Cancelled
                                                        ? SearchPhase::Cancelled
                                                        : SearchPhase::Failed);
                contract.finish(terminal);
                report.progress = contract.last();

                {
                    std::lock_guard<std::mutex> lk(shared->mu);
                    shared->report = report;
                    shared->state = State::Finishing;
                }
                if (done) {
                    try {
                        done(report);
                    } catch (...) {
                        // 结果回调不能把工作线程打穿。
                    }
                }
                {
                    std::lock_guard<std::mutex> lk(shared->mu);
                    shared->state = State::Finished;
                }
                shared->cv.notify_all();
            });
        } catch (...) {
            m_impl->state = State::Finished;
            return false;
        }
    }
    if (previous.joinable()) previous.join();
    return true;
}

bool SearchJob::requestCancel() noexcept
{
    if (!m_impl) return false;
    std::lock_guard<std::mutex> lk(m_impl->mu);
    if (m_impl->state != State::Starting && m_impl->state != State::Running &&
        m_impl->state != State::Stopping) {
        return false;
    }
    m_impl->cancel.store(true, std::memory_order_relaxed);
    m_impl->state = State::Stopping;
    m_impl->cv.notify_all();
    return true;
}

bool SearchJob::waitFor(int timeoutMs)
{
    if (!m_impl) return true;
    std::unique_lock<std::mutex> lk(m_impl->mu);
    if (m_impl->state == State::Idle || m_impl->state == State::Finished) return true;
    if (timeoutMs < 0) timeoutMs = 0;
    return m_impl->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                               [this]() { return m_impl->state == State::Finished; });
}

void SearchJob::wait()
{
    if (!m_impl) return;
    std::unique_lock<std::mutex> lk(m_impl->mu);
    m_impl->cv.wait(lk, [this]() { return m_impl->state == State::Finished; });
}

void SearchJob::join()
{
    if (!m_impl) return;
    std::thread worker;
    {
        std::lock_guard<std::mutex> lk(m_impl->mu);
        if (m_impl->worker.joinable()) worker = std::move(m_impl->worker);
    }
    if (worker.joinable()) worker.join();
}

SearchJob::State SearchJob::state() const noexcept
{
    if (!m_impl) return State::Idle;
    std::lock_guard<std::mutex> lk(m_impl->mu);
    return m_impl->state;
}

bool SearchJob::active() const noexcept
{
    const State s = state();
    return s == State::Starting || s == State::Running || s == State::Stopping ||
           s == State::Finishing;
}

uint64_t SearchJob::runId() const noexcept
{
    if (!m_impl) return 0;
    std::lock_guard<std::mutex> lk(m_impl->mu);
    return m_impl->runId;
}

SearchReport SearchJob::report() const
{
    if (!m_impl) return SearchReport{};
    std::lock_guard<std::mutex> lk(m_impl->mu);
    return m_impl->report;
}

SearchProgress SearchJob::progress() const
{
    if (!m_impl) return SearchProgress{};
    std::lock_guard<std::mutex> lk(m_impl->mu);
    return m_impl->lastProgress;
}

SearchJob::Runner SearchJob::defaultRunner()
{
    return [](const SearchOptions& opt, const std::atomic<bool>& cancel, const ProgressFn& progress) {
        SlimeEngine engine;
        SearchOutcome out;
        if (opt.cacheBandLoader) engine.runBanded(opt, cancel, progress, out);
        else engine.run(opt, cancel, progress, out);
        return out;
    };
}

} // namespace slime

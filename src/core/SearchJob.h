// SearchJob.h - 搜索任务状态机 / 两阶段取消 / 有界等待（v3.14.0）
#pragma once

#include "core/SearchProgress.h"
#include "core/SearchReport.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace slime {

class SearchJob {
public:
    enum class State : uint8_t {
        Idle = 0,
        Starting = 1,
        Running = 2,
        Stopping = 3,
        Finishing = 4,
        Finished = 5,
    };

    using Runner = std::function<SearchOutcome(const SearchOptions&, const std::atomic<bool>&,
                                               const ProgressFn&)>;
    using DoneFn = std::function<void(const SearchReport&)>;

    SearchJob();
    ~SearchJob();
    SearchJob(const SearchJob&) = delete;
    SearchJob& operator=(const SearchJob&) = delete;

    // 返回 false：已有任务在运行，或 runner 为空。
    bool start(const SearchOptions& options, SearchProgressSink progress, DoneFn done,
               Runner runner = {});
    // 阶段一：立即请求取消，不等待。返回 false 表示当前没有可取消的任务。
    bool requestCancel() noexcept;
    // 阶段二：有界等待（毫秒）。返回 true 表示已到 Finished。
    bool waitFor(int timeoutMs);
    void wait();
    // Wait+join; for legacy SearchTask::wait() and shutdown paths.
    void join();
    State state() const noexcept;
    bool active() const noexcept;
    uint64_t runId() const noexcept;
    SearchReport report() const;
    SearchProgress progress() const;

    static const char* stateName(State state) noexcept;
    static Runner defaultRunner();

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace slime

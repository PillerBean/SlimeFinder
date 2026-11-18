// SearchTask.h - 搜索任务封装（v3.14.0：内部使用 SearchJob 状态机）
#pragma once

#include "core/SearchJob.h"
#include "core/SlimeEngine.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace slime {

class SearchTask {
public:
    using DoneFn = std::function<void(const SearchOutcome& outcome)>;
    using ReportDoneFn = std::function<void(const SearchReport& report)>;

    SearchTask() = default;
    ~SearchTask();

    SearchTask(const SearchTask&) = delete;
    SearchTask& operator=(const SearchTask&) = delete;

    bool running() const noexcept { return m_job.active(); }
    SearchJob::State state() const noexcept { return m_job.state(); }

    // 启动后台搜索（若已有任务在运行则先取消并等待）。
    void start(const SearchOptions& options, const ProgressFn& progress, const DoneFn& done);
    // v3.14.4: native SearchProgress/SearchReport entry for the GUI contract migration.
    void startReport(const SearchOptions& options, const SearchProgressSink& progress, const ReportDoneFn& done);

    // 阶段一：请求取消，立即返回（不 join）。兼容旧调用。
    void cancel() noexcept;
    // 阶段二：有界等待；返回 false 表示仍在 Stopping。
    bool waitFor(int timeoutMs) { return m_job.waitFor(timeoutMs); }
    // 兼容旧调用：无限等待。
    void wait() noexcept { m_job.wait(); m_job.join(); }

    SearchProgress progressSnapshot() const { return m_job.progress(); }
    SearchReport lastReport() const { return m_job.report(); }

    // 实时计数（由本类持有；线程结束后仍可安全读取）
    std::atomic<int64_t>& liveMatches() noexcept { return *m_liveMatches; }
    const std::atomic<int64_t>& liveMatches() const noexcept { return *m_liveMatches; }
    int64_t liveMatchCount() const noexcept { return m_liveMatches->load(std::memory_order_relaxed); }
    void resetLiveMatches() noexcept { m_liveMatches->store(0, std::memory_order_relaxed); }

    int64_t liveScannedCount() const noexcept { return m_liveScanned->load(std::memory_order_relaxed); }
    int64_t liveTotalCount() const noexcept { return m_liveTotal->load(std::memory_order_relaxed); }
    std::atomic<int64_t>& liveTotal() noexcept { return *m_liveTotal; }

    int64_t liveRecordedCount() const noexcept { return m_liveRecorded->load(std::memory_order_relaxed); }
    int64_t liveSkippedCoveredCount() const noexcept { return m_liveSkipped->load(std::memory_order_relaxed); }
    int livePhase() const noexcept { return m_livePhase->load(std::memory_order_relaxed); }
    void resetBuildCounters() noexcept
    {
        m_liveRecorded->store(0, std::memory_order_relaxed);
        m_liveSkipped->store(0, std::memory_order_relaxed);
        m_livePhase->store(0, std::memory_order_relaxed);
    }

private:
    SearchJob m_job;
    std::shared_ptr<std::atomic<int64_t>> m_liveMatches = std::make_shared<std::atomic<int64_t>>(0);
    std::shared_ptr<std::atomic<int64_t>> m_liveScanned = std::make_shared<std::atomic<int64_t>>(0);
    std::shared_ptr<std::atomic<int64_t>> m_liveTotal = std::make_shared<std::atomic<int64_t>>(0);
    std::shared_ptr<std::atomic<int64_t>> m_liveRecorded = std::make_shared<std::atomic<int64_t>>(0);
    std::shared_ptr<std::atomic<int64_t>> m_liveSkipped = std::make_shared<std::atomic<int64_t>>(0);
    std::shared_ptr<std::atomic<int>> m_livePhase = std::make_shared<std::atomic<int>>(0);
};

} // namespace slime

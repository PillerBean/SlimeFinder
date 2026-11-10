// SearchTask.h - 搜索任务封装（Qt 无关）
//
// 在线程中执行 SlimeEngine::run：
//   * 支持随时取消（std::atomic<bool>）
//   * 进度通过 std::function 回调（在工作线程内调用）
//   * 完成通过 done 回调返回 SearchOutcome（工作线程内调用，调用方自行切换线程）
//
// v3.3.7（紧急修复任务书 P0-2）：实时命中计数（SearchOptions::liveMatches）由本类**持有所有权**，
// 并在 start() 时注入给引擎、同时被工作线程捕获一份 shared_ptr —— 这样即使调用方（MainWindow）
// 先析构，引擎仍在写计数也不会触到悬垂对象；调用方通过 liveMatchCount() 读取。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

#include "SlimeEngine.h"

namespace slime {

class SearchTask {
public:
    using DoneFn = std::function<void(const SearchOutcome& outcome)>;

    SearchTask() = default;
    ~SearchTask();

    SearchTask(const SearchTask&) = delete;
    SearchTask& operator=(const SearchTask&) = delete;

    bool running() const noexcept { return m_running.load(std::memory_order_acquire); }

    // 启动后台搜索（若已有任务在运行则先取消并等待）
    void start(const SearchOptions& options, const ProgressFn& progress, const DoneFn& done);

    // 请求取消（引擎尽快停止，不保存部分结果）
    void cancel() noexcept;

    // 等待任务线程结束（取消后调用可确保清理）
    void wait() noexcept;

    // v3.3.7（P0-2）：引擎实时命中计数（由本类持有；线程结束后仍可安全读取最后值）
    std::atomic<int64_t>& liveMatches() noexcept { return *m_liveMatches; }
    const std::atomic<int64_t>& liveMatches() const noexcept { return *m_liveMatches; }
    int64_t liveMatchCount() const noexcept { return m_liveMatches->load(std::memory_order_relaxed); }
    void resetLiveMatches() noexcept { m_liveMatches->store(0, std::memory_order_relaxed); }

    // v3.3.7（P0-3）：真实已扫描区块计数（同样由本类持有，供界面显示 N/M 与百分比）
    int64_t liveScannedCount() const noexcept { return m_liveScanned->load(std::memory_order_relaxed); }

    // v3.3.7.2（任务书 1.2）：引擎口径的**总扫描量**（与 liveScannedCount 同单位、同分母）。
    // 0 表示引擎未公开（例如快速估算模式的采样扫描）→ 界面应退化为只显示已扫描数。
    int64_t liveTotalCount() const noexcept { return m_liveTotal->load(std::memory_order_relaxed); }
    std::atomic<int64_t>& liveTotal() noexcept { return *m_liveTotal; }

    // v3.3.9（任务书 1.1/1.4）：缓存构建模式的实时计数 + 「缓存扫描 / 新区域扫描」阶段指示
    int64_t liveRecordedCount() const noexcept { return m_liveRecorded->load(std::memory_order_relaxed); }
    int64_t liveSkippedCoveredCount() const noexcept { return m_liveSkipped->load(std::memory_order_relaxed); }
    // 0 = 正在判定新区域；1 = 正在从缓存位图直取（缓存扫描）
    int livePhase() const noexcept { return m_livePhase->load(std::memory_order_relaxed); }
    void resetBuildCounters() noexcept
    {
        m_liveRecorded->store(0, std::memory_order_relaxed);
        m_liveSkipped->store(0, std::memory_order_relaxed);
        m_livePhase->store(0, std::memory_order_relaxed);
    }

private:
    std::thread m_thread;
    std::atomic<bool> m_cancel{ false };
    std::atomic<bool> m_running{ false };
    // 在构造期创建，生命周期覆盖「所有可能的引擎调用」；工作线程也会捕获一份
    std::shared_ptr<std::atomic<int64_t>> m_liveMatches = std::make_shared<std::atomic<int64_t>>(0);
    std::shared_ptr<std::atomic<int64_t>> m_liveScanned = std::make_shared<std::atomic<int64_t>>(0);
    std::shared_ptr<std::atomic<int64_t>> m_liveTotal = std::make_shared<std::atomic<int64_t>>(0);
    // v3.3.9（任务书 1.1/1.4）：构建模式计数 + 阶段指示
    std::shared_ptr<std::atomic<int64_t>> m_liveRecorded = std::make_shared<std::atomic<int64_t>>(0);
    std::shared_ptr<std::atomic<int64_t>> m_liveSkipped = std::make_shared<std::atomic<int64_t>>(0);
    std::shared_ptr<std::atomic<int>> m_livePhase = std::make_shared<std::atomic<int>>(0);
};

} // namespace slime

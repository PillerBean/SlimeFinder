// SearchTask.cpp - 后台搜索任务实现
#include "core/SearchTask.h"

namespace slime {

SearchTask::~SearchTask()
{
    cancel();
    wait();
}

void SearchTask::start(const SearchOptions& options, const ProgressFn& progress, const DoneFn& done)
{
    cancel();
    wait();
    m_cancel.store(false, std::memory_order_relaxed);
    m_running.store(true, std::memory_order_release);

    // v3.3.7（P0-2）：注入本类持有的实时命中计数，并让工作线程捕获 shared_ptr 一份 ——
    // 计数对象的生命周期从此与「引擎调用」绑定，而不是依赖调用方的成员析构顺序
    // （旧实现里 MainWindow::m_liveMatches 声明在 m_task 之后，会先被析构）。
    SearchOptions opt = options;
    opt.liveMatches = m_liveMatches.get();
    opt.liveScanned = m_liveScanned.get(); // v3.3.7（P0-3）：真实扫描计数
    opt.liveTotal = m_liveTotal.get();     // v3.3.7.2（任务书 1.2）：同源分母（引擎口径总扫描量）
    // v3.3.9（任务书 1.1/1.4）：构建模式计数与阶段指示（仅构建模式使用，普通搜索恒为 0）
    opt.liveRecorded = m_liveRecorded.get();
    opt.liveSkippedCovered = m_liveSkipped.get();
    opt.livePhase = m_livePhase.get();
    m_liveMatches->store(0, std::memory_order_relaxed);
    m_liveScanned->store(0, std::memory_order_relaxed);
    m_liveTotal->store(0, std::memory_order_relaxed);
    const std::shared_ptr<std::atomic<int64_t>> liveCounter = m_liveMatches;
    const std::shared_ptr<std::atomic<int64_t>> liveScanned = m_liveScanned;
    const std::shared_ptr<std::atomic<int64_t>> liveTotal = m_liveTotal;
    const std::shared_ptr<std::atomic<int64_t>> liveRecorded = m_liveRecorded;
    const std::shared_ptr<std::atomic<int64_t>> liveSkipped = m_liveSkipped;
    const std::shared_ptr<std::atomic<int>> livePhase = m_livePhase;

    const ProgressFn cb = progress;
    const DoneFn fn = done;
    m_thread = std::thread([this, opt, cb, fn, liveCounter, liveScanned, liveTotal, liveRecorded, liveSkipped,
                            livePhase]() {
        SlimeEngine engine;
        SearchOutcome out;
        engine.run(opt, static_cast<const std::atomic<bool>&>(m_cancel), cb, out);
        m_running.store(false, std::memory_order_release);
        if (fn) fn(out);
        (void)liveCounter; // 保证计数对象活到引擎返回之后（防悬垂的最后一道保险）
        (void)liveScanned;
        (void)liveTotal;
        (void)liveRecorded;
        (void)liveSkipped;
        (void)livePhase;
    });
}

void SearchTask::cancel() noexcept
{
    m_cancel.store(true, std::memory_order_relaxed);
}

void SearchTask::wait() noexcept
{
    if (m_thread.joinable()) m_thread.join();
}

} // namespace slime

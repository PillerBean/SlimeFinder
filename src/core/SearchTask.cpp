// SearchTask.cpp - 后台搜索任务实现
#include "core/SearchTask.h"

#include "core/next/Plan.h"
#include "core/next/PlanExecutor.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace slime {

namespace {

bool nextPipelineEnabled()
{
    const char* e = std::getenv("SLIME_NEXT_PIPELINE");
    if (!e || !*e) return false;
    std::string s(e);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s == "1" || s == "true" || s == "on";
}

bool canUseNextPipeline(const SearchOptions& opt, slime::next::ExecutionPlan& plan)
{
    if (opt.buildMode || opt.ignoreThreshold || opt.stopAtTarget || opt.smartRange) return false;
    if (opt.mode == Mode::Strict && (opt.algorithm != Algorithm::Exact || !opt.directionLocked)) return false;
    if (opt.mode == Mode::Density && opt.algorithm != Algorithm::Exact) return false;
    if (opt.useCache && !opt.cacheBandLoader) return false;

    slime::next::PlannerInput in;
    in.options = opt;
    in.caps.gpuAvailable = opt.gpuScan && opt.gpuScan->available();
    if (in.caps.gpuAvailable) {
        std::string name = opt.gpuScan->deviceName();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        in.caps.gpuIsAmd = name.find("amd") != std::string::npos ||
                           name.find("advanced micro devices") != std::string::npos ||
                           name.find("gfx") != std::string::npos;
    }
    plan = slime::next::makeExecutionPlan(in);
    return slime::next::PlanExecutor::supported(opt, plan);
}

} // namespace

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
        SearchOutcome out;
        bool usedNext = false;
        slime::next::ExecutionPlan plan;
        if (nextPipelineEnabled() && canUseNextPipeline(opt, plan)) {
            slime::next::MaterializeResult nr;
            std::string nerr;
            slime::next::PlanExecutor executor;
            const bool ok = executor.run(opt, plan, static_cast<const std::atomic<bool>&>(m_cancel), nr, &nerr, cb);
            if (ok) {
                out.ok = true;
                out.results = std::move(nr.results);
                out.totalFound = nr.totalFound;
                out.cancelled = nr.cancelled;
                out.chunkedPath = plan.partition.kind == slime::next::PartitionKind::Chunked;
                out.gpuMatchUsed = plan.matcher == slime::next::MatcherKind::FusedGpuPattern ||
                                   plan.matcher == slime::next::MatcherKind::FusedGpuDensity;
                out.stats.scanMs = nr.scan.ms;
                out.stats.matchMs = nr.match.ms;
                out.stats.totalMs = nr.scan.ms + nr.match.ms;
                out.stats.scannedChunks = nr.scan.chunks;
                out.stats.windowsEvaluated = nr.match.windows;
                out.stats.threadsUsed = opt.threads;
                usedNext = true;
            } else if (nr.cancelled || m_cancel.load(std::memory_order_relaxed)) {
                out.cancelled = true;
                out.error = "已取消";
                usedNext = true;
            }
        }
        if (!usedNext) {
            SlimeEngine engine;
            if (opt.cacheBandLoader) engine.runBanded(opt, static_cast<const std::atomic<bool>&>(m_cancel), cb, out);
            else engine.run(opt, static_cast<const std::atomic<bool>&>(m_cancel), cb, out);
        } else {
            if (liveCounter) liveCounter->store(out.totalFound, std::memory_order_relaxed);
            if (liveScanned) liveScanned->store(out.stats.scannedChunks, std::memory_order_relaxed);
            if (liveTotal) liveTotal->store(out.stats.scannedChunks, std::memory_order_relaxed);
        }
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

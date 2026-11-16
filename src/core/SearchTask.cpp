#include "core/SearchTask.h"

namespace slime {

SearchTask::~SearchTask()
{
    cancel();
    wait();
}

void SearchTask::start(const SearchOptions& options, const ProgressFn& progress, const DoneFn& done)
{
    if (m_job.active()) {
        m_job.requestCancel();
        m_job.wait();
    }

    m_liveMatches->store(0, std::memory_order_relaxed);
    m_liveScanned->store(0, std::memory_order_relaxed);
    m_liveTotal->store(0, std::memory_order_relaxed);
    m_liveRecorded->store(0, std::memory_order_relaxed);
    m_liveSkipped->store(0, std::memory_order_relaxed);
    m_livePhase->store(0, std::memory_order_relaxed);

    SearchOptions opt = options;
    opt.liveMatches = m_liveMatches.get();
    opt.liveScanned = m_liveScanned.get();
    opt.liveTotal = m_liveTotal.get();
    opt.liveRecorded = m_liveRecorded.get();
    opt.liveSkippedCovered = m_liveSkipped.get();
    opt.livePhase = m_livePhase.get();

    const ProgressFn cb = progress;
    const SearchProgressSink sink = [cb](const SearchProgress& p) {
        if (!cb) return;
        const bool scanning = p.phase == SearchPhase::Preparing || p.phase == SearchPhase::Scanning;
        cb(p.fraction, scanning, p.chunksPerSec, p.etaSec);
    };
    const DoneFn fn = done;
    const SearchJob::DoneFn onDone = [fn](const SearchReport& report) {
        if (fn) fn(report.outcome);
    };

    if (!m_job.start(opt, sink, onDone)) {
        SearchOutcome failed;
        failed.ok = false;
        failed.error = "无法启动搜索线程";
        if (fn) fn(failed);
    }
}

void SearchTask::startReport(const SearchOptions& options, const SearchProgressSink& progress,
                            const ReportDoneFn& done)
{
    if (m_job.active()) {
        m_job.requestCancel();
        m_job.wait();
    }

    m_liveMatches->store(0, std::memory_order_relaxed);
    m_liveScanned->store(0, std::memory_order_relaxed);
    m_liveTotal->store(0, std::memory_order_relaxed);
    m_liveRecorded->store(0, std::memory_order_relaxed);
    m_liveSkipped->store(0, std::memory_order_relaxed);
    m_livePhase->store(0, std::memory_order_relaxed);

    SearchOptions opt = options;
    opt.liveMatches = m_liveMatches.get();
    opt.liveScanned = m_liveScanned.get();
    opt.liveTotal = m_liveTotal.get();
    opt.liveRecorded = m_liveRecorded.get();
    opt.liveSkippedCovered = m_liveSkipped.get();
    opt.livePhase = m_livePhase.get();

    const ReportDoneFn fn = done;
    const SearchJob::DoneFn onDone = [fn](const SearchReport& report) {
        if (fn) fn(report);
    };
    if (!m_job.start(opt, progress, onDone)) {
        SearchReport failed;
        failed.status = SearchStatus::Failed;
        failed.error = "无法启动搜索线程";
        if (fn) fn(failed);
    }
}

void SearchTask::cancel() noexcept
{
    m_job.requestCancel();
}

} // namespace slime

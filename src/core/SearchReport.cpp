#include "core/SearchReport.h"

namespace slime {

const char* searchStatusName(SearchStatus status) noexcept
{
    switch (status) {
    case SearchStatus::Ok: return "ok";
    case SearchStatus::Cancelled: return "cancelled";
    case SearchStatus::Failed: return "failed";
    }
    return "unknown";
}

SearchReport makeSearchReport(const SearchOutcome& out, uint64_t runId, bool cancelRequested,
                              const SearchProgress& progress, const SearchOptions& options)
{
    SearchReport r;
    r.runId = runId;
    r.outcome = out;
    r.progress = progress;
    r.gpuScanUsed = out.gpuScanUsed;
    r.gpuMatchUsed = out.gpuMatchUsed;
    r.cacheUsed = options.useCache;
    r.chunked = out.chunkedPath;
    r.banded = options.cacheBandLoader != nullptr;
    r.stoppedEarly = out.stoppedEarly;
    r.fallbackReason = out.gpuFallbackReason;
    r.error = out.error;

    // 取消是终态：即使引擎在竞态中返回了 ok，也不能把部分结果当成功。
    if (cancelRequested || out.cancelled) {
        r.status = SearchStatus::Cancelled;
        r.outcome.ok = false;
        r.outcome.cancelled = true;
        r.outcome.results.clear();
        r.outcome.totalFound = 0;
        r.hitsTotal = 0;
        r.hitsReturned = 0;
        if (r.outcome.error.empty()) r.outcome.error = "已取消";
        r.error = r.outcome.error;
    } else if (!out.ok) {
        r.status = SearchStatus::Failed;
        r.hitsTotal = out.totalFound;
        r.hitsReturned = static_cast<int64_t>(out.results.size());
    } else {
        r.status = SearchStatus::Ok;
        r.hitsTotal = out.totalFound;
        r.hitsReturned = static_cast<int64_t>(out.results.size());
    }
    return r;
}

} // namespace slime

// SearchReport.h - 统一搜索结果（v3.14.0 流程架构）
#pragma once

#include "core/SearchProgress.h"
#include "core/SlimeEngine.h"

#include <cstdint>
#include <string>

namespace slime {

enum class SearchStatus : uint8_t {
    Ok = 0,
    Cancelled = 1,
    Failed = 2,
};

const char* searchStatusName(SearchStatus status) noexcept;

struct SearchReport {
    SearchStatus status = SearchStatus::Failed;
    uint64_t runId = 0;
    int64_t hitsTotal = 0;      // 满足条件的命中总数（截断前）
    int64_t hitsReturned = 0;   // 实际返回条数
    bool gpuScanUsed = false;
    bool gpuMatchUsed = false;
    bool cacheUsed = false;
    bool chunked = false;
    bool banded = false;
    bool stoppedEarly = false;
    bool truncated = false;
    std::string fallbackReason;
    std::string error;
    SearchProgress progress;
    SearchOutcome outcome;      // 兼容载荷：缓存/构建等详细字段
};

SearchReport makeSearchReport(const SearchOutcome& out, uint64_t runId, bool cancelRequested,
                              const SearchProgress& progress, const SearchOptions& options);

} // namespace slime

// Phase 2：Materialize 计划的最小执行器。
#pragma once

#include "core/next/Bitmap.h"
#include "core/next/Matcher.h"

#include <string>
#include <vector>

namespace slime::next {

struct MaterializeResult {
    std::vector<slime::MatchResult> results;
    int64_t totalFound = 0;
    bool cancelled = false;
    ScanStats scan;
    MatchStats match;
};

class MaterializePipeline {
public:
    bool run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel,
             MaterializeResult& out, std::string* err,
             const slime::ProgressFn& progress = slime::ProgressFn()) const;

    // 按锚点 z 行分块；每块扫描自己的 halo，但只匹配本块拥有的锚点行。
    bool runChunked(const slime::SearchOptions& opt, int64_t rowsPerChunk,
                    const std::atomic<bool>& cancel, MaterializeResult& out, std::string* err,
                    const slime::ProgressFn& progress = slime::ProgressFn()) const;
};

} // namespace slime::next

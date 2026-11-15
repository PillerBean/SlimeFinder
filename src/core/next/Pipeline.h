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
    ScanStats scan;
    MatchStats match;
};

class MaterializePipeline {
public:
    bool run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel,
             MaterializeResult& out, std::string* err) const;
};

} // namespace slime::next

// Phase 4：GPU 融合匹配的显式计划实现（不经过 Bitmap）。
#pragma once

#include "core/SlimeEngine.h"
#include "core/next/Collector.h"

#include <atomic>
#include <string>

namespace slime::next {

struct FusedGpuStats {
    double ms = 0.0;
    int64_t windows = 0;
    int64_t totalFound = 0;
};

class FusedGpuPipeline {
public:
    // 严格图案（方向锁定）直接调用 GPU 流式匹配，结果写入 Collector。
    bool runPattern(const slime::SearchOptions& opt, const slime::GpuScan* gpu,
                    const std::atomic<bool>& cancel, ResultCollector& collector,
                    FusedGpuStats& stats, std::string* err) const;

    // 精确密度直接调用 GPU 流式匹配。
    bool runDensity(const slime::SearchOptions& opt, const slime::GpuScan* gpu,
                    const std::atomic<bool>& cancel, ResultCollector& collector,
                    FusedGpuStats& stats, std::string* err) const;
};

} // namespace slime::next

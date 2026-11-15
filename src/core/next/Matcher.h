// Phase 2：CPU 精确 Matcher（只支持 Materialize 计划的经典子集）
#pragma once

#include "core/SlimeEngine.h"
#include "core/next/Bitmap.h"
#include "core/next/Collector.h"

#include <string>

namespace slime::next {

struct MatchStats {
    double ms = 0.0;
    int64_t windows = 0;
    int64_t totalFound = 0;
};

// 分块时只允许 Matcher 处理本块拥有的锚点行，但 rank 必须保持全局口径。
struct MatchLimits {
    bool zLimited = false;
    int64_t azMin = 0;
    int64_t azMax = -1;
    int64_t rankXMin = 0;
    int64_t rankZMin = 0;
    int64_t rankAzCount = 0;
};

class Matcher {
public:
    virtual ~Matcher() = default;
    virtual const char* name() const noexcept = 0;
    virtual bool run(const Bitmap& bitmap, const slime::SearchOptions& opt,
                     const std::atomic<bool>& cancel, ResultCollector& collector,
                     MatchStats& stats, std::string* err,
                     const slime::ProgressFn& progress = slime::ProgressFn(),
                     const MatchLimits& limits = MatchLimits()) const = 0;
};

class CpuExactMatcher final : public Matcher {
public:
    const char* name() const noexcept override { return "cpu-exact"; }
    bool run(const Bitmap& bitmap, const slime::SearchOptions& opt,
             const std::atomic<bool>& cancel, ResultCollector& collector,
             MatchStats& stats, std::string* err,
             const slime::ProgressFn& progress = slime::ProgressFn(),
             const MatchLimits& limits = MatchLimits()) const override;
};

} // namespace slime::next

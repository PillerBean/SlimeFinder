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

class Matcher {
public:
    virtual ~Matcher() = default;
    virtual const char* name() const noexcept = 0;
    virtual bool run(const Bitmap& bitmap, const slime::SearchOptions& opt,
                     const std::atomic<bool>& cancel, ResultCollector& collector,
                     MatchStats& stats, std::string* err) const = 0;
};

class CpuExactMatcher final : public Matcher {
public:
    const char* name() const noexcept override { return "cpu-exact"; }
    bool run(const Bitmap& bitmap, const slime::SearchOptions& opt,
             const std::atomic<bool>& cancel, ResultCollector& collector,
             MatchStats& stats, std::string* err) const override;
};

} // namespace slime::next

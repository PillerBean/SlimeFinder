// Phase 5：把 Planner 产出的计划分派到四个执行器。
#pragma once

#include "core/next/FusedGpu.h"
#include "core/next/Pipeline.h"
#include "core/next/Plan.h"

#include <string>

namespace slime::next {

class PlanExecutor {
public:
    bool run(const slime::SearchOptions& opt, const ExecutionPlan& plan,
             const std::atomic<bool>& cancel, MaterializeResult& out, std::string* err,
             const slime::ProgressFn& progress = slime::ProgressFn()) const;

    // 只检查计划/输入是否被当前原型支持，不执行扫描。
    static bool supported(const slime::SearchOptions& opt, const ExecutionPlan& plan,
                          std::string* why = nullptr);
};

} // namespace slime::next

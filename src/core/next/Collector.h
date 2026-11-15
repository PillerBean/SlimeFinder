// 新框架 Phase 1：结果收集策略。
// 只负责 TopN/排序/计数，不负责判定或匹配。
#pragma once

#include "core/SlimeEngine.h"
#include "core/next/Plan.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace slime::next {

struct Candidate {
    slime::MatchResult result;
    int64_t d2 = 0;      // 窗口中心到起点的 2 倍偏移平方距离
    int64_t rank = 0;    // 光栅/变体枚举序，平局用
    double density = 0.0;
};

class ResultCollector {
public:
    ResultCollector(CollectorKind kind, int64_t limit) : m_kind(kind), m_limit(limit) {}

    void add(const Candidate& c);
    void merge(const ResultCollector& other);

    int64_t totalFound() const;
    size_t size() const;
    std::vector<slime::MatchResult> results() const;

private:
    struct Better;
    void pruneLocked();

    CollectorKind m_kind = CollectorKind::DistanceTopN;
    int64_t m_limit = 0;
    int64_t m_total = 0;
    std::vector<Candidate> m_items;
    mutable std::mutex m_mu;
};

} // namespace slime::next

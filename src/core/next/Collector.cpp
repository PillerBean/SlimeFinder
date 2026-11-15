#include "core/next/Collector.h"

#include <algorithm>

namespace slime::next {

struct ResultCollector::Better {
    CollectorKind kind;
    bool operator()(const Candidate& a, const Candidate& b) const noexcept
    {
        if (kind == CollectorKind::DensityTopN && a.density != b.density) {
            return a.density > b.density;
        }
        if (a.d2 != b.d2) return a.d2 < b.d2;
        return a.rank < b.rank;
    }
};

void ResultCollector::pruneLocked()
{
    if (m_limit <= 0) return;
    const size_t keep = static_cast<size_t>(m_limit);
    const size_t trigger = (std::max)(keep * 4, size_t{ 4096 });
    if (m_items.size() <= trigger) return;
    std::sort(m_items.begin(), m_items.end(), Better{ m_kind });
    m_items.resize(keep);
}

void ResultCollector::add(const Candidate& c)
{
    std::lock_guard<std::mutex> lk(m_mu);
    ++m_total;
    if (m_kind == CollectorKind::CountOnly) return;
    m_items.push_back(c);
    pruneLocked();
}

void ResultCollector::merge(const ResultCollector& other)
{
    if (&other == this) return;
    std::vector<Candidate> items;
    int64_t total = 0;
    {
        std::lock_guard<std::mutex> lk(other.m_mu);
        items = other.m_items;
        total = other.m_total;
    }
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_total += total;
        if (m_kind != CollectorKind::CountOnly) {
            m_items.insert(m_items.end(), items.begin(), items.end());
            pruneLocked();
        }
    }
}

int64_t ResultCollector::totalFound() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_total;
}

size_t ResultCollector::size() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_kind == CollectorKind::CountOnly) return 0;
    if (m_limit > 0) return (std::min)(m_items.size(), static_cast<size_t>(m_limit));
    return m_items.size();
}

std::vector<slime::MatchResult> ResultCollector::results() const
{
    std::vector<Candidate> items;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_kind == CollectorKind::CountOnly) return {};
        items = m_items;
    }
    std::sort(items.begin(), items.end(), Better{ m_kind });
    if (m_limit > 0 && items.size() > static_cast<size_t>(m_limit)) {
        items.resize(static_cast<size_t>(m_limit));
    }
    std::vector<slime::MatchResult> out;
    out.reserve(items.size());
    for (const Candidate& c : items) out.push_back(c.result);
    return out;
}

} // namespace slime::next

#include "core/SearchProgress.h"

#include <algorithm>

namespace slime {

const char* searchPhaseName(SearchPhase phase) noexcept
{
    switch (phase) {
    case SearchPhase::Preparing: return "preparing";
    case SearchPhase::Scanning: return "scanning";
    case SearchPhase::Matching: return "matching";
    case SearchPhase::Finalizing: return "finalizing";
    case SearchPhase::Done: return "done";
    case SearchPhase::Cancelled: return "cancelled";
    case SearchPhase::Failed: return "failed";
    }
    return "unknown";
}

ProgressContract::ProgressContract(SearchProgressSink sink) : m_sink(std::move(sink))
{
}

void ProgressContract::setSink(SearchProgressSink sink)
{
    std::lock_guard<std::mutex> lk(m_mu);
    m_sink = std::move(sink);
}

void ProgressContract::publish(double fraction, SearchPhase phase, double chunksPerSec, double etaSec,
                            int64_t done, int64_t total)
{
    const bool terminalPhase = phase == SearchPhase::Done || phase == SearchPhase::Cancelled ||
                               phase == SearchPhase::Failed;
    SearchProgressSink sink;
    SearchProgress out;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_terminal) return;
        if (terminalPhase) {
            m_terminal = true;
            m_last.sequence = ++m_sequence;
            m_last.terminal = true;
            m_last.phase = phase;
            if (phase == SearchPhase::Done) m_last.fraction = 1.0;
            else m_last.fraction = std::max(m_last.fraction, std::clamp(fraction, 0.0, 1.0));
            m_last.chunksPerSec = 0.0;
            m_last.etaSec = 0.0;
            out = m_last;
        } else {
            SearchProgress p;
            p.fraction = std::clamp(fraction, 0.0, 1.0);
            p.phase = phase;
            p.chunksPerSec = chunksPerSec;
            p.etaSec = etaSec;
            p.done = done;
            p.total = total;
            if (p.fraction < m_last.fraction) p.fraction = m_last.fraction;
            if (m_last.phase == SearchPhase::Matching && p.phase == SearchPhase::Scanning) {
                p.phase = SearchPhase::Matching;
            }
            p.sequence = ++m_sequence;
            p.terminal = false;
            m_last = p;
            out = p;
        }
        sink = m_sink;
    }
    if (sink) sink(out);
}

void ProgressContract::finish(SearchPhase terminalPhase)
{
    double fraction = 0.0;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        fraction = m_last.fraction;
    }
    publish(fraction, terminalPhase);
}

SearchProgress ProgressContract::last() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_last;
}

} // namespace slime

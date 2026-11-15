// SearchProgress.h - 统一进度契约（v3.14.0 流程架构）
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace slime {

enum class SearchPhase : uint8_t {
    Preparing = 0,
    Scanning = 1,
    Matching = 2,
    Finalizing = 3,
    Done = 4,
    Cancelled = 5,
    Failed = 6,
};

const char* searchPhaseName(SearchPhase phase) noexcept;

struct SearchProgress {
    uint64_t sequence = 0;
    double fraction = 0.0;   // 0..1, 单调
    SearchPhase phase = SearchPhase::Preparing;
    int64_t done = 0;        // 0 = 引擎未公开
    int64_t total = 0;       // 0 = 引擎未公开
    double chunksPerSec = 0.0;
    double etaSec = 0.0;
    bool terminal = false;
};

using SearchProgressSink = std::function<void(const SearchProgress&)>;

// 线程安全、单调、终态幂等。所有引擎路径都通过它发布进度。
class ProgressContract {
public:
    explicit ProgressContract(SearchProgressSink sink = {});
    ProgressContract(const ProgressContract&) = delete;
    ProgressContract& operator=(const ProgressContract&) = delete;

    void setSink(SearchProgressSink sink);
    void publish(double fraction, SearchPhase phase, double chunksPerSec = 0.0, double etaSec = 0.0,
              int64_t done = 0, int64_t total = 0);
    void finish(SearchPhase terminalPhase);
    SearchProgress last() const;

private:
    mutable std::mutex m_mu;
    SearchProgressSink m_sink;
    SearchProgress m_last;
    bool m_terminal = false;
    uint64_t m_sequence = 0;
};

} // namespace slime

// PerfLog.cpp - 引擎侧性能事件实现（v3.3.7.2 任务书步骤 2 / P1-D）
#include "core/PerfLog.h"

#include "utils/IncidentLog.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace slime {
namespace perf {

namespace {

int64_t nowMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string fmt(const char* f, ...) noexcept
{
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

} // namespace

int logicalCores() noexcept
{
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 0 ? static_cast<int>(hw) : 1;
}

int64_t cpuTime100ns() noexcept
{
#ifdef _WIN32
    FILETIME create{}, exitT{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exitT, &kernel, &user)) return -1;
    auto toI64 = [](const FILETIME& f) {
        ULARGE_INTEGER u;
        u.LowPart = f.dwLowDateTime;
        u.HighPart = f.dwHighDateTime;
        return static_cast<int64_t>(u.QuadPart);
    };
    return toI64(kernel) + toI64(user);
#else
    return -1;
#endif
}

double currentWorkingSetMB() noexcept
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
#endif
    return 0.0;
}

bool CpuSampler::sample(double* cpuPercent, double* wsMB) noexcept
{
    return sample(cpuPercent, nullptr, wsMB);
}

bool CpuSampler::sample(double* cpuPercent, double* cpuAllPct, double* wsMB) noexcept
{
    // v3.4.13（任务书第 1 步）：加锁串行化 —— 多个块工作线程会同时调用本函数。
    std::lock_guard<std::mutex> lock(m_mu);
    // 注意：(std::max) 加括号是为了避开 windows.h 的 max 宏（同 MainWindow.cpp 的写法）
    if (m_cores <= 0) m_cores = (std::max)(1, logicalCores()); // v3.4.13：默认 0 ⇒ 这里才真正赋值
    const int64_t cpu = cpuTime100ns();
    const int64_t ms = nowMs();
    if (cpu < 0) return false;
    if (m_cpu100ns < 0 || m_ms < 0) {
        m_cpu100ns = cpu;
        m_ms = ms;
        return false; // 第一次只建立基线
    }
    const int64_t dCpu = cpu - m_cpu100ns;
    const int64_t dMs = ms - m_ms;
    m_cpu100ns = cpu;
    m_ms = ms;
    if (dMs <= 0) return false;
    // CPU 时间单位 100ns：dCpu/10000 = ms。
    // all  = 各核占用之和（100% = 一个核跑满；上限 = 100% × 核数）
    // cpu  = 相对全部逻辑核（100% = 所有核跑满）
    double all = 100.0 * (static_cast<double>(dCpu) / 10000.0) / static_cast<double>(dMs);
    if (all < 0.0) all = 0.0;
    const double cap = 100.0 * static_cast<double>(m_cores);
    if (all > cap) all = cap; // 物理上限：采样窗口内的 CPU 时间不可能超过「核数 × 墙钟」
    if (cpuAllPct) *cpuAllPct = all;
    if (cpuPercent) *cpuPercent = all / static_cast<double>(m_cores);
    if (wsMB) *wsMB = currentWorkingSetMB();
    return true;
}

void chunkBegin(int64_t ci, int64_t rows, int64_t z0, int64_t elapsedMs) noexcept
{
    IncidentLog::note(fmt("chunk.begin ci=%lld rows=%lld z0=%lld t=%lldms", static_cast<long long>(ci),
                          static_cast<long long>(rows), static_cast<long long>(z0),
                          static_cast<long long>(elapsedMs)));
}

void chunkEnd(int64_t ci, int64_t ms, int64_t scanned, int64_t results, int64_t elapsedMs) noexcept
{
    IncidentLog::note(fmt("chunk.end ci=%lld ms=%lld scanned=%lld results=%lld t=%lldms",
                          static_cast<long long>(ci), static_cast<long long>(ms),
                          static_cast<long long>(scanned), static_cast<long long>(results),
                          static_cast<long long>(elapsedMs)));
}

void chunkMerge(int64_t ci, int64_t waitMs, int64_t ms, int64_t entriesBefore, int64_t added,
                int64_t batched) noexcept
{
    IncidentLog::note(fmt("chunk.merge ci=%lld wait=%lldms ms=%lld entries=%lld added=%lld batched=%lld",
                          static_cast<long long>(ci), static_cast<long long>(waitMs),
                          static_cast<long long>(ms), static_cast<long long>(entriesBefore),
                          static_cast<long long>(added), static_cast<long long>(batched)));
}

void cachePrune(int64_t ci, int64_t ms, int64_t drops, int64_t entries, int64_t bytes) noexcept
{
    IncidentLog::note(fmt("cache.prune ci=%lld ms=%lld drops=%lld entries=%lld bytes=%lld",
                          static_cast<long long>(ci), static_cast<long long>(ms),
                          static_cast<long long>(drops), static_cast<long long>(entries),
                          static_cast<long long>(bytes)));
}

void cpuSample(int64_t elapsedMs, double cpuPercent, double wsMB, double peakMB) noexcept
{
    // v3.4.13：同时给出核数与「各核之和」口径，避免把 100% 误读成「只有一个核在跑」
    const int cores = logicalCores();
    IncidentLog::note(fmt("cpu.sample t=%.0fs cpu=%.1f%% cores=%d all=%.0f%% ws=%.0fMB peak=%.0fMB",
                          static_cast<double>(elapsedMs) / 1000.0, cpuPercent, cores,
                          cpuPercent * static_cast<double>(cores), wsMB, peakMB));
}

void memPeak(int64_t elapsedMs, double peakMB, double wsMB) noexcept
{
    IncidentLog::note(fmt("mem.peak t=%.0fs peak=%.0fMB ws=%.0fMB",
                          static_cast<double>(elapsedMs) / 1000.0, peakMB, wsMB));
}

void searchSummary(const std::string& text) noexcept
{
    IncidentLog::note("search.summary " + text);
}

} // namespace perf
} // namespace slime

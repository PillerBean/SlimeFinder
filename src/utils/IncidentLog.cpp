// IncidentLog.cpp - 关键信息日志实现（v3.3.7）
#include "utils/IncidentLog.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace slime {

namespace {

std::mutex g_lock;
std::filesystem::path g_dir;          // logs/
std::filesystem::path g_file;         // logs/crash-YYYY-MM-DD.log
std::filesystem::path g_marker;       // logs/.session
std::atomic<bool> g_incident{ false };
std::atomic<bool> g_verbose{ false };
std::string g_prevInfo;

std::string todayStamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_s(&tmv, &tt);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return buf;
}

std::string clockStamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_s(&tmv, &tt);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

std::string fullStamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_s(&tmv, &tt);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

// 追加一行（内部调用者需已持有 g_lock 或自行保证）
void appendLocked(const std::string& line)
{
    if (g_file.empty()) return;
    std::ofstream out(g_file, std::ios::app);
    if (!out) return;
    out << line << '\n';
}

} // namespace

void IncidentLog::init(const std::filesystem::path& logDir, const std::string& version)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_dir = logDir;
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    g_file = g_dir / ("crash-" + todayStamp() + ".log");
    g_marker = g_dir / ".session";

    // 上次会话是否异常结束？（标记存在 → 未正常退出）
    std::error_code mec;
    if (std::filesystem::exists(g_marker, mec)) {
        std::ifstream in(g_marker);
        std::stringstream ss;
        ss << in.rdbuf();
        g_prevInfo = ss.str();
        while (!g_prevInfo.empty() && (g_prevInfo.back() == '\n' || g_prevInfo.back() == '\r')) {
            g_prevInfo.pop_back();
        }
    } else {
        g_prevInfo.clear();
    }

    // 写会话标记（内容 = 启动时刻 + PID + 版本）
    {
        std::ofstream mk(g_marker, std::ios::trunc);
        if (mk) mk << fullStamp() << " pid=" << static_cast<long long>(GetCurrentProcessId())
                   << " version=" << version << '\n';
    }

    {
        std::ostringstream os;
        os << "===== 会话开始 " << fullStamp() << " 版本 " << version << " =====";
        appendLocked(os.str());
    }

    // 启动环境
    {
        std::ostringstream os;
        os << "[" << clockStamp() << "] 环境：OS=";
#ifdef _WIN32
        {
            OSVERSIONINFOEXW vi{};
            vi.dwOSVersionInfoSize = sizeof(vi);
#pragma warning(push)
#pragma warning(disable : 4996) // GetVersionExW 在本机工具链下已弃用但可用
            if (GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&vi))) {
                os << static_cast<int>(vi.dwMajorVersion) << '.' << static_cast<int>(vi.dwMinorVersion)
                   << " build " << static_cast<unsigned long>(vi.dwBuildNumber);
            } else {
                os << "Windows";
            }
#pragma warning(pop)
        }
#else
        os << "non-Windows";
#endif
        os << " | 逻辑核心=" << std::thread::hardware_concurrency() << " | " << memorySummary();
        appendLocked(os.str());
    }
}

void IncidentLog::note(const std::string& line)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_file.empty()) return;
    std::ostringstream os;
    os << "[" << clockStamp() << "] " << line;
    appendLocked(os.str());
}

void IncidentLog::context(const std::string& key, const std::string& value)
{
    note(key + " = " + value);
}

void IncidentLog::markIncident(const std::string& what)
{
    g_incident.store(true, std::memory_order_relaxed);
    note(std::string("!! 异常/中断事件：") + what + " | " + memorySummary());
}

bool IncidentLog::hadIncident()
{
    return g_incident.load(std::memory_order_relaxed);
}

void IncidentLog::sessionEndClean()
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_incident.load(std::memory_order_relaxed)) {
        // 本次会话出现过取消/错误：保留标记，便于下次启动提示（并注明原因）
        appendLocked("[" + clockStamp() + "] 会话结束（发生过异常/中断，保留 .session 标记）");
        return;
    }
    appendLocked("[" + clockStamp() + "] 会话正常结束");
    std::error_code ec;
    if (!g_marker.empty()) std::filesystem::remove(g_marker, ec);
}

bool IncidentLog::previousSessionUnclean()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return !g_prevInfo.empty();
}

std::string IncidentLog::previousSessionInfo()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_prevInfo;
}

std::filesystem::path IncidentLog::currentFile()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_file;
}

std::filesystem::path IncidentLog::markerFile()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_marker;
}

std::filesystem::path IncidentLog::logDir()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_dir;
}

void IncidentLog::setVerbose(bool on)
{
    g_verbose.store(on, std::memory_order_relaxed);
}

bool IncidentLog::verbose()
{
    return g_verbose.load(std::memory_order_relaxed);
}

std::string IncidentLog::memorySummary()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "WorkingSet=%.0fMB Peak=%.0fMB Pagefile=%.0fMB",
                      static_cast<double>(pmc.WorkingSetSize) / 1048576.0,
                      static_cast<double>(pmc.PeakWorkingSetSize) / 1048576.0,
                      static_cast<double>(pmc.PagefileUsage) / 1048576.0);
        return buf;
    }
#endif
    return "memory=unknown";
}

double IncidentLog::peakWorkingSetMB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<double>(pmc.PeakWorkingSetSize) / 1048576.0;
    }
#endif
    return 0.0;
}

} // namespace slime

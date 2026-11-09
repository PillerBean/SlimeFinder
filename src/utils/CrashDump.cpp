// CrashDump.cpp - Windows 崩溃转储实现
#include "utils/CrashDump.h"
#include "utils/IncidentLog.h" // v3.3.7：崩溃时同时写入关键信息日志

#include <chrono>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace slime {

#ifdef _WIN32

namespace {

std::mutex g_lock;
std::filesystem::path g_dir;
std::atomic<bool> g_enabled{ true };   // v3.3.7：由设置控制（默认开）
std::atomic<bool> g_dumpOnce{ false }; // v3.3.7：首次机会 dump 只写一次

std::string timestampName()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_s(&tmv, &tt);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

LONG WINAPI exceptionFilter(EXCEPTION_POINTERS* ep) noexcept
{
    if (!g_enabled.load(std::memory_order_relaxed)) return EXCEPTION_CONTINUE_SEARCH; // v3.3.7
    const DWORD code = ep ? ep->ExceptionRecord->ExceptionCode : 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        std::error_code ec;
        std::filesystem::create_directories(g_dir, ec);

        const std::string base = timestampName();
        const std::string reason = "未处理异常 code=0x" + [&]() {
            char b[16];
            std::snprintf(b, sizeof(b), "%08X", static_cast<unsigned>(code));
            return std::string(b);
        }();

        const std::filesystem::path dumpPath = std::filesystem::path(g_dir) / ("SlimeFinder_" + base + ".dmp");
        const std::filesystem::path reportPath = std::filesystem::path(g_dir) / "crash_report.txt";
        // v3.3.7：未处理异常同样写关键信息日志（放锁内，避免与其他线程交错）
        IncidentLog::markIncident("未处理异常 code=0x" + [&]() {
            char b[16];
            std::snprintf(b, sizeof(b), "%08X", static_cast<unsigned>(code));
            return std::string(b);
        }());
        {
            std::ofstream rep(reportPath, std::ios::app);
            if (rep) {
                rep << '[' << timestampName() << "] " << reason
                    << " address=0x" << std::hex
                    << (ep && ep->ExceptionRecord->ExceptionAddress
                            ? reinterpret_cast<unsigned long long>(ep->ExceptionRecord->ExceptionAddress)
                            : 0ULL)
                    << " dmp=" << dumpPath.string() << '\n';
            }
        }
        HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                              static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithUnloadedModules),
                              ep ? &mei : nullptr, nullptr, nullptr);
            CloseHandle(hFile);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER; // 交给默认处理（弹窗/退出）
}

void terminateHandler() noexcept
{
    if (!g_enabled.load(std::memory_order_relaxed)) { std::abort(); } // v3.3.7：设置可关闭
    {
        std::lock_guard<std::mutex> lock(g_lock);
        std::error_code ec;
        std::filesystem::create_directories(g_dir, ec);
        const std::string base = timestampName();
        const std::filesystem::path dumpPath = std::filesystem::path(g_dir) / ("SlimeFinder_" + base + ".dmp");
        HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpWithDataSegs, nullptr, nullptr, nullptr);
            CloseHandle(hFile);
        }
        std::ofstream rep(std::filesystem::path(g_dir) / "crash_report.txt", std::ios::app);
        if (rep) rep << '[' << base << "] std::terminate（未捕获 C++ 异常） dmp=" << dumpPath.string() << '\n';
    }
    std::abort();
}

} // namespace

void CrashDump::install(const std::filesystem::path& dumpDir)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_dir = dumpDir;
    std::error_code ec;
    std::filesystem::create_directories(dumpDir, ec);
    SetUnhandledExceptionFilter(exceptionFilter);
    std::set_terminate(terminateHandler);
}

void CrashDump::writeDump(const std::filesystem::path& dumpDir, const char* reason)
{
    if (!g_enabled.load(std::memory_order_relaxed)) return; // v3.3.7：设置可关闭
    std::lock_guard<std::mutex> lock(g_lock);
    g_dir = dumpDir.empty() ? g_dir : dumpDir;
    if (g_dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    const std::string base = timestampName();
    const std::filesystem::path dumpPath = std::filesystem::path(g_dir) / ("SlimeFinder_" + base + ".dmp");
    HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpWithDataSegs, nullptr, nullptr, nullptr);
        CloseHandle(hFile);
    }
    std::ofstream rep(std::filesystem::path(g_dir) / "crash_report.txt", std::ios::app);
    if (rep) {
        rep << '[' << base << "] " << (reason ? reason : "manual dump") << " dmp=" << dumpPath.string() << '\n';
    }
}

// ---------------------------------------------------------------- v3.3.7 新增
void CrashDump::setEnabled(bool on)
{
    g_enabled.store(on, std::memory_order_relaxed);
}

bool CrashDump::enabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

std::filesystem::path CrashDump::dumpDir()
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_dir;
}

// 首次机会（VEH）写 dump：**只写一次**，避免异常风暴把磁盘写满。
// 用于崩溃绕过 UEF/terminate 的场景（堆损坏、__fastfail、线程栈溢出等）。
bool CrashDump::writeDumpNow(const char* reason, void* exceptionPointers)
{
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    bool expected = false;
    if (!g_dumpOnce.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false; // 已经写过一份（本进程内）
    }
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    const std::string base = timestampName();
    const std::filesystem::path dumpPath = std::filesystem::path(g_dir) / ("SlimeFinder_" + base + ".dmp");
    const std::filesystem::path reportPath = std::filesystem::path(g_dir) / "crash_report.txt";
    bool wrote = false;
    HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        MINIDUMP_EXCEPTION_INFORMATION* pMei = nullptr;
        if (exceptionPointers) {
            auto* ep = static_cast<EXCEPTION_POINTERS*>(exceptionPointers);
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            pMei = &mei;
        }
        wrote = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                                  static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithUnloadedModules),
                                  pMei, nullptr, nullptr) != FALSE;
        CloseHandle(hFile);
    }
    std::ofstream rep(reportPath, std::ios::app);
    if (rep) {
        rep << '[' << base << "] " << (reason ? reason : "first-chance dump")
            << " dmp=" << dumpPath.string() << (wrote ? "" : " (MiniDumpWriteDump 失败)") << '\n';
    }
    return wrote;
}

#else // 非 Windows：空实现

void CrashDump::install(const std::filesystem::path&) {}
void CrashDump::writeDump(const std::filesystem::path&, const char*) {}
void CrashDump::setEnabled(bool) {}
bool CrashDump::enabled() { return false; }
bool CrashDump::writeDumpNow(const char*, void*) { return false; }
std::filesystem::path CrashDump::dumpDir() { return {}; }

#endif

} // namespace slime


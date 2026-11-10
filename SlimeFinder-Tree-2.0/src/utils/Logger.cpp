// Logger.cpp - 日志实现
#include "utils/Logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace slime {

namespace {
std::mutex g_mutex;
std::filesystem::path g_dir;
} // namespace

void Logger::init(const std::filesystem::path& dir)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dir = dir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
}

std::string Logger::timestamp()
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t tt = system_clock::to_time_t(now);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec, static_cast<int>(ms.count()));
    return buf;
}

std::string Logger::levelName(Level lv)
{
    switch (lv) {
    case Level::Warning: return "WARN";
    case Level::Error: return "ERROR";
    default: return "INFO";
    }
}

void Logger::log(Level level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_dir.empty()) return; // 未初始化：静默丢弃
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    if (ec) return;

    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char datebuf[32];
    std::snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);

    const std::filesystem::path file = g_dir / ("SlimeFinder_" + std::string(datebuf) + ".log");
    std::ofstream out(file, std::ios::app);
    if (!out) return;
    out << '[' << timestamp() << "][" << levelName(level) << "] " << message << '\n';
}

void Logger::info(const std::string& m) { log(Level::Info, m); }
void Logger::warning(const std::string& m) { log(Level::Warning, m); }
void Logger::error(const std::string& m) { log(Level::Error, m); }

std::string Logger::currentFile()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_dir.empty()) return {};
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char datebuf[32];
    std::snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return (g_dir / ("SlimeFinder_" + std::string(datebuf) + ".log")).string();
}

} // namespace slime

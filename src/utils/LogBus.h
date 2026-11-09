// LogBus.h - v3.3.4（任务书 2.3）：日志总线（默认关闭；两级模式；环形缓冲；按日期分文件）
//
// 与既有 utils/Logger.h 的关系（两者互相独立，LogBus 不改变 Logger 的任何行为）：
//   Logger —— 既有的轻量运行日志，Qt 无关，写 <dir>/SlimeFinder_YYYY-MM-DD.log；
//   LogBus —— 本次新增的「性能 / 诊断」日志总线，由「工具 → 启用日志」控制：
//              · 默认关闭（LogMode::Off）：log() 立刻返回，零分配、零写盘；
//              · 两级模式：简要（INFO 及以上）/ 详细（含 DEBUG、TRACE）；
//              · 内存环形缓冲最多 kMaxEntries 条，供底部「日志」面板展示；
//              · 文件按本地日期分文件：<logDir>/performance-YYYY-MM-DD.log。
//
// 本头文件不包含任何 Qt 头文件（仅标准库），核心逻辑可在纯 C++ 环境下编译与使用。
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace slime {

enum class LogLevel : int { Trace = 0, Debug = 1, Info = 2, Warning = 3, Error = 4 };

// 日志模式（任务书 2.3 的两级选项）
enum class LogMode : int {
    Off = 0,       // 默认：不写文件、不记录任何条目
    Brief = 1,     // 简要日志（推荐）：INFO 及以上
    Detailed = 2,  // 详细日志：DEBUG / TRACE 全记录（界面提示可能降低搜索速度 10~30%）
};

struct LogEntry {
    double t = 0.0;        // 相对进程启动的秒数（steady_clock）
    LogLevel level = LogLevel::Info;
    std::string text;
};

class LogBus {
public:
    static void init(const std::filesystem::path& logDir); // 目录（logs/），可重复调用
    static void setMode(LogMode m);                        // 切换模式（Off 时停止写文件）
    static LogMode mode();
    static const char* modeName(LogMode m);                 // "关闭" / "简要" / "详细"
    static bool enabled();                                  // mode != Off

    // 记录一条（线程安全；Off 时直接返回，不分配、不写盘）
    static void log(LogLevel level, const std::string& message);
    static void trace(const std::string& m);    // 仅 Detailed 记录
    static void debug(const std::string& m);    // 仅 Detailed 记录
    static void info(const std::string& m);
    static void warning(const std::string& m);
    static void error(const std::string& m);

    static std::vector<LogEntry> snapshot(); // 拷贝当前环形缓冲（最多 kMaxEntries 条）
    static void clear();
    static int64_t droppedCount();           // 因上限被丢弃的条目数（用于界面提示）

    static constexpr size_t kMaxEntries = 10000; // 任务书：内存最多 10000 条

    // 监听器（GUI 面板注册；从任意线程调用，实现方需自行切线程）
    using Listener = std::function<void()>;
    static void setListener(Listener l);

    static const char* levelName(LogLevel lv);      // "TRACE"/"DEBUG"/"INFO"/"WARN"/"ERROR"
    static std::string currentFile();               // logs/performance-YYYY-MM-DD.log（未初始化返回空）
};

} // namespace slime

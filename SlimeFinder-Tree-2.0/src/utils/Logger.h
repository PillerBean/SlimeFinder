// Logger.h - 轻量日志系统（Qt 无关）
//
// 文件：<dir>/SlimeFinder_YYYY-MM-DD.log（按日期轮转，追加写）
// 线程安全；供引擎与 GUI 共用。日志级别：Info / Warning / Error。
#pragma once

#include <filesystem>
#include <string>

namespace slime {

class Logger {
public:
    enum class Level : int { Info = 0, Warning = 1, Error = 2 };

    // 初始化日志目录（自动创建；多次调用幂等）
    static void init(const std::filesystem::path& dir);

    static void log(Level level, const std::string& message);
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);

    // 当前日志文件路径（测试/展示用），未初始化返回空
    static std::string currentFile();

private:
    static std::string timestamp();
    static std::string levelName(Level lv);
};

} // namespace slime

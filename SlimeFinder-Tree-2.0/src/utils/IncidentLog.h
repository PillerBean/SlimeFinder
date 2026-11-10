// IncidentLog.h - 关键信息日志（v3.3.7 紧急修复任务书 P1-6）
//
// 与 LogBus / Logger 的分工：
//   * LogBus/Logger 是「用户可开关的常规日志」（默认关闭，可切简要/详细）；
//   * 本模块**始终可用**，只记录「排查崩溃/取消必须知道的那几行」：
//       启动环境、搜索参数、进度状态环（每 N 秒一条）、取消/错误/终止事件、内存峰值。
//     文件固定为 <程序目录>/logs/crash-YYYY-MM-DD.log（按天分文件，追加写）。
//   * 目的：用户报告「取消后闪退、没有任何日志和 dump」时，仍然有据可查。
//
// 会话标记（logs/.session）：
//   * 启动时创建；正常退出时删除；
//   * 若启动时发现上一次的标记仍在 → 说明上次会话异常结束，GUI 可在启动时提示用户查看
//     logs/crash-*.log 与 crashes/*.dmp（最小 UI 改动，见 MainWindow）。
//
// 约束：Qt 无关、可在任何线程调用（内部加锁）、永不抛出（写失败静默忽略）。
#pragma once

#include <filesystem>
#include <string>

namespace slime {

class IncidentLog {
public:
    // 初始化：创建目录、写会话标记、记录启动环境（版本/OS/CPU/内存）
    static void init(const std::filesystem::path& logDir, const std::string& version);

    // 追加一行（自动加 [HH:MM:SS] 前缀）
    static void note(const std::string& line);

    // 键值上下文（启动参数、设置等）
    static void context(const std::string& key, const std::string& value);

    // 本次会话是否发生了「异常」（取消/错误/崩溃）——退出时若为 true 则保留标记
    static void markIncident(const std::string& what);
    static bool hadIncident();

    // 正常退出：删除会话标记（若中途发生过 incident，仍会保留标记供下次启动提示）
    static void sessionEndClean();

    // 上次会话是否异常结束（启动时调用；同时把上次的标记内容读出来）
    static bool previousSessionUnclean();
    static std::string previousSessionInfo();

    static std::filesystem::path currentFile();  // logs/crash-YYYY-MM-DD.log（未初始化返回空）
    static std::filesystem::path markerFile();   // logs/.session
    static std::filesystem::path logDir();

    // 调试模式：记录更多细节（进度状态环更密）
    static void setVerbose(bool on);
    static bool verbose();

    // 进程内存摘要（"WorkingSet=..MB Peak=..MB"），崩溃/取消/完成时写一行
    static std::string memorySummary();
    static double peakWorkingSetMB();
};

} // namespace slime

// CrashDump.h - Windows 崩溃转储（.dmp）生成（Qt 无关）
//
// 使用 SetUnhandledExceptionFilter 捕获未处理异常并生成转储文件到
// <exe 目录>/crashes/ 下，文件名含时间戳。同时把崩溃摘要写入
// <exe 目录>/crashes/crash_report.txt 便于快速排查。
//
// v3.3.7（紧急修复任务书 P0-2/P1-6）：本模块的定位是「任何可捕获的致命故障都要留下 dump」，
// 因此新增三项能力：
//   1. setEnabled(bool)：由「程序设置 → 高级 → 崩溃时生成转储文件」控制（默认开）；
//   2. writeDumpNow(reason, exceptionPointers)：供 VEH 在**首次机会（first-chance）**阶段
//      主动写 dump —— 堆损坏 / __fastfail / 线程栈溢出这类故障会绕过 UEF 与 terminate，
//      只有首次机会才能留下现场（内部只写一次，防重入）；
//   3. dumpDir()：供 GUI 提示用户「详情见 crashes/」。
#pragma once

#include <filesystem>
#include <string>

namespace slime {

class CrashDump {
public:
    // 安装全局未处理异常过滤器（重复调用幂等）
    static void install(const std::filesystem::path& dumpDir);

    // 立即为当前进程生成一份转储（供 terminate/其他入口使用）
    static void writeDump(const std::filesystem::path& dumpDir, const char* reason);

    // ---- v3.3.7 新增 ----
    static void setEnabled(bool on);   // 关闭后不再写 .dmp / crash_report.txt（默认开）
    static bool enabled();

    // 首次机会写 dump：exceptionPointers 可为空；返回是否真的写出了文件。
    // 内部有「本进程只写一次」保护，可在任意线程调用。
    static bool writeDumpNow(const char* reason, void* exceptionPointers);

    static std::filesystem::path dumpDir();
};

} // namespace slime

// SoundPlayer.h - v3.3.6（任务书 4.2）：搜索完成提示音（Windows waveOut 实现，Qt 无关）
//
// 背景：本机 Qt 6.11.2（C:\Qt\6.11.2\msvc2022_64）未安装 Qt6Multimedia，
// 因此提示音不用 Qt Multimedia，而是直接用 Windows winmm 的 waveOut API
// 播放一段内存中的 PCM 数据：
//   · 音量以百分比缩放采样振幅（0 → 静音，100 → 0.35 满量程），不改动系统音量；
//   · waveOutWrite 为异步提交，调用后立即返回，不会阻塞 GUI 线程；
//   · 本头文件只包含标准库（不含任何 Qt 头文件），非 Windows 平台为空实现。
//
// 线程约定：只允许 GUI 线程调用（内部不做同步；搜索完成时由 GUI 线程触发）。
#pragma once

namespace slime {

// 搜索完成提示音。
class SoundPlayer {
public:
    // 异步播放「搜索完成」提示音：提交后立即返回（不阻塞、不抛异常）。
    // volumePercent：音量百分比，会被夹到 0..100；< 0 视为 0（静音，直接返回）。
    // 已 setEnabled(false)、无音频设备或任何 Win32 调用失败时静默返回。
    static void playCompletionChime(int volumePercent);

    // 提示音总开关（设置对话框的「提示音」复选框）。
    // 关闭时会同时停止并释放正在播放的声音（设备句柄与缓冲一并归还）。
    static void setEnabled(bool on);
    static bool enabled();

    // 是否成功初始化过音频设备（自检 / 日志参考；最近一次播放失败后为 false）。
    static bool available();
};

} // namespace slime

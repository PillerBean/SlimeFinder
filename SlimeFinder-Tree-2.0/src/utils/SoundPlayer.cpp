// SoundPlayer.cpp - 搜索完成提示音实现（Windows waveOut；其他平台空实现）
#include "utils/SoundPlayer.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

#ifdef _WIN32
// 注意：WIN32_LEAN_AND_MEAN 会把 mmsystem.h 排除在 windows.h 之外，
// 因此这里必须单独包含 mmsystem.h（waveOut* 系列声明）。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h> // waveOutOpen / waveOutPrepareHeader / waveOutWrite（需链接 winmm.lib）
#endif

namespace slime {

#ifdef _WIN32

namespace {

// ---- 音频格式：16 位单声道 44100 Hz（waveOut 最通用的格式）----
constexpr int kSampleRate = 44100;
constexpr int kChannels = 1;
constexpr int kBitsPerSample = 16;
constexpr int kMsPerSec = 1000;

constexpr double kPi = 3.14159265358979323846;

// 满音量（100%）时的峰值振幅，占 16 位满量程的比例（留足余量，避免削波）。
constexpr double kMaxAmplitude = 0.35;

// 三音上行和弦 C6-E6-G6，每音 100 ms，总时长 300 ms（任务书 4.2 要求 250~400 ms）。
struct Note {
    double freq; // 频率（Hz）
    int ms;      // 持续时长（毫秒）
};
constexpr Note kNotes[] = {
    {1046.502, 100}, // C6
    {1318.510, 100}, // E6
    {1567.982, 100}, // G6
};
constexpr int kTotalMs = 300;

constexpr int kAttackMs = 5;    // 每个音符的淡入，避免音符起点的「咔」声
constexpr int kReleaseMs = 20;  // 每个音符的淡出，避免音符之间的「咔」声
constexpr int kFadeOutMs = 120; // 整段末尾的线性淡出，保证结尾干净收束

// 浮点采样 → 16 位 PCM（四舍五入 + 夹取，防止整数溢出）
short toPcmSample(double value)
{
    if (value > 32767.0) value = 32767.0;
    if (value < -32768.0) value = -32768.0;
    return static_cast<short>(value >= 0.0 ? value + 0.5 : value - 0.5);
}

// 生成提示音 PCM 缓冲（16 位单声道，长度固定 = kTotalMs）。
// volumePercent 会被夹到 0..100，只影响振幅、不影响时长。
// 生成量固定 13230 个采样点（实测约 0.25 ms，小于 1 ms），可在 GUI 线程直接调用。
std::vector<short> buildChime(int volumePercent)
{
    int volume = volumePercent;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    const int total = kSampleRate * kTotalMs / kMsPerSec;
    std::vector<short> pcm(static_cast<std::size_t>(total), 0);
    if (volume == 0) return pcm; // 静音：长度保持一致，内容全为 0

    const double peak = kMaxAmplitude * (static_cast<double>(volume) / 100.0) * 32767.0;
    const int attack = kSampleRate * kAttackMs / kMsPerSec;
    const int release = kSampleRate * kReleaseMs / kMsPerSec;
    const int fadeStart = total - kSampleRate * kFadeOutMs / kMsPerSec;

    int offset = 0;
    for (const Note& note : kNotes) {
        const int len = kSampleRate * note.ms / kMsPerSec;
        for (int i = 0; i < len && offset < total; ++i, ++offset) {
            double env = 1.0;
            if (attack > 0 && i < attack) env *= static_cast<double>(i) / attack;
            const int tail = len - 1 - i;
            if (release > 0 && tail < release) env *= static_cast<double>(tail) / release;
            if (offset >= fadeStart) {
                env *= static_cast<double>(total - 1 - offset) / (total - fadeStart);
            }
            if (env > 1.0) env = 1.0;
            if (env < 0.0) env = 0.0;

            const double phase = 2.0 * kPi * note.freq * static_cast<double>(i) / kSampleRate;
            pcm[static_cast<std::size_t>(offset)] = toPcmSample(peak * env * std::sin(phase));
        }
        if (offset >= total) break;
    }
    return pcm;
}

// 一次播放所需的全部状态。
// 关键点：waveOutWrite 之后设备仍会异步读取 pcm / hdr，因此这两者必须在
// playCompletionChime 返回之后继续存活，直到下一次播放或进程退出才释放。
struct ActiveVoice {
    HWAVEOUT dev = nullptr; // 设备句柄（非空表示已 waveOutOpen 成功）
    WAVEHDR hdr{};          // 缓冲头（Prepare / Write 使用；仅 prepared 时可 Unprepare）
    std::vector<short> pcm; // PCM 数据（设备读取期间不可释放、不可重分配）
    bool prepared = false;  // waveOutPrepareHeader 已成功
    bool inUse = false;     // waveOutWrite 已成功（设备可能正在读取）
};

// 安全释放：停止播放 → 取消准备 → 关闭设备 → 释放缓冲。幂等，可重复调用。
// 每个 Win32 调用的返回值都会被检查并汇总：全部成功返回 true；
// 任一步失败（设备被拔出等极端情况）返回 false，但绝不阻塞调用线程、绝不抛异常。
bool releaseVoice(ActiveVoice& v) noexcept
{
    bool ok = true;
    if (v.dev != nullptr) {
        if (v.prepared) {
            // inUse 表示缓冲已经 waveOutWrite 过，设备可能仍在读取它；
            // 此时必须先 Reset（Reset 会把未播完的缓冲标记为 WHDR_DONE 并归还），
            // 否则随后的 Unprepare / Close 会返回 WAVERR_STILLPLAYING。
            if (v.inUse && waveOutReset(v.dev) != MMSYSERR_NOERROR) {
                ok = false; // 失败也继续走后续释放步骤
            }
            if (waveOutUnprepareHeader(v.dev, &v.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                ok = false;
            }
            v.prepared = false;
        }
        if (waveOutClose(v.dev) != MMSYSERR_NOERROR) {
            // 极少数情况下设备仍在播放：再 Reset 一次并重试关闭，避免设备句柄泄漏；
            // 若仍失败则只能放弃该句柄（不阻塞 GUI 线程、不抛异常）。
            ok = false;
            if (waveOutReset(v.dev) != MMSYSERR_NOERROR) ok = false;
            if (waveOutClose(v.dev) != MMSYSERR_NOERROR) ok = false;
        }
    }
    v.dev = nullptr;
    v.hdr = WAVEHDR{};
    v.prepared = false;
    v.inUse = false;
    v.pcm.clear();
    v.pcm.shrink_to_fit(); // 真正归还内存（提示音缓冲很小，代价可忽略）
    return ok;
}

// 播放状态持有者：以「函数内静态」创建，既避开动态初始化顺序问题，
// 又能在进程退出时由析构函数关闭设备、释放缓冲，保证干净退出。
struct VoiceHolder {
    ActiveVoice voice;

    ~VoiceHolder() noexcept { (void)releaseVoice(voice); } // 进程退出清理
};

ActiveVoice& voice()
{
    static VoiceHolder holder; // C++11 起函数内静态初始化线程安全
    return holder.voice;
}

// available() 的返回值：是否成功初始化过音频设备（失败后置 false）
std::atomic<bool> g_available{false};
// 提示音总开关（setEnabled()）
std::atomic<bool> g_enabled{true};

} // namespace

void SoundPlayer::playCompletionChime(int volumePercent)
{
    // 只允许 GUI 线程调用：本模块不做线程同步（见头文件说明）。
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    if (volumePercent < 0) return; // < 0 视为静音

    int volume = volumePercent;
    if (volume > 100) volume = 100;
    if (volume == 0) return; // 静音：不必占用音频设备

    // voice() 只构造 POD 与空 vector（不分配内存、不抛异常），因此放在 try 之外。
    ActiveVoice& v = voice();
    // 上一次提示音可能仍在播放（设备正在读 v.pcm）：先安全清理，再替换。
    // 清理失败（设备被拔出等）不阻止本次尝试，available() 反映最新一次结果。
    (void)releaseVoice(v);

    try {
        v.pcm = buildChime(volume);
        if (v.pcm.empty()) return;

        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = static_cast<WORD>(kChannels);
        fmt.nSamplesPerSec = static_cast<DWORD>(kSampleRate);
        fmt.wBitsPerSample = static_cast<WORD>(kBitsPerSample);
        fmt.nBlockAlign = static_cast<WORD>(kChannels * kBitsPerSample / 8);
        fmt.nAvgBytesPerSec = static_cast<DWORD>(kSampleRate * kChannels * kBitsPerSample / 8);
        fmt.cbSize = 0;

        HWAVEOUT dev = nullptr;
        if (waveOutOpen(&dev, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            // 没有可用音频设备（或格式不被支持）：静默降级，不影响搜索功能。
            (void)releaseVoice(v);
            g_available.store(false, std::memory_order_relaxed);
            return;
        }
        v.dev = dev;
        g_available.store(true, std::memory_order_relaxed);

        v.hdr = WAVEHDR{};
        v.hdr.lpData = reinterpret_cast<LPSTR>(v.pcm.data());
        v.hdr.dwBufferLength = static_cast<DWORD>(v.pcm.size() * sizeof(short));
        if (waveOutPrepareHeader(v.dev, &v.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            (void)releaseVoice(v); // 释放刚打开的设备与缓冲
            g_available.store(false, std::memory_order_relaxed);
            return;
        }
        v.prepared = true;

        // 异步提交：立即返回；设备继续读取 v.pcm / v.hdr（由 voice() 持有到下次播放）。
        if (waveOutWrite(v.dev, &v.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            (void)releaseVoice(v);
            g_available.store(false, std::memory_order_relaxed);
            return;
        }
        v.inUse = true;
    } catch (...) {
        // 例如缓冲分配失败：异常绝不允许逃出（调用方在 GUI 线程且不处理异常）。
        (void)releaseVoice(v);
        g_available.store(false, std::memory_order_relaxed);
    }
}

void SoundPlayer::setEnabled(bool on)
{
    g_enabled.store(on, std::memory_order_relaxed);
    if (!on) {
        // 关闭提示音时必须同时停止并释放正在播放的声音（设备 + 缓冲）。
        (void)releaseVoice(voice());
    }
}

bool SoundPlayer::enabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

bool SoundPlayer::available()
{
    return g_available.load(std::memory_order_relaxed);
}

#else // 非 Windows：空实现（保证本文件在没有 winmm 的平台上也能编译）

void SoundPlayer::playCompletionChime(int volumePercent)
{
    (void)volumePercent; // 平台无关：不播放、不阻塞、不抛异常
}

void SoundPlayer::setEnabled(bool on)
{
    (void)on;
}

bool SoundPlayer::enabled()
{
    return false;
}

bool SoundPlayer::available()
{
    return false;
}

#endif

} // namespace slime

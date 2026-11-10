// LogBus.cpp - 日志总线实现（v3.3.4 任务书 2.3）
//
// 设计要点：
//   1. 默认关闭：mode 初值为 LogMode::Off，log() 在读锁内立刻返回——不构造 LogEntry、
//      不分配内存、不写盘；Off 状态下也不会创建或打开任何文件。
//   2. 级别过滤：简要 = INFO 及以上；详细 = 全部（含 DEBUG / TRACE）。
//   3. 环形缓冲：一块定长 vector 当环用，最多 kMaxEntries 条；写满后覆盖最旧的一条
//      并累加 dropped 计数（供界面提示「已丢弃 N 条」）。
//   4. 文件：<logDir>/performance-YYYY-MM-DD.log，追加写（文本模式，Windows 下为 CRLF）；
//      本地日期变化时自动关闭旧文件并重新打开；写盘走流缓冲，最多约 1 秒 flush 一次，
//      setMode(Off) 时立即 flush 并关闭。
//   5. 线程安全：全部状态由一把 mutex 保护；监听器在锁外调用，且仅在条目真的被记录时调用。
//   6. 选 std::ofstream 而不是 std::fopen 的原因：
//      · std::filesystem::path 重载在 MSVC 下用宽字符路径打开，工程路径含中文也不会失败；
//      · 共享方式与既有 Logger（同样用 ofstream）一致——运行期间允许其它程序读取日志文件，
//        而 _wfopen_s(..., "a") 默认排斥读者，会导致记事本打不开正在写的日志。
#include "utils/LogBus.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace slime {

namespace {

using Clock = std::chrono::steady_clock;

// 相对时间零点：本翻译单元加载时取一次，近似进程启动时刻
const Clock::time_point kProcessStart = Clock::now();

// 本地日期 / 本地时间戳（均为 ASCII，避免任何编码问题）
struct Stamp {
    std::string date; // 2026-09-20
    std::string line; // 2026-09-20 12:34:56.789
};

Stamp makeStamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char date[16] = {0};
    char line[48] = {0};
    std::snprintf(date, sizeof(date), "%04d-%02d-%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    std::snprintf(line, sizeof(line), "%s %02d:%02d:%02d.%03d",
                  date, tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                  static_cast<int>(ms.count()));
    Stamp st;
    st.date = date;
    st.line = line;
    return st;
}

std::string fileNameFor(const std::string& date)
{
    return "performance-" + date + ".log";
}

// 全局状态（函数内静态，首次使用即构造，避免静态初始化顺序问题）
struct State {
    std::mutex mtx;

    LogMode mode = LogMode::Off;        // 默认关闭
    std::filesystem::path dir;          // 日志目录（logs/）

    // 环形缓冲
    std::vector<LogEntry> ring;         // 惰性分配到 kMaxEntries 个槽位
    size_t head = 0;                    // 下一个写入位置
    size_t count = 0;                   // 当前有效条目数
    int64_t dropped = 0;                // 因上限被覆盖丢弃的条目数

    // 文件（追加写、带缓冲；MSVC 下与既有 Logger 一致：允许其它程序同时读取）
    std::ofstream out;
    std::string openDate;               // 已打开文件对应的日期（空 = 未打开）
    std::string attemptedDate;          // 打开失败过的日期，避免每条日志都重试
    Clock::time_point lastFlush{};

    // 监听器
    LogBus::Listener listener;

    Clock::time_point start = kProcessStart; // 条目 t 的时间零点
};

State& state()
{
    static State s;
    return s;
}

bool levelPasses(LogMode m, LogLevel lv)
{
    if (m == LogMode::Detailed) return true;
    if (m == LogMode::Brief) return static_cast<int>(lv) >= static_cast<int>(LogLevel::Info);
    return false; // Off
}

// 关闭当前文件（调用方持锁）；关闭前先 flush
void closeFileLocked(State& s)
{
    if (s.out.is_open()) {
        s.out.flush();
        s.out.close();
    }
    s.openDate.clear();
}

// 按需打开当日文件（调用方持锁）；不需要或打不开时返回 nullptr
std::ostream* fileForLocked(State& s, const std::string& date)
{
    if (s.dir.empty()) return nullptr;                 // 未 init：只进内存，不写盘
    if (s.out.is_open() && s.openDate == date) return &s.out; // 已是当日文件
    if (s.out.is_open()) closeFileLocked(s);           // 跨日期：关闭旧文件
    if (s.attemptedDate == date) return nullptr;       // 本日已失败过，不反复重试

    std::error_code ec;
    std::filesystem::create_directories(s.dir, ec);
    const std::filesystem::path p = s.dir / fileNameFor(date);

    s.out.clear();                                      // 清掉上次失败留下的状态位
    s.out.open(p, std::ios::out | std::ios::app);       // path 重载：MSVC 下正确处理中文路径
    if (!s.out.is_open()) {
        s.attemptedDate = date;
        return nullptr;
    }
    s.openDate = date;
    s.attemptedDate.clear();
    s.lastFlush = Clock::now();
    return &s.out;
}

} // namespace

void LogBus::init(const std::filesystem::path& logDir)
{
    std::lock_guard<std::mutex> lock(state().mtx);
    State& s = state();
    if (s.dir != logDir) {
        closeFileLocked(s);   // 目录变了：换目录写（下一次记录时重新打开）
        s.dir = logDir;
    }
    s.attemptedDate.clear();  // 允许重新尝试打开（例如目录刚被创建出来）
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
}

void LogBus::setMode(LogMode m)
{
    std::lock_guard<std::mutex> lock(state().mtx);
    State& s = state();
    s.mode = m;
    if (m == LogMode::Off) {
        closeFileLocked(s);   // 停止写文件（含 flush）
        s.attemptedDate.clear();
    }
    // 说明：这里不调用监听器——监听器只在「条目真的被记录」时触发（见 log()）。
}

LogMode LogBus::mode()
{
    std::lock_guard<std::mutex> lock(state().mtx);
    return state().mode;
}

const char* LogBus::modeName(LogMode m)
{
    switch (m) {
    case LogMode::Brief: return "简要";
    case LogMode::Detailed: return "详细";
    default: return "关闭";
    }
}

bool LogBus::enabled()
{
    return mode() != LogMode::Off;
}

void LogBus::log(LogLevel level, const std::string& message)
{
    Listener listener;
    {
        std::lock_guard<std::mutex> lock(state().mtx);
        State& s = state();
        if (!levelPasses(s.mode, level)) return; // Off / 级别不满足：直接返回（不分配、不写盘）

        const Clock::time_point now = Clock::now();

        LogEntry e;
        e.t = std::chrono::duration<double>(now - s.start).count();
        e.level = level;
        e.text = message;

        // 环形缓冲：写满后覆盖最旧的一条
        if (s.ring.empty()) s.ring.resize(kMaxEntries);
        if (s.count == s.ring.size()) ++s.dropped; // 本次将覆盖最旧条目
        s.ring[s.head] = std::move(e);
        s.head = (s.head + 1) % s.ring.size();
        if (s.count < s.ring.size()) ++s.count;

        // 写文件（简要 / 详细都要落盘）
        const Stamp st = makeStamp();
        if (std::ostream* os = fileForLocked(s, st.date)) {
            (*os) << '[' << st.line << "][" << levelName(level) << "] " << message << '\n';
            if (now - s.lastFlush >= std::chrono::seconds(1)) { // 最多约 1 秒 flush 一次
                os->flush();
                s.lastFlush = now;
            }
        }

        listener = s.listener;
    }
    // 锁外调用；只有条目真的被记录才会走到这里
    if (listener) listener();
}

void LogBus::trace(const std::string& m) { log(LogLevel::Trace, m); }
void LogBus::debug(const std::string& m) { log(LogLevel::Debug, m); }
void LogBus::info(const std::string& m) { log(LogLevel::Info, m); }
void LogBus::warning(const std::string& m) { log(LogLevel::Warning, m); }
void LogBus::error(const std::string& m) { log(LogLevel::Error, m); }

std::vector<LogEntry> LogBus::snapshot()
{
    std::lock_guard<std::mutex> lock(state().mtx);
    State& s = state();
    std::vector<LogEntry> out;
    out.reserve(s.count);
    if (s.count == 0) return out;

    const size_t cap = s.ring.size();
    const size_t first = (s.count == cap) ? s.head : 0; // 满环时 head 指向最旧条目
    for (size_t i = 0; i < s.count; ++i) {
        out.push_back(s.ring[(first + i) % cap]);
    }
    return out;
}

void LogBus::clear()
{
    std::lock_guard<std::mutex> lock(state().mtx);
    State& s = state();
    s.count = 0;
    s.head = 0;
    s.dropped = 0; // 缓冲清空，丢弃计数一并归零
}

int64_t LogBus::droppedCount()
{
    std::lock_guard<std::mutex> lock(state().mtx);
    return state().dropped;
}

void LogBus::setListener(Listener l)
{
    std::lock_guard<std::mutex> lock(state().mtx);
    state().listener = std::move(l);
}

const char* LogBus::levelName(LogLevel lv)
{
    switch (lv) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

std::string LogBus::currentFile()
{
    std::lock_guard<std::mutex> lock(state().mtx);
    State& s = state();
    if (s.dir.empty()) return {}; // 未初始化
    const std::string date = makeStamp().date;
    return (s.dir / fileNameFor(date)).generic_string(); // 统一用 '/'，便于界面展示
}

} // namespace slime

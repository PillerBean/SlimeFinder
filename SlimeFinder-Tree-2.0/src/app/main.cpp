// main.cpp - 程序入口
//
// 史莱姆区块查找器（主程序入口）
// - 静态初始化：崩溃转储、日志
// - 启动主窗口
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSize>

#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "gui/mainwindow/MainWindow.h"
#include "i18n/UiText.h"
#include "utils/CrashDump.h"
#include "utils/IncidentLog.h"
#include "utils/Logger.h"

namespace {

#ifdef _WIN32
// 诊断输出统一 UTF-8：崩溃报告此前用 GetModuleFileNameA（ANSI/GBK），模块路径与
// 其它 UTF-8 日志混在一起会变成乱码（证据日志可读性由污染审计发现并修复）。
std::string modulePathUtf8(HMODULE mod)
{
    wchar_t wide[MAX_PATH * 2] = L"?";
    const DWORD len = GetModuleFileNameW(mod, wide, static_cast<DWORD>(std::size(wide)));
    if (len == 0) return "?";
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(len),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return "?";
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(len), out.data(), bytes, nullptr, nullptr);
    return out;
}
#endif

// 诊断：terminate/无效参数时打印调用栈（SLIME_TERM=1 启用）
void dumpStackToStderr(const char* why)
{
#ifdef _WIN32
    std::fprintf(stderr, "[fatal] %s\n", why);
    void* frames[24] = {nullptr};
    const USHORT n = CaptureStackBackTrace(1, 24, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        HMODULE m = nullptr;
        std::string name = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &m) && m) {
            name = modulePathUtf8(m);
        }
        std::fprintf(stderr, "  #%u %p %s\n", i, frames[i], name.c_str());
    }
    std::fflush(stderr);
#else
    (void)why;
#endif
}

void onTerminate()
{
    dumpStackToStderr("terminate (unhandled exception)");
    // v3.3.7（P1-6）：terminate 也是「崩溃」，必须落 dump + 关键信息日志
    slime::IncidentLog::markIncident("std::terminate（未捕获 C++ 异常）");
    slime::CrashDump::writeDumpNow("std::terminate", nullptr);
    std::abort();
}

void onInvalidParam(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
{
    dumpStackToStderr("invalid_parameter");
    // v3.3.7：CRT 非法参数默认走 fast-fail（无 dump）——这里先落盘再终止
    slime::IncidentLog::markIncident("CRT invalid_parameter");
    slime::CrashDump::writeDumpNow("invalid_parameter", nullptr);
    std::abort();
}

// v3.3.7（P0-4/P1-6）：内存分配失败（例如 128MB 位图申请不到）时先落盘再终止，
// 避免「进程直接消失、什么线索都没有」。
void onBadAlloc()
{
    std::fprintf(stderr, "[fatal] std::bad_alloc（内存分配失败）\n");
    slime::IncidentLog::markIncident("std::bad_alloc（内存分配失败）");
    slime::CrashDump::writeDumpNow("std::bad_alloc", nullptr);
    std::abort();
}

// v3.3.7：读取 config.json 里的布尔设置（只需一个键，避免为此引入完整解析器）。
// 查找 "<key>" 之后第一个 true/false；找不到返回 def。
bool configBoolEarly(const std::filesystem::path& cfgPath, const char* key, bool def)
{
    std::ifstream in(cfgPath);
    if (!in) return def;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = text.find(needle);
    if (pos == std::string::npos) return def;
    const size_t colon = text.find(':', pos + needle.size());
    if (colon == std::string::npos) return def;
    const size_t t = text.find("true", colon);
    const size_t f = text.find("false", colon);
    if (t == std::string::npos && f == std::string::npos) return def;
    if (t != std::string::npos && (f == std::string::npos || t < f)) return true;
    return false;
}

} // namespace

namespace {

// 崩溃现场打印（VEH，常驻）：定位闪退模块与调用栈（仅真正异常时输出到 stderr）
#ifdef _WIN32
LONG WINAPI crashVeH(PEXCEPTION_POINTERS ep)
{
    // v3.5.12：良性首次机会异常（OutputDebugString 的 DBG_PRINTEXCEPTION_C/W）不是崩溃。
    // 以前这里也打印 [CRASH] 并做栈回溯，自检日志里常出现「假崩溃」，高并发（OpenMP）阶段
    // 还会拖慢甚至偶发卡死；直接放过，交给调试器处理。
    {
        const DWORD code = ep->ExceptionRecord->ExceptionCode;
        // 0x40010006/0x4001000A/0x4001000B：OutputDebugString 系列（调试输出）
        // 0xE06D7363：MSVC C++ 异常（正常的 throw/catch）
        // 0x406D1388：SetThreadName 惯例异常
        if (code == 0x40010006UL || code == 0x4001000AUL || code == 0x4001000BUL ||
            code == 0xE06D7363UL || code == 0x406D1388UL) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    std::string name = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(addr), &mod) && mod) {
        name = modulePathUtf8(mod);
    }
    std::fprintf(stderr, "[CRASH] 0x%08X at %p in %s\n", (unsigned)ep->ExceptionRecord->ExceptionCode,
                 addr, name.c_str());
#ifdef _M_X64
    std::fprintf(stderr, "[CRASH] regs rax=%p rcx=%p rdx=%p r8=%p r9=%p rsp=%p rbp=%p\n",
                 (void*)ep->ContextRecord->Rax, (void*)ep->ContextRecord->Rcx,
                 (void*)ep->ContextRecord->Rdx, (void*)ep->ContextRecord->R8,
                 (void*)ep->ContextRecord->R9, (void*)ep->ContextRecord->Rsp,
                 (void*)ep->ContextRecord->Rbp);
#endif
    void* frames[24] = {nullptr};
    const USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        HMODULE fm = nullptr;
        std::string fname = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &fm) && fm) {
            fname = modulePathUtf8(fm);
        }
        std::fprintf(stderr, "  #%u %p %s\n", i, frames[i], fname.c_str());
    }
    std::fflush(stderr);
    // v3.3.7（P0-2/P1-6）：对「会绕过 UEF/terminate」的致命异常，在首次机会阶段主动写一份
    // dump（内部只写一次）；否则用户只会看到窗口消失、既无 dump 也无日志。
    switch (ep->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case 0xC0000374 /* STATUS_HEAP_CORRUPTION */:
    case 0xC0000409 /* STATUS_STACK_BUFFER_OVERRUN / __fastfail */:
        slime::CrashDump::writeDumpNow("first-chance 致命异常", ep);
        slime::IncidentLog::markIncident("首次机会致命异常 0x" + [&]() {
            char b[16];
            std::snprintf(b, sizeof(b), "%08X", static_cast<unsigned>(ep->ExceptionRecord->ExceptionCode));
            return std::string(b);
        }());
        break;
    default:
        break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

} // namespace

namespace {
// v3.3.3（任务书 2.1）：版本号单一来源 —— 界面显示名、日志文件头、窗口标题均取此常量。
// 同时由 src/app.rc 写入 exe 的 VERSIONINFO（属性 → 详细信息 → 文件版本 3.3.3）。
constexpr const char* kVersion = "v3.12.0";
} // namespace

namespace {

// v3.3.9（任务书 5.1）：程序图标改为**资源文件**（素材：模板：原始文件/程序图标.png，
// 透明底史莱姆方块；assets/appicon.png 由 assets/appicon.qrc 编进 exe，exe 文件图标见 src/app.rc）。
// 资源缺失时回退到下面「运行时绘制」的旧图标，保证任何构建方式下都有图标。
QIcon makeAppIcon()
{
    const QIcon fromRes(QStringLiteral(":/icons/appicon.png"));
    if (!fromRes.isNull()) return fromRes;
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // 身体
    p.setBrush(QColor(0x6B, 0xC9, 0x5A));
    p.setPen(QPen(QColor(0x3E, 0x8E, 0x36), 2));
    p.drawRoundedRect(QRectF(6, 12, 52, 44), 10, 10);
    // 眼睛
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRectF(18, 24, 10, 12));
    p.drawEllipse(QRectF(36, 24, 10, 12));
    p.setBrush(QColor(0x22, 0x22, 0x22));
    p.drawEllipse(QRectF(21, 27, 5, 7));
    p.drawEllipse(QRectF(39, 27, 5, 7));
    // 嘴巴
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x22, 0x22, 0x22), 1.6));
    p.drawArc(QRectF(24, 36, 16, 10), 0, 180 * 16);
    p.end();
    return QIcon(pm);
}

} // namespace

int main(int argc, char* argv[])
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, crashVeH);
#endif
    // v3.3.7.2（F-1）：offscreen / minimal 平台走 FreeType 字体引擎，**必须**有字体目录，
    // 否则任何文字都会被渲染成方框（□）——自检截图自 v3.3.4 起全部不可读即由此而来。
    // 这里在创建 QApplication 之前补上系统字体目录（用户显式设置时不覆盖）。
    {
        const QByteArray plat = qgetenv("QT_QPA_PLATFORM");
        const bool freetypePlatform = plat.startsWith("offscreen") || plat.startsWith("minimal");
        if (freetypePlatform && qgetenv("QT_QPA_FONTDIR").isEmpty()) {
            QString fonts = qEnvironmentVariable("WINDIR");
            if (fonts.isEmpty()) fonts = QStringLiteral("C:\\Windows");
            fonts += QStringLiteral("\\Fonts");
            if (QDir(fonts).exists()) {
                qputenv("QT_QPA_FONTDIR", QDir::toNativeSeparators(fonts).toUtf8());
                std::fprintf(stderr, "[启动] offscreen/minimal 平台：已设置 QT_QPA_FONTDIR=%s（避免截图豆腐块）\n",
                             QDir::toNativeSeparators(fonts).toUtf8().constData());
            }
        }
    }
    QApplication app(argc, argv);
    slime::ui::initTextCatalog();
    QApplication::setApplicationName(QStringLiteral("史莱姆区块查找器"));
    QApplication::setApplicationVersion(QStringLiteral("3.12.0")); // v3.4.0
    QApplication::setApplicationDisplayName(QStringLiteral("史莱姆区块查找器 %1").arg(QLatin1String(kVersion)));
    QApplication::setOrganizationName(QStringLiteral("SlimeFinder"));
    app.setWindowIcon(makeAppIcon());

    // v3.3.7（P1-6）：崩溃诊断独立于普通日志，**始终安装**：
    //   * std::terminate → 写 dump（CrashDump 内部已 set_terminate，后装者生效，故顺序放在这里）
    //   * CRT 非法参数 / 内存分配失败 → 也先落盘再终止（默认行为是 fast-fail，什么都不留）
    // SLIME_TERM=1 只影响「是否打印调用栈」的额外诊断（挂到 terminate 上会覆盖 CrashDump 的处理，
    // 因此只在显式开启时安装，且仍会写 dump）。
    if (std::getenv("SLIME_TERM")) {
        std::set_terminate(onTerminate);
    }
#ifdef _WIN32
    _set_invalid_parameter_handler(onInvalidParam);
#endif
    std::set_new_handler(onBadAlloc);

    // 工作目录/程序目录
    const QString appDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const std::filesystem::path base(appDir.toStdWString());

    // v3.3.7（P1-6）：按设置决定是否生成转储（默认开；真正的接线在 MainWindow 应用设置时同步）
    const bool crashDumpOn = configBoolEarly(base / "config.json", "crashDump", true);

    slime::IncidentLog::init(base / "logs", kVersion); // 关键信息日志（常开，先于一切）
    slime::IncidentLog::note(std::string("崩溃转储：") + (crashDumpOn ? "开启（默认）" : "已关闭（设置）"));
    slime::CrashDump::install(base / "crashes");
    slime::CrashDump::setEnabled(crashDumpOn);
    slime::Logger::init(base / "logs");

    slime::Logger::info("========== 史莱姆区块查找器 " + std::string(kVersion) + " 启动 ==========");
    slime::Logger::info("Qt 版本: " + std::string(QT_VERSION_STR) + " | 逻辑核心: " + std::to_string(std::thread::hardware_concurrency()));

    // v3.3.7（P1-6）：崩溃诊断自检钩子（默认关闭，仅用于验证「崩溃必留 dump + 关键信息日志」）。
    //   SLIME_CRASH_TEST=av         → 触发访问违例（走 VEH 首次机会 dump）
    //   SLIME_CRASH_TEST=terminate  → 抛出未捕获 C++ 异常（走 std::terminate 处理）
    //   SLIME_CRASH_TEST=invalid    → 触发 CRT 非法参数（默认 fast-fail 路径）
    //   SLIME_CRASH_TEST=badalloc   → 模拟内存分配失败（std::set_new_handler 路径）
    if (const char* crashTest = std::getenv("SLIME_CRASH_TEST")) {
        slime::IncidentLog::note(std::string("崩溃自检钩子：SLIME_CRASH_TEST=") + crashTest);
        const std::string mode(crashTest);
        if (mode == "av") {
            volatile int* p = reinterpret_cast<volatile int*>(0x1);
            *p = 42; // 访问违例
        } else if (mode == "terminate") {
            throw std::runtime_error("SLIME_CRASH_TEST=terminate"); // 无 catch → terminate
        } else if (mode == "invalid") {
            std::fprintf(nullptr, "x"); // CRT 非法参数（空 FILE*）
        } else if (mode == "badalloc") {
            // 真实场景由 CRT 在 operator new 失败时回调 new_handler；这里直接调用同一入口，
            // 避免测试时把机器内存吃满。
            std::set_new_handler(onBadAlloc);
            onBadAlloc();
        }
    }

    slime::MainWindow window;
    std::fprintf(stderr, "[boot] after MainWindow construct\n");
    // v3.5.20：两种全屏（无边框全屏 / 窗口最大化），由设置决定；F11 切换
    std::fprintf(stderr, "[boot] before showWithMode\n");
    window.showWithMode();
    std::fprintf(stderr, "[boot] after showWithMode\n");

    // 无界面自检模式：SLIME_SELFTEST=1 ./SlimeFinder.exe
    const char* st = qgetenv("SLIME_SELFTEST").constData();
    if (st && std::string(st) == "1") {
        const int rc = window.runSelfTest();
        slime::IncidentLog::sessionEndClean();
        return rc;
    }
    const int rc = app.exec();
    slime::IncidentLog::sessionEndClean(); // 正常退出：清掉会话标记
    return rc;
}



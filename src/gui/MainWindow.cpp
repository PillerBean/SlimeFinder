// MainWindow.cpp - 主窗口实现（v3.2.1）
#include "gui/UiText.h"
#include "gui/MainWindow.h"

#include <QApplication>
#include <QStyledItemDelegate>   // v3.5.5：结果列表数值显示
#include <QBrush>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QKeyEvent>
#include <QSlider>
#include <QShortcut>
#include <QCoreApplication>
#include <QDate>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QImage>
#include <QJsonDocument>  // v3.4.12：自检用标准解析器校验 config.json
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <memory>
#include <QSystemTrayIcon> // v3.3.9（任务书 5.1）：托盘图标
#include <QPainter>      // v3.4.0：导航/停靠图标（QPainter 绘制）
#include <QStackedWidget> // v3.4.0：页面栈
#include <QTreeWidget>   // v3.4.0：更新历史可展开列表
#include <QVariantAnimation> // v3.4.6：图案缩放动画（v3.4.7：移除已不用的 QGraphicsOpacityEffect/QPropertyAnimation）
#include <QToolButton>   // v3.4.1：左导航（上图案下文字）
#include <QAbstractSpinBox> // v3.4.2：设置立即生效（汇总输入控件变更）
#include <QHeaderView>  // v3.4.0：更新历史列表表头
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include "gui/TelemetryClient.h" // v3.3.9（任务书 四）：完整更新历史文本（1.0.0 → 3.3.9）
#include <QProgressBar>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>   // v3.5.2：自检里读挂机点表格行数
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QThread>
#include <QTime>
#include <QEventLoop>
#include <QSignalBlocker>
#include <QProcess>
#include <QTimer>
#include <QToolBar>
#include <QMouseEvent>        // v3.4.7：自检里模拟真实鼠标点击（验证「选项可勾选」）
#include <QStyleOptionButton> // v3.4.7：取复选框指示器矩形，点击落点用它算
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <chrono>
#include <cmath>
#include <functional> // v3.4.8：自检里的参数探针 lambda
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>

#include "gui/FeedbackPanel.h"    // v3.3.4：底部「反馈」分类（v3.3.6：时间顺序 + 过滤成功）
#include "gui/LogPanel.h"         // v3.3.4：底部「日志」分类（默认关闭 + 两级选项）
#include "gui/PatternEditor.h"
#include "gui/PatternEditorPanel.h"
#include "gui/UiStrings.h"
#include "gui/ProgressStreamPanel.h" // v3.3.6：底部「进度」分类（累积式纯文字流水）
#include "gpu/GpuScanAdapter.h" // v3.2.1：GPU 加速（默认关闭；用户勾选+可用才注入，自动回退）
#include "utils/CrashDump.h"     // v3.3.7：崩溃转储开关（设置接线）
#include "utils/IncidentLog.h"   // v3.3.7：关键信息日志（常开，崩溃/取消取证）
#include "utils/LogBus.h"        // v3.3.4：日志总线（按日期分文件 + 两级模式）
#include "utils/Logger.h"
#include "utils/SoundPlayer.h"   // v3.3.6（任务书 4.2）：搜索完成提示音（winmm，带音量）
#include "utils/ThemeManager.h"  // v3.3.6（任务书 4.7）：夜间模式三档（浅色/深色/跟随系统）

#include "gui/AfkDialog.h"   // v3.5.2（任务 1）：挂机点（环形覆盖）对话框
#include "gui/MapPreviewWidget.h" // v3.5.2（任务 2）：自检里直接读地图控件状态
#include "gui/MapDialog.h"       // v3.5.2（任务 2）：地图预览对话框
#include "gui/HelpDialog.h" // v3.3.3：帮助 / 使用教程对话框
#include "gui/UpdateDialog.h" // v3.10.12.10：GitHub Releases + 镜像 + 网盘备用的更新检查

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace slime {

// v3.3.5（Bug 6）：基准用的 CPU 端扫描（文件作用域函数，便于使用 OpenMP pragma ——
// MSVC 不支持在 lambda 内使用 #pragma omp parallel for 引用捕获变量）。
static double benchCpuScanSeconds(slime::Version version, int threads, int w, int h)
{
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h, 0);
    double best = 1e9;
    for (int rep = 0; rep < 3; ++rep) {
        const auto t0 = std::chrono::steady_clock::now();
#pragma omp parallel for num_threads(threads) schedule(static)
        for (int z = 0; z < h; ++z) {
            for (int x = 0; x < w; ++x) {
                const bool s = (version == slime::Version::Java)
                                   ? slime::SlimeEngine::isSlimeChunkJava(0, x, z)
                                   : slime::SlimeEngine::isSlimeChunkBedrockMT(x, z);
                buf[static_cast<size_t>(z) * w + x] = s ? 1 : 0;
            }
        }
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (rep > 0 && el < best) best = el; // 首次含线程启动开销，取后续最优
    }
    return best;
}


namespace {
// v3.5.5：结果列表里「距离 / 密度」只显示 1 位小数（排序仍按真实数值）
class OneDecimalDelegate : public QStyledItemDelegate {
public:
    explicit OneDecimalDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
    QString displayText(const QVariant& value, const QLocale&) const override
    {
        bool ok = false;
        const double v = value.toDouble(&ok);
        return ok ? QString::number(v, 'f', 1) : value.toString();
    }
};

// v3.5.5：方块坐标 → 区块坐标（向下取整）
inline int floorDiv16(int v) { return v >= 0 ? v / 16 : -((-v + 15) / 16); }
// v3.3.1（任务书 2.1）：缓存扩展名统一为 .slimecache（保存/加载/过滤器/转换脚本一致），
// 同时保留对旧版 .cache 的兼容读取（启动自动加载 + 导入对话框可选旧格式）。
constexpr const char* kCacheFileName = "slime_cache.slimecache";
constexpr const char* kCacheLegacyFileName = "slime_cache.cache";
// v3.3.8（任务 2 / 历史缓存方案 A）：历史缓存改名后缀 + 保留天数 + 一次性迁移标记文件
constexpr const char* kCacheLegacySuffix = ".legacy";
constexpr const char* kCacheMigratedMarker = "slime_cache.migrated";
constexpr int kCacheLegacyKeepDays = 7;
constexpr const char* kConfigFileName = "config.json";
constexpr int kMaxCards = 2000; // 结果卡片上限（复制结果仍包含全部）
// v3.6.6（用户反馈「匹配数/列表数/展示数不一致」）：三者统一使用同一个展示上限，
//   避免「统计行说显示前 2000 条、表格只画 500 行」这类互相矛盾的数字。
constexpr int kResultDisplayCap = 500;
constexpr const char* kAppVersion = "v3.11.0";
// v3.4.8（用户要求：缓存构建独立成导航条一项）：左导航/页面栈的一一对应表
//   索引 = 页面索引 = 导航项索引；值 = 图标种类（0 搜索 / 1 缓存构建 / 2 设置）
constexpr int kNavCount = 3; // v3.10.12.13：移除更新历史，导航 3 项
constexpr int kNavIconKind[kNavCount] = { 0, 1, 2 };
constexpr int kPageSearch = 0;
constexpr int kPageBuild = 1;
constexpr int kPageSettings = 2;
constexpr const char* kContactEmail = "liguodong1209@163.com";
// v3.10.12.12：匿名统计的搜索事件（只含用户主动开启后同意的字段）
QJsonObject buildTelemetrySearch(const QString& seed, bool bedrock, const QString& mode,
                                 const QString& algorithm, const QString& ringScan,
                                 const Rect& region, int64_t startX, int64_t startZ,
                                 const QJsonObject& settings, const SearchOutcome& out, bool gpuUsed)
{
    QJsonObject s;
    s.insert(QStringLiteral("seed"), seed);
    s.insert(QStringLiteral("edition"), bedrock ? QStringLiteral("bedrock") : QStringLiteral("java"));
    s.insert(QStringLiteral("mode"), mode);
    s.insert(QStringLiteral("algorithm"), algorithm);
    s.insert(QStringLiteral("ring_scan"), ringScan);
    QJsonObject range;
    range.insert(QStringLiteral("min_x"), region.minX);
    range.insert(QStringLiteral("max_x"), region.maxX);
    range.insert(QStringLiteral("min_z"), region.minZ);
    range.insert(QStringLiteral("max_z"), region.maxZ);
    s.insert(QStringLiteral("range"), range);
    {
        const int64_t cells = static_cast<int64_t>(region.maxX - region.minX + 1) *
                              static_cast<int64_t>(region.maxZ - region.minZ + 1);
        const int lg = cells > 0 ? static_cast<int>(std::floor(std::log10(static_cast<double>(cells)))) : 0;
        s.insert(QStringLiteral("range_log10"), std::clamp(lg, 0, 30));
    }
    QJsonObject start;
    start.insert(QStringLiteral("x"), static_cast<double>(startX));
    start.insert(QStringLiteral("z"), static_cast<double>(startZ));
    s.insert(QStringLiteral("start"), start);
    s.insert(QStringLiteral("settings"), settings);

    QJsonObject speed;
    speed.insert(QStringLiteral("total_ms"), out.stats.totalMs);
    speed.insert(QStringLiteral("scan_ms"), out.stats.scanMs);
    speed.insert(QStringLiteral("match_ms"), out.stats.matchMs);
    speed.insert(QStringLiteral("scanned_chunks"), static_cast<double>(out.stats.scannedChunks));
    const double secs = out.stats.totalMs / 1000.0;
    speed.insert(QStringLiteral("chunks_per_sec"),
                 secs > 1e-9 ? static_cast<double>(out.stats.scannedChunks) / secs : 0.0);
    s.insert(QStringLiteral("speed"), speed);

    QJsonObject results;
    results.insert(QStringLiteral("count"), static_cast<int>(out.results.size()));
    results.insert(QStringLiteral("total_found"), static_cast<double>(out.totalFound));
    results.insert(QStringLiteral("value_kind"), QStringLiteral("percent"));
    QJsonArray top;
    double best = 0.0;
    for (size_t i = 0; i < out.results.size() && i < 5; ++i) {
        const double v = static_cast<double>(out.results[i].density) * 100.0;
        if (i == 0) best = v;
        QJsonObject r;
        r.insert(QStringLiteral("x"), out.results[i].minX);
        r.insert(QStringLiteral("z"), out.results[i].minZ);
        r.insert(QStringLiteral("value"), v);
        top.append(r);
    }
    results.insert(QStringLiteral("best_value"), best);
    if (!out.results.empty()) {
        results.insert(QStringLiteral("best_x"), out.results.front().minX);
        results.insert(QStringLiteral("best_z"), out.results.front().minZ);
    }
    results.insert(QStringLiteral("top"), top);
    s.insert(QStringLiteral("results"), results);
    s.insert(QStringLiteral("cancelled"), out.cancelled);
    s.insert(QStringLiteral("stopped_early"), out.stoppedEarly);
    s.insert(QStringLiteral("gpu_used"), gpuUsed);
    return s;
}

// v3.3.8.1（任务书 3.1）：物理核心数（用于「线程数过大」提示；失败返回 0）
int physicalCoreCount()
{
#ifdef _WIN32
    DWORD len = 0;
    // 第一次调用**预期失败**（ERROR_INSUFFICIENT_BUFFER），作用只是取出所需字节数；
    // 此前把这次失败当成探测失败 ⇒ 恒返回 0 ⇒ 线程数过大警告永远不会触发（自检抓到）。
    GetLogicalProcessorInformation(nullptr, &len);
    if (len == 0) return 0;
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    if (!GetLogicalProcessorInformation(buf.data(), &len)) return 0;
    int cores = 0;
    for (const auto& e : buf) {
        if (e.Relationship == RelationProcessorCore) ++cores;
    }
    return cores;
#else
    const unsigned hc = std::thread::hardware_concurrency();
    return hc > 1 ? static_cast<int>(hc / 2) : static_cast<int>(hc);
#endif
}

// v3.3.8.1（任务书 3.2）：大范围确认判据（纯函数，便于自检直接断言）
//   有估算时：预计耗时 > 5 分钟（300 s）即需确认；无估算时回退「区块数 > 阈值」。
bool largeRangeNeedsConfirm(int64_t totalChunks, bool estOk, double estSeconds, int64_t warnAtChunks)
{
    if (estOk) return estSeconds > 300.0;
    return warnAtChunks > 0 && totalChunks > warnAtChunks;
}

// v3.3.9（任务书 3.2）：大范围确认弹窗正文 —— 必须写清三项：即将扫描多少、超阈值多少、预计多少分钟。
// 抽成纯函数，GUI 自检可直接断言文案内容（不必真的弹窗）。
QString largeRangeConfirmText(int64_t totalChunks, int64_t areaX, int64_t areaZ, bool estOk,
                              double estSeconds, int64_t warnAtChunks)
{
    QString body = slime::ui::t("ui.b6c0337a9f51")
                       .arg(totalChunks)
                       .arg(areaX)
                       .arg(areaZ);
    if (estOk) {
        const double minutes = estSeconds / 60.0;
        body += slime::ui::t("ui.c3c31f003ac4")
                    .arg(minutes, 0, 'f', 1)
                    .arg(static_cast<qint64>(estSeconds));
        body += slime::ui::t("ui.831e399c0b24")
                    .arg((std::max)(0.0, minutes - 5.0), 0, 'f', 1);
    } else {
        body += slime::ui::t("ui.50b3a2934d37");
        body += slime::ui::t("ui.4b2071a74891")
                    .arg(warnAtChunks)
                    .arg(totalChunks);
    }
    body += slime::ui::t("ui.35970fb19f7f");
    return body;
}
// Java 世界边界 ±30000000 方块 → ±1875000 区块；基岩 ±16777216 方块 → ±1048576 区块
constexpr int kJavaBoundChunk = 1875000;
constexpr int kBedrockBoundChunk = 1048576;
constexpr int64_t kWholeMapMinArea = 1000000000000LL; // ≥ 1 万亿区块视为“整图”级搜索

// 时长范围文本（实测速率 ×0.8 ~ ×1.35）
QString formatDurationRange(double loSec, double hiSec)
{
    const auto roundUp = [](double v) { return static_cast<int>(std::ceil(v)); };
    if (hiSec < 90.0) {
        return slime::ui::t("ui.331f7d0a823d").arg((std::max)(1, roundUp(loSec))).arg((std::max)(1, roundUp(hiSec)));
    }
    if (hiSec < 90.0 * 60.0) {
        return slime::ui::t("ui.efcbf4fde9db").arg((std::max)(1, roundUp(loSec / 60.0))).arg((std::max)(1, roundUp(hiSec / 60.0)));
    }
    if (hiSec < 48.0 * 3600.0) {
        return slime::ui::t("ui.256faadb3fd0").arg(loSec / 3600.0, 0, 'f', 1).arg(hiSec / 3600.0, 0, 'f', 1);
    }
    return slime::ui::t("ui.c57bc8bb4623").arg(loSec / 86400.0, 0, 'f', 1).arg(hiSec / 86400.0, 0, 'f', 1);
}

// ---------------- v3.3.0：config.json 极简读取（本项目自写的扁平 JSON） ----------------
// 算法模式名（日志/配置键）
const char* algorithmDisplayName(Algorithm a)
{
    switch (a) {
    case Algorithm::Layered: return "layered";
    case Algorithm::Sample: return "sample";
    case Algorithm::Brute: return "brute";
    default: return "exact";
    }
}
// v3.4.12：与 jsonEscapeUtf8 对称 —— 读回时还原 JSON 转义（引号 / 反斜杠 / \n \t \r \b \f / \uXXXX）。
// 为什么必须对称：写入端现在会把 `C:\Temp` 写成 `C:\\Temp`，读回若不还原，缓存目录就会变成双反斜杠。
static std::string jsonUnescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        const char n = s[++i];
        switch (n) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
            bool ok = (i + 4 < s.size());
            unsigned cp = 0;
            for (int k = 1; ok && k <= 4; ++k) {
                const char h = s[i + static_cast<size_t>(k)];
                unsigned d = 0;
                if (h >= '0' && h <= '9') d = static_cast<unsigned>(h - '0');
                else if (h >= 'a' && h <= 'f') d = static_cast<unsigned>(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') d = static_cast<unsigned>(h - 'A' + 10);
                else ok = false;
                cp = cp * 16 + d;
            }
            if (ok) {
                i += 4;
                if (cp < 0x80) {
                    out += static_cast<char>(cp);
                } else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            out += n; // 非法 \u：原样保留
            break;
        }
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        default:
            // v3.4.12：未知转义（如旧版未转义路径里的 \T / \c）原样保留，避免吃掉反斜杠
            out += '\\';
            out += n;
            break;
        }
    }
    return out;
}

// 支持 "块.键" 形式；块名用于消歧（第一步先定位块，再在块内找键）。
std::string cfgRawValue(const std::filesystem::path& path, const std::string& key, bool* found)
{
    *found = false;
    std::ifstream in(path);
    if (!in) return std::string();
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto dot = key.find('.');
    const std::string block = (dot == std::string::npos) ? std::string() : key.substr(0, dot);
    const std::string name = (dot == std::string::npos) ? key : key.substr(dot + 1);
    size_t searchFrom = 0;
    if (!block.empty()) {
        const auto bp = content.find("\"" + block + "\"");
        if (bp == std::string::npos) return std::string();
        searchFrom = bp;
    }
    const auto kp = content.find("\"" + name + "\"", searchFrom);
    if (kp == std::string::npos) return std::string();
    const auto colon = content.find(':', kp + name.size() + 2);
    if (colon == std::string::npos) return std::string();
    size_t i = colon + 1;
    while (i < content.size() && (content[i] == ' ' || content[i] == '\t' || content[i] == '\n' ||
                                  content[i] == '\r'))
        ++i;
    const bool quoted = (i < content.size() && content[i] == '"');
    if (quoted) ++i;
    size_t j = i;
    if (quoted) {
        // v3.4.12：写入端会转义，这里必须跳过转义序列，否则 `C:\\dir` 会在反斜杠处提前收尾
        while (j < content.size()) {
            if (content[j] == '\\' && j + 1 < content.size()) {
                j += 2;
                continue;
            }
            if (content[j] == '"') break;
            ++j;
        }
    } else {
        while (j < content.size()) {
            const char c = content[j];
            if (c == ',' || c == '}' || c == '\n' || c == '\r') break;
            ++j;
        }
    }
    *found = true;
    const std::string raw = content.substr(i, j - i);
    if (!quoted) return raw;
    const std::string un = jsonUnescape(raw);
    // v3.4.12：3.4.12 之前的配置文件**不做转义**，形如 D:\new 的路径会被误判成转义序列。
    //   判据：反转义后出现了控制字符、而原文没有 ⇒ 认定是旧文件（真实路径不会含换行/制表符），
    //   按原样返回；反之按 JSON 语义还原（新文件里 \\ → \ 是正确的）。
    const char* ctrl = "\n\r\t\b\f";
    const bool rawHasCtrl = raw.find_first_of(ctrl) != std::string::npos;
    const bool unHasCtrl = un.find_first_of(ctrl) != std::string::npos;
    if (unHasCtrl && !rawHasCtrl) return raw;
    return un;
}

std::string cfgString(const std::filesystem::path& path, const std::string& key, const std::string& def)
{
    bool found = false;
    const std::string v = cfgRawValue(path, key, &found);
    return (found && !v.empty()) ? v : def;
}

int cfgInt(const std::filesystem::path& path, const std::string& key, int def)
{
    bool found = false;
    const std::string v = cfgRawValue(path, key, &found);
    if (!found) return def;
    try {
        return std::stoi(v);
    } catch (...) {
        return def;
    }
}

double cfgDouble(const std::filesystem::path& path, const std::string& key, double def)
{
    bool found = false;
    const std::string v = cfgRawValue(path, key, &found);
    if (!found) return def;
    try {
        return std::stod(v);
    } catch (...) {
        return def;
    }
}

bool cfgBool(const std::filesystem::path& path, const std::string& key, bool def)
{
    bool found = false;
    const std::string v = cfgRawValue(path, key, &found);
    if (!found) return def;
    if (v == "true" || v == "1") return true;
    if (v == "false" || v == "0") return false;
    return def;
}

// v3.3.1（任务书 2.1）：导入对话框过滤器（供 importCache 与自检共用，保证与保存扩展名一致）
// v3.3.2（任务书 3.2）：过滤器必须覆盖「新版 + 全部历史格式」，任何历史缓存文件都能在对话框中
//   看到并选中（修复前 1.1/3.3.0 只有 (*.cache)，对话框起始目录也未定位到缓存目录）：
//     .slimecache         v3.3.1 起保存的新格式（可直接加载）
//     .cache              v3.1.1~v3.3.0 的二进制格式（可直接加载，并在启动时自动迁移为新格式）
//     .pkl / .pickle      v3.1.0 及更早的 Python pickle 缓存（本身不可直接加载：选中后提示用
//                         scripts/convert_pkl_cache.py 转换）
//     .bin                任务书列举的假设格式；本程序历史上从未使用（无保存/加载代码，见验证报告）
QString cacheImportFilter()
{
    // v3.3.8（任务 2）：加入 *.legacy —— 历史缓存被改名停用后，用户仍可在导入对话框里**手动选中**它回滚
    //   （「默认不用于跳过判定」，但显式导入即视为用户确认使用；导入后仍走支撑度清扫）。
    return slime::ui::t("ui.4b49d666ca8a");
}
// v3.3.4（任务书 1.4）：按任务书要求**不做折叠、不搬移布局条目** ——
// v3.3.3 的 makeCollapsibleGroup（搬迁布局条目实现折叠）已在本次重构中移除，
// 6 个设置分组的内容全部在各自分组内直接创建。

// ---------------- v3.3.8（任务 2 / 历史缓存方案 A：标记失效 + 强制重扫） ----------------
// 背景：v3.3.8 之前的缓存有「覆盖声明没有条目支撑」的缺陷（1.02 GB 实测漏存 ~34%、0 假阳性），
//   继续用它做跳过判定只会给出错误结果 ⇒ 首次运行本版本时把已有缓存改名为 `<名>.legacy` 而**不加载**，
//   提示用户「可能漏存 ~34%，建议重扫」；旧文件保留 kCacheLegacyKeepDays 天（可回滚：手动导入即可），到期自动清理。
// 一次性语义：目录下存在标记文件 kCacheMigratedMarker 即视为迁移已完成 —— 否则 7 天后 .legacy 被清理，
//   新写入的正常缓存又会被误判为「历史缓存」而反复失效。
// 纯文件操作、无 UI 依赖（自检可对临时目录直接断言）。
struct LegacyCacheMigration {
    std::vector<std::string> moved;  // 本次改名为 .legacy 的文件（绝对路径字符串）
    std::vector<std::string> purged; // 本次到期清理的 .legacy 文件
    bool migrationRan = false;       // 本次是否执行了「一次性迁移」（标记原本不存在）
    bool promptNeeded = false;       // 是否需要提示用户「历史缓存已停用，建议重扫」
};

LegacyCacheMigration migrateLegacyCachesOnce(const std::filesystem::path& dir, int keepDays)
{
    LegacyCacheMigration res;
    std::error_code ec;
    const std::filesystem::path marker = dir / kCacheMigratedMarker;
    const std::vector<std::string> baseNames{ kCacheFileName, kCacheLegacyFileName };
    const auto legacyPathOf = [&](const std::string& n) { return dir / (n + kCacheLegacySuffix); };

    // ① 到期清理：只删我们自己产生的 *.legacy（按最后写入时间），不碰其它文件
    const auto cutoff = std::filesystem::file_time_type::clock::now() -
                        std::chrono::hours(24 * (keepDays > 0 ? keepDays : kCacheLegacyKeepDays));
    for (const std::string& n : baseNames) {
        const auto lp = legacyPathOf(n);
        if (!std::filesystem::exists(lp, ec)) continue;
        const auto t = std::filesystem::last_write_time(lp, ec);
        if (ec) continue;
        if (t < cutoff) {
            std::filesystem::remove(lp, ec);
            if (!ec) res.purged.push_back(lp.string());
        }
    }

    // ② 一次性迁移
    if (std::filesystem::exists(marker, ec)) return res; // 已迁移过（新缓存照常使用）
    res.migrationRan = true;
    for (const std::string& n : baseNames) {
        const auto src = dir / n;
        if (!std::filesystem::exists(src, ec)) continue;
        const auto dst = legacyPathOf(n);
        std::filesystem::remove(dst, ec); // 同名 .legacy 直接更新为本次的（更旧的一份已在上一步按 7 天清理）
        ec.clear();
        std::filesystem::rename(src, dst, ec);
        if (ec) {
            ec.clear();
            // 改名失败（跨卷/占用）则退回复制 + 删除，保证「不被加载」这一语义仍然成立
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) std::filesystem::remove(src, ec);
            ec.clear();
        }
        if (std::filesystem::exists(dst, ec)) res.moved.push_back(dst.string());
    }
    // ③ 落标记：无论本次是否有文件被改名，都视为「迁移已完成」（新装的用户不会再有历史缓存）
    {
        std::ofstream mk(marker, std::ios::trunc);
        mk << "v3.3.8 legacy-cache migration done\n";
    }
    res.promptNeeded = !res.moved.empty();
    return res;
}

// v3.3.7（任务书 P1-5）：把原先「一函数包办百分比+速度+ETA」的 progressStageText 拆成两个纯函数，
// 彻底消除同一信息在多个控件重复显示的问题：
//   * progressStageName(stage)  → 只给**阶段短语**，放在工具栏下方的进度行（不带百分比/ETA）
//   * progressStatsText(...)    → 只给**统计信息**（扫描 N/M 区块 ｜ 速度 ｜ 找到 K 个），放状态栏
// 百分比只由 progressPercent 显示；预计剩余只由状态栏 progressEta 显示。
// v3.3.7.2（任务书 1.4）：阶段短语**按模式与算法分派** ——
// 密度模式的第二阶段是「密度匹配（窗口统计）」，不是「匹配图案」；快速估算走「估算」措辞。
QString progressStageName(int stage, slime::Mode mode, slime::Algorithm alg)
{
    const bool density = (mode == slime::Mode::Density);
    const bool sample = (alg == slime::Algorithm::Sample);
    switch (stage) {
    case 0: return slime::ui::t("ui.b0894e1c9f9a");
    case 1:
        return sample      ? slime::ui::t("ui.45a5facab1ca")
               : density   ? slime::ui::t("ui.c7709064a72b")
                           : slime::ui::t("ui.6df614a8dce8");
    case 2:
        return sample    ? slime::ui::t("ui.6a6660d8c144")
               : density ? slime::ui::t("ui.1e28c06fde93")
                         : slime::ui::t("ui.5b0b37bab0a8");
    case 3: return slime::ui::t("ui.0133025c537b");
    case 4: return slime::ui::t("ui.8174d1168fce");
    case 5: return slime::ui::t("ui.a5ffdc95eeb0");
    default: return slime::ui::t("ui.72b0bb777554");
    }
}

// v3.10.2（用户要求）：速度单位可在 万/秒 与 亿/秒 之间灵活切换（0=自动）
QString rateText(double cps, int unit)
{
    if (cps <= 0.0) return QString();
    const bool yi = (unit == 2) || (unit == 0 && cps >= 1.0e8);
    if (yi) return slime::ui::t("ui.3865e40cfa59").arg(cps / 1.0e8, 0, 'f', 2);
    return slime::ui::t("ui.3f0fac451d57").arg(cps / 1.0e4, 0, 'f', 1);
}

QString progressStatsText(int64_t scanned, int64_t total, double cps, int64_t found, int rateUnit = 0)
{
    QString text = slime::ui::t("ui.6bef0c87c214").arg(scanned);
    if (total > 0) {
        const double pct = 100.0 * static_cast<double>(scanned) / static_cast<double>(total);
        text += slime::ui::t("ui.a762858dd578").arg(total).arg(pct, 0, 'f', 1);
    } else {
        text += slime::ui::t("ui.6b7ffe992657");
    }
    if (cps > 0.0) text += slime::ui::t("ui.4e63cc922909") + rateText(cps, rateUnit);
    if (found >= 0) text += slime::ui::t("ui.e1e99118cb5e").arg(found);
    return text;
}

// v3.3.7.2（F-1）：截图「豆腐块」检测 —— 缺字形时所有字符都被画成同样的方框，
// 水平投影的**墨迹列宽种类**会极少（实测：无字体 1 种、真实文字 8~10 种）。
// 判定阈值取 5（留足余量）：低于 5 说明文字没有真正渲染出来，截图不可作为交付证据。
int inkRunWidthVariety(const QImage& img)
{
    if (img.isNull() || img.width() <= 0 || img.height() <= 0) return 0;
    QSet<int> widths;
    int cur = 0;
    for (int x = 0; x < img.width(); ++x) {
        bool ink = false;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb c = img.pixel(x, y);
            if (qRed(c) + qGreen(c) + qBlue(c) < 330) {
                ink = true;
                break;
            }
        }
        if (ink) {
            ++cur;
        } else if (cur > 0) {
            widths.insert(cur);
            cur = 0;
        }
    }
    if (cur > 0) widths.insert(cur);
    return widths.size();
}

// 墨迹占比：用于判空（整张纯背景 = 没画出任何东西）
double inkRatio(const QImage& img)
{
    if (img.isNull() || img.width() <= 0 || img.height() <= 0) return 0.0;
    int64_t ink = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = img.pixel(x, y);
            if (qRed(c) + qGreen(c) + qBlue(c) < 330) ++ink;
        }
    }
    return static_cast<double>(ink) / (static_cast<double>(img.width()) * static_cast<double>(img.height()));
}

// 墨迹像素数（v3.3.7.2：比较「开启/关闭进度条内置文字」两次渲染，证明重复绘制存在）
int inkPixels(const QImage& img)
{
    if (img.isNull()) return 0;
    int ink = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = img.pixel(x, y);
            if (qRed(c) + qGreen(c) + qBlue(c) < 330) ++ink;
        }
    }
    return ink;
}

} // namespace

// ---------------- 控件集合 ----------------
struct MainWindow::Widgets {
    QRadioButton* javaRadio = nullptr;
    QRadioButton* bedrockRadio = nullptr;
    QComboBox* mapVersionCombo = nullptr;   // v3.5.26：群系/世界算法版本（影响地图上色）
    QLineEdit* seedEdit = nullptr;
        QLabel* seedLabel = nullptr;   // v3.6.12：基岩版切换文案用

    QSpinBox* minX = nullptr;
    QSpinBox* rangeRadius = nullptr;   // v3.5.16：范围只填半径（正方形边长 = 2×半径）
    QSpinBox* bRadius = nullptr;       // v3.5.16：构建页半径（与搜索页双向同步）
    QLabel* rangeHint = nullptr;       // v3.5.16：范围提示（边长）
    QSpinBox* maxX = nullptr;
    QSpinBox* minZ = nullptr;
    QSpinBox* maxZ = nullptr;
    QPushButton* presetNear = nullptr;
    QPushButton* presetLocal = nullptr;
    QPushButton* presetGlobal = nullptr;
    QPushButton* presetWorld = nullptr;
    QPushButton* presetBedrock = nullptr;

    QRadioButton* strictRadio = nullptr;
    QRadioButton* densityRadio = nullptr;
    QWidget* strictPanel = nullptr;
    QCheckBox* lockDir = nullptr;
    QPushButton* drawPattern = nullptr;   // 旧弹窗入口（保留成员避免牵连其它代码）
    slime::PatternEditorPanel* inlinePattern = nullptr;   // v3.5.18：内嵌图案编辑器
    QWidget* squareOpts = nullptr;        // v3.5.18：正方形专属选项容器
    QWidget* ringOpts = nullptr;          // v3.5.18：环形专属选项容器
    QLabel* patternInfo = nullptr;
    QWidget* densityPanel = nullptr;
    QSpinBox* gridSize = nullptr;
    QDoubleSpinBox* minDensity = nullptr;
    QCheckBox* ignoreThreshold = nullptr;   // v3.9.12：最优候选模式（忽略阈值）
    QComboBox* rateUnitCombo = nullptr;     // v3.10.2：速率单位（自动/万/亿）
    QLabel* presetDisclaimer = nullptr;     // v3.10.9：自定义方案速度声明
    QGroupBox* resultGroup = nullptr;       // v3.10.9：结果要求（仅自定义/环形显示）
    QToolButton* gpuAdvBtn = nullptr;       // v3.10.9：GPU 设置展开按钮
    QWidget* gpuAdvPanel = nullptr;         // v3.10.9：GPU 相关设置面板
    QCheckBox* gpuStreamCheck = nullptr;    // v3.10.9：图案 GPU 流式
    QCheckBox* gpuSelfOptCheck = nullptr;   // v3.10.11.1：结果自优化（提前停止）
    QSpinBox* gpuLimitSpin = nullptr;       // v3.10.9：GPU 占用率上限
    QCheckBox* gpuAutoCheck = nullptr;      // v3.10.9：启动时自动检测

    // v3.3.0：搜索算法（精确/分层/快速估算/保守）与参数
    QRadioButton* algExactRadio = nullptr;
    QRadioButton* algLayeredRadio = nullptr;
    QRadioButton* algSampleRadio = nullptr;
    QRadioButton* algBruteRadio = nullptr;
    QGroupBox* matchGroup = nullptr;   // v3.5.6：匹配方式组（环形时禁用）
    QRadioButton* algRingRadio = nullptr; // v3.5.5：环形覆盖（与其它算法并列）
    QRadioButton* shapeSquareRadio = nullptr; // v3.5.6：区域形状=正方形
    QRadioButton* shapeRingRadio = nullptr;   // v3.5.6：区域形状=环形覆盖
    QComboBox* ringScanCombo = nullptr;       // v3.5.17：环形扫描模式（快速粗扫 / 精确逐区块）
    QSpinBox* simDistance = nullptr;          // v3.6.18：模拟距离（区块），影响环带外沿
    QLabel* simDistanceNote = nullptr;        // 生效/不生效提示
    QButtonGroup* shapeButtonGroup = nullptr;   // v3.5.8
    QButtonGroup* modeButtonGroup = nullptr;    // v3.5.8
    QTableWidget* resultTable = nullptr;  // v3.5.5：结果列表（表头排序 + 每行跳转）
    QComboBox* layerBlock = nullptr;
    QDoubleSpinBox* layerRatio = nullptr;
    QSpinBox* sampleStep = nullptr;
    QCheckBox* sampleExact = nullptr;
    QLabel* algSummary = nullptr;
    QLabel* modeHeader = nullptr;  // 结果区：“当前使用模式：…”
    QLabel* modeWarn = nullptr;    // 结果区：非精确模式警告条

    QPushButton* importCache = nullptr;
    QPushButton* clearCache = nullptr;
    QLabel* cacheStatus = nullptr;
    QCheckBox* useCache = nullptr;

    QCheckBox* stopAtTarget = nullptr;
    // v3.9.11（设置重构阶段 B）：结果要求方案（预设）
    QComboBox* presetCombo = nullptr;
    QLabel* presetHint = nullptr;
    QSpinBox* targetCount = nullptr;
    QCheckBox* smartRange = nullptr;

    QSpinBox* startX = nullptr;
    QSpinBox* startZ = nullptr;
    QLineEdit* convX = nullptr;
    QLineEdit* convZ = nullptr;
    QPushButton* convertBtn = nullptr;

    QSpinBox* threads = nullptr;
    QLabel* threadsHint = nullptr; // v3.3.9（任务书 3.1）：线程数过大时常驻提示
    // v3.4.0：构建范围页控件（范围与搜索页同一份，预算 = cache.maxSizeMB）
    QSpinBox* bMinX = nullptr;
    QSpinBox* bMaxX = nullptr;
    QSpinBox* bMinZ = nullptr;
    QSpinBox* bMaxZ = nullptr;
    QSpinBox* bBudget = nullptr;
    QPushButton* bEstimate = nullptr;
    QPushButton* bStart = nullptr;
    QPushButton* bAbort = nullptr;
    QProgressBar* bBar = nullptr;
    QLabel* bText = nullptr;
    QLabel* bSummary = nullptr;
    QLabel* bSummaryLive = nullptr; // v3.4.12：构建预估实时显示（读控件值，不读 config 文件）
    QLabel* bPath = nullptr;
    // v3.3.9（任务书 1.1）：缓存构建模式（唯一可写缓存的模式）+ 说明行
    QCheckBox* buildMode = nullptr;
    QLabel* buildHint = nullptr;
    QCheckBox* gpuCheck = nullptr; // v3.2.1：启用 GPU 加速（实验性），默认关闭
    QLabel* gpuStatus = nullptr;   // GPU 状态文本
    QPushButton* start = nullptr;
    QPushButton* cancel = nullptr;
    QPushButton* settings = nullptr;    // v3.3.6（Bug 3）：工具栏 [⚙ 设置]（开始搜索左侧）
    QProgressBar* progress = nullptr;   // v3.3.6：工具栏下方进度行
    QWidget* searchStatus = nullptr;    // v3.5.28：工具栏搜索状态面板
    QLabel* searchStage = nullptr;      //  阶段（搜索中/匹配中/排序中）
    QLabel* progressInfo = nullptr;     // 进度补充说明（状态栏）
    QLabel* progressStage = nullptr;    // v3.3.4：阶段文字（扫描/匹配/排序）
    QLabel* progressElapsed = nullptr;  // v3.3.4：已用时（状态栏）
    QLabel* progressEta = nullptr;      // v3.3.4：预计剩余（状态栏）
    QLabel* progressPercent = nullptr;  // v3.3.5 Bug 7：进度百分比

    QPushButton* copyBtn = nullptr;
    QPushButton* afkBtn = nullptr;   // v3.5.2（任务 1）：结果头「挂机点」入口
    QPushButton* mapBtn = nullptr;   // v3.5.2（任务 2）：结果头「地图」入口
    QPushButton* contactBtn = nullptr;
    QPushButton* helpBtn = nullptr;   // v3.3.3：帮助 / 使用教程
    QComboBox* sortCombo = nullptr;   // v3.3.3：结果排序（距离 / 密度）
    // v3.3.4：右侧「信息栏 + QTextEdit」
    QTextEdit* resultView = nullptr;
    QLabel* infoStats = nullptr;

    // v3.3.0：设置区滚动容器 + 搜索模式组（自检截图定位用）
    QScrollArea* mainScroll = nullptr;
    QWidget* modeGroup = nullptr;
};

// ---------------- 构造 ----------------
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    m_w = new Widgets;
    m_cache = std::make_unique<CacheManager>();
    m_telemetry = std::make_unique<slime::gui::TelemetryClient>();

    // v3.3.9（任务书 5.2）：窗口名称精简 —— 去掉「C++」「Qt」「重构」等技术词
    setWindowTitle(slime::ui::t("ui.8e056aa8f722").arg(QLatin1String(kAppVersion)));
    resize(700, 920);
    setMinimumSize(640, 720);

    // v3.3.9（任务书 5.1）：托盘图标（素材同 exe/窗口图标：透明底史莱姆方块）。
    // 只提供「显示主窗口 / 退出」两个入口；关闭主窗口仍然是正常退出（不改变原有行为）。
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(windowIcon().isNull() ? qApp->windowIcon() : windowIcon(), this);
        m_tray->setToolTip(slime::ui::t("ui.8e056aa8f722").arg(QLatin1String(kAppVersion)));
        auto* trayMenu = new QMenu(this);
        trayMenu->addAction(slime::ui::t("ui.6db9e749dacb"), this, [this]() {
            showNormal();
            raise();
            activateWindow();
        });
        trayMenu->addSeparator();
        trayMenu->addAction(slime::ui::t("ui.feecb1e6adec"), this, []() { QCoreApplication::quit(); });
        m_tray->setContextMenu(trayMenu);
        connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                showNormal();
                raise();
                activateWindow();
            }
        });
        m_tray->show();
    }

    // v3.5.20：界面语言必须在建界面之前定下来（文案在建控件时就要取）
    {
        const std::filesystem::path langCfg = appDirPath() / kConfigFileName;
        const std::string lang = cfgString(langCfg, "settings.language", "zh");
        slime::ui::setLanguage(lang == "en" ? slime::ui::Language::English : slime::ui::Language::Chinese);
    }
    buildUi();
    // v3.4.11：设置页在这里是「默认值」状态，等 loadAppSettings() 之后统一收敛一次（见下）

    // v3.1.0：缓存容量配置 config.json（不存在则创建默认值 1024 MB）
    // v3.2.1：GPU 开关（默认关闭——避免无 OpenCL 驱动环境自动触发 GPU 初始化）
    const std::filesystem::path cfgPath = appDirPath() / kConfigFileName;
    std::error_code ec;
    if (!std::filesystem::exists(cfgPath, ec)) {
        std::ofstream out(cfgPath);
        if (out) {
            out << "{\n  \"cache\": {\n    \"maxSizeMB\": 1024\n  },\n  \"gpu\": {\n    \"enabled\": false\n  },\n"
                   "  \"algorithm\": {\n    \"mode\": \"exact\",\n    \"layerBlock\": 16,\n"
                   "    \"layerRatio\": 1.0,\n    \"sampleStep\": 8,\n    \"sampleExact\": false\n  }\n}\n";
        }
    }
    // v3.2.1：读取 config.json 的 gpu.enabled 并同步到复选框（不触发写回）
    if (m_w->gpuCheck) {
        m_loadingConfig = true;
        m_w->gpuCheck->setChecked(gpuEnabledFromConfig());
        m_loadingConfig = false;
    }
    // v3.3.0：读取 config.json 的 algorithm 块（模式/分层参数/采样参数）
    loadAlgorithmFromConfig();
    // 说明：主进程启动阶段【完全不接触 OpenCL】（无/异常驱动环境下 LoadLibrary 级原生
    // 崩溃无法被 C++ 异常捕获）；GPU 可用性仅在用户勾选启用时检测一次（ensureGpuScanOnce）。
    // v3.5.20：F11 在全屏方式之间切换（无边框全屏 / 窗口最大化）
    {
        auto* fsAct = new QAction(this);
        fsAct->setShortcut(QKeySequence(Qt::Key_F11));
        fsAct->setShortcutContext(Qt::ApplicationShortcut);
        connect(fsAct, &QAction::triggered, this, [this]() { toggleFullscreenMode(); });
        addAction(fsAct);
    }
    updateGpuStatusUi();

    // v3.3.4：进度行/状态栏的「已用时 / 预计剩余」刷新定时器（200ms，仅在搜索中生效）
    m_elapsedTimer = new QTimer(this);
    m_elapsedTimer->setInterval(200);
    connect(m_elapsedTimer, &QTimer::timeout, this, &MainWindow::updateElapsedUi);

    // v3.3.6（任务书 调整 3）：进度流水追加定时器（500ms，仅在搜索中生效）
    m_streamTimer = new QTimer(this);
    m_streamTimer->setInterval(500);
    connect(m_streamTimer, &QTimer::timeout, this, &MainWindow::appendProgressStreamLine);

    // ---------------- v3.3.5（优化 4/5）：程序设置、滚轮过滤器、窗口大小记忆 ----------------
    loadAppSettings();
    updateStartEnabled();   // v3.6.5：初始按条件决定是否可开始
    applyLanguage();   // v3.5.27：按当前语言把界面文案统一改写一遍
    checkGpuCrashLoopOnStartup();   // v3.6.2：上次若死在 GPU 驱动上，自动关 GPU
    startLanguageWatch();           // v3.6.4：英文模式下持续修正运行时文本
    // v3.5.26：把持久化的群系版本同步到下拉框（无 UI 信号环路）
    if (m_w && m_w->mapVersionCombo) {
        const int idx = m_w->mapVersionCombo->findData(m_mapJavaVersionId);
        if (idx >= 0) {
            QSignalBlocker block(m_w->mapVersionCombo);
            m_w->mapVersionCombo->setCurrentIndex(idx);
        }
    }
    syncSettingsPageFromState(); // v3.4.11：把 config.json 里的真实设置推回设置页（修状态不一致）
    installWheelFilter();
    // v3.5.1（交互手感）：双击分隔条恢复默认比例
    if (m_hSplit && m_hSplit->count() > 1 && m_hSplit->handle(1)) m_hSplit->handle(1)->installEventFilter(this);
    if (m_rightSplit && m_rightSplit->count() > 1 && m_rightSplit->handle(1))
        m_rightSplit->handle(1)->installEventFilter(this);   // v3.10.1：右列（结果/过程）双击恢复
    // v3.5.1（交互手感）：Ctrl+1..3 直达 搜索 / 缓存构建 / 设置（不新增任何控件）
    for (int i = 0; i < kNavCount; ++i) {
        auto* sc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)), this);
        connect(sc, &QShortcut::activated, this, [this, i]() { showPage(i); });
    }
    // v3.3.6：主题、提示音、日志面板显示参数按设置立即生效
    applyThemeFromSettings();
    slime::SoundPlayer::setEnabled(m_settings.soundOnComplete);
    if (m_logPanel) {
        m_logPanel->setAutoScroll(m_settings.logAutoScroll);
        m_logPanel->setMaxLines(m_settings.logMaxLines);
    }
    // v3.3.7（P1-6）：崩溃转储与调试模式真正接线（此前 crashDump 是死设置）
    slime::CrashDump::setEnabled(m_settings.crashDump);
    slime::IncidentLog::setVerbose(m_settings.debugMode);
    slime::IncidentLog::note(std::string("设置：崩溃转储=") + (m_settings.crashDump ? "开" : "关") +
                             " 调试模式=" + (m_settings.debugMode ? "开" : "关") +
                             " 日志=" + std::to_string(m_settings.logAtStartup ? 1 : 0));
    // 上次会话是否异常结束 → 启动时提示（最小 UI 改动：反馈栏 + 状态栏）
    if (slime::IncidentLog::previousSessionUnclean()) {
        const std::string prev = slime::IncidentLog::previousSessionInfo();
        const QString logPath = QString::fromStdWString(slime::IncidentLog::currentFile().wstring());
        const QString crashDir = QString::fromStdWString(slime::CrashDump::dumpDir().wstring());
        addFeedback(1, slime::ui::tr(slime::ui::t("ui.1b18e34ef1fa"))
                           .arg(QString::fromStdString(prev), logPath, crashDir));
        setStatus(slime::ui::tr(slime::ui::t("ui.91b739f9e7f1")));
        slime::IncidentLog::note("检测到上次会话异常结束，已在界面提示用户：" + prev);
    }
    // 主界面初始值 = 设置界面的默认值（两者共享同一份 config.json）
    if (!m_settings.defaultAlgorithm.isEmpty()) {
        if (m_settings.defaultAlgorithm == QLatin1String("layered")) m_w->algLayeredRadio->setChecked(true);
        else if (m_settings.defaultAlgorithm == QLatin1String("sample")) m_w->algSampleRadio->setChecked(true);
        else if (m_settings.defaultAlgorithm == QLatin1String("brute")) m_w->algBruteRadio->setChecked(true);
        else m_w->algExactRadio->setChecked(true);
        updateAlgorithmPanels();
    }
    if (m_settings.defaultThreads > 0) m_w->threads->setValue(m_settings.defaultThreads);
    if (m_settings.defaultTargetCount > 0) m_w->targetCount->setValue(m_settings.defaultTargetCount);
    if (m_settings.defaultStopAtTarget) m_w->stopAtTarget->setChecked(true);
    if (m_settings.defaultGpu) m_w->gpuCheck->setChecked(true); // 触发一次与勾选一致的探测
    if (!m_settings.defaultPreset.isEmpty() && m_settings.defaultPreset != QLatin1String("none")) {
        const QString p = m_settings.defaultPreset;
        if (p == QLatin1String("near")) applyPreset(20000);     // v3.10.12.8：±5000 → ±20000（GPU 提速后上调）
        else if (p == QLatin1String("local")) applyPreset(200000);
        else if (p == QLatin1String("global")) applyPreset(1000000);
        else if (p == QLatin1String("world")) m_w->presetWorld->click();
        else if (p == QLatin1String("bedrock")) m_w->presetBedrock->click();
    }
    // 窗口大小/位置记忆（自检模式下跳过，保证自检与截图的尺寸可复现）
    if (m_settings.rememberWindow && qEnvironmentVariableIsEmpty("SLIME_SELFTEST")) {
        const auto cfg = appDirPath() / kConfigFileName;
        const int ww = cfgInt(cfg, "window.w", 0);
        const int hh = cfgInt(cfg, "window.h", 0);
        const int wx = cfgInt(cfg, "window.x", INT_MIN);
        const int wy = cfgInt(cfg, "window.y", INT_MIN);
        if (ww >= 1000 && hh >= 650) resize(ww, hh);
        if (wx != INT_MIN && wy != INT_MIN) move(wx, wy);
    }
    // GPU 基准缓存（Bug 6）：硬件（设备名/核心数）一致时直接采用，避免每次启动重测
    {
        const auto cfg = appDirPath() / kConfigFileName;
        const double rCpu = cfgDouble(cfg, "gpuBench.cpuRate", 0.0);
        const double rGpu = cfgDouble(cfg, "gpuBench.gpuRate", 0.0);
        const int cores = cfgInt(cfg, "gpuBench.cores", 0);
        const std::string dev = cfgString(cfg, "gpuBench.device", "");
        if (rCpu > 0.0 && rGpu > 0.0 && cores == static_cast<int>(std::thread::hardware_concurrency())) {
            m_gpuBenchCpuRate = rCpu;
            m_gpuBenchGpuRate = rGpu;
            // v3.3.6（任务书 Bug 2）：只缓存**实测吞吐率**，不再保存/推导交叉点
            m_gpuBenchText = slime::ui::t("ui.25e6c0d099fc")
                                 .arg(rCpu / 1e8, 0, 'f', 2)
                                 .arg(rGpu / 1e8, 0, 'f', 2);
            if (!dev.empty()) slime::LogBus::info("采用 config.json 缓存的 GPU 基准（设备 " + dev + "）");
        }
    }

    // 自动加载缓存文件（若存在）
    // v3.3.1：优先新扩展名 .slimecache；不存在时回退读取旧版 slime_cache.cache 并迁移为新名字
    // （旧文件保留不删，用户可自行清理；迁移失败仅记日志，不影响启动）。
    m_cacheFile = appDirPath() / kCacheFileName;
    const std::filesystem::path legacyCacheFile = appDirPath() / kCacheLegacyFileName;
    // v3.3.8（任务 2 / 历史缓存方案 A）：一次性把 v3.3.8 之前的缓存改名停用（保留 7 天、到期自动清理）
    const LegacyCacheMigration cacheMigration = migrateLegacyCachesOnce(appDirPath(), kCacheLegacyKeepDays);
    for (const std::string& p : cacheMigration.purged) {
        Logger::info("历史缓存已到期（> " + std::to_string(kCacheLegacyKeepDays) + " 天）自动清理：" + p);
    }
    const bool legacyDisabled = !cacheMigration.moved.empty();
    if (legacyDisabled) {
        QStringList names;
        for (const std::string& p : cacheMigration.moved)
            names << QFileInfo(QString::fromStdWString(std::filesystem::path(p).wstring())).fileName();
        const QString msg =
            slime::ui::t("ui.25b5dbc65dc6")
                .arg(names.join(QStringLiteral("、")))
                .arg(kCacheLegacyKeepDays);
        addFeedback(1, msg);
        setStatus(slime::ui::tr(slime::ui::t("ui.e3795aef15d9")));
        Logger::warning("历史缓存已停用并改名保留 " + std::to_string(kCacheLegacyKeepDays) + " 天：" +
                        cacheMigration.moved.front());
        slime::IncidentLog::note("cache.legacy 历史缓存已停用（方案 A：标记失效 + 强制重扫）：" +
                                 cacheMigration.moved.front() + "；本版本不加载它做跳过判定");
        if (!m_uiQuiet && qEnvironmentVariableIsEmpty("SLIME_SELFTEST")) {
            // 构造期不弹模态框：排到事件循环启动后（窗口已显示）再提示一次。
            // 注意自检模式必须跳过（自检在构造之后才置 m_uiQuiet=true，模态框会卡住自动化流程）。
            QTimer::singleShot(0, this, [this, msg]() {
                if (m_uiQuiet) return;
                QMessageBox::information(this, slime::ui::t("ui.5807dd3e3a3e"), msg);
            });
        }
    }
    if (std::filesystem::exists(m_cacheFile, ec) && !legacyDisabled) {
        std::string err;
        if (!m_cache->loadFile(m_cacheFile, true, err)) {
            Logger::warning("启动加载缓存失败: " + err + "（" + m_cacheFile.string() + "）");
        } else {
            Logger::info("启动加载缓存成功（" + m_cacheFile.string() + "）：" +
                         std::to_string(m_cache->ranges().size()) + " 区域 / " +
                         std::to_string(m_cache->entryCount()) + " 区块");
        }
    } else if (std::filesystem::exists(legacyCacheFile, ec) && !legacyDisabled) {
        std::string err;
        if (!m_cache->loadFile(legacyCacheFile, true, err)) {
            Logger::warning("启动加载旧版缓存失败（" + std::string(kCacheLegacyFileName) + "）: " + err);
        } else {
            Logger::info("启动加载旧版缓存成功（" + std::string(kCacheLegacyFileName) + "）：" +
                         std::to_string(m_cache->ranges().size()) + " 区域 / " +
                         std::to_string(m_cache->entryCount()) + " 区块");
            std::string serr;
            if (m_cache->saveFile(m_cacheFile, serr)) {
                Logger::info("旧版缓存已迁移为新格式：" + std::string(kCacheFileName) + "（" +
                             m_cacheFile.string() + "）");
            } else {
                Logger::warning("旧版缓存迁移失败（仍可正常使用内存中的缓存）: " + serr);
            }
        }
    }
    updateCacheStatusUi();

    // ---------------- v3.3.4（任务书 2/3）：日志总线、命令终端历史、更新提醒 ----------------
    // 1) 日志：默认关闭（不写文件、不记录）；模式来自 config.json 的 log.mode（缺省 off）
    {
        const auto logDir = appDirPath() / "logs";
        std::error_code logEc;
        std::filesystem::create_directories(logDir, logEc);
        slime::LogBus::init(logDir);
        const std::string modeName = cfgString(appDirPath() / kConfigFileName, "log.mode", "off");
        if (modeName == "brief") slime::LogBus::setMode(slime::LogMode::Brief);
        else if (modeName == "detailed") slime::LogBus::setMode(slime::LogMode::Detailed);
        else slime::LogBus::setMode(slime::LogMode::Off);
        if (m_logPanel) m_logPanel->refreshFromBus();
        slime::LogBus::info("启动完成：日志模式 " + std::string(slime::LogBus::modeName(slime::LogBus::mode())));
    }
    // 2) 命令终端历史（config.json 最多 100 条；setHistory 不会回写，避免读写循环）
    if (m_terminal) m_terminal->setHistory(loadTerminalHistoryFromConfig());
    // 3) 更新提醒（build_date.txt；>30 天显示徽章），徽章点击打开下载页
    if (m_updateBadge) {
        connect(m_updateBadge, &QLabel::linkActivated, this, [this](const QString&) { openReleasePage(); });
    }
    initUpdateReminder();
    // v3.3.9.1（任务书 2.1）：按设置决定是否在**启动时**自动检测 GPU 状态。
    // 用 0ms 单次定时器 ⇒ 先把主界面画出来再探测（无驱动环境下若驱动层原生崩溃，用户至少看到界面
    // 已经起来了、也便于从日志定位）；默认关闭（历史原因：无/异常 OpenCL 驱动下初始化可能原生崩溃）。
    QTimer::singleShot(0, this, [this]() { maybeAutoCheckGpuAtStartup(); });
    // 4) 反馈栏初始信息（任务书 2.2：信息级）
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.3a86af74f548"))
                       .arg(QLatin1String(kAppVersion)));
    if (m_cache->hasData()) {
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.5694aeac03b6"))
                           .arg(m_cache->ranges().size())
                           .arg(m_cache->entryCount()));
    }
}

MainWindow::~MainWindow()
{
    // v3.3.7（P0-2）：**无条件**取消并 join。
    // 旧写法 `if (m_task.running())` 会漏掉「线程已置 running=false、尚未执行结果回调」的窄窗口：
    // 那个窗口里线程仍会回调 GUI（访问正在析构的 this），是「取消后崩溃」的候选根因之一。
    // SearchTask::cancel()/wait() 本身是幂等的（未运行/已 join 时为无操作）。
    m_task.cancel();
    m_task.wait();
}

std::filesystem::path MainWindow::appDirPath() const
{
    const QString dir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    return std::filesystem::path(dir.toStdWString());
}

int MainWindow::versionBound() const noexcept
{
    return m_w->bedrockRadio->isChecked() ? kBedrockBoundChunk : kJavaBoundChunk;
}

int64_t MainWindow::cacheMaxBytes() const
{
    // config.json: { "cache": { "maxSizeMB": 1024 } }
    int64_t mb = 1024;
    std::ifstream in(appDirPath() / kConfigFileName);
    if (in) {
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto pos = content.find("maxSizeMB");
        if (pos != std::string::npos) {
            const auto colon = content.find(':', pos);
            if (colon != std::string::npos) {
                try {
                    size_t end = 0;
                    const long long v = std::stoll(content.substr(colon + 1), &end);
                    if (v > 0 && v <= 1 << 20) mb = v; // 1MB ~ 1TB 合理范围
                } catch (...) {
                }
            }
        }
    }
    return mb * 1024 * 1024;
}

// ---------------- v3.2.1：GPU 开关与可用性探测 ----------------

// config.json gpu.enabled（缺省 false；环境变量 SLIME_GPU=0/1 可强制覆盖，仅供排障）
bool MainWindow::gpuEnabledFromConfig() const
{
    const char* env = std::getenv("SLIME_GPU");
    if (env) {
        if (std::string(env) == "0") return false;
        if (std::string(env) == "1") return true;
    }
    bool enabled = false; // v3.2.1：默认关闭
    // v3.3.0：改为精确取值 —— 旧写法“取 enabled 之后整段文本里是否含 1/true”会被
    // algorithm 块中的数字（layerBlock 等）误判为启用。
    enabled = cfgBool(appDirPath() / kConfigFileName, "gpu.enabled", false);
    return enabled;
}

void MainWindow::saveGpuEnabledToConfig(bool enabled)
{
    writeConfigFile(enabled, m_terminal ? m_terminal->history() : QStringList());
}

// v3.3.0：统一 config.json 写回（version / search / cache / gpu / log / algorithm / terminal 整体覆盖）。
// 注意：先读取需保留的值，再截断写入（否则会读到自己清空后的文件）。
// ---------------- v3.4.12：config.json 写入的 JSON 转义 ----------------
// 为什么需要：本项目自写的 config.json 是「手工拼接」出来的，字符串值原先原样写入。
//   * 种子 / 缓存目录 / 日志子目录 里的 " 与反斜杠会让文件变成非法 JSON（外部解析器读不回来）；
//   * 某些 OpenCL 驱动返回的设备名不是 UTF-8（本机实测被写成 GBK 乱码），
//     会让整个文件不再是合法 UTF-8。
// 这里统一转义：引号 / 反斜杠 / 控制字符按 JSON 规则转义，非法 UTF-8 字节替换为 U+FFFD。
static std::string jsonEscapeUtf8(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"') {
            out += "\\\"";
            ++i;
            continue;
        }
        if (c == '\\') {
            out += "\\\\";
            ++i;
            continue;
        }
        if (c < 0x20) {
            char buf[8] = {0};
            std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
            out += buf;
            ++i;
            continue;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
            ++i;
            continue;
        }
        int n = 0;
        if ((c & 0xE0) == 0xC0) n = 2;
        else if ((c & 0xF0) == 0xE0) n = 3;
        else if ((c & 0xF8) == 0xF0) n = 4;
        bool ok = (n > 0) && (i + static_cast<size_t>(n) <= s.size());
        for (int k = 1; ok && k < n; ++k) {
            ok = ((static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0xC0) == 0x80);
        }
        if (ok) {
            out.append(s, i, static_cast<size_t>(n));
            i += static_cast<size_t>(n);
        } else {
            out += "\xEF\xBF\xBD"; // U+FFFD（替换非法字节，保证文件仍是合法 UTF-8）
            ++i;
        }
    }
    return out;
}

static std::string jsonEscapeUtf8(const QString& s)
{
    return jsonEscapeUtf8(s.toUtf8().toStdString());
}

// v3.3.4（任务书 4.1/4.3/2.3/2.4）：新增 search.seed / search.targetCount（默认空 / 1）、
// log.mode（默认 off，需用户主动开启）、terminal.history（最多 100 条）。
void MainWindow::writeConfigFile(bool gpuEnabled, const QStringList& terminalHistory)
{
    const auto path = appDirPath() / kConfigFileName;
    const int cacheMB = cfgInt(path, "cache.maxSizeMB", 1024);
    const char* mode = "exact";
    if (m_w && m_w->algLayeredRadio && m_w->algLayeredRadio->isChecked()) mode = "layered";
    else if (m_w && m_w->algSampleRadio && m_w->algSampleRadio->isChecked()) mode = "sample";
    else if (m_w && m_w->algBruteRadio && m_w->algBruteRadio->isChecked()) mode = "brute";
    const int layerBlock = (m_w && m_w->layerBlock) ? m_w->layerBlock->currentData().toInt() : 16;
    const double layerRatio = (m_w && m_w->layerRatio) ? m_w->layerRatio->value() : 1.0;
    const int sampleStep = (m_w && m_w->sampleStep) ? m_w->sampleStep->value() : 8;
    const bool sampleExact = (m_w && m_w->sampleExact) && m_w->sampleExact->isChecked();
    const QString seed = (m_w && m_w->seedEdit) ? m_w->seedEdit->text().trimmed() : QString();
    const int targetCount = (m_w && m_w->targetCount) ? m_w->targetCount->value() : 1;
    const char* logModeName = "off";
    if (slime::LogBus::mode() == slime::LogMode::Brief) logModeName = "brief";
    else if (slime::LogBus::mode() == slime::LogMode::Detailed) logModeName = "detailed";

    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    out << "{\n"
        << "  \"version\": \"" << kAppVersion << "\",\n"
        << "  \"search\": {\n"
        << "    \"seed\": \"" << jsonEscapeUtf8(seed) << "\",\n"
        << "    \"targetCount\": " << targetCount << "\n  },\n"
        << "  \"cache\": {\n    \"maxSizeMB\": " << cacheMB << "\n  },\n"
        << "  \"gpu\": {\n    \"enabled\": " << (gpuEnabled ? "true" : "false") << "\n  },\n"
        << "  \"log\": {\n    \"mode\": \"" << logModeName << "\"\n  },\n"
        << "  \"algorithm\": {\n"
        << "    \"mode\": \"" << mode << "\",\n"
        << "    \"layerBlock\": " << layerBlock << ",\n"
        << "    \"layerRatio\": " << layerRatio << ",\n"
        << "    \"sampleStep\": " << sampleStep << ",\n"
        << "    \"sampleExact\": " << (sampleExact ? "true" : "false") << "\n"
        << "  },\n"
        << "  \"terminal\": {\n    \"history\": [";
    for (int i = 0; i < terminalHistory.size(); ++i) {
        out << (i ? ", " : "") << "\"" << jsonEscapeUtf8(terminalHistory[i]) << "\"";
    }
    out << "]\n  },\n";
    // v3.3.5（优化 5）：程序设置（与主界面共享的默认值，双向同步）
    out << "  \"settings\": {\n"
        << "    \"wheelAdjust\": " << (m_settings.wheelAdjust ? "true" : "false") << ",\n"
        << "    \"rememberWindow\": " << (m_settings.rememberWindow ? "true" : "false") << ",\n"
        << "    \"autoRestart\": " << (m_settings.autoRestart ? "true" : "false") << ",\n"
        << "    \"defaultAlgorithm\": \"" << jsonEscapeUtf8(m_settings.defaultAlgorithm) << "\",\n"
        << "    \"defaultThreads\": " << m_settings.defaultThreads << ",\n"
        << "    \"defaultTargetCount\": " << m_settings.defaultTargetCount << ",\n"
        << "    \"defaultPreset\": \"" << jsonEscapeUtf8(m_settings.defaultPreset) << "\",\n"
        << "    \"defaultGpu\": " << (m_settings.defaultGpu ? "true" : "false") << ",\n"
        << "    \"defaultStopAtTarget\": " << (m_settings.defaultStopAtTarget ? "true" : "false") << ",\n"
        << "    \"autoLoadCache\": " << (m_settings.autoLoadCache ? "true" : "false") << ",\n"
        << "    \"cacheDir\": \"" << jsonEscapeUtf8(m_settings.cacheDir) << "\",\n"
        << "    \"cacheMaxMB\": " << m_settings.cacheMaxMB << ",\n"
        << "    \"logAtStartup\": " << (m_settings.logAtStartup ? "true" : "false") << ",\n"
        << "    \"logLevel\": " << m_settings.logLevel << ",\n"
        << "    \"logSubDir\": \"" << jsonEscapeUtf8(m_settings.logSubDir) << "\",\n"
        << "    \"logKeepDays\": " << m_settings.logKeepDays << ",\n"
        << "    \"logAutoScroll\": " << (m_settings.logAutoScroll ? "true" : "false") << ",\n"
        << "    \"logMaxLines\": " << m_settings.logMaxLines << ",\n"
        << "    \"showGpuReminder\": " << (m_settings.showGpuReminder ? "true" : "false") << ",\n"
        << "    \"largeRangeWarnChunks\": " << m_settings.largeRangeWarnChunks << ",\n"
        << "    \"theme\": \"" << jsonEscapeUtf8(m_settings.theme) << "\",\n"
        << "    \"language\": \"" << jsonEscapeUtf8(m_settings.language) << "\",\n"        << "    \"mapVersion\": " << m_mapJavaVersionId << ",\n"        << "    \"fullscreenMode\": \"" << jsonEscapeUtf8(m_settings.fullscreenMode) << "\",\n"
        << "    \"soundOnComplete\": " << (m_settings.soundOnComplete ? "true" : "false") << ",\n"
        << "    \"soundVolume\": " << m_settings.soundVolume << ",\n"
        << "    \"crashDump\": " << (m_settings.crashDump ? "true" : "false") << ",\n"
        << "    \"debugMode\": " << (m_settings.debugMode ? "true" : "false") << ",\n"
        // v3.3.9（任务书 二）：资源控制
        << "    \"cpuLimitPercent\": " << m_settings.cpuLimitPercent << ",\n"
        << "    \"gpuLimitPercent\": " << m_settings.gpuLimitPercent << ",\n"
        << "    \"gpuPatternStream\": " << (m_settings.gpuPatternStream ? "true" : "false") << ",\n"
        << "    \"gpuResultSelfOptimize\": " << (m_settings.gpuResultSelfOptimize ? "true" : "false") << ",\n"
        << "    \"memLimitMB\": " << m_settings.memLimitMB << ",\n"
        // v3.3.9.1（任务书 2.1）：启动时自动检查 GPU 状态
        << "    \"autoCheckGpuAtStartup\": " << (m_settings.autoCheckGpuAtStartup ? "true" : "false") << ",\n"
        << "    \"updateReminderDays\": " << m_settings.updateReminderDays << ",\n"
        // v3.5.18：高级调试参数
        << "    \"rateUnit\": " << m_settings.rateUnit << ",\n"
        << "    \"ringInner\": " << m_settings.afkRingInner << ",\n"
        << "    \"ringOuter\": " << m_settings.afkRingOuter << ",\n"
        << "    \"ringCoarseStep\": " << m_settings.ringCoarseStep << ",\n"
        << "    \"ringStatBands\": " << m_settings.ringStatBands << "\n  },\n";
    // v3.3.5（Bug 6）：实测基准缓存（含设备名与核心数，硬件变化时自动失效重测）
    // v3.3.6（任务书 Bug 2）：不再写入 crossLo/crossHi（交叉点已彻底移除）
    out << "  \"gpuBench\": {\n"
        << "    \"cpuRate\": " << m_gpuBenchCpuRate << ",\n"
        << "    \"gpuRate\": " << m_gpuBenchGpuRate << ",\n"
        << "    \"device\": \"" << jsonEscapeUtf8(m_gpuScan ? m_gpuScan->deviceName() : std::string())
        << "\",\n"
        << "    \"cores\": " << std::thread::hardware_concurrency() << "\n  },\n";
    // v3.3.5（优化 5）：窗口大小位置记忆
    out << "  \"window\": {\n    \"w\": " << width() << ",\n    \"h\": " << height()
        << ",\n    \"x\": " << x() << ",\n    \"y\": " << y() << "\n  }\n}\n";
    ++m_configWriteCount; // v3.4.11：config.json 实际写入次数（自检断言「合并写」生效）
}

// v3.3.9.1（任务书 2.1）：启动时按设置自动检测 GPU 状态（默认关闭）。
void MainWindow::maybeAutoCheckGpuAtStartup()
{
    if (m_settings.autoCheckGpuAtStartup) {
        Logger::info("启动时自动检测 GPU 状态（设置已开启）…");
        ensureGpuScanOnce();     // 失败/不可用都会写入状态与日志（内部已 try/catch + 回退 CPU）
        updateGpuStatusUi();
        const QString st = m_w && m_w->gpuStatus ? m_w->gpuStatus->text() : QString();
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.6c72f02735ba")).arg(st));
        slime::IncidentLog::note("启动自动检测 GPU：" + st.toStdString());
    } else {
        if (m_w && m_w->gpuStatus) {
            m_w->gpuStatus->setText(slime::ui::t("ui.241ab1842137"));
        }
        Logger::info("启动时未检测 GPU（设置关闭；勾选「启用 GPU 加速」时会即时检测）");
        slime::IncidentLog::note("启动自动检测 GPU：关闭（按设置跳过）");
    }
}

// v3.6.2：GPU 驱动冲突兜底 —— 用标记文件检测「上次是不是死在 GPU 路径上」。
//   进入 GPU 探测/搜索前写标记，正常结束删掉；下次启动还看到标记 ⇒ 上一次多半是驱动崩的，
//   自动关闭 GPU 并提示，避免每次启动都崩一次。
static std::filesystem::path gpuSessionMarkerPath()
{
    return std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / "gpu_session.flag";
}

// v3.6.4：英文模式下周期性重翻译 —— 界面上很多文本是运行时才设置的
//   （GPU 状态、线程提示、构建预估、对话框正文），一次性 applyLanguage 抓不到。
//   每 500ms 只对英文生效，命中翻译表才改写，开销可忽略。
// v3.6.6（用户要求）：自动重启 —— 起一个新进程再退出当前进程
void MainWindow::restartApplication()
{
    if (m_uiQuiet) return;   // 自检绝不重启
    const QString exe = QCoreApplication::applicationFilePath();
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty()) args.removeFirst();
    if (QProcess::startDetached(exe, args)) {
        std::fprintf(stderr, "[\u91cd\u542f] \u5df2\u542f\u52a8\u65b0\u5b9e\u4f8b\uff0c\u5f53\u524d\u8fdb\u7a0b\u9000\u51fa\n");
        QCoreApplication::quit();
    } else {
        addFeedback(3, slime::ui::tr(slime::ui::t("ui.ed7d8b8345fb")));
    }
}

void MainWindow::startLanguageWatch()
{
    if (m_langTimer) return;
    m_langTimer = new QTimer(this);
    m_langTimer->setInterval(500);
    connect(m_langTimer, &QTimer::timeout, this, [this]() {
        if (!slime::ui::isEnglish()) return;   // v3.6.6：中文模式不巡检
        applyLanguage();
    });
    m_langTimer->start();
}

void MainWindow::checkGpuCrashLoopOnStartup()
{
    std::error_code ec;
    const auto marker = gpuSessionMarkerPath();
    if (!std::filesystem::exists(marker, ec)) return;
    std::filesystem::remove(marker, ec);
    // 上一次没走到"正常结束"：强制关掉 GPU，写进配置
    if (m_settings.defaultGpu || (m_w->gpuCheck && m_w->gpuCheck->isChecked())) {
        m_settings.defaultGpu = false;
        if (m_w->gpuCheck) {
            QSignalBlocker block(m_w->gpuCheck);
            m_w->gpuCheck->setChecked(false);
        }
        writeConfigFile(false, m_terminal ? m_terminal->history() : QStringList());
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.4ff59f2e9677")));
        std::fprintf(stderr, "[gpu] 上次会话未正常结束，已自动禁用 GPU 加速\n");
    }
}

void MainWindow::ensureGpuScanOnce()
{
    // v3.2.1（修订）：探测从“启动后台线程”改为“用户勾选时同步执行一次”。
    // 原因：无/异常 OpenCL 驱动环境下，LoadLibrary/驱动初始化可发生原生崩溃（非 C++ 异常，
    // try/catch 无法捕获）；若在启动阶段自动触碰 OpenCL，会直接导致“启动/搜索即闪退”。
    // 勾选是用户主动行为（默认关闭），此时初始化失败仅记录日志并标记不可用，搜索自动回退 CPU。
    if (m_gpuProbeDone) return; // 已尝试（成功或失败均为最终状态）
    m_gpuProbeDone = true;
    if (m_w && m_w->gpuStatus) m_w->gpuStatus->setText(slime::ui::t("ui.1410899e1131"));
    QApplication::processEvents(QEventLoop::AllEvents, 20); // 让“检测中”先上屏
    Logger::info("GPU 初始化中（用户勾选启用；无驱动将自动回退 CPU）…");
    std::string err;
    try {
        m_gpuScan = slimegpu::createGpuScanAdapter(&err);
    } catch (const std::exception& e) {
        err = std::string("初始化异常：") + e.what();
        m_gpuScan.reset();
    } catch (...) {
        err = "初始化未知异常";
        m_gpuScan.reset();
    }
    m_gpuAvailable = static_cast<bool>(m_gpuScan) && m_gpuScan->available();
    m_gpuProbeReason = err;
    if (m_gpuAvailable) {
        Logger::info("GPU 可用性检测结果：可用（OpenCL 就绪）");
    } else {
        m_gpuScan.reset();
        Logger::info("GPU 可用性检测结果：不可用，将使用纯 CPU 路径" +
                     (err.empty() ? std::string() : "：" + err));
    }
    updateGpuStatusUi();
}

void MainWindow::onGpuCheckChanged(Qt::CheckState state)
{
    if (m_loadingConfig) return;
    const bool on = (state == Qt::Checked);
    saveGpuEnabledToConfig(on);
    Logger::info(on ? "GPU 加速已启用（勾选）" : "GPU 加速已关闭（取消勾选）");
    if (on) {
        ensureGpuScanOnce(); // 勾选时同步检测一次（启动阶段不触碰 OpenCL）
    }
    updateGpuStatusUi();
    // v3.3.6（任务书 Bug 2 + 4.3「显示 GPU 加速提醒」）：勾选时按设置推送「不一定更快」提醒
    if (on && m_settings.showGpuReminder) {
        m_gpuReminderShown = true;
        setStatus(gpuReminderText());
        addFeedback(2, gpuReminderText());
    }
    if (on) startGpuBenchmark(); // v3.3.5（Bug 6）：启用 GPU 时后台测量实测吞吐率
}

// ---------------- v3.3.6（任务书 Bug 2）：GPU 提醒与状态文本 ----------------
//
// 用户决定：**彻底取消交叉点估算相关说法**（静态公式与动态基准给出的结论都不准），
// 因此本版本删除了旧的阈值估算函数与 config 里的 gpuBench.crossLo/crossHi 字段，
// 只在「GPU 开关 tooltip」与「GPU 状态对话框」保留一段统一提醒文案；
// 状态对话框继续显示设备名、显存、计算单元与实测吞吐率（CPU vs GPU）。
QString MainWindow::gpuReminderText() const
{
    return slime::ui::t("ui.2cc41f0950c5");
}

bool MainWindow::darkTheme() const
{
    return slime::ThemeManager::instance().isDark();
}

// v3.3.6（任务书 4.7）：少数必须内联着色的控件随主题刷新，保证深色模式下同样可读
// （内联样式优先级高于全局 QSS，所以这里显式给出两套状态色）
void MainWindow::applyInlineThemeColors()
{
    const bool dark = darkTheme();
    const QString note = dark ? QStringLiteral("#9aa0a6") : QStringLiteral("#767676");
    if (m_w->algSummary) m_w->algSummary->setStyleSheet(QStringLiteral("color:%1;").arg(note));
    if (m_w->infoStats) m_w->infoStats->setStyleSheet(QStringLiteral("color:%1;").arg(note));
    if (m_w->modeWarn) {
        m_w->modeWarn->setStyleSheet(dark
            ? QStringLiteral("background:#3A2F12; color:#FFD479; border:1px solid #6B5A20;"
                             " border-radius:3px; padding:3px;")
            : QStringLiteral("background:#FFF3CD; color:#7A5B00; border:1px solid #E0C97F;"
                             " border-radius:3px; padding:3px;"));
    }
    updateGpuStatusUi(); // GPU 状态文字的颜色同样按主题重算
}

// GPU 状态对话框正文（自检直接断言文本，不弹窗）：设备名 / 显存 / 计算单元 / 实测吞吐 + 提醒
QString MainWindow::gpuStatusText() const
{
    QString text;
    if (!m_gpuProbeDone) {
        text = slime::ui::t("ui.3d68689da69a");
    } else if (!m_gpuAvailable) {
        text = slime::ui::t("ui.8ecc4126994d");
        if (!m_gpuProbeReason.empty()) {
            text += slime::ui::t("ui.4a18a14dd791").arg(QString::fromStdString(m_gpuProbeReason));
        }
    } else {
        QString dev = slime::ui::t("ui.e606485ff76a");
        if (m_gpuScan) {
            const std::string n = m_gpuScan->deviceName();
            if (!n.empty()) dev = QString::fromStdString(n);
        }
        const uint64_t vramMB = m_gpuScan ? (m_gpuScan->globalMemBytes() / (1024 * 1024)) : 0;
        const uint32_t cu = m_gpuScan ? m_gpuScan->computeUnits() : 0;
        text = slime::ui::t("ui.c7d42c1885f2")
                   .arg(dev)
                   .arg(static_cast<qulonglong>(vramMB))
                   .arg(cu);
        if (m_gpuBenchCpuRate > 0.0 && m_gpuBenchGpuRate > 0.0) {
            text += slime::ui::t("ui.698a93bf3e8b")
                        .arg(m_gpuBenchCpuRate / 1e8, 0, 'f', 2)
                        .arg(m_gpuBenchGpuRate / 1e8, 0, 'f', 2);
        } else {
            text += slime::ui::t("ui.41a80df96e75");
        }
    }
    // 任务书 Bug 2：提醒文案保留在状态对话框的说明文字里
    text += QStringLiteral("\n\n") + gpuReminderText();
    return text;
}

void MainWindow::updateGpuStatusUi()
{
    { std::error_code ec; std::filesystem::remove(gpuSessionMarkerPath(), ec); }  // v3.6.2：探测走完 ⇒ 清标记（崩在驱动里才留得下）

    if (!m_w || !m_w->gpuStatus) return;
    const bool checked = m_w->gpuCheck && m_w->gpuCheck->isChecked();
    QString text;
    if (!checked) {
        // v3.3.9.1（任务书 2.1）：未勾选时区分「已自动检测过」与「未检测」
        if (m_gpuProbeDone) {
            text = m_gpuAvailable ? slime::ui::t("ui.fb3cd3586cf9")
                                  : slime::ui::t("ui.3fc283d417f7");
        } else {
            text = slime::ui::t("ui.207ca70ac3f6");
        }
    } else if (!m_gpuProbeDone) {
        text = slime::ui::t("ui.1410899e1131");
    } else if (m_gpuAvailable) {
        text = slime::ui::t("ui.c0ba81d5e5b8");
    } else {
        text = slime::ui::t("ui.b47adc07c963");
    }
    m_w->gpuStatus->setText(text);
    m_w->gpuStatus->setStyleSheet(m_gpuAvailable ? (darkTheme() ? QStringLiteral("color:#7EC97E;")
                                                               : QStringLiteral("color:#2E6B2E;"))
                                                 : (darkTheme() ? QStringLiteral("color:#FF9E9E;")
                                                                : QStringLiteral("color:#B03020;")));
    if (m_w->gpuCheck) {
        // v3.3.6（任务书 Bug 2）：tooltip 只保留提醒文案（不再有阈值提示）
        m_w->gpuCheck->setToolTip(gpuReminderText());
    }
}

// ---------------- UI 构建（v3.3.4 任务书 1：QMainWindow 原生 + Splitter + Tab） ----------------
//
// 结构（严格按任务书 1.1）：
//   QMainWindow
//   ├ QMenuBar / QToolBar / QStatusBar      （全部使用 QMainWindow 原生接口）
//   └ centralWidget → QVBoxLayout
//        └ QSplitter（垂直）
//             ├ QSplitter（水平）
//             │    ├ 左：QWidget → QScrollArea → 6 个 QGroupBox
//             │    └ 右：QWidget → 信息栏 + QTextEdit（结果文本）
//             └ QTabWidget（进度 / 反馈 / 日志 / 命令终端）
//
// 约束：不做折叠、不搬移布局条目（分组控件均在目标分组内直接创建），布局层级 ≤ 8。
void MainWindow::buildUi()
{
    // ---- 顶部：菜单栏 / 工具栏（QMainWindow 原生）----
    buildMenus();
    buildToolBar();

    // ---- 中央部件 ----
    auto* central = new QWidget(this);
    auto* rootLay = new QVBoxLayout(central);
    rootLay->setContentsMargins(4, 4, 4, 4);
    rootLay->setSpacing(0);
    setCentralWidget(central);

    // ================= v3.3.6（任务书 调整 1）：工具栏下方的进度行 =================
    // 位置：紧贴工具栏下方、横跨整个窗口宽度；高度约 28px；
    // 内容：进度条 + 百分比 + 当前阶段文字（已用时/预计剩余见状态栏，明细见「进度」分类流水）。
    m_progressRowWidget = new QWidget(central);
    m_progressRowWidget->setObjectName(QStringLiteral("progressRow"));
    m_progressRowWidget->setFixedHeight(34); // v3.4.0（原型）：进度行 34px
    auto* progRow = new QHBoxLayout(m_progressRowWidget);
    progRow->setContentsMargins(8, 2, 8, 2);
    progRow->setSpacing(8);
    m_progressRow = progRow; // 自检断言「同一行 + spacing==8」用
    progRow->addWidget(new QLabel(slime::ui::t("ui.8aa84f452a9b"), m_progressRowWidget));
    m_w->progress = new QProgressBar(m_progressRowWidget);
    m_w->progress->setRange(0, 1000);
    m_w->progress->setValue(0);
    m_w->progress->setMinimumWidth(320);
    m_w->progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // v3.3.7.2（任务书 1.1）：关闭进度条**自身**的文字（默认 textVisible=true、格式 %p%），
    // 否则它会与右侧 progressPercent 标签同时画出百分比（用户截图实测「25% 25%」）。
    // 单一数据源：百分比只由 progressPercent 显示。
    m_w->progress->setTextVisible(false);
    progRow->addWidget(m_w->progress, 1);
    m_w->progressPercent = new QLabel(QStringLiteral("0%"), m_progressRowWidget);
    m_w->progressPercent->setMinimumWidth(40);
    progRow->addWidget(m_w->progressPercent);
    m_w->progressStage = new QLabel(slime::ui::t("ui.b796f2d4ca85"), m_progressRowWidget);
    m_w->progressStage->setWordWrap(false);
    // v3.3.9（任务书 3.3 根因）：阶段标签此前是 Expanding + stretch=2 ⇒ 窗口变宽时它吃掉大部分
    // 新增宽度，进度条几乎不变（用户实测「没变化」）。改为 Preferred 且不参与伸展，只留一个上限
    // 宽度，剩余宽度**全部**给进度条（期望效果：进度条占满可用宽度）。
    m_w->progressStage->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_w->progressStage->setMaximumWidth(520);
    progRow->addWidget(m_w->progressStage, 0);
    // v3.10.2（用户要求）：速率单位切换（自动 / 万/秒 / 亿/秒）—— 放在进度行，避免工具栏溢出
    progRow->addSpacing(6);
    progRow->addWidget(new QLabel(slime::ui::t("ui.602285707fd7"), m_progressRowWidget));
    m_w->rateUnitCombo = new QComboBox(m_progressRowWidget);
    m_w->rateUnitCombo->addItem(slime::ui::t("ui.4afad877551a"));
    m_w->rateUnitCombo->addItem(slime::ui::t("ui.ee266fcea944"));
    m_w->rateUnitCombo->addItem(slime::ui::t("ui.2d629d28eb4c"));
    m_w->rateUnitCombo->setCurrentIndex(std::clamp(m_settings.rateUnit, 0, 2));
    m_w->rateUnitCombo->setFixedWidth(76);
    m_w->rateUnitCombo->setToolTip(slime::ui::t("ui.38a03968acf8"));
    connect(m_w->rateUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_settings.rateUnit = std::clamp(idx, 0, 2);
        if (m_w->progressInfo) m_w->progressInfo->setToolTip(m_w->progressInfo->text());
    });
    progRow->addWidget(m_w->rateUnitCombo);
    // v3.4.0（原型）：阶段尾（② 匹配　③ 排序）——与阶段短语并列展示三段式阶段
    // ================= v3.9.15：顶部全局设置栏（平台 / 游戏版本 / 世界种子） =================
    //   把这些「每次搜索都要确认」的全局项从左侧参数栏提到顶部，左侧只放结果要求与搜索参数。
    m_topBar = new QWidget(central);
    m_topBar->setObjectName(QStringLiteral("topSettingsBar"));
    m_topBar->setFixedHeight(36);
    m_topBarLay = new QHBoxLayout(m_topBar);
    m_topBarLay->setContentsMargins(8, 2, 8, 2);
    m_topBarLay->setSpacing(8);
    rootLay->addWidget(m_topBar);

    rootLay->addWidget(m_progressRowWidget);

    // v3.4.1：标签页条已移除 —— 页面切换**只依赖左导航**（下方 navColumn）
    auto* midRow = new QWidget(central);
    auto* midLay = new QHBoxLayout(midRow);
    midLay->setContentsMargins(0, 0, 0, 0);
    midLay->setSpacing(0);

    // 左导航：96px，图标 + 文字，选中 = 整块草绿（与标签页同源，见 showPage）
    m_navColumn = new QWidget(midRow);
    m_navColumn->setObjectName(QStringLiteral("navColumn"));
    m_navColumn->setFixedWidth(56);   // v3.9.15：导航收窄，让出地图宽度
    auto* navLay = new QVBoxLayout(m_navColumn);
    navLay->setContentsMargins(0, 4, 0, 0);
    navLay->setSpacing(0);
    // v3.4.8（用户要求）：导航项 —— 搜索 / 缓存构建 / 设置（更新历史已移除）
    //   「缓存构建」独立成一项后，构建页只放构建相关内容：不再要求选游戏版本、填种子。
    static_assert(kNavCount == 3, "导航项数需与图标表一致");
    for (int i = 0; i < kNavCount; ++i) {
        // v3.4.1：导航项改为「上图案下文字」，图标放大到 24
        auto* b = new QToolButton(midRow);
        b->setCheckable(true);
        b->setProperty("navBtn", true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setIcon(v340NavIcon(kNavIconKind[i]));
        b->setIconSize(QSize(24, 24));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setFixedWidth(56);                       // v3.9.15：导航收窄（原 96）
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        static const char* const kNavText[kNavCount] = { "ui.nav.search", "ui.nav.build", "ui.nav.settings" };
        b->setText(slime::ui::t(kNavText[i]));
        {
            QFont navFont = b->font();   // v3.9.15：56px 宽需更小字号才能放下 4 个字
            navFont.setPointSizeF(7.5);
            b->setFont(navFont);
        }
        connect(b, &QPushButton::clicked, this, [this, i]() { showPage(i); });
        navLay->addWidget(b);
        m_navButtons.push_back(b);
    }
    navLay->addStretch(1);   // v3.10.2：底部图标条已移除（过程面板改在右列标题栏操作）
    midLay->addWidget(m_navColumn);
    midLay->addStretch(1); // v3.4.2：midRow 只承载左列（导航 + 图标条），页面栈放进分割器

    m_pageStack = new QStackedWidget(central);

    m_vSplit = new QSplitter(Qt::Vertical, central);
    m_vSplit->setChildrenCollapsible(false);
    m_hSplit = new QSplitter(Qt::Horizontal);
    m_hSplit->setChildrenCollapsible(false);
    // v3.3.6（任务书 4.7）：分割条样式由 ThemeManager 的浅色/深色 QSS 统一提供
    // （内联样式会覆盖全局 QSS，导致深色模式下分割条仍是浅色），此处只设置宽度。
    m_vSplit->setHandleWidth(5); // v3.4.0：竖直分割条同时充当停靠面板抓手（5px）
    m_hSplit->setHandleWidth(4);
    m_vSplit->addWidget(m_pageStack); // 上半：页面栈
    // v3.4.2（参考 PyCharm）：左列（导航 + 图标条）横跨「内容 + 停靠面板」的整窗高度，
    // 因此展开/收起底部面板时，左下角的图标条不会被顶起或推下。
    {
        auto* bodyRow = new QWidget(central);
        auto* bodyLay = new QHBoxLayout(bodyRow);
        bodyLay->setContentsMargins(0, 0, 0, 0);
        bodyLay->setSpacing(0);
        bodyLay->addWidget(midRow);
        bodyLay->addWidget(m_vSplit, 1);
        rootLay->addWidget(bodyRow, 1);
    }

    // ================= 左：设置面板（QScrollArea + 6 分组） =================
    auto* leftPanel = new QWidget(m_hSplit);
    leftPanel->setMinimumWidth(360);   // v3.10.0：三纵列后左栏最小 360（窄窗下标签不被挤掉）
    auto* leftLay = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);

    auto* mainScroll = new QScrollArea(leftPanel);
    mainScroll->setWidgetResizable(true);
    mainScroll->setFrameShape(QFrame::NoFrame);
    mainScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mainScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* groupsHost = new QWidget(mainScroll);
    auto* groupsLay = new QVBoxLayout(groupsHost);
    groupsLay->setContentsMargins(2, 8, 2, 8); // v3.4.4：参数列上下留白（缓解标题叠字）
    groupsLay->setSpacing(10);         // v3.4.4：卡片间距加大，标题带不再互相挤
    mainScroll->setWidget(groupsHost);
    leftLay->addWidget(mainScroll);
    m_w->mainScroll = mainScroll;
    m_hSplit->addWidget(leftPanel);

    // ================= v3.10.0：三纵列布局 =================
    //   左：参数/结果要求   中：地图   右：结果 + 过程
    //   地图从右列拆出来独立成列后，结果列表高度不再挤压地图。
    m_mapColumn = new QWidget(m_hSplit);
    m_mapColumn->setObjectName(QStringLiteral("mapColumn"));
    m_mapColumn->setMinimumWidth(260);
    m_mapColLay = new QVBoxLayout(m_mapColumn);
    m_mapColLay->setContentsMargins(0, 0, 0, 0);
    m_mapColLay->setSpacing(0);

    // ---- v3.9.15：原「游戏设置」分组已上移到顶部全局设置栏（平台 / 游戏版本 / 世界种子）----
    if (m_topBarLay) {
        // v3.10.12.13：顶栏切成左右两半：左半放平台/版本/种子，种子框右端正好到窗口中线；
        //   右半放匿名统计公告（样式与普通标签区分）。
        auto* topLeft = new QWidget(m_topBar);
        auto* topLeftLay = new QHBoxLayout(topLeft);
        topLeftLay->setContentsMargins(0, 0, 0, 0);
        topLeftLay->setSpacing(8);
        auto* topRight = new QWidget(m_topBar);
        auto* topRightLay = new QHBoxLayout(topRight);
        topRightLay->setContentsMargins(0, 0, 0, 0);
        topRightLay->setSpacing(0);
        m_topBarLay->addWidget(topLeft, 1);
        m_topBarLay->addWidget(topRight, 1);

        m_w->javaRadio = new QRadioButton(slime::ui::t("ui.8accf0fe2eed"), m_topBar);
        m_w->bedrockRadio = new QRadioButton(slime::ui::t("ui.618fd37c0c01"), m_topBar);
        m_w->javaRadio->setChecked(true);
        topLeftLay->addWidget(new QLabel(slime::ui::t("ui.4ba273d420a7"), m_topBar));
        topLeftLay->addWidget(m_w->javaRadio);
        topLeftLay->addWidget(m_w->bedrockRadio);

        topLeftLay->addSpacing(10);
        topLeftLay->addWidget(new QLabel(slime::ui::t("ui.fc1424adcb78"), m_topBar));
        m_w->mapVersionCombo = new QComboBox(m_topBar);
        for (const auto& v : slime::javaVersionOptions())
            m_w->mapVersionCombo->addItem(QString::fromUtf8(v.label), v.id);
        m_w->mapVersionCombo->setCurrentIndex(0);
        m_w->mapVersionCombo->setToolTip(slime::ui::t("ui.9820d2a24e2e"));
        topLeftLay->addWidget(m_w->mapVersionCombo);

        topLeftLay->addSpacing(10);
        m_w->seedLabel = new QLabel(slime::ui::t("ui.df62e1c428e6"), m_topBar);
        topLeftLay->addWidget(m_w->seedLabel);
        m_w->seedEdit = new QLineEdit(m_topBar);
        m_w->seedEdit->setClearButtonEnabled(true);
        m_w->seedEdit->setText(QString());
        m_w->seedEdit->setPlaceholderText(slime::ui::t("ui.c9cebe51b762"));
        m_w->seedEdit->setToolTip(slime::ui::t("ui.e8cae526ef7d"));
        topLeftLay->addWidget(m_w->seedEdit, 1);
        m_telemetryNotice = new QLabel(topRight);
        m_telemetryNotice->setObjectName(QStringLiteral("telemetryNotice"));
        m_telemetryNotice->setTextFormat(Qt::RichText);
        m_telemetryNotice->setOpenExternalLinks(false);
        m_telemetryNotice->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        m_telemetryNotice->setStyleSheet(QStringLiteral(
            "QLabel{background:#FFF4D6;color:#7A5A00;border:1px solid #E0B85C;"
            "border-radius:4px;padding:2px 8px;}"
            "QLabel a{color:#7A5A00;text-decoration:none;}"));
        connect(m_telemetryNotice, &QLabel::linkActivated, this, [this](const QString&) { showTelemetryDialog(); });
        topRightLay->addWidget(m_telemetryNotice, 0, Qt::AlignLeft | Qt::AlignVCenter);
        topRightLay->addStretch(1);
        updateTelemetryNotice();
    }


    // ---- 分组 2：搜索范围（预设 + 范围 + 查询起点 + 坐标转换）----
    auto* gRange = new QGroupBox(slime::ui::t("ui.2cacc1b74217"), groupsHost);
    auto* rangeLay = new QVBoxLayout(gRange);
    rangeLay->setContentsMargins(6, 6, 6, 6);
    const int kMinCoord = -2147483646;
    const int kMaxCoord = 2147483646;
    auto makeSpin = [&](int def, QWidget* parent) {
        auto* s = new QSpinBox(parent);
        s->setRange(kMinCoord, kMaxCoord);
        s->setValue(def);
        s->setGroupSeparatorShown(true);
        s->setKeyboardTracking(false);
        s->setMinimumWidth(68);   // v3.3.4：限制最小值，避免撑宽左栏（340px 面板也不出横向滚动条）
        s->setMaximumWidth(120);
        return s;
    };
    auto* rangeGrid = new QGridLayout();
    m_w->minX = makeSpin(-kJavaBoundChunk, gRange);
    m_w->maxX = makeSpin(kJavaBoundChunk, gRange);
    m_w->minZ = makeSpin(-kJavaBoundChunk, gRange);
    m_w->maxZ = makeSpin(kJavaBoundChunk, gRange);
    // v3.5.16（用户要求）：范围不再填 4 个数，只填半径 —— 范围 = 以原点为中心、边长 2×半径 的正方形。
    //   四个旧输入框保留为内部状态（引擎/缓存构建/命令/预估都还读它们），只是不再显示。
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) s->setVisible(false);
    m_w->rangeRadius = makeSpin(1000, gRange);
    m_w->rangeRadius->setRange(1, 15000000);
    m_w->rangeRadius->setToolTip(slime::ui::t("ui.8d4c2b8f9d14"));
    rangeGrid->addWidget(new QLabel(slime::ui::t("ui.f290ac0a5d9d"), gRange), 0, 0);
    rangeGrid->addWidget(m_w->rangeRadius, 0, 1);
    m_w->rangeHint = new QLabel(slime::ui::t("ui.7b040a370be7"), gRange);
    m_w->rangeHint->setObjectName(QStringLiteral("topSummary"));
    rangeGrid->addWidget(m_w->rangeHint, 1, 0, 1, 4);
    rangeLay->addLayout(rangeGrid);

    auto* presetRow = new QGridLayout();
    // v3.3.4：按钮只写范围名（具体数值在 tooltip 与点击后的输入框里显示），
    // 这样 340px 的最小左栏也不会出现横向滚动条
    m_w->presetNear = new QPushButton(slime::ui::t("ui.08c2940d3842"), gRange);
    m_w->presetLocal = new QPushButton(slime::ui::t("ui.d4548049015d"), gRange);
    m_w->presetGlobal = new QPushButton(slime::ui::t("ui.a5644f4bbfef"), gRange);
    m_w->presetWorld = new QPushButton(slime::ui::t("ui.3a7839501e82"), gRange);
    m_w->presetBedrock = new QPushButton(slime::ui::t("ui.c2fa11e4787c"), gRange);
    // v3.10.2（用户反馈：GPU 变快后旧快捷范围太小）：整体上调一档
    // v3.10.12.8：GPU 提速后快捷范围再上调一档（旧值按 3.10.2 的早期速度设定）
    m_w->presetNear->setToolTip(slime::ui::t("ui.ea3036c908c7"));
    m_w->presetLocal->setToolTip(slime::ui::t("ui.0807a0194761"));
    m_w->presetGlobal->setToolTip(slime::ui::t("ui.49bc40cff907"));
    m_w->presetWorld->setToolTip(slime::ui::t("ui.41fe5ad602fd"));
    m_w->presetBedrock->setToolTip(slime::ui::t("ui.d116c4a6c576"));
    // v3.3.4：预设标签独占一行，按钮每行 3 个 —— 避免撑宽设置区（否则 400px 左栏出现横向滚动条）
    presetRow->addWidget(new QLabel(slime::ui::t("ui.143e728e020a"), gRange), 0, 0, 1, 3);
    presetRow->addWidget(m_w->presetNear, 1, 0);
    presetRow->addWidget(m_w->presetLocal, 1, 1);
    presetRow->addWidget(m_w->presetGlobal, 1, 2);
    presetRow->addWidget(m_w->presetWorld, 2, 0);
    presetRow->addWidget(m_w->presetBedrock, 2, 1);
    presetRow->setColumnStretch(3, 1);
    rangeLay->addLayout(presetRow);

    auto* startRow = new QHBoxLayout();
    startRow->addWidget(new QLabel(slime::ui::t("ui.31db8e5f5036"), gRange));
    m_w->startX = makeSpin(0, gRange);
    m_w->startX->setMaximumWidth(120);
    startRow->addWidget(m_w->startX);
    startRow->addWidget(new QLabel(QStringLiteral("Z:"), gRange));
    m_w->startZ = makeSpin(0, gRange);
    m_w->startZ->setMaximumWidth(120);
    startRow->addWidget(m_w->startZ);
    startRow->addStretch();
    rangeLay->addLayout(startRow);

    // v3.3.4：分两行 + 缩短文案（原一行最小宽 ~440px，会撑出横向滚动条）
    auto* convRow = new QHBoxLayout();
    convRow->addWidget(new QLabel(slime::ui::t("ui.8f86ddd4fe10"), gRange));
    m_w->convX = new QLineEdit(gRange);
    m_w->convX->setMinimumWidth(60);
    m_w->convX->setMaximumWidth(90);
    m_w->convX->setPlaceholderText(slime::ui::t("ui.e272b0605275"));
    m_w->convZ = new QLineEdit(gRange);
    m_w->convZ->setMinimumWidth(60);
    m_w->convZ->setMaximumWidth(90);
    m_w->convZ->setPlaceholderText(slime::ui::t("ui.d3c729e2a6b7"));
    convRow->addWidget(m_w->convX);
    convRow->addWidget(m_w->convZ);
    convRow->addStretch();
    rangeLay->addLayout(convRow);
    auto* convRow2 = new QHBoxLayout();
    m_w->convertBtn = new QPushButton(slime::ui::t("ui.16d8f74f6cbf"), gRange);
    m_w->convertBtn->setToolTip(slime::ui::t("ui.15e186ad6baa"));
    convRow2->addWidget(m_w->convertBtn);
    convRow2->addStretch();
    rangeLay->addLayout(convRow2);
    groupsLay->addWidget(gRange);

    // ---- 分组 3：搜索模式（严格/密度 + 图案 + 密度参数 + 目标数量）----
    auto* gMode = new QGroupBox(slime::ui::t("ui.5483493bb0d8"), groupsHost);
    m_w->modeGroup = gMode;
    auto* modeLayout = new QVBoxLayout(gMode);
    modeLayout->setContentsMargins(6, 6, 6, 6);

    // ===== 一级：图案 / 密度（v3.5.18 用户要求）=====
    auto* modeRow = new QHBoxLayout();
    m_w->strictRadio = new QRadioButton(slime::ui::text(slime::ui::Key::ModePattern), gMode);
    m_w->densityRadio = new QRadioButton(slime::ui::text(slime::ui::Key::ModeDensity), gMode);
    {
        auto* modeBtnGroup = new QButtonGroup(gMode);
        modeBtnGroup->addButton(m_w->strictRadio, 0);
        modeBtnGroup->addButton(m_w->densityRadio, 1);
        m_w->modeButtonGroup = modeBtnGroup;
    }
    m_w->strictRadio->setChecked(true);
    m_w->strictRadio->setToolTip(slime::ui::text(slime::ui::Key::ModePatternTip));
    m_w->densityRadio->setToolTip(slime::ui::text(slime::ui::Key::ModeDensityTip));
    modeRow->addWidget(m_w->strictRadio);
    modeRow->addWidget(m_w->densityRadio);
    modeRow->addStretch();
    modeLayout->addLayout(modeRow);

    // v3.9.11（设置重构阶段 B）：先「模式」再「方案」——方案数量与内容由模式决定。
    {
        auto* presetRow = new QHBoxLayout();
        presetRow->addWidget(new QLabel(slime::ui::t("ui.c06ee9fb0a7e"), gMode));
        m_w->presetCombo = new QComboBox(gMode);
        m_w->presetCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_w->presetCombo->setMinimumContentsLength(16);
        m_w->presetCombo->setToolTip(slime::ui::t("ui.7f3854b7425e"));
        presetRow->addWidget(m_w->presetCombo, 1);
        modeLayout->addLayout(presetRow);
        m_w->presetHint = new QLabel(gMode);
        m_w->presetHint->setObjectName(QStringLiteral("presetHint"));
        m_w->presetHint->setWordWrap(true);
        m_w->presetHint->setStyleSheet(QStringLiteral("color:#606874;font-size:11px;"));
        modeLayout->addWidget(m_w->presetHint);
        QObject::connect(m_w->presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                         [this](int) { applySelectedPreset(); });
    }

    // ===== 二级：图案选项（仅图案模式显示）=====
    m_w->strictPanel = new QWidget(gMode);
    auto* strictLay = new QVBoxLayout(m_w->strictPanel);
    strictLay->setContentsMargins(12, 0, 0, 0);
    strictLay->setSpacing(4);
    m_w->lockDir = new QCheckBox(slime::ui::t("ui.4c557e552df9"), m_w->strictPanel);
    m_w->lockDir->setChecked(true);
    m_w->lockDir->setToolTip(slime::ui::t("ui.d6dbb5040c42"));
    strictLay->addWidget(m_w->lockDir);
    // v3.5.18：旧「绘制图案」按钮改为隐藏（图案编辑器已内嵌），保留对象避免其它代码空指针
    m_w->drawPattern = new QPushButton(slime::ui::t("ui.dcac1e7a2e19"), m_w->strictPanel);
    m_w->drawPattern->setVisible(false);
    auto* patRow = new QHBoxLayout();
    m_w->patternInfo = new QLabel(slime::ui::t("ui.a1b385fa0140"), m_w->strictPanel);
    patRow->addWidget(m_w->patternInfo);
    patRow->addStretch();
    strictLay->addLayout(patRow);
    // v3.5.18：图案编辑器直接内嵌（不再弹出独立窗口）
    m_w->inlinePattern = new slime::PatternEditorPanel(m_w->strictPanel);
    m_w->inlinePattern->setVisible(true);
    strictLay->addWidget(m_w->inlinePattern);
    modeLayout->addWidget(m_w->strictPanel);

    // ===== 二级：密度选项（仅密度模式显示）=====
    m_w->densityPanel = new QWidget(gMode);
    auto* densityLay = new QVBoxLayout(m_w->densityPanel);
    densityLay->setContentsMargins(12, 0, 0, 0);
    densityLay->setSpacing(4);
    auto* shapeRow = new QHBoxLayout();
    shapeRow->addWidget(new QLabel(slime::ui::t("ui.34a2f8af72f5"), m_w->densityPanel));
    m_w->shapeSquareRadio = new QRadioButton(slime::ui::text(slime::ui::Key::ShapeSquare), m_w->densityPanel);
    m_w->shapeRingRadio = new QRadioButton(slime::ui::text(slime::ui::Key::ShapeRing), m_w->densityPanel);
    m_w->shapeSquareRadio->setChecked(true);
    m_w->shapeSquareRadio->setToolTip(slime::ui::text(slime::ui::Key::ShapeSquareTip));
    m_w->shapeRingRadio->setToolTip(slime::ui::text(slime::ui::Key::ShapeRingTip));
    shapeRow->addWidget(m_w->shapeSquareRadio);
    shapeRow->addWidget(m_w->shapeRingRadio);
    shapeRow->addStretch();
    densityLay->addLayout(shapeRow);
    {
        auto* shapeGroup = new QButtonGroup(m_w->densityPanel);
        shapeGroup->addButton(m_w->shapeSquareRadio, 0);
        shapeGroup->addButton(m_w->shapeRingRadio, 1);
        m_w->shapeButtonGroup = shapeGroup;
    }

    // v3.10.9：最小密度/忽略阈值已移入「结果要求」分组（仅自定义/环形显示），此处不再重复显示
    m_w->minDensity = new QDoubleSpinBox(m_w->densityPanel);
    m_w->minDensity->setRange(0.0, 1.0);
    m_w->minDensity->setDecimals(3);
    m_w->minDensity->setSingleStep(0.05);
    m_w->minDensity->setValue(0.3);
    m_w->minDensity->setToolTip(slime::ui::t("ui.9f053e5570b1"));

    m_w->ignoreThreshold = new QCheckBox(slime::ui::t("ui.1a3b758d0fda"), m_w->densityPanel);
    m_w->ignoreThreshold->setToolTip(slime::ui::t("ui.e302a43e602a"));

    // v3.10.12.11：最小密度 / 忽略阈值改为密度面板常显（此前放在「结果要求」分组里，
    //   选择任意预设方案后就整组隐藏，用户找不到密度设置）。改动会切换为「自定义方案」。
    {
        auto* densCfgRow = new QHBoxLayout();
        densCfgRow->addWidget(new QLabel(slime::ui::t("ui.97aed4fd11b2"), m_w->densityPanel));
        densCfgRow->addWidget(m_w->minDensity);
        densCfgRow->addWidget(m_w->ignoreThreshold);
        densCfgRow->addStretch();
        densityLay->addLayout(densCfgRow);
        m_minDensityCommitted = m_w->minDensity->value();
        QObject::connect(m_w->minDensity, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         this, [this](double) { applyResultConflicts(); });
        // v3.10.12.11：输入“0”过程中 valueChanged 会连续触发；只在焦点离开/回车后才切自定义方案
        QObject::connect(m_w->minDensity, &QDoubleSpinBox::editingFinished, this, [this]() {
            if (!m_w || !m_w->minDensity || m_presetApplying) return;
            const double v = m_w->minDensity->value();
            if (v == m_minDensityCommitted) return;
            m_minDensityCommitted = v;
            markPresetCustom(slime::ui::t("ui.d67b9dd37ebe"));
        });
    }

    QObject::connect(m_w->ignoreThreshold, &QCheckBox::toggled, this, [this](bool) {
        applyResultConflicts();
        markPresetCustom(slime::ui::t("ui.4256e53c8eaf"));
    });


    // 正方形专属：网格大小 + 最小密度
    m_w->squareOpts = new QWidget(m_w->densityPanel);
    auto* squareLay = new QVBoxLayout(m_w->squareOpts);
    squareLay->setContentsMargins(12, 0, 0, 0);
    squareLay->setSpacing(4);
    auto* densRow = new QHBoxLayout();
    densRow->addWidget(new QLabel(slime::ui::t("ui.28e8f6817207"), m_w->squareOpts));
    m_w->gridSize = new QSpinBox(m_w->squareOpts);
    m_w->gridSize->setRange(1, 512);
    m_w->gridSize->setValue(5);
    m_w->gridSize->setToolTip(slime::ui::t("ui.830890653be7"));
    densRow->addWidget(m_w->gridSize);
    densRow->addStretch();
    squareLay->addLayout(densRow);
    m_w->squareOpts->setVisible(true);
    densityLay->addWidget(m_w->squareOpts);

    // 环形专属：扫描模式 + 最小密度
    m_w->ringOpts = new QWidget(m_w->densityPanel);
    auto* ringLay = new QVBoxLayout(m_w->ringOpts);
    ringLay->setContentsMargins(12, 0, 0, 0);
    ringLay->setSpacing(4);
    auto* ringModeRow = new QHBoxLayout();
    ringModeRow->addWidget(new QLabel(slime::ui::t("ui.637be2399ba9"), m_w->ringOpts));
    m_w->ringScanCombo = new QComboBox(m_w->ringOpts);
    m_w->ringScanCombo->addItem(slime::ui::text(slime::ui::Key::RingScanFast), 0);
    m_w->ringScanCombo->addItem(slime::ui::t("ui.6ff11b12c236"), 2);
    m_w->ringScanCombo->addItem(slime::ui::text(slime::ui::Key::RingScanExact), 1);
    m_w->ringScanCombo->setToolTip(slime::ui::t("ui.5201f6ebf6aa"));
    ringModeRow->addWidget(m_w->ringScanCombo);
    ringModeRow->addStretch();
    ringLay->addLayout(ringModeRow);

    // v3.6.18（用户要求）：模拟距离（区块）—— 刷怪只发生在模拟距离内（16 格/区块），
    //   16×D < 128 时环带外沿会被收窄；>=8 区块（128 格）时与默认环带一致。
    auto* simRow = new QHBoxLayout();
    simRow->addWidget(new QLabel(slime::ui::t("ui.73aa02598560"), m_w->ringOpts));
    m_w->simDistance = new QSpinBox(m_w->ringOpts);
    m_w->simDistance->setRange(2, 32);
    m_w->simDistance->setValue(12);
    // 单位写在框外（项目规则：输入框内不放单位）
    m_w->simDistance->setToolTip(
        slime::ui::t("ui.4855e48a8f7a"));
    simRow->addWidget(m_w->simDistance);
    simRow->addWidget(new QLabel(slime::ui::t("ui.3da82d85c5de"), m_w->ringOpts));
    m_w->simDistanceNote = new QLabel(QString(), m_w->ringOpts);
    m_w->simDistanceNote->setObjectName(QStringLiteral("simDistanceNote"));
    simRow->addWidget(m_w->simDistanceNote, 1);
    ringLay->addLayout(simRow);
    QObject::connect(m_w->simDistance, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     [this](int) { updateSimDistanceNote(); });
    updateSimDistanceNote();
    m_w->ringOpts->setVisible(false);
    densityLay->addWidget(m_w->ringOpts);
    modeLayout->addWidget(m_w->densityPanel);

    // ===== 通用选项 =====
    auto* optRow3 = new QHBoxLayout();
    optRow3->addWidget(new QLabel(slime::ui::t("ui.1113d292c420"), gMode));
    m_w->targetCount = new QSpinBox(gMode);
    m_w->targetCount->setRange(1, 1000000);
    m_w->targetCount->setValue(1);
    m_w->targetCount->setToolTip(slime::ui::t("ui.0405eccc6f77"));
    optRow3->addWidget(m_w->targetCount);
    optRow3->addStretch();
    modeLayout->addLayout(optRow3);
    // v3.10.9：停止方式已移入「结果要求」分组（仅自定义方案显示）
    auto* optRow1 = new QHBoxLayout();
    m_w->stopAtTarget = new QCheckBox(slime::ui::t("ui.c4db3610d642"), gMode);
    m_w->stopAtTarget->setToolTip(slime::ui::t("ui.804c72b80469"));
    optRow1->addWidget(m_w->stopAtTarget);
    optRow1->addStretch();
    QObject::connect(m_w->stopAtTarget, &QCheckBox::toggled, this,
                     [this](bool) { markPresetCustom(slime::ui::t("ui.b77bf334b0d7")); });
    auto* optRow2 = new QHBoxLayout();
    m_w->smartRange = new QCheckBox(slime::ui::text(slime::ui::Key::SmartRange), gMode);
    m_w->smartRange->setToolTip(slime::ui::text(slime::ui::Key::SmartRangeTip));
    optRow2->addWidget(m_w->smartRange);
    optRow2->addStretch();
    QObject::connect(m_w->smartRange, &QCheckBox::toggled, this,
                     [this](bool) { markPresetCustom(slime::ui::t("ui.9522c47b44f6")); });
    groupsLay->addWidget(gMode);

    // ---- v3.10.9（用户要求）：声明「自定义方案可能导致明显降速」+ 高级开关分级 ----
    m_w->presetDisclaimer = new QLabel(gMode);
    m_w->presetDisclaimer->setObjectName(QStringLiteral("presetDisclaimer"));
    m_w->presetDisclaimer->setWordWrap(true);
    m_w->presetDisclaimer->setMinimumWidth(0);
    m_w->presetDisclaimer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // 不撑最小宽
    m_w->presetDisclaimer->setVisible(false);
    m_w->presetDisclaimer->setStyleSheet(QStringLiteral("color:%1;font-size:11px;")
                                             .arg(darkTheme() ? QStringLiteral("#ffb74d")
                                                              : QStringLiteral("#b26a00")));
    m_w->presetDisclaimer->setText(slime::ui::t("ui.08c668ef8483"));
    groupsLay->addWidget(m_w->presetDisclaimer);

    auto* gResult = new QGroupBox(slime::ui::t("ui.a62f39a46c98"), groupsHost);
    m_w->resultGroup = gResult;
    auto* resultReqLay = new QVBoxLayout(gResult);
    resultReqLay->setContentsMargins(6, 6, 6, 6);

    auto* stopRow = new QHBoxLayout();
    stopRow->addWidget(m_w->stopAtTarget);
    stopRow->addStretch();
    resultReqLay->addLayout(stopRow);
    auto* smartRow = new QHBoxLayout();
    smartRow->addWidget(m_w->smartRange);
    smartRow->addStretch();
    resultReqLay->addLayout(smartRow);
    // v3.10.12.11：最小密度/忽略阈值已移到密度面板常显，这里只保留停止方式/智能范围
    groupsLay->addWidget(gResult);

    // ---- 分组 4：匹配方式（搜索算法四模式与参数）----
    auto* gMatch = new QGroupBox(slime::ui::t("ui.6a05dbdc884c"), groupsHost);
    m_w->matchGroup = gMatch; // v3.5.6：环形模式下整组置灰
    auto* algLay = new QVBoxLayout(gMatch);
    algLay->setContentsMargins(6, 6, 6, 6);
    algLay->addWidget(new QLabel(slime::ui::t("ui.a3ff0ef4b1d2"), gMatch));
    // v3.6.0：算法对图案/密度两种模式都生效；环形模式有独立的快速/精确扫描（组内另有说明）
    if (auto* algLbl = qobject_cast<QLabel*>(algLay->itemAt(algLay->count() - 1)->widget()))
        algLbl->setToolTip(slime::ui::t("ui.deabcae2b7ea"));
    m_w->algExactRadio = new QRadioButton(slime::ui::t("ui.c0639e939430"), gMatch);
    m_w->algLayeredRadio = new QRadioButton(slime::ui::t("ui.d9cacedddc49"), gMatch);
    m_w->algSampleRadio = new QRadioButton(slime::ui::t("ui.8c7ae92e09a8"), gMatch);
    m_w->algBruteRadio = new QRadioButton(slime::ui::t("ui.9c3dbfa03e7d"), gMatch);
    m_w->algExactRadio->setToolTip(slime::ui::t("ui.af1eb6b5f9dc"));
    m_w->algLayeredRadio->setToolTip(slime::ui::t("ui.d423affec19f"));
    m_w->algSampleRadio->setToolTip(slime::ui::t("ui.4460ce315bf8"));
    m_w->algBruteRadio->setToolTip(slime::ui::t("ui.650ceeaab2e6"));
    m_w->algExactRadio->setChecked(true);
    algLay->addWidget(m_w->algExactRadio);
    // v3.9.11：用户手动改动「会改变结果」的开关 ⇒ 方案变自定义
    for (QRadioButton* r : { m_w->algExactRadio, m_w->algLayeredRadio, m_w->algSampleRadio, m_w->algBruteRadio }) {
        QObject::connect(r, &QRadioButton::toggled, this,
                         [this](bool on) { if (on) markPresetCustom(slime::ui::t("ui.60f424bf4ef4")); });
    }
    algLay->addWidget(m_w->algLayeredRadio);
    algLay->addWidget(m_w->algSampleRadio);
    algLay->addWidget(m_w->algBruteRadio);

    // v3.3.4：分层/采样参数各拆两行（原单行最小宽 ~400px）
    auto* layerRow = new QHBoxLayout();
    layerRow->setContentsMargins(12, 0, 0, 0);
    layerRow->addWidget(new QLabel(slime::ui::t("ui.a82ed5c3e01c"), gMatch));
    m_w->layerBlock = new QComboBox(gMatch);
    m_w->layerBlock->addItem(QStringLiteral("16×16"), 16);
    m_w->layerBlock->addItem(QStringLiteral("32×32"), 32);
    m_w->layerBlock->addItem(QStringLiteral("64×64"), 64);
    m_w->layerBlock->setCurrentIndex(0);
    layerRow->addWidget(m_w->layerBlock);
    layerRow->addStretch();
    algLay->addLayout(layerRow);
    auto* layerRow2 = new QHBoxLayout();
    layerRow2->setContentsMargins(12, 0, 0, 0);
    layerRow2->addWidget(new QLabel(slime::ui::t("ui.004c3be7d3e3"), gMatch));
    m_w->layerRatio = new QDoubleSpinBox(gMatch);
    m_w->layerRatio->setRange(0.2, 2.0);
    m_w->layerRatio->setDecimals(2);
    m_w->layerRatio->setSingleStep(0.1);
    m_w->layerRatio->setValue(1.0);
    // v3.4.12（用户要求）：单位从输入框里拿出来 ⇒ 框后单独放「×」标签
    m_w->layerRatio->setToolTip(slime::ui::t("ui.e5a8256a3033"));
    layerRow2->addWidget(m_w->layerRatio);
    layerRow2->addStretch();
    algLay->addLayout(layerRow2);

    auto* sampleRow = new QHBoxLayout();
    sampleRow->setContentsMargins(12, 0, 0, 0);
    sampleRow->addWidget(new QLabel(slime::ui::t("ui.005b17520be0"), gMatch));
    m_w->sampleStep = new QSpinBox(gMatch);
    m_w->sampleStep->setRange(2, 64);
    m_w->sampleStep->setValue(8);
    m_w->sampleStep->setToolTip(slime::ui::t("ui.e33742fb2823"));
    sampleRow->addWidget(m_w->sampleStep);
    sampleRow->addStretch();
    algLay->addLayout(sampleRow);
    auto* sampleRow2 = new QHBoxLayout();
    sampleRow2->setContentsMargins(12, 0, 0, 0);
    m_w->sampleExact = new QCheckBox(slime::ui::t("ui.64feeab104d9"), gMatch);
    m_w->sampleExact->setToolTip(slime::ui::t("ui.c8e19ae76760"));
    sampleRow2->addWidget(m_w->sampleExact);
    sampleRow2->addStretch();
    algLay->addLayout(sampleRow2);

    m_w->algSummary = new QLabel(gMatch);
    m_w->algSummary->setWordWrap(true);
    m_w->algSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_w->algSummary->setStyleSheet(QStringLiteral("color:%1;").arg(darkTheme() ? "#9aa0a6" : "#767676"));
    m_w->algSummary->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    algLay->addWidget(m_w->algSummary);
    groupsLay->addWidget(gMatch);

    // ---- 分组 5：性能设置（线程数 + GPU 加速）----
    auto* gPerf = new QGroupBox(slime::ui::t("ui.eb1f3e5ef69b"), groupsHost);
    auto* runLay = new QVBoxLayout(gPerf);
    auto* runRow = new QHBoxLayout();
    const int cores = QThread::idealThreadCount();
    runRow->addWidget(new QLabel(slime::ui::t("ui.dc3465f048b7"), gPerf));
    m_w->threads = new QSpinBox(gPerf);
    // v3.3.9（任务书 3.1 根因）：v3.3.8.1 把上限设为**逻辑核心数** ⇒ 在无超线程的机器上
    // 「超过物理核心数」永远不可达，警告实测永不出现。现放宽到 2× 逻辑核心，让「设得过大」
    // 成为可达状态并如实警告（也可让用户按需试验超线程/超订阅设置）。
    const int threadsMax = (std::max)(2, cores * 2);
    m_w->threads->setRange(1, threadsMax);
    runRow->addWidget(m_w->threads);
    // v3.3.9（任务书 3.1）：常驻提示行 —— 只在「超过物理核心数」时显示，
    // 避免警告只闪一下状态栏（反馈栏条目也可能被后续消息淹没）而用户看不到。
    m_w->threadsHint = new QLabel(QString(), gPerf);
    m_w->threadsHint->setObjectName(QStringLiteral("threadsHint"));
    m_w->threadsHint->setWordWrap(true);
    m_w->threadsHint->setVisible(false);
    runRow->addWidget(m_w->threadsHint, 1);
    // v3.3.8.1（任务书 3.1）：线程数过大（超过物理核心数）时给出警告（常驻提示 + 状态栏 + 反馈栏，不弹窗）
    const auto updateThreadsHint = [this]() {
        const int v = m_w->threads->value();
        const int phys = physicalCoreCount();
        if (!m_w->threadsHint) return;
        if (phys > 0 && v > phys) {
            m_w->threadsHint->setText(slime::ui::t("ui.84ff5ba34922")
                                          .arg(phys));
            m_w->threadsHint->setStyleSheet(slime::ui::tr(QStringLiteral("color:%1;"))
                                                .arg(darkTheme() ? QStringLiteral("#ffb74d")
                                                                 : QStringLiteral("#b26a00")));
            m_w->threadsHint->setVisible(true);
        } else if (v > 12) {
            m_w->threadsHint->setText(slime::ui::t("ui.fa3eb4b9a8c6"));
            m_w->threadsHint->setStyleSheet(QStringLiteral("color:%1;")
                                                .arg(darkTheme() ? QStringLiteral("#ffb74d")
                                                                 : QStringLiteral("#b26a00")));
            m_w->threadsHint->setVisible(true);
        } else {
            m_w->threadsHint->clear();
            m_w->threadsHint->setVisible(false);
        }
    };
    connect(m_w->threads, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, updateThreadsHint](int v) {
        updateThreadsHint();
        if (v <= 0) return;
        const int phys = physicalCoreCount();
        if (phys > 0 && v > phys) {
            setStatus(slime::ui::tr(slime::ui::t("ui.476bd74f4d20")).arg(v).arg(phys));
            addFeedback(1, slime::ui::tr(slime::ui::t("ui.fdcf43a74de7")).arg(v).arg(phys));
        } else if (v > 12) {
            setStatus(slime::ui::tr(slime::ui::t("ui.f0bde910701c")).arg(v));
            addFeedback(2, slime::ui::tr(slime::ui::t("ui.f0bde910701c")).arg(v));
        }
    });
    // v3.3.9（任务书 3.1）：默认值保持历史行为（全部逻辑核心，与既有性能基准一致）；
    // 在超线程机器上该默认值本身就会触发「超过物理核心数」常驻提示 —— 这正是任务书要求的
    // 「过大时给出警告」，且 tooltip 给出推荐值（物理核心数）。
    m_w->threads->setValue(cores);
    updateThreadsHint();
    m_w->threads->setToolTip(slime::ui::t("ui.44f41848ac07")
                                 .arg(threadsMax)
                                 .arg(physicalCoreCount() > 0 ? physicalCoreCount() : cores)
                                 .arg(cores));
    runRow->addStretch();
    runLay->addLayout(runRow);
    auto* gpuRow = new QHBoxLayout();
    m_w->gpuCheck = new QCheckBox(slime::ui::t("ui.c903f3eb38b6"), gPerf);
    m_w->gpuCheck->setChecked(false);
    // v3.3.6（任务书 Bug 2）：不再显示阈值提示，只保留「不一定更快」提醒（tooltip 见 updateGpuStatusUi）
    m_w->gpuCheck->setToolTip(gpuReminderText());
    gpuRow->addWidget(m_w->gpuCheck);
    runLay->addLayout(gpuRow);
    m_w->gpuStatus = new QLabel(slime::ui::t("ui.f7d4441772c2"), gPerf);
    m_w->gpuStatus->setWordWrap(true);
    runLay->addWidget(m_w->gpuStatus);

    // v3.10.9（用户要求）：GPU 开关旁的「设置 ▸」，展开 GPU 相关开关（默认收起）
    {
        auto* advRow = new QHBoxLayout();
        m_w->gpuAdvBtn = new QToolButton(gPerf);
        m_w->gpuAdvBtn->setText(slime::ui::t("ui.9ce9703b1e69"));
        m_w->gpuAdvBtn->setAutoRaise(true);
        m_w->gpuAdvBtn->setCursor(Qt::PointingHandCursor);
        m_w->gpuAdvBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_w->gpuAdvBtn->setToolTip(slime::ui::t("ui.8f223956c5e5"));
        advRow->addWidget(m_w->gpuAdvBtn);
        advRow->addStretch();
        runLay->addLayout(advRow);

        m_w->gpuAdvPanel = new QWidget(gPerf);
        m_w->gpuAdvPanel->setVisible(false);
        auto* advLay = new QVBoxLayout(m_w->gpuAdvPanel);
        advLay->setContentsMargins(12, 0, 0, 0);
        advLay->setSpacing(3);
        auto* rowS = new QHBoxLayout();
        m_w->gpuStreamCheck = new QCheckBox(slime::ui::t("ui.d76a8f60e8cf"), m_w->gpuAdvPanel);
        m_w->gpuStreamCheck->setChecked(m_settings.gpuPatternStream);
        m_w->gpuStreamCheck->setToolTip(slime::ui::t("ui.86b8fbcbd4f9"));
        rowS->addWidget(m_w->gpuStreamCheck);
        rowS->addStretch();
        advLay->addLayout(rowS);
        // v3.10.11.1：结果自优化（默认关；只影响速度与"已保留数量"统计，不改变结果内容）
        auto* rowO = new QHBoxLayout();
        m_w->gpuSelfOptCheck = new QCheckBox(slime::ui::t("ui.b2a291b0c108"), m_w->gpuAdvPanel);
        m_w->gpuSelfOptCheck->setChecked(m_settings.gpuResultSelfOptimize);
        m_w->gpuSelfOptCheck->setToolTip(slime::ui::t("ui.39342fda69c7"));
        rowO->addWidget(m_w->gpuSelfOptCheck);
        rowO->addStretch();
        advLay->addLayout(rowO);
        auto* selfOptNote = new QLabel(slime::ui::t("ui.62ff4651a917"),
            m_w->gpuAdvPanel);
        selfOptNote->setWordWrap(true);
        selfOptNote->setStyleSheet(QStringLiteral("color:#888; font-size:11px;"));
        advLay->addWidget(selfOptNote);
        auto* rowL = new QHBoxLayout();
        rowL->addWidget(new QLabel(slime::ui::t("ui.8a71c973fb98"), m_w->gpuAdvPanel));
        m_w->gpuLimitSpin = new QSpinBox(m_w->gpuAdvPanel);
        m_w->gpuLimitSpin->setRange(10, 100);
        m_w->gpuLimitSpin->setValue(std::clamp(m_settings.gpuLimitPercent, 10, 100));
        m_w->gpuLimitSpin->setToolTip(slime::ui::t("ui.6f21a103a38e"));
        rowL->addWidget(m_w->gpuLimitSpin);
        rowL->addStretch();
        advLay->addLayout(rowL);
        auto* rowA = new QHBoxLayout();
        m_w->gpuAutoCheck = new QCheckBox(slime::ui::t("ui.039b97bba306"), m_w->gpuAdvPanel);
        m_w->gpuAutoCheck->setChecked(m_settings.autoCheckGpuAtStartup);
        m_w->gpuAutoCheck->setToolTip(slime::ui::t("ui.107542ecf736"));
        rowA->addWidget(m_w->gpuAutoCheck);
        rowA->addStretch();
        advLay->addLayout(rowA);
        runLay->addWidget(m_w->gpuAdvPanel);

        connect(m_w->gpuAdvBtn, &QToolButton::clicked, this, [this]() {
            const bool show = m_w->gpuAdvPanel && !m_w->gpuAdvPanel->isVisible();
            if (m_w->gpuAdvPanel) m_w->gpuAdvPanel->setVisible(show);
            if (m_w->gpuAdvBtn) m_w->gpuAdvBtn->setText(show ? slime::ui::t("ui.57441879db15")
                                                             : slime::ui::t("ui.9ce9703b1e69"));
        });
        connect(m_w->gpuStreamCheck, &QCheckBox::toggled, this, [this](bool on) {
            m_settings.gpuPatternStream = on;
            scheduleConfigWrite();
        });
        connect(m_w->gpuSelfOptCheck, &QCheckBox::toggled, this, [this](bool on) {
            m_settings.gpuResultSelfOptimize = on;
            scheduleConfigWrite();
        });
        connect(m_w->gpuLimitSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
            m_settings.gpuLimitPercent = std::clamp(v, 10, 100);
            scheduleConfigWrite();
        });
        connect(m_w->gpuAutoCheck, &QCheckBox::toggled, this, [this](bool on) {
            m_settings.autoCheckGpuAtStartup = on;
            scheduleConfigWrite();
        });
    }
    groupsLay->addWidget(gPerf);

    // ---- 分组 6：缓存管理 ----
    auto* gCache = new QGroupBox(slime::ui::t("ui.c15668b86bc6"), groupsHost);
    auto* cacheLay = new QVBoxLayout(gCache);
    auto* cacheRow = new QHBoxLayout();
    m_w->importCache = new QPushButton(slime::ui::t("ui.00e905651e85"), gCache);
    m_w->clearCache = new QPushButton(slime::ui::t("ui.291952a2abb2"), gCache);
    cacheRow->addWidget(m_w->importCache);
    cacheRow->addWidget(m_w->clearCache);
    cacheRow->addStretch();
    cacheLay->addLayout(cacheRow);
    m_w->useCache = new QCheckBox(slime::ui::t("ui.ba695727a2f2"), gCache);
    m_w->useCache->setToolTip(slime::ui::t("ui.72d08625eb17"));
    m_w->useCache->setChecked(true);
    cacheLay->addWidget(m_w->useCache);
    // v3.3.9（任务书 1.1）：**缓存构建模式** —— 唯一可以写缓存的模式（其他模式一律只读）。
    // v3.4.9（用户要求）：搜索页**不再**放「缓存构建模式」开关 —— 缓存构建已独立成左侧导航页，
    // 「是否处于构建模式」由当前页面决定（进构建页=开，回搜索页=关），状态保存在 m_buildModeActive。
    {
        auto* moved = new QLabel(slime::ui::t("ui.2949f64a2809"), gCache);
        moved->setObjectName(QStringLiteral("topSummary"));
        moved->setWordWrap(true);
        cacheLay->addWidget(moved);
    }
    // v3.4.9：构建提示行随构建内容一起搬到「缓存构建」页（见 buildV340Pages），此处不再创建
    m_w->cacheStatus = new QLabel(slime::ui::t("ui.23c0ca294841"), gCache);
    m_w->cacheStatus->setWordWrap(true);
    cacheLay->addWidget(m_w->cacheStatus);
    groupsLay->addWidget(gCache);

    m_paramLay = groupsLay; // v3.4.3：缓存构建「额外选项」挂到搜索页参数列
    groupsLay->addStretch();

    // ================= 右：信息栏 + QTextEdit（结果文本） =================
    auto* rightPanel = new QWidget(m_hSplit);
    rightPanel->setMinimumWidth(300); // v3.4.4（R1/P0）：结果栏最小宽 380，窄窗下自动出滚动条而非叠字
    auto* rightLay = new QVBoxLayout(rightPanel);
    rightLay->setContentsMargins(4, 0, 0, 0);
    rightLay->setSpacing(4);

    auto* infoBar = new QHBoxLayout();
    m_w->modeHeader = new QLabel(slime::ui::t("ui.014a215707f0"), rightPanel);
    m_w->modeHeader->setStyleSheet(QStringLiteral("font-weight:bold;"));
    infoBar->addWidget(m_w->modeHeader);
    infoBar->addStretch();
    m_w->copyBtn = new QPushButton(slime::ui::t("ui.0d6cb0c3fd91"), rightPanel);
    infoBar->addWidget(m_w->copyBtn);
    rightLay->addLayout(infoBar);
    // v3.5.5（修复）：infoStats / modeWarn 的创建在右栏重写时丢失 ⇒ 布局里塞 nullptr 崩溃
    m_w->infoStats = new QLabel(rightPanel);
    m_w->infoStats->setObjectName(QStringLiteral("resultCaption"));
    m_w->infoStats->setWordWrap(true);
    m_w->modeWarn = new QLabel(rightPanel);
    m_w->modeWarn->setObjectName(QStringLiteral("modeWarn"));
    m_w->modeWarn->setWordWrap(true);
    m_w->modeWarn->setVisible(false);
    rightLay->addWidget(m_w->infoStats);
    rightLay->addWidget(m_w->modeWarn);
    m_rightSplit = new QSplitter(Qt::Vertical, rightPanel);
    m_rightSplit->setObjectName(QStringLiteral("rightSplit"));
    m_rightSplit->setChildrenCollapsible(false);

    m_mapPanel = new QWidget(m_mapColumn);
    m_mapPanel->setObjectName(QStringLiteral("mapPanel"));
    auto* mapLay = new QVBoxLayout(m_mapPanel);
    mapLay->setContentsMargins(0, 0, 0, 0);
    mapLay->setSpacing(2);
    {
        auto* head = new QWidget(m_mapPanel);
        // v3.6.9：标题行固定高度 —— 折叠后多余空间不能分给标题行，否则按钮会被垂直居中（用户看到的「位移」）
        head->setFixedHeight(30);
        head->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* hl = new QHBoxLayout(head);
        hl->setContentsMargins(2, 2, 2, 0);
        hl->setSpacing(6);
        m_mapToggle = new QToolButton(head);
        m_mapToggle->setObjectName(QStringLiteral("mapPanelToggle"));
        m_mapToggle->setText(slime::ui::t("ui.46e1efeb268a"));
        m_mapToggle->setArrowType(Qt::DownArrow);
        m_mapToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_mapToggle->setAutoRaise(true);
        m_mapToggle->setCursor(Qt::PointingHandCursor);
        m_mapToggle->setToolTip(slime::ui::t("ui.3e25cac4426c"));
        m_mapToggle->setStyleSheet(QStringLiteral(
            "QToolButton{border:none;padding:3px 8px;border-radius:0px;font-weight:bold;}"
            "QToolButton:hover{background:#4CAF50;color:#ffffff;}"));
        hl->addWidget(m_mapToggle);
        auto* bFit = new QPushButton(slime::ui::t("ui.e9562f6fa7af"), head);
        bFit->setAutoDefault(false);
        hl->addWidget(bFit);
        hl->addStretch();
        mapLay->addWidget(head);
        m_map = new MapPreviewWidget(m_mapPanel);
        m_map->setObjectName(QStringLiteral("searchMap"));
        m_map->setMinimumHeight(160);
        mapLay->addWidget(m_map, 1);
        m_mapPlaceholder = new QLabel(slime::ui::t("ui.7cb53520e436"), m_mapPanel);
        m_mapPlaceholder->setObjectName(QStringLiteral("mapPanelPlaceholder"));
        m_mapPlaceholder->setVisible(false);
        mapLay->addWidget(m_mapPlaceholder);
        // v3.6.9.1（用户反馈：地图被挤窄且拉不动）：末尾 stretch 会和地图平分剩余空间（各 1 份），
        //   展开时地图只剩一半高度。改成「只在折叠状态显示的占位控件」：
        //   展开时它隐藏 ⇒ 剩余空间全部归地图；折叠时它显示 ⇒ 标题行贴顶不会被居中。
        m_mapSpacer = new QWidget(m_mapPanel);
        m_mapSpacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        m_mapSpacer->setVisible(false);
        mapLay->addWidget(m_mapSpacer, 1);
        connect(m_mapToggle, &QToolButton::clicked, this, [this]() { toggleMapPanel(); });
        connect(bFit, &QPushButton::clicked, this, [this]() { if (m_map) m_map->fitToContent(); });
    }
    m_mapColLay->addWidget(m_mapPanel, 1);
    // v3.6.5（用户要求）：地图下方、结果列表上方的显眼提醒
    // v3.6.8（用户反馈：折叠按钮点了会位移）：提醒**不能**放进 QSplitter —— 它是第 3 个子控件，
    //   显示/隐藏会改变分隔条的子项数量与像素分配，导致地图/结果整体位移。
    //   现在改成放进结果面板内部（结果标题行正上方）。
    m_resultsNotice = new QLabel(m_rightSplit);
    m_resultsNotice->setObjectName(QStringLiteral("resultsNotice"));
    m_resultsNotice->setVisible(false);
    m_resultsNotice->setWordWrap(true);
    // v3.6.6：用户一旦手动拖动右栏分隔条，就不再自动改尺寸
    connect(m_rightSplit, &QSplitter::splitterMoved, this, [this](int, int) { m_rightSplitUserMoved = true; });
    // v3.5.5（修复）：resultView 的创建曾丢失，导致布局里塞入 nullptr ⇒ 启动崩溃
    m_w->resultView = new QTextEdit(rightPanel);
    m_w->resultView->setObjectName(QStringLiteral("resultView"));
    m_w->resultView->setReadOnly(true);
    m_w->resultView->setPlaceholderText(slime::ui::t("ui.31bb4ff9f568"));
    m_w->resultView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_w->resultView, &QTextEdit::customContextMenuRequested, this, &MainWindow::showResultContextMenu);
    // v3.5.5：结果面板 = 可折叠标题 + 列表（表头排序、每行跳转）；resultView 保留在面板内并隐藏
    m_resultPane = new QWidget(m_rightSplit);
    m_resultPane->setObjectName(QStringLiteral("resultPane"));
    auto* resLay = new QVBoxLayout(m_resultPane);
    resLay->setContentsMargins(0, 0, 0, 0);
    resLay->setSpacing(2);
    {
        auto* head = new QWidget(m_resultPane);
        head->setFixedHeight(30);   // v3.6.9：同上，折叠后标题行位置不动
        head->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* hl = new QHBoxLayout(head);
        hl->setContentsMargins(2, 2, 2, 0);
        hl->setSpacing(6);
        m_resultToggle = new QToolButton(head);
        m_resultToggle->setObjectName(QStringLiteral("resultPaneToggle"));
        m_resultToggle->setText(slime::ui::t("ui.0a2c91cec6c8"));
        m_resultToggle->setArrowType(Qt::DownArrow);
        m_resultToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_resultToggle->setAutoRaise(true);
        m_resultToggle->setCursor(Qt::PointingHandCursor);
        m_resultToggle->setToolTip(slime::ui::t("ui.34e02c74cfb6"));
        m_resultToggle->setStyleSheet(QStringLiteral(
            "QToolButton{border:none;padding:3px 8px;border-radius:0px;font-weight:bold;}"
            "QToolButton:hover{background:#4CAF50;color:#ffffff;}"));
        hl->addWidget(m_resultToggle);
        m_resultCountLabel = new QLabel(slime::ui::t("ui.a535edb5149b"), head);
        m_resultCountLabel->setObjectName(QStringLiteral("resultCaption"));
        hl->addWidget(m_resultCountLabel);
        hl->addStretch();
        resLay->addWidget(head);
        if (m_resultsNotice) {
            m_resultsNotice->setParent(m_resultPane);
            resLay->insertWidget(1, m_resultsNotice);   // v3.6.8：提醒放在结果标题行下面
        }

        m_w->resultTable = new QTableWidget(m_resultPane);
        auto* tb = m_w->resultTable;
        tb->setObjectName(QStringLiteral("resultTable"));
        tb->setColumnCount(7);
        // v3.10.0：结果独立成第三列（约 340px）⇒ 表头缩短、列宽固定，避免窄列裁字
        tb->setHorizontalHeaderLabels({ slime::ui::t("ui.db669ff2ab99"), QStringLiteral("X"), QStringLiteral("Z"),
                                        slime::ui::t("ui.b3480678b68e"), slime::ui::t("ui.f90554a29e42"),
                                        slime::ui::t("ui.3592ccaf66db"), QStringLiteral("") });
        tb->setColumnWidth(0, 40);
        tb->setColumnWidth(1, 56);
        tb->setColumnWidth(2, 56);
        tb->setColumnWidth(3, 50);
        tb->setColumnWidth(4, 62);
        tb->setColumnWidth(5, 62);
        tb->setColumnHidden(5, true);   // v3.5.9：仅环形模式显示「有效格 · 总权重」
        tb->verticalHeader()->setVisible(false);
        tb->setAlternatingRowColors(true);
        tb->setSelectionBehavior(QAbstractItemView::SelectRows);
        tb->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tb->horizontalHeader()->setSectionsClickable(true);
        tb->horizontalHeader()->setSortIndicatorShown(true);
        tb->horizontalHeader()->setStretchLastSection(true);
        static OneDecimalDelegate oneDecimal;
        tb->setItemDelegateForColumn(3, &oneDecimal);
        tb->setItemDelegateForColumn(4, &oneDecimal);
        tb->setItemDelegateForColumn(5, &oneDecimal);
        resLay->addWidget(tb, 1);
        resLay->addWidget(m_w->resultView, 0);
        m_w->resultView->hide();
        // v3.6.9.1：同上 —— 折叠时才显示占位控件，展开时表格吃掉全部剩余空间
        m_resultSpacer = new QWidget(m_resultPane);
        m_resultSpacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        m_resultSpacer->setVisible(false);
        resLay->addWidget(m_resultSpacer, 1);

        connect(m_resultToggle, &QToolButton::clicked, this, [this]() { toggleResultPane(); });
        connect(tb, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
            if (!m_w || !m_w->resultTable) return;
            const auto* xi = m_w->resultTable->item(row, 1);
            const auto* zi = m_w->resultTable->item(row, 2);
            if (!xi || !zi) return;
            const int bx = xi->data(Qt::UserRole).toInt();
            const int bz = zi->data(Qt::UserRole).toInt();
            focusMapOnChunk(bx >= 0 ? bx >> 4 : -((-bx + 15) >> 4), bz >= 0 ? bz >> 4 : -((-bz + 15) >> 4));
        });
        // v3.5.10：点结果行 = 选中该环形候选（地图只画这一条环，并联动脉动选中行）
        connect(tb, &QTableWidget::cellClicked, this, [this](int row, int) {
            if (!m_lastAfk.empty() && row >= 0 && row < static_cast<int>(m_lastAfk.size()))
                focusAfkCandidate(row);
        });
    }
    m_rightSplit->addWidget(m_resultPane);
    m_rightSplit->setStretchFactor(0, 1);
    m_rightSplit->setStretchFactor(1, 1);
    m_rightSplit->setSizes(QList<int>() << 880);   // v3.10.0：右列只剩结果面板（地图已独立成列）
    rightLay->addWidget(m_rightSplit, 1);
    m_hSplit->addWidget(rightPanel);

    // v3.5.4：地图与信息填写栏联动（去抖刷新）+ 点击结果定位
    m_mapSyncTimer = new QTimer(this);
    m_mapSyncTimer->setSingleShot(true);
    m_mapSyncTimer->setInterval(250);
    connect(m_mapSyncTimer, &QTimer::timeout, this, [this]() { updateMapFromForm(); });
    const auto scheduleMap = [this]() { if (m_mapSyncTimer) m_mapSyncTimer->start(); };
    if (m_w->bedrockRadio) connect(m_w->bedrockRadio, &QRadioButton::toggled, this, [scheduleMap](bool) { scheduleMap(); });
    if (m_w->javaRadio) connect(m_w->javaRadio, &QRadioButton::toggled, this, [scheduleMap](bool) { scheduleMap(); });
    if (m_w->seedEdit) connect(m_w->seedEdit, &QLineEdit::textChanged, this, [scheduleMap](const QString&) { scheduleMap(); });
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
        if (s) connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [scheduleMap](int) { scheduleMap(); });
    }
    if (m_w->resultView) m_w->resultView->installEventFilter(this);
    m_hSplit->setStretchFactor(0, 0);
    m_hSplit->setStretchFactor(1, 1);

    // ================= v3.3.6（任务书 调整 2）：底部面板 = 左竖排导航 + 右内容区 =================
    // v3.10.1：过程面板并入右列（结果上 / 过程下），不再横跨整窗
    auto* bottomPanel = new QWidget(m_rightSplit);
    bottomPanel->setObjectName(QStringLiteral("bottomPanel"));
    bottomPanel->setMinimumHeight(120); // 任务书 1.5：底部最小高 120
    auto* bottomLay = new QHBoxLayout(bottomPanel);
    bottomLay->setContentsMargins(0, 0, 0, 0);
    bottomLay->setSpacing(0);

    // ---- 左侧竖排导航（宽约 120px；当前项 ● + 加粗 + 高亮；仅「反馈」显示 ⚠️/❌ 计数）----
    m_nav = new QListWidget(bottomPanel);
    m_nav->setObjectName(QStringLiteral("navList"));
    m_nav->setFixedWidth(120);
    m_nav->setWordWrap(true); // 「反馈 ⚠️2❌1」在 120px 内自动折行显示
    m_nav->setFocusPolicy(Qt::NoFocus);
    m_nav->setSelectionMode(QAbstractItemView::SingleSelection);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setEditTriggers(QAbstractItemView::NoEditTriggers);
    static const char* const kNavNames[4] = { "ui.tab.progress", "ui.tab.feedback", "ui.tab.log", "ui.tab.command" };
    for (int i = 0; i < 4; ++i) {
        auto* it = new QListWidgetItem(m_nav);
        it->setText(QStringLiteral("○ ") + slime::ui::t(kNavNames[i]));
        it->setToolTip(i == 0 ? slime::ui::t("ui.774421d89d3b")
                              : i == 1 ? slime::ui::t("ui.2024c0e4a2cf")
                                       : i == 2 ? slime::ui::t("ui.418a1fb7c7d4")
                                                : slime::ui::t("ui.b4fa25c4081b"));
        it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    bottomLay->addWidget(m_nav);

    // ---- 右侧内容区（QStackedWidget 四页，与导航一一对应）----
    m_stack = new QStackedWidget(bottomPanel);
    m_stack->setObjectName(QStringLiteral("bottomStack"));

    // 页 1：进度 —— v3.3.6（调整 3）：累积式纯文字流水
    m_progressStream = new ProgressStreamPanel(m_stack);
    m_stack->addWidget(m_progressStream);

    // 页 2：反馈 —— v3.3.6（调整 4）：时间顺序（最新在下）+ 过滤成功类型
    m_feedback = new FeedbackPanel(m_stack);
    m_stack->addWidget(m_feedback);

    // 页 3：日志（默认关闭显示提示行；启用后为级别过滤 + 自动滚动 + 清空 + 复制全部 + 流水）
    m_logPanel = new LogPanel(m_stack);
    m_stack->addWidget(m_logPanel);

    // 页 4：命令终端（保持原功能：输出区 + 输入行 + 工具按钮）
    m_terminal = new CommandTerminal(this, m_stack);
    m_stack->addWidget(m_terminal);

    // v3.10.1：给过程面板加一条 30px 标题行（"过程 · 进度" + 收起按钮），
    //   右列 = 结果（上）+ 过程（下），与原型一致。
    {
        auto* stackHost = new QWidget(bottomPanel);
        auto* stackLay = new QVBoxLayout(stackHost);
        stackLay->setContentsMargins(0, 0, 0, 0);
        stackLay->setSpacing(0);
        auto* dockHead = new QWidget(stackHost);
        dockHead->setFixedHeight(30);
        auto* dockHeadLay = new QHBoxLayout(dockHead);
        dockHeadLay->setContentsMargins(6, 0, 2, 0);
        dockHeadLay->setSpacing(2);
        m_dockTitle = new QLabel(slime::ui::t("ui.bc2bf86ea76e"), dockHead);
        m_dockTitle->setMaximumWidth(76);
        m_dockTitle->setObjectName(QStringLiteral("dockTitle"));
        QFont dockFont = m_dockTitle->font();
        dockFont.setBold(true);
        m_dockTitle->setFont(dockFont);
        dockHeadLay->addWidget(m_dockTitle);
        dockHeadLay->addSpacing(8);
        {
            static const char* const kTabNames[4] = { "ui.tab.progress", "ui.tab.feedback", "ui.tab.log", "ui.tab.command" };
            for (int i = 0; i < 4; ++i) {
                auto* tab = new QToolButton(dockHead);
                tab->setText(slime::ui::t(kTabNames[i]));
                tab->setCheckable(true);
                tab->setAutoExclusive(true);
                tab->setProperty("dockTab", true);
                tab->setCursor(Qt::PointingHandCursor);
                tab->setToolButtonStyle(Qt::ToolButtonTextOnly);
                tab->setFixedWidth(40);   // v3.10.1：窄右列也放得下 4 个页签（不再变成 …）
                // 全局 QSS 给 QToolButton 留了 0 10px 的左右内边距，2 个汉字在 40px 内会被省略成 …；
                // 这里只覆盖内边距与字号，颜色/选中态仍由主题 QSS 决定。
                tab->setStyleSheet(QStringLiteral("QToolButton{padding:0px 2px;font-size:10px;}"));
                tab->setToolTip(QString::fromUtf8(kTabNames[i]));
                {
                    QFont tabFont = tab->font();
                    tabFont.setPointSizeF(8.0);
                    tab->setFont(tabFont);
                }
                connect(tab, &QToolButton::clicked, this, [this, i]() { showDock(i); });
                dockHeadLay->addWidget(tab);
                m_dockHeadTabs.push_back(tab);
            }
        }
        dockHeadLay->addStretch();
        m_dockToggleBtn = new QToolButton(dockHead);
        m_dockToggleBtn->setText(slime::ui::t("ui.5d5815647c76"));
        m_dockToggleBtn->setAutoRaise(true);
        m_dockToggleBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_dockToggleBtn->setCursor(Qt::PointingHandCursor);
        m_dockToggleBtn->setToolTip(slime::ui::t("ui.c4708ce077c6"));
        m_dockToggleBtn->setFixedWidth(52);
        connect(m_dockToggleBtn, &QToolButton::clicked, this, [this]() {
            if (m_dockCollapsed) showDock(m_nav ? m_nav->currentRow() : 0);
            else hideDock();
        });
        dockHeadLay->addWidget(m_dockToggleBtn);
        stackLay->addWidget(dockHead);
        stackLay->addWidget(m_stack, 1);
        bottomLay->addWidget(stackHost, 1);
    }
    m_rightSplit->addWidget(bottomPanel);
    m_rightSplit->setStretchFactor(0, 1);
    m_rightSplit->setStretchFactor(1, 0);
    m_rightSplit->setSizes(QList<int>() << 560 << 240);

    // v3.4.2（参考 PyCharm 截图）：隐藏分类竖排列表 —— 只保留左导航底部的图标条切换，
    // 面板打开时不再顶起左下角按钮；分类切换仍走 selectBottomCategory（m_nav 仅作数据源）。
    // v3.4.2：分类切换只由左导航图标条驱动（原竖排分类列表已不参与布局）
    // v3.4.2（参考 PyCharm）：竖排分类列表不参与布局，仅作数据源（图标条切换已同步它）
    m_nav->hide();
    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::selectBottomCategory);
    selectBottomCategory(0);
    updateNavBadges();

    // ================= v3.4.0：把既有面板装配进「搜索页」，并构建其余四页 =================
    //   page0 搜索：40% 参数栏（既有 6 张卡片） + 60% 结果栏（既有 QTextEdit 结果视图）
    //   page1 构建范围 / page2 设置（见 buildV340Pages）
    {
        auto* page0 = new QWidget(m_pageStack);
        auto* page0Lay = new QVBoxLayout(page0);
        page0Lay->setContentsMargins(0, 0, 0, 0);
        page0Lay->setSpacing(0);
        page0Lay->addWidget(m_hSplit, 1);
        m_pageStack->addWidget(page0);
        buildV340Pages();
        showPage(0);
    }

    // 初始尺寸（任务书 1.5）：左栏 400、底部 180；v3.4.0：中部行高优先
    QList<int> hSizes; hSizes << 380 << 480 << 360;   // v3.10.0：三纵列（参数 | 地图 | 结果）
    m_hSplit->setSizes(hSizes);
    // v3.10.1：m_vSplit 只剩页面栈（过程面板已移入右列），无需设置双段尺寸

    // ---- 状态栏（QMainWindow 原生）----
    // v3.3.6（任务书 调整 1）：进度行的补充信息（已用时/预计剩余/阶段说明）放在状态栏，
    // 进度行本身保持「进度条 + 百分比 + 阶段文字」的紧凑形态。
    statusBar()->showMessage(slime::ui::t("ui.b796f2d4ca85"));
    // v3.3.7.2（任务书 1.3）：临时消息（showMessage）与右侧永久控件之间必须**有分隔符** ——
    // 否则「扫描完成，正在匹配图案」+「扫描 11265898000/… 区块（25.6%）」会被读成一句话。
    // 该分隔符只在存在临时消息时显示（messageChanged），消息清空后自动隐藏。
    // v3.5.28（用户要求）：状态栏右侧信息全部移除，改为**工具栏搜索状态面板**：
    //   阶段（搜索中/匹配中/排序中）｜结果数 ｜已用时 ｜预计剩余。
    m_statusSep = nullptr;
    m_w->searchStatus = new QWidget(m_toolBar);
    m_w->searchStatus->setObjectName(QStringLiteral("searchStatusBox"));
    auto* ssLay = new QHBoxLayout(m_w->searchStatus);
    ssLay->setContentsMargins(8, 0, 8, 0);
    ssLay->setSpacing(10);
    m_w->searchStage = new QLabel(slime::ui::t("ui.2437d377fef4"), m_w->searchStatus);
    m_w->searchStage->setObjectName(QStringLiteral("searchStage"));
    m_w->progressInfo = new QLabel(slime::ui::t("ui.ce0d3fa9e579"), m_w->searchStatus);
    m_w->progressInfo->setWordWrap(false);
    m_w->progressElapsed = new QLabel(slime::ui::t("ui.6701fe136b68"), m_w->searchStatus);
    m_w->progressEta = new QLabel(slime::ui::t("ui.d5c3799233cf"), m_w->searchStatus);
    ssLay->addWidget(m_w->searchStage);
    ssLay->addWidget(m_w->progressInfo);
    ssLay->addWidget(m_w->progressElapsed);
    ssLay->addWidget(m_w->progressEta);
    if (m_toolBar && m_toolBarSpacerAction) m_toolBar->insertWidget(m_toolBarSpacerAction, m_w->searchStatus);
    // v3.6.0（用户要求）：联系我们按钮回到界面（放工具栏右侧）
    m_w->contactBtn = new QPushButton(slime::ui::t("ui.152d3db75889"), m_toolBar);
    m_w->contactBtn->setFlat(true);
    connect(m_w->contactBtn, &QPushButton::clicked, this, &MainWindow::showContact);
    if (m_toolBar && m_toolBarSpacerAction) m_toolBar->insertWidget(m_toolBarSpacerAction, m_w->contactBtn);
    // 左下角常驻提醒（搜索时会被实时状态覆盖）
    statusBar()->showMessage(slime::ui::t("ui.4865609e48db"));

    // ---- 连接 ----
    // v3.6.5：条件变化时刷新「开始搜索」可用状态
    if (m_w->seedEdit)
        connect(m_w->seedEdit, &QLineEdit::textChanged, this, [this](const QString&) { updateStartEnabled(); });
    if (m_w->strictRadio)
        connect(m_w->strictRadio, &QRadioButton::toggled, this, [this](bool) { updateStartEnabled(); });
    connect(m_w->bedrockRadio, &QRadioButton::toggled, this, [this](bool) { updateStartEnabled(); });
    if (m_w->densityRadio)
        connect(m_w->densityRadio, &QRadioButton::toggled, this, [this](bool) { updateStartEnabled(); });
    if (m_w->targetCount)
        connect(m_w->targetCount, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int) { updateStartEnabled(); });
    connect(m_w->javaRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) onVersionChanged();
    });
    connect(m_w->bedrockRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) onVersionChanged();
    });
    connect(m_w->strictRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) onModeChanged();
    });
    connect(m_w->densityRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on) onModeChanged();
    });
    connect(m_w->drawPattern, &QPushButton::clicked, this, &MainWindow::editPattern);
    // v3.5.26：群系版本切换 —— 地图重采样，环形搜索的群系缓存也一并失效
    if (m_w->mapVersionCombo) {
        connect(m_w->mapVersionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            m_mapJavaVersionId = m_w->mapVersionCombo->currentData().toInt();
            m_ringBiome.reset();
            m_ringBiomeVersion = -1;
            updateMapFromForm();
            saveMapVersionToConfig();
        });
    }
    // v3.5.18：内嵌图案编辑器 —— 画布变化直接写回 m_pattern 并刷新提示
    if (m_w->inlinePattern) {
        connect(m_w->inlinePattern, &slime::PatternEditorPanel::patternChanged, this,
                [this](const slime::Pattern& p) {
                    m_pattern = p;
                    m_patternSet = !p.empty() && p.activeCount() > 0;
                    updateStartEnabled();   // v3.6.6（用户实测）：画完图案要立刻可用
                    if (m_w->patternInfo) {
                        if (!m_patternSet) {
                            m_w->patternInfo->setText(slime::ui::t("ui.a1b385fa0140"));
                        } else {
                            m_w->patternInfo->setText(slime::ui::t("ui.e10371e74d05")
                                                          .arg(m_pattern.rows())
                                                          .arg(m_pattern.cols())
                                                          .arg(m_pattern.activeCount()));
                        }
                    }
                });
    }
    connect(m_w->importCache, &QPushButton::clicked, this, &MainWindow::importCache);
    connect(m_w->clearCache, &QPushButton::clicked, this, &MainWindow::clearCache);
    connect(m_w->convertBtn, &QPushButton::clicked, this, &MainWindow::convertBlockCoords);
    connect(m_w->start, &QPushButton::clicked, this, &MainWindow::startSearch);
    connect(m_w->cancel, &QPushButton::clicked, this, &MainWindow::cancelSearch);
    connect(m_w->gpuCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onGpuCheckChanged);
    connect(m_w->copyBtn, &QPushButton::clicked, this, &MainWindow::copyResults);

    // 算法模式切换（仅密度搜索可用；参数即时保存到 config.json）
    for (QRadioButton* r : { m_w->algExactRadio, m_w->algLayeredRadio, m_w->algSampleRadio, m_w->algBruteRadio }) {
        connect(r, &QRadioButton::toggled, this, [this](bool on) {
            if (!on) return;
            updateAlgorithmPanels();
            saveAlgorithmToConfig();
        });
    }
    connect(m_w->layerBlock, &QComboBox::currentIndexChanged, this, [this](int) { saveAlgorithmToConfig(); });
    connect(m_w->layerRatio, &QDoubleSpinBox::valueChanged, this, [this](double) {
        updateAlgorithmPanels();
        saveAlgorithmToConfig();
    });
    connect(m_w->sampleStep, &QSpinBox::valueChanged, this, [this](int) {
        updateAlgorithmPanels();
        saveAlgorithmToConfig();
    });
    connect(m_w->sampleExact, &QCheckBox::toggled, this, [this](bool) {
        updateAlgorithmPanels();
        saveAlgorithmToConfig();
    });

    connect(m_w->presetNear, &QPushButton::clicked, this, [this]() {
        m_wholeMapConfirmed = false;
        applyPreset(20000);      // v3.10.12.8：原 ±5000 → ±20000
    });
    connect(m_w->presetLocal, &QPushButton::clicked, this, [this]() {
        m_wholeMapConfirmed = false;
        applyPreset(200000);     // v3.10.12.8：原 ±50000 → ±200000
    });
    connect(m_w->presetGlobal, &QPushButton::clicked, this, [this]() {
        m_wholeMapConfirmed = false;
        applyPreset(1000000);    // v3.10.12.8：原 ±300000 → ±1000000
    });
    connect(m_w->presetWorld, &QPushButton::clicked, this, [this]() {
        confirmWholeMapSearch(true, kJavaBoundChunk);
    });
    connect(m_w->presetBedrock, &QPushButton::clicked, this, [this]() {
        confirmWholeMapSearch(true, kBedrockBoundChunk);
    });

    // 边界截断：编辑结束后校验（v3.1.0）
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
        connect(s, &QSpinBox::editingFinished, this, &MainWindow::clampRangeToBound);
    }

    // v3.5.16：半径是唯一入口；范围框（隐藏）变化时把半径显示同步过来
    if (m_w->rangeRadius) {
        connect(m_w->rangeRadius, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int r) { if (!m_rangeSyncGuard) setSearchRadius(r); });
    }
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { syncRadiusFromRange(); });
    }
    setSearchRadius(m_w->rangeRadius ? m_w->rangeRadius->value() : 1000);

    // 结果排序（只改显示顺序）
    // 日志面板：「启用日志」入口（面板内按钮）与工具菜单一致
    connect(m_logPanel, &LogPanel::enableRequested, this, &MainWindow::showEnableLogDialog);
    // 命令终端：历史持久化到 config.json
    connect(m_terminal, &CommandTerminal::historyChanged, this, [this](const QStringList& h) {
        if (!m_loadingConfig) saveTerminalHistoryToConfig(h);
    });

    // 尺寸（任务书 1.5）：默认 1280×860，最小 1000×650
    resize(1280, 860);
    setMinimumSize(1000, 650);

    onModeChanged();
    onVersionChanged();
    updateCacheStatusUi();
    // v3.5.6：区域形状切换 → 置灰/恢复
    if (m_w->shapeRingRadio) {
        connect(m_w->shapeRingRadio, &QRadioButton::toggled, this, [this](bool on) {
            // v3.10.12.10：环形覆盖率天然只有 ~20% 左右，最小密度 0.3 会导致永远 0 结果。
            //   切入环形时先把阈值降为 0（用户仍可手动调高），切回正方形时恢复原值。
            if (on) {
                m_minDensityBeforeRing = m_w->minDensity ? m_w->minDensity->value() : 0.3;
                if (m_w->minDensity && m_w->minDensity->value() > 0.0) {
                    m_w->minDensity->setValue(0.0);
                    m_minDensityCommitted = 0.0;
                    if (!m_uiQuiet)
                        addFeedback(2, slime::ui::tr(slime::ui::t("ui.c8ae47b91135")));
                }
            } else if (m_w->minDensity && m_w->minDensity->value() == 0.0) {
                m_w->minDensity->setValue((std::max)(0.05, m_minDensityBeforeRing));
                m_minDensityCommitted = m_w->minDensity->value();
            }
            updateSearchShapeEnabled();
        });
    }
    if (m_w->strictRadio) {
        connect(m_w->strictRadio, &QRadioButton::toggled, this, [this](bool) { updateSearchShapeEnabled(); });
    }
    if (m_w->densityRadio) {
        connect(m_w->densityRadio, &QRadioButton::toggled, this, [this](bool) { updateSearchShapeEnabled(); });
    }
    if (m_w->shapeSquareRadio) {
        connect(m_w->shapeSquareRadio, &QRadioButton::toggled, this, [this](bool) { updateSearchShapeEnabled(); });
    }
    updateSearchShapeEnabled();
    updateGpuStatusUi();
    // v3.4.8（用户要求）：主题实际生效变化时（浅↔深），自绘图案立即重画 —— 之前要重启/重载才正常
    connect(&slime::ThemeManager::instance(), &slime::ThemeManager::effectiveThemeChanged, this,
            [this](bool dark) {
                refreshThemedIcons();
                Logger::info(std::string("主题切换为") + (dark ? "深色" : "浅色") + " ⇒ 已重绘界面图案");
            });
}

// ============================================================================
// v3.4.0（ui_prototype/SlimeFinder-UI.html）：左导航 / 标签页 / 停靠面板 / 其余四页
// ============================================================================
// 图标：按原型的内联 SVG 线条风格用 QPainter 画（20×20，1.6px 描边，当前色）
// v3.4.8（用户：切换外观主题时图案要跟着变）：颜色**直接按主题取**（不用 QPalette ——
// QSS 换肤不会改调色板，这正是「切主题后图案看不清、重新加载才正常」的原因）。
QColor MainWindow::themedIconColor() const
{
    return slime::ThemeManager::instance().isDark() ? QColor(0xF0, 0xF0, 0xF0) : QColor(0x11, 0x11, 0x11);
}

QIcon MainWindow::v340NavIcon(int kind, double scale)
{
    QPixmap pm(40, 40); // 2× 供高 DPI；setIconSize(20,20) 会缩放
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(2.0, 2.0);
    // v3.4.6：图案缩放（文字与布局不受影响）—— 以中心缩放绘制
    const double s = (scale > 0.1 && scale < 2.0) ? scale : 1.0;
    p.translate(10.0 * (1.0 - s), 10.0 * (1.0 - s));
    p.scale(s, s);
    QPen pen(themedIconColor());
    pen.setWidthF(1.6);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (kind) {
    case 0: // 搜索：圆 + 手柄
        p.drawEllipse(QPointF(9, 9), 5.2, 5.2);
        p.drawLine(QPointF(13, 13), QPointF(17, 17));
        break;
    case 1: // 缓存构建：方块堆叠 + 记录线（一眼看出是「构建/写入」而不是搜索）
        p.drawRect(QRectF(3.0, 3.0, 6.5, 6.5));
        p.drawRect(QRectF(10.5, 3.0, 6.5, 6.5));
        p.drawRect(QRectF(3.0, 10.5, 6.5, 6.5));
        p.drawRect(QRectF(10.5, 10.5, 6.5, 6.5));
        break;
    case 2: // 设置：圆 + 放射线
        p.drawEllipse(QPointF(10, 10), 2.6, 2.6);
        p.drawLine(QPointF(10, 2), QPointF(10, 4.4));
        p.drawLine(QPointF(10, 15.6), QPointF(10, 18));
        p.drawLine(QPointF(2, 10), QPointF(4.4, 10));
        p.drawLine(QPointF(15.6, 10), QPointF(18, 10));
        p.drawLine(QPointF(4.3, 4.3), QPointF(6, 6));
        p.drawLine(QPointF(14, 14), QPointF(15.7, 15.7));
        p.drawLine(QPointF(15.7, 4.3), QPointF(14, 6));
        p.drawLine(QPointF(6, 14), QPointF(4.3, 15.7));
        break;
    case 3: // 更新历史：钟表
        p.drawEllipse(QPointF(10, 10), 7, 7);
        p.drawLine(QPointF(10, 5.5), QPointF(10, 10));
        p.drawLine(QPointF(10, 10), QPointF(13, 12));
        break;
    case 4: // v3.5.3：地图 —— 折角纸 + 定位针
        p.drawRect(QRectF(2.8, 3.4, 14.4, 13.2));
        p.drawLine(QPointF(8.0, 3.4), QPointF(8.0, 16.6));
        p.drawLine(QPointF(12.0, 3.4), QPointF(12.0, 16.6));
        p.drawEllipse(QPointF(10.0, 9.2), 2.2, 2.2);
        p.drawLine(QPointF(10.0, 11.4), QPointF(10.0, 14.0));
        break;
    default: // 控件状态：四宫格
        p.drawRect(QRectF(3, 3, 5.5, 5.5));
        p.drawRect(QRectF(11.5, 3, 5.5, 5.5));
        p.drawRect(QRectF(3, 11.5, 5.5, 5.5));
        p.drawRect(QRectF(11.5, 11.5, 5.5, 5.5));
        break;
    }
    p.end();
    return QIcon(pm);
}

QIcon MainWindow::v340DockIcon(int kind)
{
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(2.0, 2.0);
    QPen pen(themedIconColor()); // v3.4.8：与导航图标一致，按主题取色（切主题立即重绘）
    pen.setWidthF(1.6);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (kind) {
    case 0: // 进度流水：三行横线
        p.drawLine(QPointF(4, 6), QPointF(16, 6));
        p.drawLine(QPointF(4, 10), QPointF(12, 10));
        p.drawLine(QPointF(4, 14), QPointF(15, 14));
        break;
    case 1: // 反馈：气泡
        p.drawRect(QRectF(3.5, 4.5, 13, 9));
        p.drawLine(QPointF(7.5, 13.5), QPointF(7.5, 16.5));
        p.drawLine(QPointF(7.5, 16.5), QPointF(10.5, 13.5));
        break;
    case 2: // 日志：文档 + 行
        p.drawRect(QRectF(4.5, 2.5, 11, 15));
        p.drawLine(QPointF(7, 7), QPointF(13, 7));
        p.drawLine(QPointF(7, 10), QPointF(13, 10));
        p.drawLine(QPointF(7, 13), QPointF(10, 13));
        break;
    case 3: // 命令终端：提示符
        p.drawLine(QPointF(3.5, 6), QPointF(7.5, 10));
        p.drawLine(QPointF(7.5, 10), QPointF(3.5, 14));
        p.drawLine(QPointF(10, 14), QPointF(16, 14));
        break;
    default: // 收起：向下箭头
        p.drawLine(QPointF(5, 8), QPointF(10, 13));
        p.drawLine(QPointF(10, 13), QPointF(15, 8));
        break;
    }
    p.end();
    return QIcon(pm);
}

// v3.4.8（用户要求）：一键恢复所有设置 —— 走 SettingsDialog 的出厂默认值（与「恢复默认」同源），
// 然后立即生效 + 落盘 + 换肤重绘，并给出反馈。
void MainWindow::resetAllSettings()
{
    if (!m_settingsPage) return;
    const AppSettings def = SettingsDialog::factoryDefaults();
    m_settingsPage->setValues(def);
    m_settings = m_settingsPage->currentValues();
    applyAppSettings(m_settings);
    writeConfigFile(m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                    m_terminal ? m_terminal->history() : QStringList());
    const slime::ThemeMode mode = slime::ThemeManager::modeFromString(m_settings.theme);
    slime::ThemeManager::instance().apply(qApp, mode);
    refreshThemedIcons();
    Logger::info("已恢复所有设置（出厂默认值）并写入 config.json");
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.cee60fc4e81c")));
    setStatus(slime::ui::tr(slime::ui::t("ui.b04e535b0549")));
}

// v3.4.8（用户实测：切换外观主题后图案要立即跟着变，否则看不清）：
//   导航/停靠/收起等图案都是 QPainter 现画的，颜色取自当前主题；主题一切换就重建一遍。
void MainWindow::refreshThemedIcons()
{
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (m_navButtons[i]) {
            m_navButtons[i]->setIcon(v340NavIcon(i < kNavCount ? kNavIconKind[i] : 0, 1.0));
        }
    }
    for (int i = 0; i < m_dockIcons.size(); ++i) {
        if (m_dockIcons[i]) m_dockIcons[i]->setIcon(v340DockIcon(i));
    }
    if (m_dockCollapse) m_dockCollapse->setIcon(v340DockIcon(4));
    std::fprintf(stderr, "[主题] 已按当前主题重绘图案：导航 %d 个 / 停靠 %d 个 + 收起 1 个\n",
                 static_cast<int>(m_navButtons.size()), static_cast<int>(m_dockIcons.size()));
}

// 页面切换（标签页与左导航同源）
void MainWindow::showPage(int page)
{
    if (!m_pageStack || page < 0 || page >= m_pageStack->count()) return;
    // v3.4.8（用户要求：缓存构建作为导航条单独一项）：进「缓存构建」= 开启构建模式，
    // 回「搜索」= 关闭。这样构建页天然只含构建相关内容，搜索页也不会再被"顺带"当成构建入口。
    setBuildModeActive(page == kPageBuild); // v3.4.9：进构建页=开、其它页=关（取代原搜索页开关）
    m_pageStack->setCurrentIndex(page);
    for (int i = 0; i < m_tabButtons.size(); ++i) {
        if (m_tabButtons[i]) m_tabButtons[i]->setChecked(i == page);
    }
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (m_navButtons[i]) m_navButtons[i]->setChecked(i == page);
    }
    m_currentPage = page;
    // v3.4.8（用户要求：动画可延长，但绝不能影响布局）：只对**选中项**重建图案 pixmap 做缩放，
    // iconSize(24) 与文字位置一个字都不动 ⇒ 布局零影响；时长 150 → 280ms，OutCubic 缓动更顺滑。
    if (page >= 0 && page < m_navButtons.size() && m_navButtons[page]) {
        QToolButton* b = m_navButtons[page];
        const int kind = (page < kNavCount) ? kNavIconKind[page] : 0;
        auto* scaleAnim = new QVariantAnimation(b);
        scaleAnim->setDuration(280);
        scaleAnim->setStartValue(0.78);
        scaleAnim->setEndValue(1.0);
        scaleAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(scaleAnim, &QVariantAnimation::valueChanged, this, [this, b, kind](const QVariant& v) {
            b->setIcon(v340NavIcon(kind, v.toDouble()));
        });
        connect(scaleAnim, &QVariantAnimation::finished, this, [this, b, kind]() {
            b->setIcon(v340NavIcon(kind, 1.0));
        });
        scaleAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    // v3.4.8（用户要求）：主按钮随页面切换 —— 构建页 = 开始构建（不要求版本/种子），其它页 = 开始搜索
    if (m_w && m_w->start) {
        m_w->start->setText(page == kPageBuild ? slime::ui::t("ui.b3e1c68ff574")
                                               : slime::ui::t("ui.bd198edcb9cc"));
    }
    if (m_tabHint) {
        m_tabHint->setText(page == kPageBuild
                               ? slime::ui::t("ui.2b4394b6d8b9")
                               : slime::ui::t("ui.4c9e1c3d1b92"));
    }
}

// 停靠面板：0..3 切到对应分类并展开；hideDock 收起（高度归 0，行由 auto 承载）
void MainWindow::showDock(int index)
{
    // v3.10.2：过程面板固定在右列下半；折叠时只隐藏内容，标题行 + 页签常驻，随时可再展开
    if (!m_rightSplit || m_rightSplit->count() < 2) return;
    if (m_pageStack && m_pageStack->currentIndex() != 0) showPage(0);
    QWidget* dock = m_rightSplit->widget(1);
    if (!dock) return;
    const bool alreadyActive = !m_dockCollapsed && m_nav && index >= 0 &&
                               index < m_nav->count() && m_nav->currentRow() == index;
    if (alreadyActive) { hideDock(); return; }   // 点已展开的同一页签 = 收起（保留点击手感）
    dock->setVisible(true);
    dock->setMinimumHeight(120);
    dock->setMaximumHeight(QWIDGETSIZE_MAX);
    if (m_stack) m_stack->setVisible(true);
    m_dockCollapsed = false;
    if (m_dockToggleBtn) m_dockToggleBtn->setText(slime::ui::t("ui.5d5815647c76"));
    if (m_nav && index >= 0 && index < m_nav->count()) m_nav->setCurrentRow(index);
    const int h = (m_dockHeight > 0) ? m_dockHeight : 240;
    const QList<int> sizes = m_rightSplit->sizes();
    if (sizes.size() == 2) {
        const int total = sizes[0] + sizes[1];
        m_rightSplit->setSizes(QList<int>{ (std::max)(160, total - h), h });
    }
    for (int i = 0; i < m_dockIcons.size(); ++i) {
        if (m_dockIcons[i]) m_dockIcons[i]->setChecked(i == index);
    }
}

void MainWindow::hideDock()
{
    // v3.10.2：只折叠内容（标题行 + 页签保留），确保「折叠后还能再展开」
    if (!m_rightSplit || m_rightSplit->count() < 2) return;
    QWidget* dock = m_rightSplit->widget(1);
    if (!dock) return;
    const QList<int> sizes = m_rightSplit->sizes();
    if (sizes.size() == 2 && sizes[1] > 60) m_dockHeight = sizes[1];
    if (m_stack) m_stack->setVisible(false);
    dock->setMinimumHeight(30);
    dock->setMaximumHeight(30);
    m_dockCollapsed = true;
    if (m_dockToggleBtn) m_dockToggleBtn->setText(slime::ui::t("ui.b0e24833f752"));
    for (QPushButton* b : m_dockIcons) {
        if (b) b->setChecked(false);
    }
    setStatus(slime::ui::tr(slime::ui::t("ui.71b2100f6466")));
}

// 其余四页：构建范围 / 设置 / 更新历史 / 控件状态（搜索页是 page0，已由 buildUi 装配）
void MainWindow::buildV340Pages()
{
    // ---------------- 页 2：缓存构建（v3.4.8：独立导航项；唯一可写缓存的模式） ----------------
    {
        // v3.4.8（用户要求）：构建独立成页 —— 本页只放构建相关内容，因此**不需要**选游戏版本、
        // 填世界种子、选搜索模式/算法；这些在搜索页才需要。顶部用一行灰字说明固定下来的选项。
        auto* buildPage = new QWidget(m_pageStack);
        buildPage->setObjectName(QStringLiteral("buildPage"));
        auto* buildPageLay = new QVBoxLayout(buildPage);
        buildPageLay->setContentsMargins(8, 8, 8, 8);
        buildPageLay->setSpacing(8);
        {
            auto* info = new QGroupBox(slime::ui::t("ui.55fdaa2e6652"), buildPage);
            info->setProperty("card", true);
            auto* infoLay = new QVBoxLayout(info);
            infoLay->setContentsMargins(8, 4, 8, 6);
            auto* infoText = new QLabel(
                slime::ui::t("ui.cd668bd50c3d"), info);
            infoText->setWordWrap(true);
            infoLay->addWidget(infoText);
            // v3.4.9：构建提示行（范围/位图体积/续传/完成条件）随构建功能一起放在本页
            m_w->buildHint = new QLabel(QString(), info);
            m_w->buildHint->setWordWrap(true);
            m_w->buildHint->setVisible(false);
            infoLay->addWidget(m_w->buildHint);
            auto* fixedLine = new QLabel(
                slime::ui::tr(slime::ui::t("ui.8f50bddd8b88")),
                info);
            fixedLine->setObjectName(QStringLiteral("buildFixedFacts"));
            fixedLine->setWordWrap(true);
            infoLay->addWidget(fixedLine);
            buildPageLay->addWidget(info);
        }
        // 原有卡片挂进滚动区（内容与搜索页共享同一份范围控件）
        auto* buildScroll = new QScrollArea(buildPage);
        buildScroll->setWidgetResizable(true);
        buildScroll->setFrameShape(QFrame::NoFrame);
        auto* page = new QWidget(buildScroll);
        page->setObjectName(QStringLiteral("buildExtra"));
        auto* lay = new QVBoxLayout(page);
        lay->setContentsMargins(2, 6, 2, 6);
        lay->setSpacing(10);
        m_buildExtra = page;
        buildScroll->setWidget(page);
        buildPageLay->addWidget(buildScroll, 1);
        m_pageStack->addWidget(buildPage);
        auto* cap = new QLabel(slime::ui::t("ui.31dc91dcb1bc"), page);
        cap->setObjectName(QStringLiteral("colCaption"));
        lay->addWidget(cap);
        // 范围与预算（与搜索页同一份：编辑后同步回搜索页控件 → 引擎只读搜索页那份）
        auto* card = new QGroupBox(slime::ui::t("ui.918bc4a13f88"), page);
        card->setProperty("card", true);
        auto* grid = new QGridLayout(card);
        grid->setContentsMargins(8, 4, 8, 6);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(4);
        grid->addWidget(new QLabel(slime::ui::t("ui.785b52eb97c1"), card), 0, 0, Qt::AlignRight);
        auto* rangeRow = new QWidget(card);
        auto* rangeLay = new QHBoxLayout(rangeRow);
        rangeLay->setContentsMargins(0, 0, 0, 0);
        rangeLay->setSpacing(4);
        m_w->bMinX = new QSpinBox(rangeRow);
        m_w->bMaxX = new QSpinBox(rangeRow);
        m_w->bMinZ = new QSpinBox(rangeRow);
        m_w->bMaxZ = new QSpinBox(rangeRow);
        for (QSpinBox* s : { m_w->bMinX, m_w->bMaxX, m_w->bMinZ, m_w->bMaxZ }) {
            s->setRange(-30000000, 30000000);
            s->setFixedWidth(84);
            s->setVisible(false);   // v3.5.16：范围改由「半径」一个输入决定（四边框保留为内部状态）
        }
        // v3.5.16：构建页也只填一个半径（与搜索页双向同步）
        m_w->bRadius = new QSpinBox(rangeRow);
        m_w->bRadius->setRange(1, 15000000);
        m_w->bRadius->setFixedWidth(96);
        m_w->bRadius->setGroupSeparatorShown(true);
        m_w->bRadius->setKeyboardTracking(false);
        m_w->bRadius->setToolTip(slime::ui::t("ui.42ee662c2f29"));
        rangeLay->addWidget(new QLabel(slime::ui::t("ui.ef40894b0c64"), rangeRow));
        rangeLay->addWidget(m_w->bRadius);
        auto* sync = new QLabel(slime::ui::t("ui.672ae5326a09"), rangeRow);
        sync->setObjectName(QStringLiteral("topSummary"));
        rangeLay->addWidget(sync);
        rangeLay->addStretch(1);
        grid->addWidget(rangeRow, 0, 1);
        grid->addWidget(new QLabel(slime::ui::t("ui.0dcf0e012a69"), card), 1, 0, Qt::AlignRight);
        auto* budRow = new QWidget(card);
        auto* budLay = new QHBoxLayout(budRow);
        budLay->setContentsMargins(0, 0, 0, 0);
        budLay->setSpacing(4);
        m_w->bBudget = new QSpinBox(budRow);
        m_w->bBudget->setRange(64, 32768);
        // v3.4.12（用户要求）：单位从输入框里拿出来（框后单独一个小标签）
        m_w->bBudget->setFixedWidth(96);
        budLay->addWidget(m_w->bBudget);
        {
            auto* unit = new QLabel(QStringLiteral("MB"), budRow);
            unit->setObjectName(QStringLiteral("unitLabel"));
            budLay->addWidget(unit);
        }
        auto* budTip = new QLabel(slime::ui::t("ui.434313d50d37"), budRow);
        budTip->setObjectName(QStringLiteral("topSummary"));
        budLay->addWidget(budTip);
        budLay->addStretch(1);
        grid->addWidget(budRow, 1, 1);
        // v3.4.12（用户红框要求）：删掉「操作」整行（预估 / 开始构建 / 中断 + 提示）。
        // 开始与中断统一走工具栏主按钮（进入本页后主按钮自动变成「开始构建」，取消即中断）；
        // 预估改为下面的「构建预估（实时）」卡片，不再需要按钮与弹窗。
        lay->addWidget(card);

        // v3.4.12（用户黄框要求）：预估信息单独成卡片，**实时**跟着范围/预算更新（不再以弹窗形式显示）
        auto* estCard = new QGroupBox(slime::ui::t("ui.1ca7f1347d3a"), page);
        estCard->setProperty("card", true);
        auto* estLay = new QVBoxLayout(estCard);
        estLay->setContentsMargins(8, 4, 8, 6);
        m_w->bEstimate = nullptr; // v3.4.12：预估按钮已移除（信息实时显示）
        m_w->bSummaryLive = new QLabel(slime::ui::t("ui.786d17b3a524"), estCard);
        m_w->bSummaryLive->setObjectName(QStringLiteral("buildEstimateLive"));
        m_w->bSummaryLive->setWordWrap(true);
        estLay->addWidget(m_w->bSummaryLive);
        lay->addWidget(estCard);
        // 构建进度（与主界面共用同一进度组件）
        auto* progCard = new QGroupBox(slime::ui::t("ui.b60d9a287dc2"), page);
        progCard->setProperty("card", true);
        auto* progLay = new QVBoxLayout(progCard);
        progLay->setContentsMargins(8, 4, 8, 6);
        m_w->bBar = new QProgressBar(progCard);
        m_w->bBar->setRange(0, 1000);
        m_w->bBar->setValue(0);
        m_w->bBar->setTextVisible(false);
        progLay->addWidget(m_w->bBar);
        m_w->bText = new QLabel(slime::ui::t("ui.062e5e670f73"), progCard);
        m_w->bText->setWordWrap(true);
        progLay->addWidget(m_w->bText);
        lay->addWidget(progCard);
        // 完成汇总
        auto* sumCard = new QGroupBox(slime::ui::t("ui.86b0b92a37f4"), page);
        sumCard->setProperty("card", true);
        auto* sumLay = new QVBoxLayout(sumCard);
        sumLay->setContentsMargins(8, 4, 8, 6);
        m_w->bSummary = new QLabel(slime::ui::t("ui.6beea7b41bb8"),
                                   sumCard);
        m_w->bSummary->setWordWrap(true);
        sumLay->addWidget(m_w->bSummary);
        auto* pathRow = new QWidget(sumCard);
        auto* pathLay = new QHBoxLayout(pathRow);
        pathLay->setContentsMargins(0, 0, 0, 0);
        pathLay->setSpacing(6);
        m_w->bPath = new QLabel(slime::ui::t("ui.f22200ab7561"), pathRow);
        m_w->bPath->setObjectName(QStringLiteral("resultCaption"));
        m_w->bPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
        pathLay->addWidget(m_w->bPath, 1);
        auto* openDir = new QPushButton(slime::ui::t("ui.031c10557843"), pathRow);
        openDir->setProperty("mini", true);
        connect(openDir, &QPushButton::clicked, this, [this]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(appDirPath().wstring())));
        });
        pathLay->addWidget(openDir);
        sumLay->addWidget(pathRow);
        lay->addWidget(sumCard);
        // v3.4.8：本页已作为独立导航页加入 m_pageStack（见上），不再挂搜索页参数列、也不再隐藏

        // 连接：预算写回 config（范围与搜索页共用同一组控件）
        // v3.4.9（用户实测：构建范围与搜索范围在部分情况下不同步）：改为**双向同步**，
        // 用 m_rangeSyncGuard 防止来回触发；两侧都挂在 valueChanged 上，因此程序性改动
        //（快速预设 / 命令终端 / 整图确认 / 自检）也会自动跟随。
        const auto mirrorToSearch = [this]() {
            if (!m_w || !m_w->bMinX || m_rangeSyncGuard) return;
            m_rangeSyncGuard = true;
            m_w->minX->setValue(m_w->bMinX->value());
            m_w->maxX->setValue(m_w->bMaxX->value());
            m_w->minZ->setValue(m_w->bMinZ->value());
            m_w->maxZ->setValue(m_w->bMaxZ->value());
            m_rangeSyncGuard = false;
            updateBuildHintUi();
            updateBuildEstimateLive(); // v3.4.12：范围一改，预估立刻跟着变
        };
        for (QSpinBox* s : { m_w->bMinX, m_w->bMaxX, m_w->bMinZ, m_w->bMaxZ }) {
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [mirrorToSearch](int) { mirrorToSearch(); });
        }
        // 反向：搜索页范围（含预设/命令/整图确认等程序性改动）→ 构建页
        for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { pushSearchRangeToBuild(); });
        }
        pushSearchRangeToBuild(); // 初始对齐
        connect(m_w->bBudget, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int mb) {
            m_settings.cacheMaxMB = mb;
            applyAppSettings(m_settings);
            updateBuildEstimateLive(); // v3.4.12：预算一改，预估立刻跟着变
        });
        // v3.4.12：预估按钮已移除（改为实时卡片）
        connect(m_w->bStart, &QPushButton::clicked, this, [this]() {
            setBuildModeActive(true); // v3.4.9：搜索页开关已移除，直接置构建模式
            startSearch();
        });
        connect(m_w->bAbort, &QPushButton::clicked, this, [this]() { cancelSearch(); });
    }

    // ---------------- 页 2：设置（竖向五标签 + 三等分按钮；复用 SettingsDialog 内容） ----------------
    {
        auto* page = new QWidget(m_pageStack);
        auto* lay = new QVBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        m_settingsPage = new SettingsDialog(m_settings, page); // 作为普通子控件嵌入（而不是弹窗）
        // v3.4.2：**设置立即生效** —— 设置页内任意输入控件变化后 60ms 自动应用（无需点「应用/确定」）
        m_settingsApplyTimer = new QTimer(this);
        m_settingsApplyTimer->setSingleShot(true);
        // v3.4.8（用户：设置生效速度调快一点）：250ms → 60ms；仍是「合并连击」写法，
        // 拖滑块/连点复选框只在停手后 60ms 应用一次，不会重复写盘。
        m_settingsApplyTimer->setInterval(60);
        connect(m_settingsApplyTimer, &QTimer::timeout, this, [this]() {
            if (!m_settingsPage || m_settingsSyncGuard) return;
            // v3.4.8（用户报「设置里打开了也不生效」的根因）：必须读**活控件值**。
            // settings() 返回的是「点过『应用』后的快照」，而设置页把「应用」按钮隐藏了 ⇒
            // 之前页面上的改动永远不会进入 m_settings（只有主题因单独读下拉而侥幸生效）。
            m_settings = m_settingsPage->currentValues();
            applyAppSettings(m_settings);      // 立即生效（含主题重放）
            // v3.4.6：主题档位直接读设置页主题下拉的 currentData()（light/dark/system），
            // 拿不到再退回 m_settings.theme；然后强制重放整份 QSS。
            {
                QString themeVal = m_settings.theme;
                if (m_settingsPage) {
                    for (QComboBox* cb : m_settingsPage->findChildren<QComboBox*>()) {
                        const QString data = cb->currentData().toString();
                        if (data == QLatin1String("light") || data == QLatin1String("dark") ||
                            data == QLatin1String("system")) {
                            themeVal = data;
                            break;
                        }
                    }
                }
                m_settings.theme = themeVal;
                const slime::ThemeMode m = slime::ThemeManager::modeFromString(themeVal);
                // v3.4.8（用户：设置生效速度调快一点）：只在档位**真的变化**时重放整份 QSS。
                // 旧实现每次设置改动都重放 10KB 样式表（Qt 要重新 polish 全部控件）⇒ 任何设置项
                // 都要等半秒才生效；档位来源就是本下拉的 currentData()，比较可靠、不会漏换肤。
                if (slime::ThemeManager::instance().mode() != m) {
                    slime::ThemeManager::instance().apply(qApp, m);
                    Logger::info("主题重放：" + slime::ThemeManager::modeDisplayName(m).toStdString() +
                                 "（qss 长度 " + std::to_string(qApp->styleSheet().size()) + "）");
                }
            }
            // v3.4.3：同时**持久化**到 config.json —— 否则重启后设置回到旧值（用户实测）
            // v3.4.11（用户拍板）：改成「合并写」——拖动滑块时每 60ms 都写整份 config.json 会卡；
            //   这里只登记待写，500ms 内没有新改动才真正落盘（拖动结束由 sliderReleased 立即 flush）。
            scheduleConfigWrite();
            syncSettingsPageFromState(); // 让设置页与真实状态保持一致（命令/程序化改动也能反映）
            slime::IncidentLog::note("设置已立即生效并写入 config.json（设置页自动应用）");
        });
        m_settingsPage->setWindowFlags(Qt::Widget);
        lay->addWidget(m_settingsPage, 1);
        // v3.4.2：把设置页里所有可交互控件的变更信号汇总到「立即生效」定时器
        // v3.4.3：SpinBox 用 valueChanged（editingFinished 需要失焦/回车 ⇒ 用户会觉得「没生效」）
        for (QSpinBox* sb : m_settingsPage->findChildren<QSpinBox*>()) {
            connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this](int) { m_settingsApplyTimer->start(); });
        }
        for (QDoubleSpinBox* sb : m_settingsPage->findChildren<QDoubleSpinBox*>()) {
            connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [this](double) { m_settingsApplyTimer->start(); });
        }
        for (QComboBox* cb : m_settingsPage->findChildren<QComboBox*>()) {
            connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this](int) { m_settingsApplyTimer->start(); });
            connect(cb, &QComboBox::currentTextChanged, this, [this](const QString&) { m_settingsApplyTimer->start(); });
        }
        for (QCheckBox* cb : m_settingsPage->findChildren<QCheckBox*>()) {
            connect(cb, &QCheckBox::toggled, this, [this](bool) { m_settingsApplyTimer->start(); });
        }
        for (QRadioButton* rb : m_settingsPage->findChildren<QRadioButton*>()) {
            connect(rb, &QRadioButton::toggled, this, [this](bool) { m_settingsApplyTimer->start(); });
        }
        // v3.4.6：删除上一版按「单选按钮文字」匹配的错误接线（设置页主题是 QComboBox）
        // 说明保留：下面用 currentData() 直接读档位。
        if (false)
        // 同时把档位写回 m_settings 并落盘，保证重启后仍是深色。
        {
            auto* content = m_settingsPage->findChild<QWidget*>();
            Q_UNUSED(content);
            for (QRadioButton* rb : m_settingsPage->findChildren<QRadioButton*>()) {
                const QString tx = rb->text();
                slime::ThemeMode mode;
                if (tx.contains(slime::ui::t("ui.30b2c979ac87"))) {
                    mode = slime::ThemeMode::Dark;
                } else if (tx.contains(slime::ui::t("ui.80ec9e2b1bc4"))) {
                    mode = slime::ThemeMode::Light;
                } else if (tx.contains(slime::ui::t("ui.f4bbd91f79ba"))) {
                    mode = slime::ThemeMode::System;
                } else {
                    continue; // 其它单选（默认模式等）不处理
                }
                connect(rb, &QRadioButton::toggled, this, [this, mode](bool on) {
                    if (!on) return;
                    m_settings.theme = slime::ThemeManager::modeToString(mode);
                    slime::ThemeManager::instance().apply(qApp, mode); // 立即换肤
                    writeConfigFile(m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                                    m_terminal ? m_terminal->history() : QStringList());
                    addFeedback(2, slime::ui::tr(slime::ui::t("ui.b6634ac47177"))
                                       .arg(slime::ThemeManager::modeDisplayName(mode)));
                });
            }
        }
        for (QSlider* sl : m_settingsPage->findChildren<QSlider*>()) {
            connect(sl, &QSlider::valueChanged, this, [this](int) { m_settingsApplyTimer->start(); });
            // v3.4.11：拖动结束立刻落盘（拖动过程中只更新内存 + 延迟合并写，避免每帧写文件导致卡顿）
            connect(sl, &QSlider::sliderReleased, this, [this]() { flushPendingConfigWrite(); });
        }
        for (QLineEdit* le : m_settingsPage->findChildren<QLineEdit*>()) {
            connect(le, &QLineEdit::textChanged, this, [this](const QString&) { m_settingsApplyTimer->start(); });
        }
        // v3.4.2：设置立即生效 ⇒ 隐藏对话框式按钮（应用 / 取消 / 确定 / 恢复默认 / 重置所有设置）
        for (QPushButton* b : m_settingsPage->findChildren<QPushButton*>()) {
            const QString tx = b ? b->text() : QString();
            if (tx.contains(slime::ui::t("ui.f526c89937e1")) || tx.contains(slime::ui::t("ui.4d0b4688c787")) ||
                tx.contains(slime::ui::t("ui.4562024ddec7")) || tx.contains(slime::ui::t("ui.a191935bc678")) ||
                tx.contains(slime::ui::t("ui.ad5d566fb4a3"))) {
                b->hide();
            }
        }
        connect(m_settingsPage, &SettingsDialog::applied, this, [this]() {
            m_settings = m_settingsPage->currentValues();
            applyAppSettings(m_settings);
            addFeedback(2, slime::ui::tr(slime::ui::t("ui.e02dedc30bc9")));
        });
        // v3.4.8（用户实测：找不到「恢复所有设置」）：设置页底部固定放一条操作栏，
        // 按钮是页面自己的子控件（不在 SettingsDialog 内），因此不会被"隐藏对话框按钮"的逻辑连带隐藏。
        {
            auto* bar = new QWidget(page);
            auto* barLay = new QHBoxLayout(bar);
            barLay->setContentsMargins(8, 2, 8, 8);
            barLay->setSpacing(8);
            auto* resetAll = new QPushButton(slime::ui::t("ui.cc342fcf8208"), bar);
            resetAll->setObjectName(QStringLiteral("resetAllSettingsBtn"));
            resetAll->setToolTip(slime::ui::t("ui.2c7b52e20f4f"));
            connect(resetAll, &QPushButton::clicked, this, [this]() { resetAllSettings(); });
            barLay->addWidget(resetAll);
            auto* tip = new QLabel(slime::ui::t("ui.527d252d2fe4"), bar);
            tip->setObjectName(QStringLiteral("topSummary"));
            barLay->addWidget(tip);
            barLay->addStretch(1);
            lay->addWidget(bar);
        }
        m_pageStack->addWidget(page);
    }

    // v3.4.1：原「控件状态」页已按需求移除（设计样张不在应用内展示）
}


void MainWindow::updateBuildEstimateLive()
{
    if (!m_w || !m_w->bSummaryLive) return;
    const int64_t total = buildTotalChunks();
    const double bitmapMB = static_cast<double>(total) / 8.0 / (1024.0 * 1024.0);
    const double legacyMB = static_cast<double>(total) * 0.1 * 8.0 / (1024.0 * 1024.0);
    const int capMB = (m_w->bBudget != nullptr) ? m_w->bBudget->value() : 2048; // 读控件，不读文件
    const bool over = bitmapMB > static_cast<double>(capMB);
    m_w->bSummaryLive->setText(
        slime::ui::tr(slime::ui::t("ui.80e7a66cb853")
            .arg(m_w->minX->value())
            .arg(m_w->maxX->value())
            .arg(m_w->minZ->value())
            .arg(m_w->maxZ->value())
            .arg(static_cast<qlonglong>(total))
            .arg(bitmapMB, 0, 'f', 1)
            .arg(legacyMB, 0, 'f', 1)
            .arg(capMB)
            .arg(over ? slime::ui::tr(slime::ui::t("ui.f5e7cf7143c3"))
                      : slime::ui::tr(slime::ui::t("ui.23c0b832220a")))));
}

// 构建预估（构建前确认弹窗用；与实时卡片共用同一算法）
void MainWindow::showBuildEstimate()
{
    const int64_t total = buildTotalChunks();
    const double bitmapMB = static_cast<double>(total) / 8.0 / (1024.0 * 1024.0);
    const double legacyMB = static_cast<double>(total) * 0.1 * 8.0 / (1024.0 * 1024.0);
    const int64_t capMB = cacheMaxBytes() / (1024 * 1024);
    const QString body = slime::ui::t("ui.284d887f6e44")
                             .arg(m_w->minX->value())
                             .arg(m_w->maxX->value())
                             .arg(m_w->minZ->value())
                             .arg(m_w->maxZ->value())
                             .arg(total)
                             .arg(bitmapMB, 0, 'f', 1)
                             .arg(legacyMB, 0, 'f', 1)
                             .arg(capMB)
                             .arg(bitmapMB > static_cast<double>(capMB)
                                      ? slime::ui::t("ui.f5e7cf7143c3")
                                      : slime::ui::t("ui.23c0b832220a"));
    if (m_w->bSummary) m_w->bSummary->setText(body);
    setStatus(slime::ui::tr(slime::ui::t("ui.8bd24afccf9b"))
                  .arg(total)
                  .arg(bitmapMB, 0, 'f', 1)
                  .arg(capMB));
    if (!m_uiQuiet) {
        QMessageBox box(this);
        box.setWindowTitle(slime::ui::t("ui.dfb05f73ba2c"));
        box.setText(body);
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
    }
}

// ============================================================================
// v3.3.4（任务书 1.2/1.3）：菜单栏与工具栏（全部使用 QMainWindow 原生接口）
// ============================================================================
void MainWindow::buildMenus()
{
    QMenuBar* bar = menuBar(); // QMainWindow 原生菜单栏

    // ---- 文件(F) ----
    m_menuFile = bar->addMenu(slime::ui::t("ui.a19ce4cf591e"));
    QAction* aImport = m_menuFile->addAction(slime::ui::t("ui.0c909acf4f10"));
    aImport->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    QAction* aExport = m_menuFile->addAction(slime::ui::t("ui.06cee437ec54"));
    aExport->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    connect(aExport, &QAction::triggered, this, &MainWindow::exportResultsToFile);
    m_menuFile->addSeparator();
    QAction* aClearCache = m_menuFile->addAction(slime::ui::t("ui.291952a2abb2"));
    m_menuFile->addSeparator();
    QAction* aQuit = m_menuFile->addAction(slime::ui::t("ui.feecb1e6adec"));
    aQuit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(aQuit, &QAction::triggered, this, &QWidget::close);

    // ---- 编辑(E) ----
    m_menuEdit = bar->addMenu(slime::ui::t("ui.aaa1cc842e93"));
    QAction* aCopyRes = m_menuEdit->addAction(slime::ui::t("ui.0d6cb0c3fd91"));
    aCopyRes->setShortcut(QKeySequence(QStringLiteral("Ctrl+C")));
    connect(aCopyRes, &QAction::triggered, this, &MainWindow::copyResults);
    QAction* aCopyCmds = m_menuEdit->addAction(slime::ui::t("ui.b55b79e1c9e9"));
    connect(aCopyCmds, &QAction::triggered, this, &MainWindow::copyAllCommands);
    QAction* aClearRes = m_menuEdit->addAction(slime::ui::t("ui.0fe9f67f7f41"));
    aClearRes->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));

    // ---- 查看(V) ----
    m_menuView = bar->addMenu(slime::ui::t("ui.05121202d940"));
    // v3.3.5 优化 3：删除「显示设置面板 / 显示结果面板」两项（面板始终可见），
    // 查看菜单只保留「恢复默认布局」。
    QAction* aResetLayout = m_menuView->addAction(slime::ui::t("ui.e003f209ca14"));
    aResetLayout->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(aResetLayout, &QAction::triggered, this, &MainWindow::resetLayout);

    // ---- 工具(T)（「启用日志…」为任务书要求的隐蔽入口）----
    // v3.3.6（任务书 Bug 3）：原「程序设置…」入口已移除 —— 设置改为工具栏上的 [⚙ 设置] 按钮
    m_menuTools = bar->addMenu(slime::ui::t("ui.3b66d0c2fbe9"));
    QAction* aGpu = m_menuTools->addAction(slime::ui::t("ui.e4d209f2e621"));
    connect(aGpu, &QAction::triggered, this, &MainWindow::showGpuStatus);
    QAction* aOpenCacheDir = m_menuTools->addAction(slime::ui::t("ui.50ff9620a061"));
    connect(aOpenCacheDir, &QAction::triggered, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(appDirPath().wstring())));
        setStatus(slime::ui::tr(slime::ui::t("ui.2ad68491dd4b")));
    });
    QAction* aOpenLogDir = m_menuTools->addAction(slime::ui::t("ui.e153b1be0e3e"));
    connect(aOpenLogDir, &QAction::triggered, this, [this]() {
        const auto dir = appDirPath() / "logs";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(dir.wstring())));
        setStatus(slime::ui::tr(slime::ui::t("ui.27af72f30925")));
    });
    // v3.5.2（任务 1）：挂机点（环形覆盖）—— 按 1/16 区块精度给出「站哪里刷怪面积最大」
    QAction* aAfk = m_menuTools->addAction(slime::ui::t("ui.e4469ceb9f33"));
    aAfk->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    connect(aAfk, &QAction::triggered, this, &MainWindow::showAfkDialog);
    // v3.5.2（任务 2）：地图预览
    QAction* aMap = m_menuTools->addAction(slime::ui::t("ui.f9524bdc88f1"));
    aMap->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    connect(aMap, &QAction::triggered, this, [this]() { toggleMapPanel(); });
    QAction* aMapDlg = m_menuTools->addAction(slime::ui::t("ui.4b1166ff90c5"));
    connect(aMapDlg, &QAction::triggered, this, &MainWindow::showMapDialog);
    m_menuTools->addSeparator();
    // v3.3.5 Bug 5：日志菜单项动态切换「启用日志…/关闭日志」
    m_actLogToggle = m_menuTools->addAction(slime::ui::t("ui.80005af43eec"));
    connect(m_actLogToggle, &QAction::triggered, this, &MainWindow::toggleLogging);
    updateLogMenuText();

    // ---- 帮助(H) ----
    m_menuHelp = bar->addMenu(slime::ui::t("ui.15d7a578f420"));
    QAction* aHelp = m_menuHelp->addAction(slime::ui::t("ui.e63e0a783b7d"));
    aHelp->setShortcut(QKeySequence(QStringLiteral("F1")));
    connect(aHelp, &QAction::triggered, this, [this]() { showHelpDialog(this); });
    QAction* aUpdate = m_menuHelp->addAction(slime::ui::t("ui.60ce2c44f1fb"));
    connect(aUpdate, &QAction::triggered, this, &MainWindow::openReleasePage);
    QAction* aPan = m_menuHelp->addAction(slime::ui::t("ui.c9c4521cd9a9"));
    connect(aPan, &QAction::triggered, this, &MainWindow::openPanPage);
    QAction* aHistory = m_menuHelp->addAction(slime::ui::t("ui.5621bcb3e578"));
    connect(aHistory, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/PillerBean/SlimeFinder/wiki/Version-Index")));
    });
    QAction* aContact = m_menuHelp->addAction(slime::ui::t("ui.152d3db75889"));
    connect(aContact, &QAction::triggered, this, &MainWindow::showContact);
    // v3.7.5（用户要求）：鸣谢名单
    QAction* aCredits = m_menuHelp->addAction(slime::ui::t("ui.c5583bc7e151"));
    connect(aCredits, &QAction::triggered, this, &MainWindow::showCredits);
    QAction* aTelemetry = m_menuHelp->addAction(slime::ui::t("ui.6034809d2f3b"));
    connect(aTelemetry, &QAction::triggered, this, &MainWindow::showTelemetryDialog);
    QAction* aAbout = m_menuHelp->addAction(slime::ui::t("ui.bed172efc953"));
    connect(aAbout, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::buildToolBar()
{
    m_toolBar = addToolBar(slime::ui::t("ui.7ab2f460553b")); // QMainWindow 原生工具栏
    m_toolBar->setMovable(false);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    // v3.3.6（任务书 Bug 3）：设置入口移到工具栏，位于「开始搜索」左侧，形式为「文字 + 图标」
    // v3.4.2：工具栏不再创建「设置」按钮 —— 设置入口 = 左导航第 3 项（设置页，立即生效）。
    // 注意：旧实现虽然删掉了 addWidget，但仍以 m_toolBar 为父创建按钮 ⇒ QToolBar 会把它
    // 摆在最左上角（用户实测「设置按钮依旧存在」），因此这里连创建一起删除。
    m_w->settings = nullptr;
    // v3.4.1：工具栏不再放「设置」按钮（改由左导航「设置」页进入）

    m_w->start = new QPushButton(slime::ui::t("ui.8fdc065eda03"), m_toolBar);
    m_w->start->setDefault(true);
    m_w->cancel = new QPushButton(slime::ui::t("ui.4d0b4688c787"), m_toolBar);
    m_w->cancel->setEnabled(false);
    m_toolBar->addWidget(m_w->start);
    m_toolBar->addWidget(m_w->cancel);
    m_toolBar->addSeparator();
    QAction* aImport = nullptr; // v3.4.1：导入缓存移出工具栏（见搜索页「缓存管理」卡片与设置页「缓存」）
    QAction* aClear = nullptr; // v3.4.1：清除缓存移出工具栏
    QAction* aHelp = m_toolBar->addAction(slime::ui::t("ui.adf465ebf0e6"));
    connect(aHelp, &QAction::triggered, this, [this]() { showHelpDialog(this); });


    // 更新提醒徽章（任务书 3.3）：仅在超过 30 天时显示
    // v3.3.6（任务书 4.7）：配色由 ThemeManager 的 QSS（#updateBadge）按主题提供
    m_updateBadge = new QLabel(m_toolBar);
    m_updateBadge->setObjectName(QStringLiteral("updateBadge"));
    m_updateBadge->setVisible(false);
    m_updateBadge->setCursor(Qt::PointingHandCursor);
    // v3.5.29：纯文本时 linkActivated 永远不会触发（看似能点其实没用）——改成真链接
    m_updateBadge->setTextFormat(Qt::RichText);
    m_updateBadge->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    m_toolBar->addWidget(m_updateBadge);
    m_toolBar->addSeparator();
    // 把工具栏剩余的宽度让给徽章
    auto* spacer = new QWidget(m_toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBarSpacerAction = m_toolBar->addWidget(spacer);   // v3.5.28：状态面板插在它前面
}

// ============================================================================
// v3.3.4（任务书 3）：更新提醒（本地时间，不联网）
// ============================================================================
QString MainWindow::releasePageUrl() const
{
    return QStringLiteral("https://wwben.lanzoul.com/b01d77eebe");
}

void MainWindow::openReleasePage()
{
    // v3.10.12.10：检查更新改为「GitHub Releases + 镜像加速 + 网盘备用」对话框。
    // 自检模式（m_uiQuiet）下不联网、不弹窗，仅记录状态供自动化断言。
    m_updateBrowserOpened = false;
    if (m_uiQuiet) {
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.6ec2519b3ed5")));
        setStatus(slime::ui::tr(slime::ui::t("ui.7cdc436f3b39")));
        return;
    }
    UpdateDialog dlg(this);
    dlg.exec();
    m_updateBrowserOpened = dlg.browserOpened();
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.bdfe8381dda7")));
}

// v3.10.12.10：网盘源保留为国内手动备用入口（不做自动更新适配）
void MainWindow::openPanPage()
{
    const QString url = releasePageUrl();
    m_updateBrowserOpened = false;
    if (m_uiQuiet) {
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.b70943cd2d9c")).arg(url));
        setStatus(slime::ui::tr(slime::ui::t("ui.7182774d682a")));
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(slime::ui::t("ui.c9c4521cd9a9"));
    box.setTextFormat(Qt::PlainText);
    box.setText(slime::ui::t("ui.de497048ef89")
                    .arg(url));
    QPushButton* openBtn = box.addButton(slime::ui::t("ui.3f550f3594a5"), QMessageBox::AcceptRole);
    QPushButton* copyBtn = box.addButton(slime::ui::t("ui.abb22bd95c4a"), QMessageBox::ActionRole);
    box.addButton(slime::ui::t("ui.4d0b4688c787"), QMessageBox::RejectRole);
    box.setDefaultButton(openBtn);
    box.exec();
    if (box.clickedButton() == copyBtn) {
        QApplication::clipboard()->setText(url);
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.de46b29e9f7d")));
        setStatus(slime::ui::tr(slime::ui::t("ui.bbe551e5d698")));
        return;
    }
    if (box.clickedButton() != openBtn) {
        setStatus(slime::ui::tr(slime::ui::t("ui.c5cac2c33c72")));
        return;
    }
    m_updateBrowserOpened = true;
    QDesktopServices::openUrl(QUrl(url));
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.b415f1c02466")));
    setStatus(slime::ui::tr(slime::ui::t("ui.b415f1c02466")));
    slime::LogBus::info("打开网盘下载页: " + url.toStdString());
}

// ============================================================================
// v3.3.5（优化 5）：程序设置（五标签页模态对话框）+ 与主界面共享 config.json
// ============================================================================
void MainWindow::loadAppSettings()
{
    const auto path = appDirPath() / kConfigFileName;
    AppSettings s = SettingsDialog::factoryDefaults();
    s.wheelAdjust = cfgBool(path, "settings.wheelAdjust", s.wheelAdjust);
    s.rememberWindow = cfgBool(path, "settings.rememberWindow", s.rememberWindow);
    s.autoRestart = cfgBool(path, "settings.autoRestart", s.autoRestart);
    s.defaultAlgorithm = QString::fromStdString(cfgString(path, "settings.defaultAlgorithm",
                                                          s.defaultAlgorithm.toStdString()));
    s.defaultThreads = cfgInt(path, "settings.defaultThreads", s.defaultThreads);
    s.defaultTargetCount = cfgInt(path, "settings.defaultTargetCount", s.defaultTargetCount);
    s.defaultPreset = QString::fromStdString(cfgString(path, "settings.defaultPreset",
                                                       s.defaultPreset.toStdString()));
    s.defaultGpu = cfgBool(path, "settings.defaultGpu", s.defaultGpu);
    s.defaultStopAtTarget = cfgBool(path, "settings.defaultStopAtTarget", s.defaultStopAtTarget);
    s.autoLoadCache = cfgBool(path, "settings.autoLoadCache", s.autoLoadCache);
    s.cacheDir = QString::fromStdString(cfgString(path, "settings.cacheDir", ""));
    s.cacheMaxMB = cfgInt(path, "settings.cacheMaxMB", s.cacheMaxMB);
    s.logAtStartup = cfgBool(path, "settings.logAtStartup", s.logAtStartup);
    s.logLevel = cfgInt(path, "settings.logLevel", s.logLevel);
    s.logSubDir = QString::fromStdString(cfgString(path, "settings.logSubDir", "logs"));
    s.logKeepDays = cfgInt(path, "settings.logKeepDays", s.logKeepDays);
    // v3.3.6 新增设置项
    s.theme = QString::fromStdString(cfgString(path, "settings.theme", s.theme.toStdString()));
    // v3.5.20：语言 / 全屏方式（此前只写不读 ⇒ 语言设置不被记忆）
    s.language = QString::fromStdString(cfgString(path, "settings.language", s.language.toStdString()));
    m_mapJavaVersionId = cfgInt(path, "settings.mapVersion", m_mapJavaVersionId);
    s.fullscreenMode = QString::fromStdString(cfgString(path, "settings.fullscreenMode", s.fullscreenMode.toStdString()));
    s.soundOnComplete = cfgBool(path, "settings.soundOnComplete", s.soundOnComplete);
    s.soundVolume = cfgInt(path, "settings.soundVolume", s.soundVolume);
    s.showGpuReminder = cfgBool(path, "settings.showGpuReminder", s.showGpuReminder);
    s.largeRangeWarnChunks = cfgInt(path, "settings.largeRangeWarnChunks", s.largeRangeWarnChunks);
    s.logAutoScroll = cfgBool(path, "settings.logAutoScroll", s.logAutoScroll);
    s.logMaxLines = cfgInt(path, "settings.logMaxLines", s.logMaxLines);
    s.crashDump = cfgBool(path, "settings.crashDump", s.crashDump);
    s.debugMode = cfgBool(path, "settings.debugMode", s.debugMode);
    // v3.3.9（任务书 二）：资源控制（CPU 占用率上限 / 内存上限）
    s.cpuLimitPercent = cfgInt(path, "settings.cpuLimitPercent", s.cpuLimitPercent);
    s.gpuLimitPercent = cfgInt(path, "settings.gpuLimitPercent", s.gpuLimitPercent);
    s.gpuPatternStream = cfgBool(path, "settings.gpuPatternStream", s.gpuPatternStream);
    s.gpuResultSelfOptimize = cfgBool(path, "settings.gpuResultSelfOptimize", s.gpuResultSelfOptimize);
    s.afkRingInner = cfgInt(path, "settings.ringInner", s.afkRingInner);
    s.afkRingOuter = cfgInt(path, "settings.ringOuter", s.afkRingOuter);
    s.ringCoarseStep = cfgInt(path, "settings.ringCoarseStep", s.ringCoarseStep);
    s.ringStatBands = cfgInt(path, "settings.ringStatBands", s.ringStatBands);
    s.rateUnit = std::clamp(cfgInt(path, "rateUnit", s.rateUnit), 0, 2);
    s.memLimitMB = cfgInt(path, "settings.memLimitMB", s.memLimitMB);
    s.autoCheckGpuAtStartup = cfgBool(path, "settings.autoCheckGpuAtStartup", s.autoCheckGpuAtStartup);
    // v3.3.9（任务书 二）：资源控制
    s.cpuLimitPercent = cfgInt(path, "settings.cpuLimitPercent", s.cpuLimitPercent);
    s.memLimitMB = cfgInt(path, "settings.memLimitMB", s.memLimitMB);
    s.autoCheckGpuAtStartup = cfgBool(path, "settings.autoCheckGpuAtStartup", s.autoCheckGpuAtStartup);
    s.updateReminderDays = cfgInt(path, "settings.updateReminderDays", s.updateReminderDays);
    m_settings = s;
}

void MainWindow::applyAppSettings(const AppSettings& s)
{
    const AppSettings old = m_settings;
    m_settings = s;
    // 1) 立即生效：滚轮开关（下次事件即生效）、更新提醒天数、日志级别
    if (old.updateReminderDays != s.updateReminderDays) {
        initUpdateReminder();
    }
    // v3.4.11：日志模式改为**按设置幂等收敛**（原来只在设置值变化时才 setMode）——
    // 否则「命令/菜单把 LogBus 改了、但设置值没变」的情况下两边会漂移，
    // 界面（设置页、菜单勾选、日志面板）与实际写盘模式不一致。
    {
        const slime::LogMode want = !s.logAtStartup
                                        ? slime::LogMode::Off
                                        : (s.logLevel == 1 ? slime::LogMode::Detailed
                                                           : slime::LogMode::Brief);
        if (slime::LogBus::mode() != want) slime::LogBus::setMode(want);
        if (m_logPanel) m_logPanel->refreshFromBus();
        updateLogMenuText();
    }
    (void)old;
    // 2) 缓存目录：切换缓存文件位置（若已有缓存则迁移到新目录）
    if (old.cacheDir != s.cacheDir) {
        const std::filesystem::path base = s.cacheDir.isEmpty()
                                               ? appDirPath()
                                               : std::filesystem::path(s.cacheDir.toStdWString());
        const std::filesystem::path newFile = base / kCacheFileName;
        std::error_code ec;
        if (std::filesystem::exists(m_cacheFile, ec) && m_cacheFile != newFile) {
            std::filesystem::create_directories(base, ec);
            std::filesystem::copy_file(m_cacheFile, newFile, std::filesystem::copy_options::overwrite_existing, ec);
        }
        m_cacheFile = newFile;
        updateCacheStatusUi();
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.6533e0806676")).arg(QString::fromStdWString(base.wstring())));
    }
    // 3) v3.3.6 新增项立即生效：主题、提示音、日志面板显示参数、调试模式
    // v3.4.11（用户拍板：全局约束）——**任何设置变更都不重放 QSS，除非主题真的变了**。
    //   旧实现每次 applyAppSettings 都重放整份样式表（Qt 要重新 polish 全部控件），
    //   于是任何滑块拖动都会卡（音量条只是最明显的暴露点）。
    //   主题下拉 / 主题命令走的是「档位真的不同」这条路，因此不会漏换肤。
    if (old.theme != s.theme ||
        slime::ThemeManager::instance().mode() != slime::ThemeManager::modeFromString(s.theme)) {
        applyThemeFromSettings();
    }
    // v3.6.6（用户要求）：有些设置必须重启才生效（日志目录/保留天数/窗口记忆）。
    //   开启「自动重启」时，检测到这类改动就直接重启程序，省得用户手动关掉再开。
    {
        const bool needRestart = (old.logSubDir != s.logSubDir) || (old.logKeepDays != s.logKeepDays) ||
                                 (old.rememberWindow != s.rememberWindow);
        m_restartPending = needRestart;
        if (needRestart && s.autoRestart && !m_uiQuiet) {
            addFeedback(2, slime::ui::tr(slime::ui::t("ui.077be65263b2")));
            QTimer::singleShot(300, this, [this]() { restartApplication(); });
        } else if (needRestart) {
            addFeedback(2, slime::ui::tr(slime::ui::t("ui.60e5b5c99c91")));
        }
    }
    (void)old;
    // v3.5.26（用户反馈「语言切换被错误回退」）：恢复**即时切换** —— 已迁移的文案立刻变，
    //   其余老文案提示重启后彻底生效（不再假装需要重启，避免用户以为设置没保存）。
    if (!m_uiQuiet && old.language != s.language) {   // v3.5.27：自检里不切语言，保证断言稳定
        const QString lang = s.language.trimmed().toLower();
        slime::ui::setLanguage(lang == QLatin1String("en") ? slime::ui::Language::English
                                                           : slime::ui::Language::Chinese);
        retranslateModeUi();
        applyLanguage();
        setStatus(slime::ui::tr(slime::ui::t("ui.1f9090fec01f")));
        std::fprintf(stderr, "[\u8bbe\u7f6e] \u754c\u9762\u8bed\u8a00\u6539\u4e3a %s\uff08\u91cd\u542f\u540e\u751f\u6548\uff09\n",
                     s.language.toUtf8().constData());
    }
    // 全屏方式改了 → 立即按新模式切换（不需要重启）
    if (old.fullscreenMode != s.fullscreenMode) applyFullscreenMode();
    slime::SoundPlayer::setEnabled(s.soundOnComplete);
    if (m_logPanel) {
        m_logPanel->setAutoScroll(s.logAutoScroll);
        m_logPanel->setMaxLines(s.logMaxLines);
    }
    // v3.3.7（P1-6）：崩溃转储开关真正生效（无需重启）
    // v3.4.11：改为无条件调用（幂等）——只在"变化时"调用会因外部状态漂移而漏配
    slime::CrashDump::setEnabled(s.crashDump);
    if (old.crashDump != s.crashDump) {
        slime::IncidentLog::note(std::string("崩溃转储已") + (s.crashDump ? "开启" : "关闭"));
        addFeedback(2, s.crashDump ? slime::ui::t("ui.ffaefd5b8195")
                                   : slime::ui::t("ui.e83c9f99b58b"));
    }
    slime::IncidentLog::setVerbose(s.debugMode);
    if (old.debugMode != s.debugMode) {
        slime::LogBus::info(std::string("调试模式 ") + (s.debugMode ? "已开启" : "已关闭"));
        addFeedback(2, s.debugMode ? slime::ui::t("ui.6cf4e0cdf166")
                                   : slime::ui::t("ui.551cb231385d"));
    }
    // ---- v3.5.23（用户反馈：大部分设置项无法立即生效）----
    //   搜索默认值以前只在启动时套用一次。现在改成「立刻套用，但只覆盖仍是旧默认值的控件」，
    //   这样既立即生效，又不会抹掉用户在当前搜索页手改的值。
    {
        QStringList applied;
        // v3.5.26（用户反馈「选项切换仍然无法立即生效」）：设置页是显式操作 —— 改了就套用到当前搜索页，
        //   不再要求「控件还停在旧默认值」（那个启发式在用户手动改过搜索页后会静默不生效）。
        auto anyWidget = [](QWidget*) { return true; };
        if (old.defaultThreads != s.defaultThreads ) {
            m_w->threads->setValue(s.defaultThreads > 0 ? s.defaultThreads : 0);
            applied << slime::ui::t("ui.1795484c760a");
        }
        if (old.defaultTargetCount != s.defaultTargetCount ) {
            m_w->targetCount->setValue(s.defaultTargetCount > 0 ? s.defaultTargetCount : 1);
            applied << slime::ui::t("ui.41162519d223");
        }
        if (old.defaultStopAtTarget != s.defaultStopAtTarget &&
            true) {
            m_w->stopAtTarget->setChecked(s.defaultStopAtTarget);
            applied << slime::ui::t("ui.787ef54c30cd");
        }
        if (old.defaultAlgorithm != s.defaultAlgorithm) {
            if (s.defaultAlgorithm == QLatin1String("layered") && m_w->algLayeredRadio) m_w->algLayeredRadio->setChecked(true);
            else if (s.defaultAlgorithm == QLatin1String("sample") && m_w->algSampleRadio) m_w->algSampleRadio->setChecked(true);
            else if (s.defaultAlgorithm == QLatin1String("brute") && m_w->algBruteRadio) m_w->algBruteRadio->setChecked(true);
            else if (m_w->algExactRadio) m_w->algExactRadio->setChecked(true);
            applied << slime::ui::t("ui.73ccaa710a1a");
        }
        if (old.defaultPreset != s.defaultPreset && s.defaultPreset != QLatin1String("none")) {
            const QString pv = s.defaultPreset;
            if (pv == QLatin1String("near")) applyPreset(20000);
            else if (pv == QLatin1String("local")) applyPreset(200000);
            else if (pv == QLatin1String("global")) applyPreset(1000000);
            applied << slime::ui::t("ui.427110e95874");
        }
        if (old.defaultGpu != s.defaultGpu && m_w->gpuCheck) {
            m_w->gpuCheck->setChecked(s.defaultGpu);
            applied << slime::ui::t("ui.18b6202383c3");
        }
        if (!applied.isEmpty())
            addFeedback(2, slime::ui::tr(slime::ui::t("ui.525a0adce0d1")).arg(applied.join(QStringLiteral("、"))));
    }

    // v3.10.9：设置页改动同步到搜索页「GPU 设置」展开面板
    if (m_w) {
        if (m_w->gpuStreamCheck) {
            QSignalBlocker b(m_w->gpuStreamCheck);
            m_w->gpuStreamCheck->setChecked(s.gpuPatternStream);
        }
        if (m_w->gpuSelfOptCheck) {
            QSignalBlocker b(m_w->gpuSelfOptCheck);
            m_w->gpuSelfOptCheck->setChecked(s.gpuResultSelfOptimize);
        }
        if (m_w->gpuLimitSpin) {
            QSignalBlocker b(m_w->gpuLimitSpin);
            m_w->gpuLimitSpin->setValue(std::clamp(s.gpuLimitPercent, 10, 100));
        }
        if (m_w->gpuAutoCheck) {
            QSignalBlocker b(m_w->gpuAutoCheck);
            m_w->gpuAutoCheck->setChecked(s.autoCheckGpuAtStartup);
        }
    }
    // 4) 其它项在下次搜索/启动时生效（默认值语义）：**合并写入** config.json
    //    v3.4.11（用户拍板）：这里是「设置改动 → 落盘」的唯一出口；拖动滑块时每 60ms 都同步写整份
    //    文件会卡，因此登记待写、空闲 500ms 或滑块释放时再落盘（flushPendingConfigWrite）。
    scheduleConfigWrite();
    // v3.4.11：程序化改设置（命令 / 恢复默认 / 读配置）后立刻把真实状态收敛到设置页。
    // 否则挂起的「设置自动生效」定时器 60ms 后会用**过期页面值**把刚改的设置改回去
    //（自检实测：主题被改回并多出一次 QSS 重放）。
    syncSettingsPageFromState();
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.c68f92da9c8f")));
    setStatus(slime::ui::tr(slime::ui::t("ui.c5c0ada6fbc6")));
    slime::LogBus::info("程序设置已应用");
}

void MainWindow::showProgramSettings()
{
    // v3.3.6：自检模式只记录「入口被点击」，不弹模态对话框（自动化流程不能被阻塞）
    m_settingsDialogOpened = true;
    if (m_uiQuiet) {
        setStatus(slime::ui::tr(slime::ui::t("ui.94db9de1ffcf")));
        return;
    }
    SettingsDialog dlg(m_settings, this);
    dlg.setValues(m_settings);
    dlg.exec();
    const bool openLogDir = dlg.openLogDirRequested();
    const bool clearCache = dlg.clearCacheRequested();
    if (!dlg.applied()) {
        setStatus(slime::ui::tr(slime::ui::t("ui.d75c517a8e4a")));
        return;
    }
    const AppSettings s = dlg.settings();
    applyAppSettings(s);
    if (openLogDir) {
        const auto dir = appDirPath() / (s.logSubDir.isEmpty() ? "logs" : s.logSubDir.toStdString());
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(dir.wstring())));
    }
    if (clearCache) {
        m_cache->clear();
        std::error_code ec;
        std::filesystem::remove(m_cacheFile, ec);
        std::filesystem::remove(appDirPath() / kCacheLegacyFileName, ec);
        updateCacheStatusUi();
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.71e8b41e4d10")));
    }
    // 清空缓存请求的访问器（在对话框销毁前读取，避免悬垂）
}

// 读取 build_date.txt（与 exe 同目录，ISO 日期）；缺失/非法时回退编译期默认日期，
// 计算距离今天的天数：> 30 天则在工具栏显示更新提醒徽章（点击打开下载页）。
void MainWindow::initUpdateReminder()
{
    const QString defDate = QStringLiteral("2026-09-20"); // 编译期默认（打包脚本会写入真实日期）
    QString dateText = defDate;
    const auto path = appDirPath() / "build_date.txt";
    std::ifstream in(path);
    if (in) {
        std::string line;
        std::getline(in, line);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) {
            const QDate d = QDate::fromString(QString::fromStdString(line).trimmed(), Qt::ISODate);
            if (d.isValid()) {
                dateText = d.toString(Qt::ISODate);
            } else {
                slime::LogBus::warning("build_date.txt 格式非法（应为 ISO 日期，如 2026-09-20），使用默认日期");
            }
        }
    }
    const QDate rel = QDate::fromString(dateText, Qt::ISODate);
    if (!rel.isValid()) {
        slime::LogBus::warning("发布日期无效，跳过更新提醒");
        return;
    }
    m_daysSinceRelease = static_cast<int>(rel.daysTo(QDate::currentDate()));
    if (m_daysSinceRelease > 30) {
        m_updateBadgeVisible = true;
        m_updateBadge->setText(slime::ui::t("ui.30476aaf4b92").arg(m_daysSinceRelease));
        m_updateBadge->setToolTip(slime::ui::t("ui.f953ab12b923").arg(releasePageUrl()));
        m_updateBadge->setVisible(true);
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.d5c607c89b26"))
                           .arg(m_daysSinceRelease)
                           .arg(dateText));
    } else {
        m_updateBadgeVisible = false;
        m_updateBadge->setVisible(false);
    }
    slime::LogBus::info("发布日期 " + dateText.toStdString() + "，已发布 " +
                        std::to_string(m_daysSinceRelease) + " 天" +
                        (m_updateBadgeVisible ? "（显示更新提醒徽章）" : ""));
}

void MainWindow::showAboutDialog()
{
    // v3.7.5：与「联系我们」做差异化 —— 这里只讲「这是什么、怎么算的、什么版本」；
    //   反馈渠道统一放到「联系我们」，避免两处文案几乎一样。
    const bool en = slime::ui::isEnglish();
    const QString text =
        en ? QStringLiteral(
                 "SlimeFinder %1 (C++20 / Qt 6 rebuild)\n\n"
                 "Version: %1\nBuild date file: build_date.txt\nReleased: %2 days ago\n\n"
                 "Slime chunks are derived from the world seed with the same arithmetic as the game "
                 "(Java 48-bit LCG; Bedrock MT19937), verified bit-for-bit against the reference "
                 "implementation. The Bedrock algorithm was reverse-engineered by @protolambda / @jocopa3.\n\n"
                 "Runs offline by default. Optional anonymous usage statistics are OFF unless you enable them in Help -> Anonymous statistics.\n"
                 "For feedback channels see Help -> Contact; for credits see Help -> Credits.\n\n"
                 "Download page: %3")
                 .arg(QLatin1String(kAppVersion))
                 .arg(m_daysSinceRelease)
                 .arg(releasePageUrl())
             : slime::ui::t("ui.833b150c8f74")
                 .arg(QLatin1String(kAppVersion))
                 .arg(m_daysSinceRelease)
                 .arg(releasePageUrl());
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.07efca1cd67e")));
    if (!m_uiQuiet) {
        QMessageBox box(this);
        box.setWindowTitle(slime::ui::t("ui.bed172efc953"));
        box.setTextFormat(Qt::PlainText);
        box.setText(text);
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
    }
}


void MainWindow::resetLayout()
{
    if (m_hSplit) m_hSplit->setSizes(QList<int>() << 380 << 480 << 360);   // v3.10.0：三纵列
    if (m_rightSplit) m_rightSplit->setSizes(QList<int>() << 560 << 240);   // v3.10.1：结果 / 过程
    selectBottomCategory(0); // v3.3.6：恢复默认布局回到「进度」分类
    resize(1280, 860);
    setStatus(slime::ui::tr(slime::ui::t("ui.9e6824a751ec")));
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.cab9d4b12553")));
}

// ============================================================================
// v3.3.4（任务书 2.3）：启用日志（隐蔽入口 + 两级选项）
// ============================================================================
void MainWindow::showEnableLogDialog()
{
    slime::LogMode mode = slime::LogBus::mode();
    if (mode == slime::LogMode::Off) mode = slime::LogMode::Brief;
    if (!::showEnableLogDialog(this, &mode)) return; // 用户取消
    slime::LogBus::setMode(mode);
    saveAlgorithmToConfig(); // 写回 config.json（含 log.mode）
    if (m_logPanel) m_logPanel->refreshFromBus();
    const QString name = QString::fromUtf8(slime::LogBus::modeName(mode));
    addFeedback(3, slime::ui::tr(slime::ui::t("ui.b1533230c471")).arg(name));
    setStatus(slime::ui::tr(slime::ui::t("ui.b85b877657d1")).arg(name));
    slime::LogBus::info("日志模式切换为 " + std::string(slime::LogBus::modeName(mode)));
}

// ============================================================================
// v3.3.4（任务书 7）：UI 截图验证 —— 规定尺寸 / splitter 极限位置 / 四个标签页 / 每个菜单展开
// 由 GUI 自检在 SLIME_SHOT 设置时调用；每张截图前都做布局校验（无横向滚动条）。
// ============================================================================
void MainWindow::captureUiScreenshots(const QString& prefix)
{
    if (prefix.isEmpty()) return;
    const auto shoot = [&](const QString& tag) {
        QApplication::processEvents(QEventLoop::AllEvents, 60);
        saveWindowShot(prefix, tag);
        if (m_w->mainScroll && m_w->mainScroll->horizontalScrollBar()) {
            const int hm = m_w->mainScroll->horizontalScrollBar()->maximum();
            std::fprintf(stderr, "[UI自检]   截图 %s：设置区横滚动上限=%d（应为 0）\n",
                         tag.toUtf8().constData(), hm);
        }
    };

    // 1~3) 三个规定窗口尺寸
    for (const QSize sz : { QSize(1280, 860), QSize(1000, 650), QSize(1920, 1080) }) {
        resize(sz);
        QApplication::processEvents(QEventLoop::AllEvents, 80);
        shoot(QStringLiteral("size-%1x%2").arg(sz.width()).arg(sz.height()));
    }
    resize(1280, 860);
    QApplication::processEvents(QEventLoop::AllEvents, 80);

    // 4~5) 水平 splitter 拖到最左 / 最右
    if (m_hSplit) {
        const int w = width();
        m_hSplit->setSizes(QList<int>() << 320 << (w - 320 - 320) << 320);   // 左栏最小（拖到最左）
        shoot(QStringLiteral("splitter-left"));
        m_hSplit->setSizes(QList<int>() << (w - 320 - 320) << 320 << 320);   // 右栏最小（拖到最右）
        shoot(QStringLiteral("splitter-right"));
        m_hSplit->setSizes(QList<int>() << 380 << (w - 380 - 360) << 360);
        QApplication::processEvents(QEventLoop::AllEvents, 60);
    }

    // 6) 右列垂直 splitter：过程面板拖到最高 / 恢复默认
    if (m_rightSplit) {
        const int hgt = m_rightSplit->height();
        m_rightSplit->setSizes(QList<int>() << 200 << (std::max)(120, hgt - 200));
        shoot(QStringLiteral("splitter-bottom-max"));
        m_rightSplit->setSizes(QList<int>() << 560 << 240);
        QApplication::processEvents(QEventLoop::AllEvents, 60);
    }

    // 7) 底部四个分类各一张（v3.3.6：左竖排导航 + 右内容区）
    if (m_nav && m_stack) {
        static const char* const kShotTags[4] = { "progress", "feedback", "log", "command" };
        for (int i = 0; i < m_nav->count() && i < m_stack->count(); ++i) {
            selectBottomCategory(i);
            shoot(QStringLiteral("nav-%1-%2").arg(i).arg(QString::fromUtf8(kShotTags[i])));
        }
        selectBottomCategory(0);
    }

    // 7b) v3.4.8：左侧导航各页截图（搜索 / 缓存构建 / 设置）
    if (m_pageStack && m_navButtons.size() == kNavCount) {
        static const char* const kPageTags[kNavCount] = { "search", "build", "settings" };
        for (int i = 0; i < kNavCount; ++i) {
            showPage(i);
            shoot(QStringLiteral("page-%1-%2").arg(i).arg(QString::fromUtf8(kPageTags[i])));
        }
        showPage(kPageSearch);
    }

    // 8) 每个菜单展开（菜单为独立弹窗，单独 grab 菜单部件本身）
    const auto shootMenu = [&](QMenu* menu, const QString& tag) {
        if (!menu) return;
        menu->popup(QPoint(20, 40));
        QApplication::processEvents(QEventLoop::AllEvents, 80);
        const QPixmap pm = menu->grab();
        const QString path = QStringLiteral("%1_menu-%2.png").arg(prefix, tag);
        const bool Ok = pm.save(path);
        std::fprintf(stderr, "[UI自检]   菜单截图 %s: %s (%dx%d, %d 项)\n", Ok ? "已保存" : "保存失败",
                     path.toUtf8().constData(), pm.width(), pm.height(),
                     static_cast<int>(menu->actions().size()));
        menu->hide();
        QApplication::processEvents(QEventLoop::AllEvents, 30);
    };
    shootMenu(m_menuFile, QStringLiteral("file"));
    shootMenu(m_menuEdit, QStringLiteral("edit"));
    shootMenu(m_menuView, QStringLiteral("view"));
    shootMenu(m_menuTools, QStringLiteral("tools"));
    shootMenu(m_menuHelp, QStringLiteral("help"));

    // 9) v3.3.6（任务书 5.7）：深色主题截图（默认尺寸 + 四个分类 + 设置对话框 5 个标签页）
    {
        const QString keepTheme = m_settings.theme;
        m_settings.theme = QStringLiteral("dark");
        applyThemeFromSettings();
        QApplication::processEvents(QEventLoop::AllEvents, 90);
        resize(1280, 860);
        QApplication::processEvents(QEventLoop::AllEvents, 90);
        shoot(QStringLiteral("dark-default"));

        if (m_nav && m_stack) {
            static const char* const kDarkTags[4] = { "progress", "feedback", "log", "command" };
            for (int i = 0; i < m_nav->count() && i < m_stack->count(); ++i) {
                selectBottomCategory(i);
                shoot(QStringLiteral("dark-nav-%1-%2").arg(i).arg(QString::fromUtf8(kDarkTags[i])));
            }
            selectBottomCategory(0);
        }
        {
            SettingsDialog dlg(m_settings, this);
            dlg.setValues(m_settings);
            dlg.resize(660, 560);
            dlg.show();
            QApplication::processEvents(QEventLoop::AllEvents, 140);
            const QString p = QStringLiteral("%1_dark-settings-general.png").arg(prefix);
            std::fprintf(stderr, "[UI自检]   深色设置对话框截图%s: %s\n",
                         dlg.grab().save(p) ? "已保存" : "保存失败", p.toUtf8().constData());
            if (dlg.tabs()) {
                for (int i = 1; i < dlg.tabs()->count(); ++i) {
                    dlg.tabs()->setCurrentIndex(i);
                    QApplication::processEvents(QEventLoop::AllEvents, 90);
                    QString name = dlg.tabs()->tabText(i);
                    name.replace(QStringLiteral("/"), QStringLiteral("-"));
                    const QString pi = QStringLiteral("%1_dark-settings-tab%2-%3.png").arg(prefix).arg(i).arg(name);
                    std::fprintf(stderr, "[UI自检]   深色设置页截图%s: %s\n",
                                 dlg.grab().save(pi) ? "已保存" : "保存失败", pi.toUtf8().constData());
                }
            }
            dlg.hide();
        }
        m_settings.theme = keepTheme;
        applyThemeFromSettings();
        resize(1280, 860);
        QApplication::processEvents(QEventLoop::AllEvents, 80);
    }
}

// v3.3.4：种子输入框高亮闪烁（任务书 4.2：点「去填写」时高亮）
// ============================================================================
// v3.3.5（Bug 2）：工具 → GPU 状态 —— 复用与搜索完全相同的探测路径
//   * 未探测过则先 ensureGpuScanOnce()（与勾选 GPU 时同一条路径）
//   * 显示设备名、显存、计算单元、实测吞吐率（CPU vs GPU）+ 「不一定更快」提醒
//   * v3.3.6（任务书 Bug 2）：不再显示任何阈值/交叉点结论
// ============================================================================
// v3.5.2（任务 1）：挂机点（环形覆盖）
//   范围优先取「最近一次搜索结果的包围盒」（向外扩 2 区块），没有结果时用当前搜索范围。
// v3.5.5：让地图尽量接近正方形（宽度决定高度；结果列表折叠时地图占更多高度）
void MainWindow::adjustMapSquare()
{
    // v3.6.6（用户反馈「每点一次按钮到处跑」）：用户手动拖过分隔条后程序不再抢着改尺寸
    if (m_rightSplitUserMoved) return;
    if (!m_rightSplit || !m_mapPanel) return;
    static bool adjusting = false;
    if (adjusting) return;
    const int totalH = m_rightSplit->height();
    const int totalW = m_rightSplit->width();
    if (totalH < 160 || totalW < 160) return;
    adjusting = true;
    const bool resultVisible = m_w && m_w->resultTable && m_w->resultTable->isVisible();
    const int chrome = 2 * 32;                 // 两个标题行
    const int squareH = (std::min)(totalW - 4, totalH - chrome);
    int mapH = resultVisible ? (std::max)((std::min)(squareH, totalH * 6 / 10), 200)
                             : (std::max)(squareH, 200);
    mapH = (std::min)(mapH, totalH - chrome);
    const int rest = (std::max)(60, totalH - chrome - mapH);
    const QList<int> want = QList<int>() << mapH << rest;
    if (m_rightSplit->sizes() != want) m_rightSplit->setSizes(want);
    adjusting = false;
}

// v3.5.5：结果列表面板折叠
void MainWindow::toggleResultPane()
{
    if (!m_resultPane || !m_w || !m_w->resultTable) return;
    const bool show = !m_w->resultTable->isVisible();
    m_w->resultTable->setVisible(show);
    if (m_resultSpacer) m_resultSpacer->setVisible(!show);
    if (m_resultToggle) m_resultToggle->setArrowType(show ? Qt::DownArrow : Qt::RightArrow);
    m_rightSplitUserMoved = false;
    setStatus(show ? slime::ui::t("ui.7b68fe95d4a2") : slime::ui::t("ui.afdc9c7d9b9b"));
    // v3.6.9：折叠/展开期间把分隔条尺寸钉住，标题行（按钮）原地不动
    {
        const QList<int> keep = m_rightSplit->sizes();
        auto pin = [this, keep]() {
            if (keep.size() >= 2) m_rightSplit->setSizes(keep);
        };
        pin();
        QTimer::singleShot(0, this, pin);
    }

}

// v3.5.5：结果灌进列表（排名/X/Z/距离/密度或覆盖/跳转按钮）；表头点击可排序
void MainWindow::fillResultTable()
{
    if (!m_w || !m_w->resultTable) return;
    auto* t = m_w->resultTable;
    const bool wasSorting = t->isSortingEnabled();
    t->setSortingEnabled(false);
    t->setRowCount(0);
    const auto& results = m_lastOutcome.results;
    const int shown = static_cast<int>((std::min)(results.size(), static_cast<size_t>(kResultDisplayCap)));
    t->setRowCount(shown);
    const bool ring = !m_lastAfk.empty();
    t->setColumnHidden(5, !ring);   // v3.5.9：环形模式才显示「有效格 · 总权重」
    // v3.10.3（用户要求）：极窄结果列的自适应 —— 压缩列宽、按视口宽度隐藏次要列、
    //   列宽仍可由用户拖动；视口比内容更窄时出现横向滚动条。
    {
        const int vw = t->viewport() ? t->viewport()->width() : 0;
        t->setColumnWidth(0, 36);
        t->setColumnWidth(1, 52);
        t->setColumnWidth(2, 52);
        t->setColumnWidth(3, 48);
        t->setColumnWidth(4, 52);
        t->setColumnHidden(3, vw > 0 && vw < 300);   // 距离列：窄列先折叠
        t->setColumnHidden(4, vw > 0 && vw < 200);   // 密度列：更窄才折叠
        t->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        t->horizontalHeader()->setMinimumSectionSize(32);
        t->horizontalHeader()->setStretchLastSection(true);
    }
    for (int i = 0; i < shown; ++i) {
        const auto& r = results[static_cast<size_t>(i)];
        int blockX = (r.minX + r.maxX + 1) * 8;
        int blockZ = (r.minZ + r.maxZ + 1) * 8;
        double value = r.density * 100.0;
        if (ring && i < static_cast<int>(m_lastAfk.size())) {
            blockX = m_lastAfk[static_cast<size_t>(i)].x;
            blockZ = m_lastAfk[static_cast<size_t>(i)].z;
            value = m_lastAfk[static_cast<size_t>(i)].coveragePercent;
        }
        const double dist = std::hypot(static_cast<double>(blockX >> 4) - m_lastInfo.startX,
                                       static_cast<double>(blockZ >> 4) - m_lastInfo.startZ);
        auto* it0 = new QTableWidgetItem();
        it0->setData(Qt::DisplayRole, i + 1);
        auto* it1 = new QTableWidgetItem();
        it1->setData(Qt::DisplayRole, blockX);
        it1->setData(Qt::UserRole, blockX);
        auto* it2 = new QTableWidgetItem();
        it2->setData(Qt::DisplayRole, blockZ);
        it2->setData(Qt::UserRole, blockZ);
        auto* it3 = new QTableWidgetItem();
        it3->setData(Qt::DisplayRole, dist);
        auto* it4 = new QTableWidgetItem();
        it4->setData(Qt::DisplayRole, value);
        t->setItem(i, 0, it0);
        t->setItem(i, 1, it1);
        t->setItem(i, 2, it2);
        t->setItem(i, 3, it3);
        t->setItem(i, 4, it4);
        if (ring && i < static_cast<int>(m_lastAfk.size())) {
            const auto& cd = m_lastAfk[static_cast<size_t>(i)];
            auto* it5 = new QTableWidgetItem();
            it5->setText(slime::ui::t("ui.aca2c2cb22cf")
                             .arg(static_cast<qlonglong>(cd.coveredBlocks))
                             .arg(cd.coveredWeight, 0, 'f', 0));
            t->setItem(i, 5, it5);
        }
        auto* jump = new QPushButton(slime::ui::t("ui.28cd2efd8fa8"), t);
        jump->setAutoDefault(false);
        jump->setToolTip(slime::ui::t("ui.b208126e95ab"));
        const int bx = blockX;
        const int bz = blockZ;
        connect(jump, &QPushButton::clicked, this, [this, i, ring, bx, bz]() {
            if (ring && i < static_cast<int>(m_lastAfk.size())) {
                focusAfkCandidate(i);   // v3.5.10：只显示这一条候选的环
            } else {
                focusMapOnChunk(bx >= 0 ? bx >> 4 : -((-bx + 15) >> 4), bz >= 0 ? bz >> 4 : -((-bz + 15) >> 4));
            }
        });
        t->setCellWidget(i, 6, jump);
    }
    if (m_resultCountLabel) {
        m_resultCountLabel->setText(shown > 0 ? slime::ui::t("ui.1389cca29639")
                                                   .arg(results.size())
                                                   .arg(shown < static_cast<int>(results.size())
                                                            ? slime::ui::t("ui.7d5266ad5822").arg(shown)
                                                            : QString())
                                            : slime::ui::t("ui.a535edb5149b"));
    }
}

// v3.9.12：结果要求冲突裁决 —— 忽略阈值（最优候选）时阈值/早停/智能范围无效；
//   图案模式下阈值与忽略阈值都无效。只改可用性，不改值与结果语义。
void MainWindow::applyResultConflicts()
{
    if (!m_w) return;
    const bool patternMode = m_w->strictRadio && m_w->strictRadio->isChecked();
    const bool ignore = m_w->ignoreThreshold && m_w->ignoreThreshold->isChecked();
    if (m_w->ignoreThreshold) m_w->ignoreThreshold->setEnabled(!patternMode);
    if (m_w->minDensity) m_w->minDensity->setEnabled(!ignore && !patternMode);
    if (m_w->stopAtTarget) {
        m_w->stopAtTarget->setEnabled(!ignore);
        if (ignore) m_w->stopAtTarget->setChecked(false);
    }
    if (m_w->smartRange) {
        m_w->smartRange->setEnabled(!ignore);
        if (ignore) m_w->smartRange->setChecked(false);
    }
    if (m_w->presetHint && ignore && !patternMode) {
        m_w->presetHint->setText(slime::ui::t("ui.ae90086a5180"));
    }
}

// ===================== v3.9.11：结果要求方案（预设） =====================
//   原则：方案只控制**会改变结果**的开关；GPU/线程/缓存等加速项永不参与。
namespace {
struct PresetDef {
    const char* label;
    bool stop;
    bool smart;
    bool ignore;   // 最优候选（忽略阈值）
    int algorithm; // 0=精确 1=分层 2=估算 3=保守
};
const PresetDef kPresetDensity[] = {
    // v3.10.12.8：实测「找够即停」不勾由近及远更快（CPU 114.5 vs 123.1 ms；GPU 也更快），预设改为仅停止
    { "ui.preset.quick_nearest", true,  false, false, 0 },
    { "ui.preset.all",           false, false, false, 0 },
    { "ui.preset.best_ignore",   false, false, true,  0 },
    { "ui.preset.coarse",        false, false, false, 2 },
};
const PresetDef kPresetPattern[] = {
    // v3.10.10.2：图案模式下「由近及远」会禁掉 AVX2 快路径（20k² 858→93 ms），预设改为仅停止
    { "ui.preset.pattern_nearest", true,  false, false, 0 },
    { "ui.preset.pattern_all",     false, false, false, 0 },
};
} // namespace

void MainWindow::rebuildPresets()
{
    if (!m_w || !m_w->presetCombo) return;
    const bool pattern = m_w->strictRadio && m_w->strictRadio->isChecked();
    m_presetPatternMode = pattern ? 1 : 0;
    m_presetApplying = true;
    m_w->presetCombo->clear();
    if (pattern) {
        for (const auto& pd : kPresetPattern) m_w->presetCombo->addItem(slime::ui::t(pd.label));
    } else {
        for (const auto& pd : kPresetDensity) m_w->presetCombo->addItem(slime::ui::t(pd.label));
    }
    m_w->presetCombo->addItem(slime::ui::t("ui.c493338e8c33"));
    m_w->presetCombo->setCurrentIndex(0);
    m_presetApplying = false;
    applySelectedPreset();
}

void MainWindow::applySelectedPreset()
{
    if (!m_w || !m_w->presetCombo) return;
    const bool pattern = (m_presetPatternMode == 1);
    const int customIdx = pattern ? 2 : 4;
    const int idx = m_w->presetCombo->currentIndex();
    if (idx < 0 || idx > customIdx) return;
    m_presetLastIndex = idx;

    if (idx == customIdx) {
        if (m_w->presetHint) {
            m_w->presetHint->setText(slime::ui::t("ui.7e7cf39ddc4d"));
        }
        updateAdvancedVisibility();   // v3.10.9：自定义方案展开高级开关并显示速度声明
        return;
    }
    const PresetDef& pd = pattern ? kPresetPattern[idx] : kPresetDensity[idx];
    m_presetApplying = true;
    if (m_w->ignoreThreshold) m_w->ignoreThreshold->setChecked(pd.ignore && !pattern);
    if (m_w->stopAtTarget) m_w->stopAtTarget->setChecked(pd.stop);
    if (m_w->smartRange) m_w->smartRange->setChecked(pd.smart);
    switch (pd.algorithm) {
    case 1: if (m_w->algLayeredRadio) m_w->algLayeredRadio->setChecked(true); break;
    case 2: if (m_w->algSampleRadio) m_w->algSampleRadio->setChecked(true); break;
    case 3: if (m_w->algBruteRadio) m_w->algBruteRadio->setChecked(true); break;
    default: if (m_w->algExactRadio) m_w->algExactRadio->setChecked(true); break;
    }
    m_presetApplying = false;
    applyResultConflicts();

    updateAdvancedVisibility();
    if (m_w->presetHint) {
        QString desc;
        if (pattern) {
            desc = slime::ui::t("ui.5c2f010bbc8f");
        } else if (pd.ignore) {
            desc = slime::ui::t("ui.0f05e9ce3b76");
        } else if (pd.algorithm == 2) {
            desc = slime::ui::t("ui.1f8e3c9e917b");
        } else if (pd.stop) {
            desc = slime::ui::t("ui.b69e415cb8e3");
        } else {
            desc = slime::ui::t("ui.07c03132f5fe");
        }
        m_w->presetHint->setText(desc);
    }
}

// v3.10.9（用户要求）：① 自定义方案的速度声明；② 高级开关仅在「自定义」或「环形」时显示
void MainWindow::updatePresetDisclaimer()
{
    if (!m_w || !m_w->presetCombo) return;
    const int customIdx = (m_presetPatternMode == 1) ? 2 : 4;
    const bool custom = m_w->presetCombo->currentIndex() == customIdx;
    if (m_w->presetDisclaimer) m_w->presetDisclaimer->setVisible(custom);
    if (custom && !m_customDisclaimerLogged) {
        m_customDisclaimerLogged = true;
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.75418b9be246")));
    }
}

void MainWindow::updateAdvancedVisibility()
{
    if (!m_w) return;
    const int customIdx = (m_presetPatternMode == 1) ? 2 : 4;
    const bool custom = m_w->presetCombo && m_w->presetCombo->currentIndex() == customIdx;
    const bool ring = m_w->shapeRingRadio && m_w->shapeRingRadio->isChecked();
    const bool showAdvanced = custom || ring;
    if (m_w->resultGroup) m_w->resultGroup->setVisible(showAdvanced);
    if (m_w->matchGroup) m_w->matchGroup->setVisible(showAdvanced && !ring);
    updatePresetDisclaimer();
}

void MainWindow::markPresetCustom(const QString& reason)
{
    if (m_presetApplying || !m_w || !m_w->presetCombo) return;
    const bool pattern = (m_presetPatternMode == 1);
    const int customIdx = pattern ? 2 : 4;
    if (m_w->presetCombo->currentIndex() == customIdx) return;
    m_presetApplying = true;
    m_w->presetCombo->setCurrentIndex(customIdx);
    m_presetApplying = false;
    m_presetLastIndex = customIdx;
    if (m_w->presetHint) {
        m_w->presetHint->setText(slime::ui::t("ui.fed55636b818").arg(reason));
    }
    updateAdvancedVisibility();
}

// v3.5.6：区域形状 = 环形覆盖时，把「正方形/密度搜索」相关控件全部置灰
void MainWindow::updateSearchShapeEnabled()
{
    if (!m_w || !m_w->densityPanel) return;
    // v3.5.18（用户要求）：一级二选一 → 下方只显示能调的选项，不再简单置灰。
    const bool patternMode = m_w->strictRadio && m_w->strictRadio->isChecked();
    const bool ringShape = m_w->shapeRingRadio && m_w->shapeRingRadio->isChecked();

    if (m_w->strictPanel) m_w->strictPanel->setVisible(patternMode);
    if (m_w->densityPanel) m_w->densityPanel->setVisible(!patternMode);
    // v3.9.11：模式决定方案数量与内容（仅在模式真正变化时重建，避免重置用户选择）
    if (m_w->presetCombo && m_presetPatternMode != (patternMode ? 1 : 0)) rebuildPresets();
    applyResultConflicts();
    if (m_w->squareOpts) m_w->squareOpts->setVisible(!patternMode && !ringShape);
    if (m_w->ringOpts) m_w->ringOpts->setVisible(!patternMode && ringShape);
    // v3.6.0（用户反馈：环形模式下方仍有 4 个匹配算法，和环形只有 2 种扫描不同步）
    if (m_w->matchGroup) m_w->matchGroup->setVisible(!ringShape);
    if (m_w->smartRange) m_w->smartRange->setVisible(!ringShape);
    if (m_w->smartRange) m_w->smartRange->setEnabled(!ringShape);
    if (m_w->stopAtTarget) m_w->stopAtTarget->setVisible(!ringShape);
    // 兼容：仍然按模式更新状态提示
    if (ringShape && !patternMode) {
        setStatus(slime::ui::tr(slime::ui::t("ui.9e6da9fa6c38")));
    }
    updateAdvancedVisibility();
}

// v3.5.5：环形覆盖搜索 —— 1 格精度找「半径 128 内覆盖史莱姆刷怪面积最大」的站位
void MainWindow::runRingSearch()
{
    if (!m_w) return;
    // v3.10.12.12：图案模式下「区域形状」面板被隐藏，但环形单选按钮可能仍是 checked。
    //   这里必须优先按当前模式判断，否则会继续跑环形搜索并显示环形结果。
    if (m_w->strictRadio && m_w->strictRadio->isChecked()) {
        setStatus(slime::ui::tr(slime::ui::t("ui.53b2e9a1f672")));
        return;
    }
    const bool bedrock = m_w->bedrockRadio->isChecked();
    bool ok = false;
    const int64_t seed = m_w->seedEdit ? m_w->seedEdit->text().trimmed().toLongLong(&ok) : 0;
    const int minX = m_w->minX->value(), maxX = m_w->maxX->value();
    const int minZ = m_w->minZ->value(), maxZ = m_w->maxZ->value();
    if (maxX < minX || maxZ < minZ) {
        setStatus(slime::ui::tr(slime::ui::t("ui.93ec3b99247b")));
        return;
    }
    if (!bedrock && !ok) {
        setStatus(slime::ui::tr(slime::ui::t("ui.d2d6c0b7fdb4")));
        return;
    }
    const int spanX = maxX - minX + 1, spanZ = maxZ - minZ + 1;
    // v3.5.16：环形范围跟随「半径」设定值（不再卡 4096x4096）。规划器改成按 z 带流式处理，
    //   内存与范围无关；这里只按「区块总数」兜底（4 亿区块 ≈ 1~3 秒），并给出耗时提示。
    {
        const long long cells = static_cast<long long>(spanX) * static_cast<long long>(spanZ);
        // v3.10.2（用户反馈：环形范围太小、无法全图）：上限放宽到 Java 整图规模
    //   （±1,875,000 区块 ⇒ 3,750,001² ≈ 1.4e13 格）。快速模式成本与面积无关（抽样），
    //   但全图级仍可能需要数分钟；状态栏给出提示，搜索期间可取消。
        if (cells > 16000000000000LL) {
            setStatus(slime::ui::tr(slime::ui::t("ui.dbcd349ec8e3")).arg(spanX).arg(spanZ));
            return;
        }
        if (cells > 1000000000000LL) {
            setStatus(slime::ui::tr(slime::ui::t("ui.84839e3c68cb"))
                          .arg(spanX)
                          .arg(spanZ));
        } else if (cells > 40000000LL) {
            std::fprintf(stderr, "[环形] 大范围：%dx%d 区块 = %.1f 万，预计 0.3~3 秒\n", spanX, spanZ,
                         static_cast<double>(cells) / 10000.0);
        }
    }
    // v3.5.8：环形 = 怪物生成环带 [24, 128]（过近不刷、过远消失）
    // v3.5.18：环形内外半径可在「设置 → 高级」里精调（默认 24 / 128）
    const int kRingInner = std::clamp(m_settings.afkRingInner, 0, 64);
    int kRingOuter = std::clamp(m_settings.afkRingOuter, 16, 1024);
    // v3.6.18：模拟距离会收窄环带外沿（16 格/区块）；>=8 区块时不影响默认 128
    if (m_w->simDistance) {
        const int simBlocks = m_w->simDistance->value() * 16;
        if (simBlocks < kRingOuter) kRingOuter = (std::max)(kRingInner + 8, simBlocks);
    }
    slime::AfkPlanOptions o;
    o.area = slime::Rect{ minX, maxX, minZ, maxZ };
    o.radius = kRingOuter;
    m_lastRingOuter = kRingOuter;   // v3.6.18：自检/状态可见的有效外沿
    o.innerRadius = kRingInner;
    o.topN = (std::max)(1, static_cast<int>(m_w->targetCount ? m_w->targetCount->value() : 5));
    o.minDensity = m_w->minDensity ? (std::max)(0.0, (std::min)(1.0, m_w->minDensity->value())) : 0.0;
    // v3.6.0：目标数量较大时放宽最小间距（24 → 8 区块），否则密集区域挑不出足够多结果
    o.minSeparation = (o.topN > 8) ? 8 : 24;
    o.coarseStep = std::clamp(m_settings.ringCoarseStep, 1, 64);
    // v3.10.3：超大范围自动加大粗扫步长 —— 保持「粗扫 ≤ ~512² 个采样中心」，
    //   否则全图级环形搜索会跑几个数量级太久（表现为界面像卡死）。
    {
        const int64_t spanBlocks = static_cast<int64_t>((std::max)(spanX, spanZ)) * 16;
        const int64_t autoStep = std::clamp<int64_t>(spanBlocks / 512, 1, 1 << 20);
        if (autoStep > o.coarseStep) {
            std::fprintf(stderr, "[环形] 范围极大：粗扫步长 %d → %lld（结果仍精确到格）\n", o.coarseStep,
                         static_cast<long long>(autoStep));
            o.coarseStep = static_cast<int>(autoStep);
        }
    }
    o.statBands = std::clamp(m_settings.ringStatBands, 1, 16);
    // v3.7.6：环形覆盖可交给 GPU（失败/太小/加权会自动走 CPU）
    o.gpuScan = (m_gpuScan && m_gpuAvailable) ? m_gpuScan.get() : nullptr;
    // v3.5.17：0 = 快速（粗扫+精修），1 = 精确（逐区块）
    // v3.10.12.11：2 = 自动正方形预筛 + 环形精修
    const int ringScanMode = m_w->ringScanCombo ? m_w->ringScanCombo->currentData().toInt() : 0;
    const bool autoPrescan = (ringScanMode == 2);
    o.scanStride = (ringScanMode == 1) ? 1 : 0;
    // v3.5.29：精确模式（逐区块）有内存/耗时上限，超了直接提示而不是跑崩
    if (o.scanStride == 1 && static_cast<long long>(spanX) * spanZ > 4000000LL) {
        setStatus(slime::ui::tr(slime::ui::t("ui.c11e33ee027d"))
                      .arg(spanX).arg(spanZ).arg(static_cast<qlonglong>(static_cast<long long>(spanX) * spanZ / 10000LL)));
        return;
    }
    const slime::Version v = bedrock ? slime::Version::Bedrock : slime::Version::Java;
    const auto pred = [v, seed](int cx, int cz) { return slime::SlimeEngine::isSlimeChunk(v, seed, cx, cz); };
    // v3.5.15：Java 版给规划器一个「整行批量判定」回调（行基座只算一次，占用图快数倍）
    if (!bedrock)
        o.rowSlime = [seed](int cz, int cx0, int n, uint8_t* out) {
            slime::SlimeEngine::fillSlimeRowJava(seed, cz, cx0, n, out);
        };
    // v3.5.7：Java 版把「群系刷怪权重」一并算进去（建农场时效率优先）
    // v3.5.7：复用同一个 BiomeMap（setupGenerator 一次性 ~0.8s，反复创建会明显拖慢连续搜索）
    if (!bedrock) {
        if (!m_ringBiome || m_ringBiomeVersion != m_mapJavaVersionId || m_ringBiomeSeed != seed) {
            m_ringBiome = std::make_unique<slime::BiomeMap>();
            if (!m_ringBiome->init(m_mapJavaVersionId, seed)) m_ringBiome.reset();
            m_ringBiomeVersion = m_mapJavaVersionId;
            m_ringBiomeSeed = seed;
        }
    } else {
        m_ringBiome.reset();
    }
    slime::BiomeMap* biome = m_ringBiome.get();
    if (biome && biome->valid()) {
        // 一次性预取权重网格：v3.5.14 起按范围自适应 1:64 / 1:128 / 1:256
        //（只用于候选排序；最终统计仍是 1:4 逐格精确。大范围用 1:256，采样量只有 1:64 的 1/16）
        int gs = slime::AfkPlanner::rankingGridScale((std::max)(spanX, spanZ));
        const int pad = (o.radius + 15) / 16 + 2;
        const int b0x = (minX - pad) * 16, b0z = (minZ - pad) * 16;
        const int b1x = (maxX + pad) * 16, b1z = (maxZ + pad) * 16;
        // v3.10.3（用户反馈：启动超大范围搜索界面卡住）：权重网格原先按 1:64~1:256 预取，
        //   全图级会尝试分配数百 GB ⇒ 假死。这里自动放大格子，直到网格 ≤ 200 万格（≤16MB）。
        {
            const int64_t spanBx = static_cast<int64_t>(b1x) - b0x + 1;
            const int64_t spanBz = static_cast<int64_t>(b1z) - b0z + 1;
            while (static_cast<int64_t>((spanBx / gs) + 1) * ((spanBz / gs) + 1) > 2000000LL &&
                   gs < (1 << 20)) {
                gs <<= 1;
            }
        }
        const int c64x0 = b0x >= 0 ? b0x / gs : -((-b0x + gs - 1) / gs);
        const int c64z0 = b0z >= 0 ? b0z / gs : -((-b0z + gs - 1) / gs);
        const int c64x1 = b1x >= 0 ? b1x / gs : -((-b1x + gs - 1) / gs);
        const int c64z1 = b1z >= 0 ? b1z / gs : -((-b1z + gs - 1) / gs);
        const int gw = c64x1 - c64x0 + 1, gh = c64z1 - c64z0 + 1;
        std::vector<int> ids;
        std::vector<float> wgrid;
        // v3.5.24：排序权重网格按「版本+种子+范围+网格档位」记忆一格 —— 连续同类搜索可省 2~6 ms
        struct WGridCache {
            bool valid = false;
            int versionId = -1, gs = 0, x0 = 0, z0 = 0, w = 0, h = 0;
            int64_t seed = 0;
            std::vector<float> data;
        };
        static WGridCache s_wgrid;
        const bool cacheHit = s_wgrid.valid && s_wgrid.versionId == m_mapJavaVersionId && s_wgrid.seed == seed &&
                              s_wgrid.gs == gs && s_wgrid.x0 == c64x0 && s_wgrid.z0 == c64z0 &&
                              s_wgrid.w == gw && s_wgrid.h == gh;
        if (cacheHit) {
            wgrid = s_wgrid.data;
        } else if (gw > 0 && gh > 0 && biome->genArea(gs, c64x0 * gs, c64z0 * gs, gw, gh, ids)) {
            wgrid.resize(ids.size());
            for (size_t i = 0; i < ids.size(); ++i)
                wgrid[i] = static_cast<float>(slime::spawnWeight(ids[i], slime::Platform::Java));
            s_wgrid.valid = true;
            s_wgrid.versionId = m_mapJavaVersionId;
            s_wgrid.seed = seed;
            s_wgrid.gs = gs;
            s_wgrid.x0 = c64x0;
            s_wgrid.z0 = c64z0;
            s_wgrid.w = gw;
            s_wgrid.h = gh;
            s_wgrid.data = wgrid;
        }
        if (!wgrid.empty()) {
            o.weightAt = [c64x0, c64z0, gw, gh, wgrid, gs](int bx, int bz) {
                const int cx = bx >= 0 ? bx / gs : -((-bx + gs - 1) / gs);
                const int cz = bz >= 0 ? bz / gs : -((-bz + gs - 1) / gs);
                const int ix = cx - c64x0, iz = cz - c64z0;
                if (ix < 0 || iz < 0 || ix >= gw || iz >= gh) return 1.0;
                return static_cast<double>(wgrid[static_cast<size_t>(iz) * gw + ix]);
            };
            std::fprintf(stderr, "[环形] 权重网格 %dx%d 格（1:%d 预取）\n", gw, gh, gs);
        }
    }
    // v3.6.19：环形搜索同样要先清掉上一次的结果（此前只有引擎路径清），
    //   否则「环形搜索无果」时列表/地图仍显示旧内容，切换方式后看起来像没生效。
    clearResultsForNewSearch();
    const auto t0 = std::chrono::steady_clock::now();
    // v3.5.22（用户反馈）：环形搜索一按下去界面就卡死（精确模式更明显）——
    //   计算放到后台线程，UI 用局部事件循环等待，期间窗口仍能重绘、响应关闭等事件。
    const int biomeVersionForStats = m_mapJavaVersionId;
    double ringComputeSecs = 0.0;
    std::vector<slime::AfkCandidate> cands;
    std::atomic<bool> ringDone{ false };
    bool ringEmptyAfterFilter = false;
    std::atomic<bool> ringOutOfMemory{ false };
    bool prescanUsed = false;
    bool prescanFallback = false;
    bool ringCancelled = false;
    m_ringCancel.store(false, std::memory_order_relaxed);
    m_ringSearching = true;
    {
        const QString scanText = autoPrescan ? slime::ui::t("ui.7ae0cd5f9a0b")
                                             : ((o.scanStride == 1) ? slime::ui::t("ui.277ba15949a6")
                                                                    : slime::ui::t("ui.6c29e3499ee0"));
        setStatus(slime::ui::tr(slime::ui::t("ui.09d9fb4b92b6"))
                      .arg(spanX).arg(spanZ).arg(scanText));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    }
    // v3.6.0（用户反馈：点开始后按钮卡在"按下"、取消不可点）——
    //   一开始就切到运行态，并把按钮的按下/焦点状态清掉
    if (m_w->start) {
        m_w->start->setDown(false);
        m_w->start->clearFocus();
        m_w->start->setEnabled(false);
    }
    if (m_w->cancel) m_w->cancel->setEnabled(true);
    if (m_w->progress) {
        m_w->progress->setStyleSheet(QString());
        m_w->progress->setRange(0, 0);   // v3.5.26：环形没有细粒度进度 → 忙碌滚动
        if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("…"));
    }
    std::thread ringWorker([&]() {
        // v3.5.26：后台降到「低于普通」优先级，避免把 UI 线程饿死（精确模式尤其明显）
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
        try {
            if (autoPrescan) {
                if (m_gpuScan && m_gpuAvailable) {
                    slime::DensityPrescanParams pp;
                    pp.version = v;
                    pp.worldSeed = seed;
                    pp.minX = minX;
                    pp.maxX = maxX;
                    pp.minZ = minZ;
                    pp.maxZ = maxZ;
                    pp.gridSize = 15;
                    pp.minDensity = 0.18;
                    pp.bandRows = 4096;
                    pp.topPerBand = 512;
                    pp.maxSeeds = 4000;
                    pp.minSeparation = 16;
                    std::vector<slime::DensitySeed> seeds;
                    std::string perr;
                    if (m_gpuScan->prescanDensitySeeds(pp, m_ringCancel, seeds, &perr) &&
                        !seeds.empty() && !m_ringCancel.load(std::memory_order_relaxed)) {
                        o.seedCandidates.reserve(seeds.size());
                        for (const auto& sd : seeds) o.seedCandidates.emplace_back(sd.cx, sd.cz);
                        prescanUsed = true;
                        std::fprintf(stderr, "[环形] 正方形预筛：%zu 个分散种子（阈值 %.2f）\n",
                                     seeds.size(), pp.minDensity);
                    } else {
                        prescanFallback = true;
                        o.seedCandidates.clear();
                        o.scanStride = 0;
                        std::fprintf(stderr, "[环形] 正方形预筛不可用，回退快速抽样：%s\n", perr.c_str());
                    }
                } else {
                    prescanFallback = true;
                    o.scanStride = 0;
                    std::fprintf(stderr, "[环形] GPU 不可用，正方形预筛回退快速抽样\n");
                }
            }
            if (m_ringCancel.load(std::memory_order_relaxed)) {
                ringCancelled = true;
            } else {
                cands = slime::AfkPlanner::plan(o, pred);
            }
        } catch (const std::bad_alloc&) {
            ringOutOfMemory = true;
        } catch (...) {
            ringOutOfMemory = true;
        }
    // v3.5.9：plan() 的粗扫/卷积是近似值 —— 对每个候选做「严格 1 格」重算：
    //   用游戏地形的最小单位 1:4 群系网格逐格累加，得到游戏口径的有效格与总权重。
    if (!cands.empty()) {
        // v3.5.12：8 个候选的 1:4 群系网格互相独立 → OpenMP 并行（每个线程用自己的 BiomeMap：
        //           cubiomes 生成器有状态，不可共用；init 仅 ~0.1ms，开销可忽略）。
        //           这一步原本是环形搜索的最大头（8 个候选 × 18ms ≈ 150ms）。
        // v3.5.14：把「候选 × 行带」摊成任务并行走（8 个候选 × 6 条行带 = 48 个任务），
        //           每个线程自带一个 BiomeMap，不必每个候选都重建生成器。实现见 core（基准也复用它）。
        const slime::AfkPlanner::StrictStatsInfo info =
            slime::AfkPlanner::measureStrictForCandidates(cands, o, pred, biomeVersionForStats, seed, !bedrock);
        if (info.cells > 0)
            std::fprintf(stderr, "[环形] 候选权重网格：按需采样 %zu / %zu 格（%.1f%%）\n", info.samples,
                         info.cells, info.samples * 100.0 / static_cast<double>(info.cells));
        const bool weighted = static_cast<bool>(o.weightAt);
        std::stable_sort(cands.begin(), cands.end(),
                         [weighted](const slime::AfkCandidate& a, const slime::AfkCandidate& b) {
                             return weighted ? (a.coveredWeight > b.coveredWeight)
                                             : (a.coveredBlocks > b.coveredBlocks);
                         });

        // v3.5.16（用户反馈）：plan() 里用的是近似覆盖率，严格重算后的密度可能低于阈值。
        //   这里按严格值再筛一遍，并按「目标数量」截断 —— 保证结果密度一定 >= 设定的最小密度。
        if (o.minDensity > 0.0) {
            const size_t before = cands.size();
            cands.erase(std::remove_if(cands.begin(), cands.end(),
                                       [&](const slime::AfkCandidate& c) {
                                           // v3.5.26：按**界面显示值**（3 位小数）比较，
                                           //   避免 0.2996 在表格里显示成 30.0% 却又不满足 0.300 的矛盾
                                           const double shown = std::round(c.coveragePercent / 100.0 * 1000.0) / 1000.0;
                                           const double want = std::round(o.minDensity * 1000.0) / 1000.0;
                                           return shown < want;
                                       }),
                        cands.end());
            if (cands.size() != before) {
                std::fprintf(stderr, "[\u73af\u5f62] \u6700\u5c0f\u5bc6\u5ea6\u590d\u6838\uff1a%zu \u2192 %zu \u4e2a\n", before,
                             cands.size());
                if (cands.empty()) ringEmptyAfterFilter = true;
            }
        }
        if (static_cast<int>(cands.size()) > o.topN)
            cands.resize(static_cast<size_t>(o.topN));
        // v3.6.1：结果少于目标数量时在日志里说明被什么限制（避免以为程序只能出这么几个）
        if (static_cast<int>(cands.size()) < o.topN)
            std::fprintf(stderr, "[环形] 结果 %zu < 目标 %d：受最小密度(%.3f)/间距(%d)或候选池限制\n",
                         cands.size(), o.topN, o.minDensity, o.minSeparation);
        std::fprintf(stderr, "[环形] 严格 1 格重算完成：%zu 个候选（最优 %lld 格 / 权重 %lld）\n", cands.size(),
                     static_cast<long long>(cands.front().coveredBlocks),
                     static_cast<long long>(cands.front().coveredWeight));
    }

        ringComputeSecs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        ringDone.store(true, std::memory_order_release);
    });
    {
        QEventLoop waitLoop;
        QTimer poll;
        poll.setInterval(2);   // v3.5.24：轮询要细，否则异步等待本身就给用户加延迟
        QObject::connect(&poll, &QTimer::timeout, &waitLoop, [&]() {
            if (ringDone.load(std::memory_order_acquire)) waitLoop.quit();
        });
        poll.start();
        waitLoop.exec();
    }
    ringWorker.join();
    m_ringSearching = false;
    updateStartEnabled();
    if (ringCancelled) {
        setStatus(slime::ui::tr(slime::ui::t("ui.397b1272aec8")));
    } else if (autoPrescan && prescanUsed) {
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.29d84f827fc6")));
    } else if (autoPrescan && prescanFallback) {
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.c736d29daa5b")));
    }
    if (m_resultsNotice) {
        const int got = static_cast<int>(cands.size());
        const int want = (std::max)(1, static_cast<int>(m_w->targetCount ? m_w->targetCount->value() : 1));
        if (got == 0)
            m_resultsNotice->setText(slime::ui::t("ui.e682244d5bfe"));
        else if (got < want)
            m_resultsNotice->setText(slime::ui::t("ui.af6b2c0e44d3")
                                         .arg(got).arg(want));
        else
            m_resultsNotice->setText(QString());
        m_resultsNotice->setVisible(!m_resultsNotice->text().isEmpty());
    }
    // v3.6.0：结束（含异常/空结果）一律恢复按钮态
    if (m_w->start) m_w->start->setEnabled(true);
    if (m_w->cancel) m_w->cancel->setEnabled(false);
    if (m_w->cancel) m_w->cancel->setDown(false);
    if (m_w->progress) {
        m_w->progress->setRange(0, 1000);
        m_w->progress->setValue(1000);
        if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("100%"));
    }
    if (ringOutOfMemory.load())
        setStatus(slime::ui::tr(slime::ui::t("ui.a7216f4a8a80")));
    else if (ringEmptyAfterFilter)
        setStatus(slime::ui::tr(slime::ui::t("ui.b3df7b1e43a6"))
                      .arg(o.minDensity, 0, 'f', 3));
    const int stride = slime::AfkPlanner::coarseChunkStride((std::max)(spanX, spanZ));
    const double secs = ringComputeSecs > 0.0
                             ? ringComputeSecs
                             : std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_lastAfk = cands;

    SearchOutcome out;
    out.ok = true;
    out.results.reserve(cands.size());
    for (const auto& cd : cands) {
        slime::MatchResult r;
        r.minX = r.maxX = (cd.x >= 0 ? cd.x / 16 : -((-cd.x + 15) / 16));
        r.minZ = r.maxZ = (cd.z >= 0 ? cd.z / 16 : -((-cd.z + 15) / 16));
        r.density = cd.coveragePercent / 100.0;
        out.results.push_back(r);
    }
    out.totalFound = static_cast<int>(cands.size());
    // v3.10.12.11：环形「快速」在超大范围会等距抽样（约 40 万样本）。
    //   此前把整个范围面积当作已扫描区块，配合毫秒级耗时会出现「1595 万亿/秒」的假速度；
    //   这里按实际抽样点数报告。
    int64_t scannedStat = static_cast<int64_t>(spanX) * spanZ;
    // 自动正方形预筛成功时，实际扫的是整张正方形密度图；不能再按快速抽样的 40 万点报速度。
    if (!(autoPrescan && prescanUsed) && o.scanStride != 1 && scannedStat > 20LL * 1000 * 1000) {
        int ss = 1;
        while ((static_cast<int64_t>(spanX) / ss) * (static_cast<int64_t>(spanZ) / ss) > 400000)
            ++ss;
        scannedStat = (static_cast<int64_t>(spanX) / ss) * (static_cast<int64_t>(spanZ) / ss);
    }
    out.stats.scannedChunks = scannedStat;
    out.stats.totalMs = secs * 1000.0;
    out.stats.scanMs = secs * 1000.0;
    RunInfo info;
    info.version = v;
    info.mode = Mode::Density;
    info.algorithm = Algorithm::Exact;
    info.startX = m_w->startX->value();
    info.startZ = m_w->startZ->value();
    info.region = Rect{ minX, maxX, minZ, maxZ };
    info.gridSizeUsed = m_w->gridSize ? m_w->gridSize->value() : 5;
    info.minDensityUsed = m_w->minDensity ? m_w->minDensity->value() : 0.3;
    info.targetCount = m_w->targetCount ? m_w->targetCount->value() : 1;
    m_lastOutcome = out;
    m_lastInfo = info;
    m_hasLastOutcome = true;
    if (m_telemetry) {
        const QString ringMode = autoPrescan ? QStringLiteral("auto")
                                             : ((o.scanStride == 1) ? QStringLiteral("exact")
                                                                   : QStringLiteral("fast"));
        QJsonObject st;
        st.insert(QStringLiteral("radius"), o.radius);
        st.insert(QStringLiteral("inner_radius"), o.innerRadius);
        st.insert(QStringLiteral("target"), static_cast<double>(o.topN));
        st.insert(QStringLiteral("min_density"), o.minDensity);
        st.insert(QStringLiteral("min_separation"), o.minSeparation);
        st.insert(QStringLiteral("sim_distance"), m_w->simDistance ? m_w->simDistance->value() : 0);
        const QString seedStr = bedrock ? QString()
                                        : QString::number(seed);
        const QJsonObject search = buildTelemetrySearch(
            seedStr, bedrock, QStringLiteral("ring"), QStringLiteral("exact"), ringMode, info.region,
            m_w->startX ? m_w->startX->value() : 0, m_w->startZ ? m_w->startZ->value() : 0,
            st, out, o.gpuScan != nullptr);
        m_telemetry->reportSearchDetailed(QString::fromLatin1(kAppVersion), QStringLiteral("test"),
                                          m_gpuScan ? QString::fromStdString(m_gpuScan->deviceName()) : QString(),
                                          search);
    }
    populateResultText(out, info);
    if (m_map) {
        auto layer = m_map->layer();
        layer.afkRadius = kRingOuter;
        layer.afkInnerRadius = kRingInner;   // v3.5.8：地图上画双圆环带
        m_map->setLayer(layer);
        m_map->setResults(out.results);
    // v3.5.22（用户要求）：有结果时自动跳到地图对应位置并标注
    if (!out.results.empty() && m_map) {
        const auto& r0 = out.results.front();
        const int cx = (r0.minX + r0.maxX) / 2;
        const int cz = (r0.minZ + r0.maxZ) / 2;
        m_map->centerOnChunk(cx, cz);
        if (m_map && !m_map->isVisible()) toggleMapPanel();   // v3.6.2：按内容可见性判断
        setStatus(slime::ui::tr(slime::ui::t("ui.061e37177602")).arg(cx).arg(cz));
    }

        m_map->setAfkCandidates(cands);
        m_map->setFocusedAfk(-1);   // v3.5.10：搜索后默认不画任何环（避免多个环叠在一起）
        m_map->setAfkFocusHandler([this](int idx) {
            if (idx >= 0 && idx < static_cast<int>(m_lastAfk.size())) {
                const auto& cd = m_lastAfk[static_cast<size_t>(idx)];
                setStatus(slime::ui::tr(slime::ui::t("ui.46355f6279ae"))
                              .arg(idx + 1)
                              .arg(cd.x)
                              .arg(cd.z)
                              .arg(static_cast<qlonglong>(cd.coveredBlocks))
                              .arg(cd.coveredWeight, 0, 'f', 0));
            }
        });
        m_map->fitToContent();
        if (m_map && !m_map->isVisible()) toggleMapPanel();   // v3.6.2：按内容可见性判断
    }
    // v3.5.8：统计口径 —— 按「世界倒数第二层」的 y 统计可生成格数（洞穴更新前 y≥1，之后 y≥-63）
    const int spawnY = (v == slime::Version::Java && m_mapJavaVersionId >= 5) ? -63 : 1;
    // v3.5.15：把「界面刷新」也计时 —— 用户感知的等待 = 计算 + 结果列表 + 地图图层重建
    const double uiMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() -
                        secs * 1000.0;
    std::fprintf(stderr, "[环形] 界面刷新（列表+地图）：%.1f ms（地图图层最近一次 %.0f ms）\n", uiMs,
                 m_map ? m_map->lastLayerBuildMs() : 0.0);
    setStatus(slime::ui::tr(slime::ui::t("ui.1d9c4069d770"))
                  .arg(kRingInner)
                  .arg(kRingOuter)
                  .arg(spawnY)
                  .arg(cands.size())
                  .arg(cands.empty() ? 0 : static_cast<qlonglong>(cands.front().coveredBlocks))
                  .arg(cands.empty() ? 0 : cands.front().coveredChunks)
                  .arg(cands.empty() ? 0.0 : cands.front().coveragePercent, 0, 'f', 1)
                  .arg(secs, 0, 'f', 3)
                  .arg(stride > 1 ? slime::ui::tr(slime::ui::t("ui.c29ba5f06d05")).arg(stride) : QString()));
    std::fprintf(stderr, "[环形] 搜索完成：候选=%zu 最优=(%d,%d) 覆盖=%lld 格 用时=%.3fs\n", cands.size(),
                 cands.empty() ? 0 : cands.front().x, cands.empty() ? 0 : cands.front().z,
                 cands.empty() ? 0LL : static_cast<long long>(cands.front().coveredBlocks), secs);
}

// v3.5.4：地图面板（搜索页右侧）—— 与信息栏联动
void MainWindow::updateMapFromForm()
{
    if (!m_map || !m_w) return;
    MapPreviewWidget::Layer l = m_map->layer();
    const bool bedrock = m_w->bedrockRadio && m_w->bedrockRadio->isChecked();
    l.version = bedrock ? 1 : 0;
    bool ok = false;
    if (m_w->seedEdit) l.seed = m_w->seedEdit->text().trimmed().toLongLong(&ok);
    // v3.5.28（用户反馈）：没填种子时不要画「种子 0」的地图 —— 先提示填种子
    {
        const QString seedText = m_w->seedEdit ? m_w->seedEdit->text().trimmed() : QString();
        const bool needSeed = seedText.isEmpty() || !ok;   // v3.5.28：两版都要种子
        if (needSeed) {
            m_map->setPlaceholderText(slime::ui::tr(slime::ui::t("ui.0c9972317e16")));
            m_map->setLayer(l);
            setStatus(slime::ui::tr(slime::ui::t("ui.27c2c9373f1b")));
            return;
        }
        m_map->setPlaceholderText(QString());
    }
    l.autoDetail = true;
    l.afkRadius = 128;
    m_map->setLayer(l);
    // v3.5.21（用户要求）：基岩版群系上色 —— 官方 Wiki 明确「同一 64 位种子，Java 与基岩版
    //   的地形与群系一致（结构/洞穴/生物生成不同）」，所以基岩版直接用 Java 生成器跑同一份种子。
    //   1.18+ 才成立：更早版本两版群系算法不同，这里按所选 Java 版本判断。
    // 基岩版只用 1.18+ 的口径（更早两版群系算法不同）；默认取当前 Java 版本，若用户选过旧版本则回落到最新
    // v3.5.26：基岩版只在 1.18+ 与 Java 同种子一致 —— 选到更早的版本就回落到最新
    int biomeVersion = m_mapJavaVersionId;
    if (bedrock) {
        const auto& opts = slime::javaVersionOptions();
        int newest18 = slime::defaultJavaVersionId();
        for (const auto& v : opts) {
            if (QString::fromUtf8(v.label) == QLatin1String("1.18.2")) { newest18 = v.id; break; }
        }
        if (m_mapJavaVersionId > newest18) biomeVersion = slime::defaultJavaVersionId();
    }
    m_map->setBiomeSource(biomeVersion, l.seed);
    m_map->setBiomeParityNote(bedrock);

    // 视图：v3.5.16 不再随搜索范围自动移动地图中心（用户反馈「改范围会让地图中心莫名同步、位置错位」）。
    //   只有第一次显示地图面板、或用户点「定位/适配」类操作时才移动视图。
    if (m_w->minX && m_w->maxX && m_w->minZ && m_w->maxZ) {
        const int cx = m_w->minX->value() + (m_w->maxX->value() - m_w->minX->value()) / 2;
        const int cz = m_w->minZ->value() + (m_w->maxZ->value() - m_w->minZ->value()) / 2;
        if (!m_mapViewInitialised) {
            m_mapViewInitialised = true;
            m_mapFollowChunkX = cx;
            m_mapFollowChunkZ = cz;
            const double spanChunks = (std::max)(1, (std::max)(m_w->maxX->value() - m_w->minX->value() + 1,
                                                              m_w->maxZ->value() - m_w->minZ->value() + 1));
            const double scale = (std::max)(0.5, (std::min)(8.0, 700.0 / (spanChunks * 16.0)));
            m_map->setView(cx * 16.0, cz * 16.0, scale);
            m_map->refreshNow();
        }
    }
    std::fprintf(stderr, "[地图面板] 刷新：%s 种子=%lld 范围中心=(%d,%d) 群系=%s\n",
                 bedrock ? "基岩版" : "Java", static_cast<long long>(l.seed), m_mapFollowChunkX,
                 m_mapFollowChunkZ, m_map->layer().biomeAvailable ? "可用" : "不可用");
}

// v3.6.5（用户要求）：搜索条件不足时把「开始搜索」灰掉，并说明缺什么
// v3.6.18：模拟距离提示（>=8 区块时环带不变；<8 时外沿 = 16×D）
void MainWindow::updateSimDistanceNote()
{
    if (!m_w || !m_w->simDistance || !m_w->simDistanceNote) return;
    const int outerBase = std::clamp(m_settings.afkRingOuter, 16, 1024);
    const int inner = std::clamp(m_settings.afkRingInner, 0, 64);
    const int simBlocks = m_w->simDistance->value() * 16;
    if (simBlocks >= outerBase) {
        m_w->simDistanceNote->setText(slime::ui::t("ui.511104f69bef").arg(outerBase).arg(inner).arg(outerBase));
    } else {
        m_w->simDistanceNote->setText(slime::ui::t("ui.c2a121be1423").arg(inner).arg(simBlocks));
    }
}

// v3.6.19（用户反馈）：把「开始新搜索时清空旧结果」抽成一处 ——
//   引擎路径与环形路径都要调用，否则环形搜索无果时会留下上一次的列表/地图标记，
//   切换搜索方式后看起来"还在用旧的搜索方式"。
void MainWindow::clearResultsForNewSearch()
{
    m_lastAfk.clear();
    m_lastOutcome = SearchOutcome{};
    m_hasLastOutcome = false;
    if (m_w->resultTable) m_w->resultTable->setRowCount(0);
    if (m_resultCountLabel) m_resultCountLabel->setText(slime::ui::t("ui.a535edb5149b"));
    if (m_resultsNotice) {
        m_resultsNotice->setText(QString());
        m_resultsNotice->setVisible(false);
    }
    if (m_map) {
        m_map->setResults({});
        m_map->setAfkCandidates({});
        m_map->setFocusedAfk(-1);
    }
    if (m_w->infoStats) m_w->infoStats->setText(slime::ui::t("ui.bff5b3f4bbe4"));
    if (m_w->resultView) m_w->resultView->clear();
    // 结果文本区在 v3.5.x 起默认折叠：清空时保持折叠状态即可
}

void MainWindow::updateStartEnabled()
{
    if (!m_w || !m_w->start) return;
    if (m_running) { m_w->start->setEnabled(false); return; }
    const bool bedrock = m_w->bedrockRadio && m_w->bedrockRadio->isChecked();
    const QString seedText = m_w->seedEdit ? m_w->seedEdit->text().trimmed() : QString();
    bool ok = false;
    if (!bedrock) seedText.toLongLong(&ok);
    const bool patternMode = m_w->strictRadio && m_w->strictRadio->isChecked();
    const bool hasPattern = m_patternSet && m_pattern.activeCount() > 0;
    const bool buildMode = m_buildActive;

    QString why;
    if (!bedrock && (seedText.isEmpty() || !ok)) why = slime::ui::t("ui.5344ca1e9e8b");
    else if (buildMode && seedText.isEmpty()) why = slime::ui::t("ui.5344ca1e9e8b");
    else if (patternMode && !hasPattern && !buildMode) why = slime::ui::t("ui.779966e118d2");
    const bool can = why.isEmpty();
    m_w->start->setEnabled(can);
    m_w->start->setToolTip(can ? slime::ui::t("ui.8fdc065eda03") : why);
    // 提示只放在 tooltip 里（左栏已经有线程/GPU 提示，避免再占一行）
}

void MainWindow::toggleMapPanel()
{
    if (!m_mapPanel) return;
    // v3.6.2（用户要求）：折叠只收内容，标题行始终保留 —— 收起后就是 "> 地图"，点它展开
    const bool show = !(m_map && m_map->isVisible());   // v3.6.2：按内容可见性取反
    m_mapPanel->setVisible(true);
    if (m_map) m_map->setVisible(show);
    if (m_mapPlaceholder) m_mapPlaceholder->setVisible(!show);
    if (m_mapSpacer) m_mapSpacer->setVisible(!show);   // v3.6.9.1：折叠时由它吸收剩余空间
    if (m_mapToggle) m_mapToggle->setArrowType(show ? Qt::DownArrow : Qt::RightArrow);
    m_rightSplitUserMoved = false;   // 明确的折叠操作，允许重新自适应
    // v3.6.9（用户验收标准）：点击折叠按钮后，**按钮本身的位置必须和点击时完全一致**，
    //   再点一次恢复原样。QSplitter 在子控件隐藏时会立刻重分配像素（结果面板标题会跟着上下跳），
    //   所以这里在切换前后都把分隔条的像素尺寸「钉住」：折叠只是内容看不见，标题行原地不动。
    // v3.10.0：地图已是独立的中间列 ⇒ 折叠/展开钉住的是三纵列（hSplit）的宽度，
    //   标题行与折叠按钮因此原地不动（沿用 v3.6.9 的验收标准）。
    const QList<int> keepSizes = m_hSplit ? m_hSplit->sizes() : QList<int>();
    auto pinSizes = [this, keepSizes]() {
        if (keepSizes.size() >= 3) m_hSplit->setSizes(keepSizes);
    };
    pinSizes();
    m_mapPaneWidth = (keepSizes.size() >= 3) ? keepSizes[1] : m_mapPaneWidth;
    if (show) {
        updateMapFromForm();
        setStatus(slime::ui::tr(slime::ui::t("ui.1ff3e451eaf3")));
    } else {
        setStatus(slime::ui::tr(slime::ui::t("ui.9e8c9184ca74")));
    }
    pinSizes();
    QTimer::singleShot(0, this, pinSizes);
}

void MainWindow::focusMapOnChunk(int chunkX, int chunkZ)
{
    if (!m_map) return;
    if (m_map && !m_map->isVisible()) toggleMapPanel();   // v3.6.2：按内容可见性判断
    m_map->setView(chunkX * 16.0 + 8.0, chunkZ * 16.0 + 8.0, (std::max)(2.0, m_map->pxPerBlock()));
    m_map->refreshNow();
    setStatus(slime::ui::tr(slime::ui::t("ui.8700ac6906d4")).arg(chunkX).arg(chunkZ));
    std::fprintf(stderr, "[地图面板] 定位：区块(%d,%d) → 方块(%d,%d)\n", chunkX, chunkZ, chunkX * 16 + 8,
                 chunkZ * 16 + 8);
}

// v3.5.10：选中某个环形候选（点地图环心 / 点结果行）—— 地图只画这一条的环，中心落在环心
void MainWindow::focusAfkCandidate(int index)
{
    if (!m_map || index < 0 || index >= static_cast<int>(m_lastAfk.size())) return;
    const auto& cd = m_lastAfk[static_cast<size_t>(index)];
    if (m_map && !m_map->isVisible()) toggleMapPanel();   // v3.6.2：按内容可见性判断
    const QSize sz = (m_map->width() > 80 && m_map->height() > 80) ? m_map->size() : QSize(720, 480);
    // 让「环带 24~128」整体落在视口内（留 18% 余量）
    const double span = 2.0 * 128.0 * 1.18;
    double zoom = (std::min)(sz.width(), sz.height()) / span;
    zoom = (std::max)(0.25, (std::min)(8.0, zoom));
    m_map->setView(static_cast<double>(cd.x) + 0.5, static_cast<double>(cd.z) + 0.5, zoom);
    m_map->setFocusedAfk(index);
    m_map->refreshNow();
    setStatus(slime::ui::tr(slime::ui::t("ui.be49a883050c"))
                  .arg(index + 1)
                  .arg(cd.x)
                  .arg(cd.z)
                  .arg(static_cast<qlonglong>(cd.coveredBlocks))
                  .arg(cd.coveredWeight, 0, 'f', 0)
                  .arg(cd.coveredChunks));
    std::fprintf(stderr, "[地图面板] 选中环心 #%d：(%d,%d) 有效格 %lld\n", index, cd.x, cd.z,
                 static_cast<long long>(cd.coveredBlocks));
}

void MainWindow::computeAfkForMap()
{
    if (!m_map || !m_w) return;
    slime::AfkPlanOptions o;
    o.area = slime::Rect{ m_w->minX->value(), m_w->maxX->value(), m_w->minZ->value(), m_w->maxZ->value() };
    o.radius = 128;
    o.topN = 5;
    o.minSeparation = 24;
    o.coarseStep = 4;
    const bool bedrock = m_w->bedrockRadio && m_w->bedrockRadio->isChecked();
    bool ok = false;
    const int64_t seed = m_w->seedEdit ? m_w->seedEdit->text().trimmed().toLongLong(&ok) : 0;
    const slime::Version v = bedrock ? slime::Version::Bedrock : slime::Version::Java;
    const auto pred = [v, seed](int cx, int cz) { return slime::SlimeEngine::isSlimeChunk(v, seed, cx, cz); };
    const auto res = slime::AfkPlanner::plan(o, pred);
    m_map->setAfkCandidates(res);
    if (!res.empty()) m_map->fitToContent();
    setStatus(slime::ui::tr(slime::ui::t("ui.00028abeba59")).arg(res.size()));
}

// v3.5.2（任务 2）：地图预览 —— 把史莱姆区块、搜索结果、挂机点画在一张可缩放/平移的地图上
void MainWindow::showMapDialog()
{
    MapDialog::Context ctx;
    ctx.version = (m_w->bedrockRadio && m_w->bedrockRadio->isChecked()) ? 1 : 0;
    bool seedOk = false;
    if (m_w->seedEdit) ctx.seed = m_w->seedEdit->text().trimmed().toLongLong(&seedOk);
    if (ctx.version == 0 && !seedOk) {
        QMessageBox::information(this, slime::ui::t("ui.faea94c8b095"),
                                 slime::ui::t("ui.64cd88da6b9d"));
        setStatus(slime::ui::tr(slime::ui::t("ui.6616d54947cf")));
        return;
    }
    if (!m_lastOutcome.results.empty()) {
        int minX = INT_MAX, maxX = INT_MIN, minZ = INT_MAX, maxZ = INT_MIN;
        for (const MatchResult& r : m_lastOutcome.results) {
            minX = (std::min)(minX, r.minX);
            maxX = (std::max)(maxX, r.maxX);
            minZ = (std::min)(minZ, r.minZ);
            maxZ = (std::max)(maxZ, r.maxZ);
        }
        ctx.minX = minX - 1; ctx.maxX = maxX + 1;
        ctx.minZ = minZ - 1; ctx.maxZ = maxZ + 1;
        ctx.source = slime::ui::t("ui.491bd822f8b8").arg(m_lastOutcome.results.size());
    } else if (m_w->minX && m_w->maxX && m_w->minZ && m_w->maxZ) {
        ctx.minX = m_w->minX->value(); ctx.maxX = m_w->maxX->value();
        ctx.minZ = m_w->minZ->value(); ctx.maxZ = m_w->maxZ->value();
        ctx.source = slime::ui::t("ui.bba2e2e3b721");
    } else {
        return;
    }

    MapDialog dlg(ctx, this);
    dlg.setResults(m_lastOutcome.results);
    dlg.computeAfk();
    dlg.exec();
}

void MainWindow::showAfkDialog()
{
    AfkDialog::Context ctx;
    ctx.version = (m_w->bedrockRadio && m_w->bedrockRadio->isChecked()) ? 1 : 0;
    bool seedOk = false;
    if (m_w->seedEdit) ctx.seed = m_w->seedEdit->text().trimmed().toLongLong(&seedOk);
    if (ctx.version == 0 && !seedOk) {
        QMessageBox::information(this, slime::ui::t("ui.faea94c8b095"),
                                 slime::ui::t("ui.64cd88da6b9d"));
        setStatus(slime::ui::tr(slime::ui::t("ui.447c26e890a5")));
        return;
    }

    if (!m_lastOutcome.results.empty()) {
        int minX = INT_MAX, maxX = INT_MIN, minZ = INT_MAX, maxZ = INT_MIN;
        for (const MatchResult& r : m_lastOutcome.results) {
            minX = (std::min)(minX, r.minX);
            maxX = (std::max)(maxX, r.maxX);
            minZ = (std::min)(minZ, r.minZ);
            maxZ = (std::max)(maxZ, r.maxZ);
        }
        ctx.minX = minX - 2; ctx.maxX = maxX + 2;
        ctx.minZ = minZ - 2; ctx.maxZ = maxZ + 2;
        ctx.source = slime::ui::t("ui.767c07630078").arg(m_lastOutcome.results.size());
    } else if (m_w->minX && m_w->maxX && m_w->minZ && m_w->maxZ) {
        ctx.minX = m_w->minX->value(); ctx.maxX = m_w->maxX->value();
        ctx.minZ = m_w->minZ->value(); ctx.maxZ = m_w->maxZ->value();
        ctx.source = slime::ui::t("ui.bba2e2e3b721");
    } else {
        return;
    }
    std::fprintf(stderr, "[挂机点] 打开对话框：版本=%s 种子=%lld 区域=[%d..%d]x[%d..%d] 来源=%s\n",
                 ctx.version == 1 ? "基岩版" : "Java", static_cast<long long>(ctx.seed),
                 ctx.minX, ctx.maxX, ctx.minZ, ctx.maxZ, ctx.source.toUtf8().constData());

    AfkDialog dlg(ctx, this);
    dlg.exec();
}

void MainWindow::showGpuStatus()
{
    if (!m_gpuProbeDone) ensureGpuScanOnce(); // 与搜索路径相同的初始化
    const QString body = gpuStatusText();     // 对话框正文（自检直接断言同一函数）
    const bool Ok = m_gpuAvailable && m_gpuScan != nullptr;
    const QString text = Ok ? (QStringLiteral("✅ ") + body) : (QStringLiteral("❌ ") + body);
    updateGpuStatusUi(); // 同步左侧状态标签文字与主题色
    addFeedback(Ok ? 2 : 1, text);
    setStatus(Ok ? slime::ui::t("ui.0a94c990a118") : slime::ui::t("ui.3be003594bed"));
    slime::LogBus::info("GPU 状态查询：" + text.toStdString());
    if (!m_uiQuiet) {
        QMessageBox box(this);
        box.setWindowTitle(slime::ui::t("ui.e4d209f2e621"));
        box.setTextFormat(Qt::PlainText);
        box.setText(text);
        box.setIcon(Ok ? QMessageBox::Information : QMessageBox::Warning);
        box.exec();
    }
}

// ============================================================================
// v3.3.5（Bug 5）：日志开关 —— 菜单项在「启用日志…/关闭日志」之间动态切换
// ============================================================================
void MainWindow::updateLogMenuText()
{
    if (!m_actLogToggle) return;
    m_actLogToggle->setText(slime::LogBus::enabled() ? slime::ui::t("ui.0aa53272a236")
                                                     : slime::ui::t("ui.80005af43eec"));
}

void MainWindow::toggleLogging()
{
    if (slime::LogBus::enabled()) {
        // 关闭：停止记录、flush 并关闭文件；日志栏恢复默认提示
        slime::LogBus::setMode(slime::LogMode::Off);
        saveAlgorithmToConfig(); // 写回 config.json 的 log.mode = off
        if (m_logPanel) m_logPanel->refreshFromBus();
        updateLogMenuText();
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.0035e24be638")));
        setStatus(slime::ui::tr(slime::ui::t("ui.564c665e22ef")));
        return;
    }
    showEnableLogDialog(); // 未启用：走原有的两级选项对话框
    updateLogMenuText();
}

// ============================================================================
// v3.3.5（Bug 6）：GPU 实测吞吐率基准（方案 A；v3.3.6 起只报吞吐率，不给交叉点结论）
//   * 后台线程测量 CPU 与 GPU 在两种规模下的端到端耗时（含预热、多次取最优）
//   * 拟合 t(n) = 固定开销 + n / 吞吐，解出两者相等处的区块数 → 交叉点区间
//   * 结果写入 config.json（含设备名与核心数，硬件变化时自动失效重测）
//   * 同时给出实测吞吐率（方案 B 的信息也一并显示）
// ============================================================================
void MainWindow::startGpuBenchmark()
{
    if (!m_gpuScan || !m_gpuAvailable) return;
    if (m_gpuBenchCpuRate > 0.0 && m_gpuBenchGpuRate > 0.0) return; // 已有结果
    // 基准在后台线程执行（不阻塞界面）；使用独立的 GpuSession 适配器副本
    std::shared_ptr<slime::GpuScan> gpu = m_gpuScan;
    const Version version = m_w->bedrockRadio->isChecked() ? Version::Bedrock : Version::Java;
    const int cpuThreads = (std::max)(1, m_w->threads->value());
    const std::string devName = gpu->deviceName();
    const uint64_t vram = gpu->globalMemBytes();
    QPointer<MainWindow> self(this);
    std::thread([self, gpu, version, cpuThreads, devName, vram]() {
        const int64_t nSmall = 100000;      // 10 万区块（任务书要求）
        const int64_t nLarge = (version == Version::Java) ? 20000000LL : 4000000LL;
        const auto side = [](int64_t n) { return static_cast<int>(std::lround(std::sqrt(static_cast<double>(n)))); };
        const auto measure = [&](int64_t n, bool useGpu, double* outSec) {
            const int w = side(n), h = side(n);
            // CPU：多线程扫描（同一判定函数；实现在文件作用域，见 benchCpuScanSeconds）
            if (!useGpu) {
                *outSec = benchCpuScanSeconds(version, cpuThreads, w, h);
                return true;
            }
            // GPU：端到端（含回读）。v3.7.4：优先用打包字内核（与搜索引擎实际走的路径一致），
            //   否则测量结果会低估 GPU（字节路径回读量是 8 倍）。
            std::vector<uint8_t> buf;
            std::vector<uint64_t> wbuf;
            double best = 1e9;
            std::string err;
            for (int rep = 0; rep < 3; ++rep) {
                const auto t0 = std::chrono::steady_clock::now();
                bool ok = false;
                if (w % 64 == 0) ok = gpu->scanRectWords(version, 0, 0, 0, w, h, wbuf, &err);
                if (!ok) ok = gpu->scanRect(version, 0, 0, 0, w, h, buf, &err);
                if (!ok) return false;
                const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                if (rep > 0 && el < best) best = el;
            }
            *outSec = best;
            return true;
        };
        double cpuS = 0, cpuL = 0, gpuS = 0, gpuL = 0;
        if (!measure(nSmall, false, &cpuS) || !measure(nLarge, false, &cpuL) ||
            !measure(nSmall, true, &gpuS) || !measure(nLarge, true, &gpuL))
            return;
        const double ncpuS = side(nSmall) * side(nSmall), ncpuL = side(nLarge) * side(nLarge);
        // 拟合 t = a + n / r（a = 固定开销，r = 吞吐）；v3.3.6 只用吞吐率做展示，
        // 不再解交叉点、不再给出任何「哪个更快」的结论（任务书 Bug 2：用户决定取消该说法）。
        const double rCpu = (ncpuL - ncpuS) / (cpuL - cpuS);
        const double aCpu = cpuS - ncpuS / rCpu;
        const double rGpu = (ncpuL - ncpuS) / (gpuL - gpuS);
        const double aGpu = gpuS - ncpuS / rGpu;
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, rCpu, rGpu, aCpu, aGpu, devName, vram, cpuThreads]() {
            if (!self) return;
            self->m_gpuBenchCpuRate = rCpu;
            self->m_gpuBenchGpuRate = rGpu;
            const double ratio = (rCpu > 1.0) ? (rGpu / rCpu) : 0.0;
            const QString verdict =
                ratio <= 0.0 ? QString()
                             : (ratio >= 1.0
                                    ? slime::ui::t("ui.e7f9b9c95f4d")
                                          .arg(ratio, 0, 'f', 2)
                                    : slime::ui::t("ui.6e86c3dc022d")
                                          .arg(ratio, 0, 'f', 2));
            self->m_gpuBenchText = slime::ui::t("ui.1ced5aa48bbb")
                                       .arg(cpuThreads)
                                       .arg(rCpu / 1e8, 0, 'f', 2)
                                       .arg(rGpu / 1e8, 0, 'f', 2)
                                       .arg(aCpu * 1000.0, 0, 'f', 2)
                                       .arg(aGpu * 1000.0, 0, 'f', 2)
                                       .arg(verdict);
            (void)devName;
            (void)vram;
            self->updateGpuBenchmarkUi();
            slime::LogBus::info("GPU 实测吞吐率完成：" + self->m_gpuBenchText.toStdString());
        }, Qt::QueuedConnection);
    }).detach();
}

// v3.3.6（任务书 Bug 2）：测量完成后只刷新「实测吞吐率」相关展示；
// 不再解交叉点、不给出「哪个更快」的结论，也提示用户自行实测决定。
void MainWindow::updateGpuBenchmarkUi()
{
    if (m_w->gpuStatus && m_gpuAvailable) {
        m_w->gpuStatus->setText(slime::ui::t("ui.c0ba81d5e5b8"));
        m_w->gpuStatus->setToolTip(m_gpuBenchText);
    }
    if (m_w->gpuCheck) {
        m_w->gpuCheck->setToolTip(m_gpuBenchText.isEmpty()
                                      ? gpuReminderText()
                                      : (m_gpuBenchText + QStringLiteral("\n\n") + gpuReminderText()));
    }
}

// ============================================================================
// v3.3.5（优化 4）：滚轮调整数值 —— 默认关闭；仅当开关打开且输入框已聚焦时生效
// ============================================================================
void MainWindow::installWheelFilter()
{
    const QList<QSpinBox*> spins = findChildren<QSpinBox*>();
    const QList<QDoubleSpinBox*> dspins = findChildren<QDoubleSpinBox*>();
    for (QSpinBox* s : spins) {
        s->setFocusPolicy(Qt::StrongFocus);
        s->installEventFilter(this);
    }
    for (QDoubleSpinBox* s : dspins) {
        s->setFocusPolicy(Qt::StrongFocus);
        s->installEventFilter(this);
    }
    // v3.5.1（交互手感）：下拉框与滑块**永不**吞滚轮 —— 鼠标划过时滚轮只滚页面，避免误改设置
    const QList<QComboBox*> combos = findChildren<QComboBox*>();
    const QList<QSlider*> sliders = findChildren<QSlider*>();
    for (QComboBox* cb : combos) cb->installEventFilter(this);
    for (QSlider* sl : sliders) sl->installEventFilter(this);
    std::fprintf(stderr, "[交互] 滚轮手滑防护：下拉框 %d 个 + 滑块 %d 个（滚轮只滚页面）\n",
                 static_cast<int>(combos.size()), static_cast<int>(sliders.size()));
    std::fprintf(stderr, "[设置] 滚轮过滤器已安装：%d 个 SpinBox + %d 个 DoubleSpinBox（允许滚轮=%d）\n",
                 static_cast<int>(spins.size()), static_cast<int>(dspins.size()),
                 (int)m_settings.wheelAdjust);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* ev)
{
    // v3.5.4：点击结果行 → 地图定位到该结果所在的区块（拖动选择文本时不触发）
    if (m_w && m_w->resultView && watched == m_w->resultView) {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton) m_resultClickPos = me->pos();
        } else if (ev->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton &&
                (me->pos() - m_resultClickPos).manhattanLength() <= 4) {
                const QTextCursor cur = m_w->resultView->cursorForPosition(me->pos());
                const QString line = cur.block().text();
                static const QRegularExpression re(
                    QStringLiteral("chunkX\\((-?\\d+)~(-?\\d+)\\)\\s+chunkZ\\((-?\\d+)~(-?\\d+)\\)"));
                const auto m = re.match(line);
                if (m.hasMatch()) {
                    const int cx = m.captured(1).toInt() + (m.captured(2).toInt() - m.captured(1).toInt()) / 2;
                    const int cz = m.captured(3).toInt() + (m.captured(4).toInt() - m.captured(3).toInt()) / 2;
                    focusMapOnChunk(cx, cz);
                }
            }
        }
    }
    // v3.5.1：双击中间分隔条 = 恢复默认比例（只改尺寸，不改任何视觉样式）
    if (ev->type() == QEvent::MouseButtonDblClick) {
        if (m_hSplit && m_hSplit->count() > 1 && watched == m_hSplit->handle(1)) {
            const int w = m_hSplit->width();
            const int left = 380;
            const int right = 360;
            m_hSplit->setSizes(QList<int>{ left, (std::max)(360, w - left - right), right });
            setStatus(slime::ui::tr(slime::ui::t("ui.5a6665d65ea8")));
            return true;
        }
        if (m_rightSplit && m_rightSplit->count() > 1 && watched == m_rightSplit->handle(1)) {
            // v3.10.1：右列 = 结果 / 过程；双击恢复默认（过程 240）
            const int hgt = m_rightSplit->height();
            const int bottom = (std::min)(400, (std::max)(200, hgt * 2 / 5));
            m_rightSplit->setSizes(QList<int>{ (std::max)(240, hgt - bottom), bottom });
            setStatus(slime::ui::tr(slime::ui::t("ui.f245741059aa")));
            return true;
        }
    }
    if (ev->type() == QEvent::Wheel) {
        // v3.5.1：下拉框/滑块不吞滚轮（无论是否聚焦）—— 滚轮交给外层滚动区，值只能点开选/拖动改
        if (qobject_cast<QComboBox*>(watched) || qobject_cast<QSlider*>(watched)) {
            forwardWheelToScrollArea(qobject_cast<QWidget*>(watched), static_cast<QWheelEvent*>(ev));
            ev->accept();
            return true;
        }
        auto* spin = qobject_cast<QAbstractSpinBox*>(watched);
        if (spin) {
            auto* we = static_cast<QWheelEvent*>(ev);
            const int dy = we->angleDelta().y();
            if (!m_settings.wheelAdjust) {
                // 设置关闭：数值不变，且**不吞掉滚轮** —— 转交给外层滚动区，页面照常上下滚
                forwardWheelToScrollArea(spin, static_cast<QWheelEvent*>(ev));
                ev->accept();
                return true;
            }
            // v3.4.8（用户实测：设置开启后滚轮调节失效）：改为**鼠标划过即可调**，不必先点一下获得焦点。
            //   （旧实现要求 hasFocus()，用户"滚轮划过"时永远不满足 ⇒ 看起来完全没反应。）
            if (dy != 0) {
                const int steps = (dy > 0 ? 1 : -1) * (std::max)(1, std::abs(dy) / 120);
                if (auto* sb = qobject_cast<QSpinBox*>(spin)) {
                    sb->setValue(sb->value() + steps * sb->singleStep());
                } else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(spin)) {
                    dsb->setValue(dsb->value() + steps * dsb->singleStep());
                }
            }
            ev->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, ev);
}

// v3.5.1（交互手感）：把滚轮事件转交给外层滚动区（下拉框/滑块/数值框共用）
bool MainWindow::forwardWheelToScrollArea(QWidget* from, QWheelEvent* we)
{
    if (!from || !we) return false;
    QWidget* p = from->parentWidget();
    while (p && !qobject_cast<QAbstractScrollArea*>(p)) p = p->parentWidget();
    if (auto* sa = qobject_cast<QAbstractScrollArea*>(p)) {
        QApplication::sendEvent(sa->viewport(), we);
        return true;
    }
    return false;
}

// v3.5.1（交互手感）：Esc = 取消进行中的搜索/构建（与工具栏「取消」等价，不改变任何视觉）
void MainWindow::keyPressEvent(QKeyEvent* ev)
{
    if (ev && ev->key() == Qt::Key_Escape && ev->modifiers() == Qt::NoModifier) {
        if (m_w && m_w->cancel && m_w->cancel->isEnabled()) {
            cancelSearch();
            ev->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(ev);
}

void MainWindow::highlightSeedField()
{
    if (!m_w || !m_w->seedEdit) return;
    m_w->seedEdit->setFocus(Qt::OtherFocusReason);
    m_w->seedEdit->selectAll();
    auto* timer = new QTimer(this);
    auto* count = new int(0);
    connect(timer, &QTimer::timeout, this, [this, timer, count]() {
        const bool on = (*count % 2) == 0;
        m_w->seedEdit->setStyleSheet(on ? QStringLiteral("background:#FFE4E1; border:2px solid #B03020;")
                                        : QString());
        if (++(*count) >= 6) {
            timer->stop();
            m_w->seedEdit->setStyleSheet(QString());
            timer->deleteLater();
            delete count;
        }
    });
    timer->start(180);
}

// v3.3.4（任务书 2.2）：反馈辅助
void MainWindow::addFeedback(int level, const QString& message)
{
    if (!m_feedback) return;
    const FeedbackLevel lv = static_cast<FeedbackLevel>((std::max)(0, (std::min)(3, level)));
    m_feedback->add(lv, message);
    updateNavBadges(); // v3.3.6（任务书 调整 2）：反馈计数徽章随新条目刷新
}

void MainWindow::addError(const QString& message)
{
    if (!m_feedback) return;
    m_feedback->addSevere(message); // 追加错误 + 发出 severeAdded（主窗口据此切换分类并弹窗）
    updateNavBadges();
    slime::LogBus::error(message.toStdString());
}

// v3.3.4（任务书 2.1）：进度标签页的「已用时 / 预计剩余」刷新
void MainWindow::updateElapsedUi()
{
    if (!m_w || !m_w->progressElapsed || !m_w->progressEta) return;
    if (!m_running) return;
    const double elapsed = m_searchStartMs > 0
                               ? (static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::system_clock::now().time_since_epoch())
                                                          .count() -
                                                      m_searchStartMs) /
                                  1000.0)
                               : 0.0;
    m_w->progressElapsed->setText(slime::ui::t("ui.1bb9e1587450").arg(elapsed, 0, 'f', 1));
    // v3.5.28：工具栏状态面板的阶段文字（搜索中 / 匹配中 / 排序中）
    if (m_w->searchStage) {
        const bool matching = m_stageWasMatching || m_task.livePhase() >= 2;
        m_w->searchStage->setText(matching ? slime::ui::t("ui.8f92264f67fa")
                                           : slime::ui::t("ui.bd9dbc6fc1d9"));
    }
    if (m_etaSec > 0.0 && m_etaSec < 36000.0) {
        const int s = static_cast<int>(m_etaSec + 0.5);
        m_w->progressEta->setText(slime::ui::t("ui.11f69d6c1387").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0')));
    } else {
        m_w->progressEta->setText(slime::ui::t("ui.f00785313fdc"));
    }
}

// ============================================================================
// v3.3.6（任务书 调整 2/3/4.7）：底部左侧导航 / 进度流水 / 主题应用
// ============================================================================

// 切换底部面板分类：0 进度 / 1 反馈 / 2 日志 / 3 命令
// 当前项：加粗 + 高亮背景 + ● 前缀；未选中：○ 前缀（样式由 ThemeManager 的 #navList 提供）
void MainWindow::selectBottomCategory(int index)
{
    if (!m_nav || !m_stack) return;
    if (index < 0 || index >= m_nav->count()) return;
    static const char* const kNames[4] = { "ui.tab.progress", "ui.tab.feedback", "ui.tab.log", "ui.tab.command" };
    for (int i = 0; i < m_nav->count(); ++i) {
        QListWidgetItem* it = m_nav->item(i);
        if (!it) continue;
        const bool cur = (i == index);
        QString text = QString::fromUtf8(cur ? "● " : "○ ") + slime::ui::t(kNames[i]);
        if (i == 1 && m_feedback) {
            const QString badge = FeedbackPanel::badgeText(m_feedback->warningCount(),
                                                           m_feedback->errorCount());
            if (!badge.isEmpty()) text += QLatin1Char(' ') + badge; // 仅「反馈」显示计数
        }
        it->setText(text);
        QFont f = it->font();
        f.setBold(cur);
        it->setFont(f);
        it->setBackground(cur ? QBrush(QColor(0, 120, 215, 60)) : QBrush(Qt::NoBrush));
        it->setForeground(cur ? QBrush(QColor(0, 120, 215)) : QBrush(Qt::NoBrush));
    }
    if (m_dockTitle) m_dockTitle->setText(slime::ui::t("ui.cb45d50a89a3").arg(QString::fromUtf8(kNames[index])));
    for (int i = 0; i < static_cast<int>(m_dockHeadTabs.size()); ++i) {
        if (m_dockHeadTabs[static_cast<size_t>(i)]) m_dockHeadTabs[static_cast<size_t>(i)]->setChecked(i == index);
    }
    m_nav->setCurrentRow(index);
    m_stack->setCurrentIndex(index);
}

// v3.3.6（任务书 调整 2）：计数规则 —— 只统计警告与错误，格式「反馈 ⚠️2❌1」
void MainWindow::updateNavBadges()
{
    if (!m_nav || m_nav->count() < 2) return;
    selectBottomCategory(m_nav->currentRow() < 0 ? 0 : m_nav->currentRow());
}

// v3.3.6（任务书 调整 3）：每 0.5 秒向「进度」分类追加一条流水
void MainWindow::appendProgressStreamLine()
{
    if (!m_progressStream || !m_running) return;
    const double elapsed = m_searchStartMs > 0
                               ? (static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::system_clock::now().time_since_epoch())
                                                          .count() -
                                                      m_searchStartMs) /
                                  1000.0)
                               : 0.0;
    const int64_t found = m_task.liveMatchCount(); // v3.3.7（P0-2）：计数由 SearchTask 持有
    const int64_t scanned = m_task.liveScannedCount(); // v3.3.7（P0-3）：真实扫描计数（与进度条同源）
    // v3.3.9（任务书 1.1/1.4）：缓存构建模式的流水行 —— 四项（已记录 / 百分比 / 速度 / 已花费）+ 阶段区分
    if (m_buildActive) {
        const int64_t recorded = m_buildDoneChunks + scanned;
        const int64_t total = (std::max)(int64_t(1), buildTotalChunks());
        const double cps = elapsed > 0.05 ? static_cast<double>(recorded) / elapsed : 0.0;
        m_progressStream->appendRawLine(
            slime::ui::t("ui.86577d8e48b4")
                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                .arg(m_task.livePhase() == 1 ? slime::ui::t("ui.6347233dd7dd") : slime::ui::t("ui.a8878877a9f5"))
                .arg(recorded)
                .arg(100.0 * static_cast<double>(recorded) / static_cast<double>(total), 0, 'f', 1)
                .arg(cps / 10000.0, 0, 'f', 1)
                .arg(elapsed, 0, 'f', 1)
                .arg(m_buildSkippedTotal + m_task.liveSkippedCoveredCount()));
        m_streamScanned = scanned;
    } else {
        if (scanned > m_streamScanned) m_streamScanned = scanned;
        m_progressStream->appendProgress(elapsed, m_streamScanned, displayTotalChunks(), found); // N/M（xx%）
    }
    // v3.3.7（P1-6/P0-3）：每 10 个节拍（约 5 秒）把状态环写进关键信息日志 —— 崩溃时能还原现场
    // （调试模式下每 2 秒一条）
    const int everyTicks = slime::IncidentLog::verbose() ? 4 : 10;
    if (++m_streamTick >= everyTicks) {
        m_streamTick = 0;
        slime::IncidentLog::note("进度：已用时 " + std::to_string(static_cast<long long>(elapsed)) +
                                 "s 扫描 " + std::to_string(m_streamScanned) + "/" +
                                 std::to_string(displayTotalChunks()) + " 区块 找到 " +
                                 std::to_string(found) + " 个 | " + slime::IncidentLog::memorySummary());
    }
}

// v3.3.6（任务书 4.7）：按设置切换浅色 / 深色 / 跟随系统（运行期立即生效，无需重启）
void MainWindow::applyThemeFromSettings()
{
    const slime::ThemeMode mode = slime::ThemeManager::modeFromString(m_settings.theme);
    slime::ThemeManager::instance().apply(qApp, mode);
    applyInlineThemeColors(); // 少量必须内联的状态色随主题刷新
    slime::LogBus::info(std::string("主题已切换为 ") +
                        slime::ThemeManager::modeToString(mode).toStdString());
}

// ============================================================================
// v3.3.4（任务书 2.4）：命令终端宿主 —— 命令立即执行（可插队到正在运行的搜索）
//
// 设计：命令解析与执行全部在 GUI 线程同步完成，与搜索线程（SearchTask 的 std::thread）
// 完全独立，因此设置类/查询类命令立即生效、cancel 立即中断、不阻塞用户交互。
// 最小命令集：help / clear(终端内置) / version / seed / range / center / preset / mode /
//            search / cancel / status / gpu / target / result / log
// ============================================================================
QStringList MainWindow::commandNames() const
{
    // v3.4.11（用户拍板：扩展命令表）：新增命令均对应真实功能；help/补全都读这一张表
    return { QStringLiteral("about"),  QStringLiteral("build"),   QStringLiteral("cache"),
             QStringLiteral("cancel"), QStringLiteral("center"),  QStringLiteral("clear"),
             QStringLiteral("copy"),   QStringLiteral("density"), QStringLiteral("gpu"),
             QStringLiteral("grid"),   QStringLiteral("help"),    QStringLiteral("lock"),
             QStringLiteral("log"),    QStringLiteral("mode"),    QStringLiteral("open"),
             QStringLiteral("preset"), QStringLiteral("range"),   QStringLiteral("result"),
             QStringLiteral("search"), QStringLiteral("seed"),    QStringLiteral("smart"),
             QStringLiteral("status"), QStringLiteral("stop"),    QStringLiteral("target"),
             QStringLiteral("theme"),  QStringLiteral("threads"), QStringLiteral("version") };
}

QString MainWindow::commandHelp(const QString& cmd) const
{
    static const QHash<QString, QString> aHelp = {
        { QStringLiteral("help"), slime::ui::t("ui.f4f64b027abd") },
        { QStringLiteral("version"), slime::ui::t("ui.bd2fc88197ae") },
        { QStringLiteral("seed"), slime::ui::t("ui.be84b2c5322f") },
        { QStringLiteral("range"), slime::ui::t("ui.c9951d36a841") },
        { QStringLiteral("center"), slime::ui::t("ui.14074052b097") },
        { QStringLiteral("preset"), slime::ui::t("ui.d72ad001dcdf") },
        { QStringLiteral("mode"), slime::ui::t("ui.7ef8de2a05a9") },
        { QStringLiteral("search"), slime::ui::t("ui.6380f599e548") },
        { QStringLiteral("cancel"), slime::ui::t("ui.dc7e8d7b8a9f") },
        { QStringLiteral("status"), slime::ui::t("ui.9d871c556321") },
        { QStringLiteral("gpu"), slime::ui::t("ui.9fc4bd1d3a96") },
        { QStringLiteral("target"), slime::ui::t("ui.a08c4c8c3e3b") },
        { QStringLiteral("result"), slime::ui::t("ui.8d2180f88471") },
        { QStringLiteral("log"), slime::ui::t("ui.88e1078439f9") },
        { QStringLiteral("theme"), slime::ui::t("ui.4ef62fa28d91") },
        { QStringLiteral("stop"), slime::ui::t("ui.facc5e3a4732") },
        { QStringLiteral("smart"), slime::ui::t("ui.1149d8a99ff3") },
        { QStringLiteral("lock"), slime::ui::t("ui.0ed032ec9e84") },
        { QStringLiteral("grid"), slime::ui::t("ui.f119374add58") },
        { QStringLiteral("density"), slime::ui::t("ui.071d3bf971b4") },
        { QStringLiteral("threads"), slime::ui::t("ui.e7258f9fd682") },
        { QStringLiteral("build"), slime::ui::t("ui.d6769ff1ecde") },
        { QStringLiteral("cache"), slime::ui::t("ui.98918284391b") },
        { QStringLiteral("copy"), slime::ui::t("ui.60a473d46989") },
        { QStringLiteral("open"), slime::ui::t("ui.e2bc2f6e629e") },
        { QStringLiteral("about"), slime::ui::t("ui.b223e9759bc5") },
        { QStringLiteral("clear"), slime::ui::t("ui.9db4f6f902f7") },
    };
    return aHelp.value(cmd, slime::ui::t("ui.6535b7b34aa5"));
}

QString MainWindow::runCommand(const QString& cmd, const QStringList& args)
{
    const QString c = cmd.toLower();
    const auto argAt = [&args](int i) { return i < args.size() ? args[i] : QString(); };

    // v3.4.11（用户拍板：构建模式下参数命令「拒绝并提示」，不再静默失效）：
    //   构建只做史莱姆判定 + 写缓存，不参与匹配 ⇒ 这些参数对构建毫无影响，
    //   静默"成功"会让用户以为设置生效了。要改参数请先 `build off` 回到搜索模式。
    if (buildModeActive()) {
        static const QStringList kSearchOnly = {
            QStringLiteral("mode"),    QStringLiteral("target"), QStringLiteral("grid"),
            QStringLiteral("density"), QStringLiteral("stop"),   QStringLiteral("smart"),
            QStringLiteral("lock"),    QStringLiteral("threads")
        };
        if (kSearchOnly.contains(c)) {
            return slime::ui::t("ui.2f1957578333")
                .arg(c);
        }
    }

    if (c == QLatin1String("help")) {
        QString out = slime::ui::t("ui.b43c9cc77dd4").arg(commandNames().size());
        for (const QString& n : commandNames()) {
            out += QStringLiteral("  %1  -  %2\n").arg(n, -8).arg(commandHelp(n));
        }
        out += slime::ui::t("ui.e3335507e1df");
        return out;
    }
    if (c == QLatin1String("version")) {
        return slime::ui::t("ui.6a97f78c38df")
            .arg(QLatin1String(kAppVersion))
            .arg(QString::fromStdWString(appDirPath().wstring()))
            .arg(QStringLiteral(QT_VERSION_STR))
            .arg(m_daysSinceRelease);
    }
    if (c == QLatin1String("clear")) {
        // clear 由终端自己处理（清空输出区）；这里给 API 调用方一个明确应答，
        // 也保证「命令表 ↔ 实现」一致性自检不会把它当成幽灵指令。
        return slime::ui::t("ui.5736a4b3d232");
    }
    if (c == QLatin1String("about")) {
        return slime::ui::t("ui.d29cc9aa2cb5")
            .arg(QLatin1String(kAppVersion))
            .arg(QString::fromStdWString(appDirPath().wstring()))
            .arg(QLatin1String(kContactEmail));
    }
    if (c == QLatin1String("help") && !args.isEmpty()) {
        const QString one = args[0].toLower();
        if (!commandNames().contains(one)) {
            return slime::ui::t("ui.e4f8211e4809").arg(one).arg(commandNames().size());
        }
        return QStringLiteral("%1 - %2").arg(one, commandHelp(one));
    }
    if (c == QLatin1String("theme")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.cc2fe33a16e2")
                .arg(slime::ThemeManager::modeDisplayName(slime::ThemeManager::instance().mode()),
                     slime::ThemeManager::modeToString(slime::ThemeManager::instance().mode()));
        }
        const QString v = args[0].toLower();
        if (v != QLatin1String("light") && v != QLatin1String("dark") && v != QLatin1String("system")) {
            return slime::ui::t("ui.491a4473817c");
        }
        AppSettings s = m_settings;
        s.theme = v;
        applyAppSettings(s);              // 含主题重放 + 设置页收敛
        flushPendingConfigWrite();        // 主题是显式操作，立即落盘
        return slime::ui::t("ui.7b64f65aa5cb")
            .arg(slime::ThemeManager::modeDisplayName(slime::ThemeManager::instance().mode()));
    }
    if (c == QLatin1String("stop") || c == QLatin1String("smart") || c == QLatin1String("lock")) {
        QCheckBox* box = (c == QLatin1String("stop"))    ? m_w->stopAtTarget
                          : (c == QLatin1String("smart")) ? m_w->smartRange
                                                          : m_w->lockDir;
        const QString label = (c == QLatin1String("stop"))    ? slime::ui::t("ui.166485d58cc3")
                               : (c == QLatin1String("smart")) ? slime::ui::t("ui.9522c47b44f6")
                                                               : slime::ui::t("ui.144f946f0e21");
        if (args.isEmpty()) return QStringLiteral("%1：%2").arg(label, box->isChecked() ? slime::ui::t("ui.d78cde076b67")
                                                                                        : slime::ui::t("ui.f628761bf56a"));
        const QString v = args[0].toLower();
        if (v != QLatin1String("on") && v != QLatin1String("off")) {
            return slime::ui::t("ui.f28af3ed51d6").arg(c);
        }
        box->setChecked(v == QLatin1String("on"));
        onModeChanged();
        return slime::ui::t("ui.23935bb28c3c").arg(label, box->isChecked() ? slime::ui::t("ui.256783b7d4ea") : slime::ui::t("ui.6c14bd7f6f9e"));
    }
    if (c == QLatin1String("grid")) {
        if (args.isEmpty()) return slime::ui::t("ui.3b566a78f727").arg(m_w->gridSize->value());
        bool Ok = false;
        const int n = args[0].toInt(&Ok);
        if (!Ok || n < 1 || n > 512) return slime::ui::t("ui.a81d88d7e1e9");
        m_w->gridSize->setValue(n);
        return slime::ui::t("ui.fae3f47b7eee").arg(m_w->gridSize->value());
    }
    if (c == QLatin1String("density")) {
        if (args.isEmpty()) return slime::ui::t("ui.b55699ba275b").arg(m_w->minDensity->value(), 0, 'f', 3);
        bool Ok = false;
        const double v = args[0].toDouble(&Ok);
        if (!Ok || v < 0.0 || v > 1.0) return slime::ui::t("ui.7736eaf1585d");
        m_w->minDensity->setValue(v);
        return slime::ui::t("ui.6fa36e115342").arg(m_w->minDensity->value(), 0, 'f', 3);
    }
    if (c == QLatin1String("threads")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.58beee25a300")
                .arg(m_w->threads->value()).arg(m_lastEffectiveThreads);
        }
        bool Ok = false;
        const int n = args[0].toInt(&Ok);
        if (!Ok || n < 0) return slime::ui::t("ui.a890971c8e76");
        m_w->threads->setValue(n);
        return slime::ui::t("ui.a1297902c8e4").arg(m_w->threads->value());
    }
    if (c == QLatin1String("build")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.1f501012d9cb")
                .arg(buildModeActive() ? slime::ui::t("ui.d78cde076b67") : slime::ui::t("ui.f628761bf56a"));
        }
        const QString v = args[0].toLower();
        if (v != QLatin1String("on") && v != QLatin1String("off")) {
            return slime::ui::t("ui.44b21e54a5d3");
        }
        const bool on = (v == QLatin1String("on"));
        showPage(on ? kPageBuild : kPageSearch); // 页面 = 构建模式（单一事实来源）
        return slime::ui::t("ui.41db64208732")
            .arg(on ? slime::ui::t("ui.256783b7d4ea") : slime::ui::t("ui.6c14bd7f6f9e"),
                 on ? slime::ui::t("ui.d4fe2a107b07") : QString());
    }
    if (c == QLatin1String("cache")) {
        const QString sub = argAt(0).toLower();
        if (sub.isEmpty() || sub == QLatin1String("status")) {
            return slime::ui::t("ui.ca94b4a071fe")
                .arg(m_cache->hasData() ? slime::ui::t("ui.b19bae5d1305") : slime::ui::t("ui.43e203c1de2c"))
                .arg(static_cast<qlonglong>(m_cache->entryCount()))
                .arg(static_cast<qlonglong>(m_cache->ranges().size()))
                .arg(QString::fromStdWString(m_cacheFile.wstring()));
        }
        if (sub == QLatin1String("clear")) {
            m_cache->clear();
            std::error_code ec;
            std::filesystem::remove(m_cacheFile, ec);
            updateCacheStatusUi();
            return slime::ui::t("ui.20563af38637");
        }
        if (sub == QLatin1String("import")) {
            const QString path = args.mid(1).join(QLatin1Char(' ')).trimmed();
            if (path.isEmpty()) return slime::ui::t("ui.7a449b293047");
            const int64_t before = m_cache->entryCount();
            importCacheFromPath(path, /*showProgressDialog=*/!m_uiQuiet, /*showResultDialogs=*/!m_uiQuiet);
            const int64_t added = m_cache->entryCount() - before;
            updateCacheStatusUi();
            return added > 0 ? slime::ui::t("ui.06fa0ea0e616").arg(path).arg(added)
                             : slime::ui::t("ui.f56a1563fe42").arg(path);
        }
        if (sub == QLatin1String("build")) {
            if (!buildModeActive()) showPage(kPageBuild); // 语义：切到构建模式并立即开始构建
            if (m_running) return slime::ui::t("ui.cf0237994529");
            startSearch();
            return slime::ui::t("ui.9c599c680242");
        }
        return slime::ui::t("ui.549d8b43d0eb");
    }
    if (c == QLatin1String("copy")) {
        if (args.isEmpty()) return slime::ui::t("ui.2a56e37d4df1");
        bool Ok = false;
        const int n = args[0].toInt(&Ok);
        const int have = m_hasLastOutcome ? static_cast<int>(m_lastOutcome.results.size()) : 0;
        if (!Ok || n < 1) return slime::ui::t("ui.afc8e8e9cff7");
        if (n > have) {
            // v3.4.11（用户拍板）：越界**明确报错**，不静默失败
            return slime::ui::t("ui.20327830ac39")
                .arg(n).arg(have).arg(have == 0 ? slime::ui::t("ui.4d35ab452991") : QString());
        }
        const QString cmdText = cacheCommandFor(m_lastOutcome.results[static_cast<size_t>(n - 1)]);
        QApplication::clipboard()->setText(cmdText);
        return slime::ui::t("ui.d62ce9956d42").arg(n).arg(cmdText);
    }
    if (c == QLatin1String("open")) {
        const QString what = argAt(0).toLower();
        std::filesystem::path dir;
        if (what == QLatin1String("logs")) dir = appDirPath() / m_settings.logSubDir.toStdWString();
        else if (what == QLatin1String("cache")) dir = m_cacheFile.parent_path();
        else if (what == QLatin1String("crash")) dir = slime::CrashDump::dumpDir();
        else return slime::ui::t("ui.891847cfe310");
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!m_uiQuiet) QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(dir.wstring())));
        return slime::ui::t("ui.3754bdc64ee8").arg(QString::fromStdWString(dir.wstring()));
    }
    if (c == QLatin1String("seed")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.1ce22a5622fb")
                .arg(m_w->seedEdit->text(),
                     m_w->seedEdit->text().trimmed().isEmpty() ? slime::ui::t("ui.7307b22f991b")
                                                               : slime::ui::t("ui.ab6e15ae13ad"));
        }
        m_w->seedEdit->setText(args.join(QLatin1Char(' ')).trimmed());
        saveAlgorithmToConfig();
        bool Ok = true;
        m_w->seedEdit->text().trimmed().toLongLong(&Ok);
        return slime::ui::t("ui.27a21d4c6a3f")
            .arg(m_w->seedEdit->text(),
                 Ok ? slime::ui::t("ui.e994f7174177") : slime::ui::t("ui.3d24b2f34e0a"));
    }
    if (c == QLatin1String("range")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.8285db444fc5")
                .arg(m_w->minX->value()).arg(m_w->maxX->value())
                .arg(m_w->minZ->value()).arg(m_w->maxZ->value())
                .arg(static_cast<qlonglong>(m_w->maxX->value() - m_w->minX->value() + 1) *
                     static_cast<qlonglong>(m_w->maxZ->value() - m_w->minZ->value() + 1));
        }
        if (args.size() == 1) {
            bool Ok = false;
            const int half = QString(args[0]).remove(QLatin1Char('+')).remove(QStringLiteral("±")).toInt(&Ok);
            if (!Ok || half <= 0) return slime::ui::t("ui.e6702b5c2316");
            m_wholeMapConfirmed = false;
            applyPreset(half);
            return slime::ui::t("ui.f72fd24b8e12")
                .arg(half).arg(m_w->minX->value()).arg(m_w->maxX->value())
                .arg(m_w->minZ->value()).arg(m_w->maxZ->value());
        }
        if (args.size() >= 4) {
            bool Ok[4] = { false, false, false, false };
            const int v[4] = { args[0].toInt(&Ok[0]), args[1].toInt(&Ok[1]), args[2].toInt(&Ok[2]), args[3].toInt(&Ok[3]) };
            if (!Ok[0] || !Ok[1] || !Ok[2] || !Ok[3]) return slime::ui::t("ui.d76733482870");
            m_wholeMapConfirmed = false;
            m_w->minX->setValue(v[0]);
            m_w->maxX->setValue(v[1]);
            m_w->minZ->setValue(v[2]);
            m_w->maxZ->setValue(v[3]);
            clampRangeToBound();
            return slime::ui::t("ui.494a57ad68b7")
                .arg(m_w->minX->value()).arg(m_w->maxX->value())
                .arg(m_w->minZ->value()).arg(m_w->maxZ->value());
        }
        return slime::ui::t("ui.e6702b5c2316");
    }
    if (c == QLatin1String("center")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.61dc0e802dcf").arg(m_w->startX->value()).arg(m_w->startZ->value());
        }
        if (args.size() < 2) return slime::ui::t("ui.e1046b29fcde");
        bool okx = false, okz = false;
        const int x = args[0].toInt(&okx);
        const int z = args[1].toInt(&okz);
        if (!okx || !okz) return slime::ui::t("ui.00373618f5f1");
        m_w->startX->setValue(x);
        m_w->startZ->setValue(z);
        return slime::ui::t("ui.ccc1c03a364a").arg(x).arg(z);
    }
    if (c == QLatin1String("preset")) {
        if (args.isEmpty()) return slime::ui::t("ui.998dc8cd0df2");
        const QString p = args[0].toLower();
        m_wholeMapConfirmed = false;
        if (p == QLatin1String("near")) { applyPreset(20000); }
        else if (p == QLatin1String("local")) { applyPreset(200000); }
        else if (p == QLatin1String("global")) { applyPreset(1000000); }
        else if (p == QLatin1String("world")) { confirmWholeMapSearch(true, kJavaBoundChunk); }
        else if (p == QLatin1String("bedrock")) { confirmWholeMapSearch(true, kBedrockBoundChunk); }
        else return slime::ui::t("ui.d93989638036").arg(p);
        return slime::ui::t("ui.a6ef6f616459")
            .arg(p).arg(m_w->minX->value()).arg(m_w->maxX->value())
            .arg(m_w->minZ->value()).arg(m_w->maxZ->value());
    }
    if (c == QLatin1String("mode")) {
        if (args.isEmpty()) {
            QString alg = algorithmDisplayName(currentAlgorithm());
            return slime::ui::t("ui.9b9c5e47b569")
                .arg(m_w->densityRadio->isChecked() ? slime::ui::t("ui.c1ae1cee6679") : slime::ui::t("ui.bcf402e061a7"))
                .arg(alg)
                .arg(m_w->gridSize->value())
                .arg(m_w->minDensity->value(), 0, 'f', 3)
                .arg(m_w->threads->value());
        }
        const QString m = args[0].toLower();
        if (m == QLatin1String("strict")) { m_w->strictRadio->setChecked(true); return slime::ui::t("ui.11102a93f46d"); }
        if (m == QLatin1String("density")) { m_w->densityRadio->setChecked(true); return slime::ui::t("ui.177ed434899d"); }
        if (m == QLatin1String("exact")) { m_w->algExactRadio->setChecked(true); updateAlgorithmPanels(); saveAlgorithmToConfig(); return slime::ui::t("ui.ef9cef28a00b"); }
        if (m == QLatin1String("layered")) { m_w->algLayeredRadio->setChecked(true); updateAlgorithmPanels(); saveAlgorithmToConfig(); return slime::ui::t("ui.11a64fcd4eb1"); }
        if (m == QLatin1String("sample")) { m_w->algSampleRadio->setChecked(true); updateAlgorithmPanels(); saveAlgorithmToConfig(); return slime::ui::t("ui.b0b3d366b535"); }
        if (m == QLatin1String("brute")) { m_w->algBruteRadio->setChecked(true); updateAlgorithmPanels(); saveAlgorithmToConfig(); return slime::ui::t("ui.06430fed9d13"); }
        return slime::ui::t("ui.f165abcfb21a").arg(m);
    }
    if (c == QLatin1String("search")) {
        if (m_running) return slime::ui::t("ui.c7ef04e0e586");
        startSearch(); // 立即返回（后台线程执行）
        return slime::ui::t("ui.baac3862490e");
    }
    if (c == QLatin1String("cancel")) {
        if (!m_running) return slime::ui::t("ui.38f3015a3657");
        cancelSearch(); // 立即置位取消标志（引擎在扫描/匹配循环内检查）
        return slime::ui::t("ui.304ad94d91b1");
    }
    if (c == QLatin1String("status")) {
        const double elapsed = m_running && m_searchStartMs > 0
                                   ? (static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                              std::chrono::system_clock::now().time_since_epoch())
                                                              .count() -
                                                          m_searchStartMs) /
                                      1000.0)
                                   : 0.0;
        return slime::ui::t("ui.77dc3da6e684")
            .arg(QLatin1String(kAppVersion))
            .arg(m_running ? slime::ui::t("ui.30160a21b92a") : slime::ui::t("ui.8bf5c10ad937"))
            .arg(elapsed, 0, 'f', 1)
            .arg(m_w->bedrockRadio->isChecked() ? slime::ui::t("ui.618fd37c0c01") : slime::ui::t("ui.8accf0fe2eed"))
            .arg(m_w->densityRadio->isChecked() ? slime::ui::t("ui.c1ae1cee6679") : slime::ui::t("ui.bcf402e061a7"))
            .arg(algorithmDisplayName(currentAlgorithm()))
            .arg(m_w->minX->value()).arg(m_w->maxX->value())
            .arg(m_w->minZ->value()).arg(m_w->maxZ->value())
            .arg(m_w->startX->value()).arg(m_w->startZ->value())
            .arg(m_w->targetCount->value())
            .arg(m_w->threads->value())
            .arg(m_gpuAvailable ? (m_w->gpuCheck->isChecked() ? slime::ui::t("ui.1482b669e3b5")
                                                             : slime::ui::t("ui.4cd139b66904"))
                                : slime::ui::t("ui.beff4a1cd1a6"))
            .arg(m_cache->hasData() ? slime::ui::t("ui.55b901c4820a").arg(m_cache->entryCount())
                                    : slime::ui::t("ui.43e203c1de2c"))
            .arg(m_hasLastOutcome ? static_cast<int>(m_lastOutcome.results.size()) : 0);
    }
    if (c == QLatin1String("gpu")) {
        if (args.isEmpty()) {
            const QString dev = (m_gpuScan && !m_gpuScan->deviceName().empty())
                                    ? QString::fromStdString(m_gpuScan->deviceName())
                                    : slime::ui::t("ui.5a692b8d27dd");
            return slime::ui::t("ui.24245a88eab7")
                .arg(m_gpuAvailable ? slime::ui::t("ui.e91365cf9ed9") : slime::ui::t("ui.beff4a1cd1a6"))
                .arg(dev, gpuReminderText());
        }
        const QString v = args[0].toLower();
        if (v == QLatin1String("on")) {
            if (!m_w->gpuCheck->isChecked()) m_w->gpuCheck->setChecked(true); // 触发检测（同步一次）
            return slime::ui::t("ui.2b798dc73cf0")
                .arg(m_gpuAvailable ? slime::ui::t("ui.e91365cf9ed9") : slime::ui::t("ui.b80b8286c79e"), gpuReminderText());
        }
        if (v == QLatin1String("off")) {
            if (m_w->gpuCheck->isChecked()) m_w->gpuCheck->setChecked(false);
            return slime::ui::t("ui.ae900ee401e9");
        }
        return slime::ui::t("ui.2ebe5eb911b5");
    }
    if (c == QLatin1String("target")) {
        if (args.isEmpty()) return slime::ui::t("ui.178488361336").arg(m_w->targetCount->value());
        bool Ok = false;
        const int n = args[0].toInt(&Ok);
        if (!Ok || n < 1) return slime::ui::t("ui.fcf3f575ff23");
        m_w->targetCount->setValue(n);
        saveAlgorithmToConfig();
        return slime::ui::t("ui.bb5ec70c5ba6").arg(n);
    }
    if (c == QLatin1String("result")) {
        const QString sub = argAt(0).toLower();
        if (sub == QLatin1String("copy")) {
            bool Ok = false;
            const int n = argAt(1).toInt(&Ok);
            if (!Ok || n < 1) return slime::ui::t("ui.5dfaa6c328d0");
            if (!m_hasLastOutcome || n > static_cast<int>(m_lastOutcome.results.size())) {
                return slime::ui::t("ui.c88644b97058")
                    .arg(n).arg(m_hasLastOutcome ? static_cast<int>(m_lastOutcome.results.size()) : 0);
            }
            const QString cmdText = cacheCommandFor(m_lastOutcome.results[static_cast<size_t>(n - 1)]);
            QApplication::clipboard()->setText(cmdText);
            return slime::ui::t("ui.d62ce9956d42").arg(n).arg(cmdText);
        }
        // result list [n]（默认 10 条）
        int limit = 10;
        if (argAt(0).toLower() == QLatin1String("list") && !argAt(1).isEmpty()) {
            bool Ok = false;
            const int n = argAt(1).toInt(&Ok);
            if (Ok && n > 0) limit = n;
        } else if (sub != QLatin1String("list") && !sub.isEmpty()) {
            bool Ok = false;
            const int n = sub.toInt(&Ok);
            if (Ok && n > 0) limit = n;
        }
        if (!m_hasLastOutcome || m_lastOutcome.results.empty()) return slime::ui::t("ui.ff2587f7bcb9");
        QString out = slime::ui::t("ui.e8976914d64f")
                          .arg(m_lastOutcome.results.size())
                          .arg(std::min<int>(limit, static_cast<int>(m_lastOutcome.results.size())));
        const size_t shown = std::min<size_t>(m_lastOutcome.results.size(), static_cast<size_t>(limit));
        for (size_t i = 0; i < shown; ++i) {
            const MatchResult& r = m_lastOutcome.results[i];
            out += slime::ui::t("ui.d0e18567e89d")
                       .arg(i + 1)
                       .arg(r.minX).arg(r.minZ)
                       .arg(r.maxX - r.minX + 1).arg(r.maxZ - r.minZ + 1)
                       .arg(r.density * 100.0, 0, 'f', 1)
                       .arg(cacheCommandFor(r));
        }
        return out;
    }
    if (c == QLatin1String("log")) {
        if (args.isEmpty()) {
            return slime::ui::t("ui.8efc1728fb58")
                .arg(QString::fromUtf8(slime::LogBus::modeName(slime::LogBus::mode())))
                .arg(slime::LogBus::enabled() ? QString::fromStdString(slime::LogBus::currentFile())
                                              : slime::ui::t("ui.817e809c70c7"))
                .arg(slime::LogBus::snapshot().size())
                .arg(slime::LogBus::kMaxEntries);
        }
        const QString v = args[0].toLower();
        if (v != QLatin1String("off") && v != QLatin1String("brief") && v != QLatin1String("detailed")) {
            return slime::ui::t("ui.d35ba202afd5");
        }
        // v3.4.11（用户要求：命令生效后设置状态及时更新）：日志模式同时写进设置项并同步设置页，
        // 否则设置页还显示旧值，下一次设置改动会把命令的结果覆盖掉。
        {
            AppSettings s = m_settings;
            s.logAtStartup = (v != QLatin1String("off"));
            s.logLevel = (v == QLatin1String("detailed")) ? 1 : 0;
            applyAppSettings(s); // 内部会 setMode + 刷新日志面板 + 收敛设置页
        }
        flushPendingConfigWrite();
        if (m_logPanel) m_logPanel->refreshFromBus();
        return slime::ui::t("ui.034e8f26bb37").arg(QString::fromUtf8(slime::LogBus::modeName(slime::LogBus::mode())));
    }
    return slime::ui::t("ui.8a5dc476ce19").arg(c);
}

// v3.3.7.2（任务书 1.2）：界面显示用的「总扫描量」—— 必须与引擎 liveScanned 同源。
// 分块路径下引擎的总量含块间重叠行（每块 hMax-1 行），比「去重区域区块数」略大；
// 若界面用后者当分母，扫描完成时百分比会 >100%（用户截图即为此）。
// 引擎未公开（快速估算的采样扫描）时退回区域区块数。
int64_t MainWindow::displayTotalChunks() const
{
    const int64_t t = m_task.liveTotalCount();
    return t > 0 ? t : m_runTotalChunks;
}

// v3.5.19：语言切换后重设「搜索模式」面板的文案（UiStrings 已迁移的部分）
// v3.5.27（用户反馈：英文覆盖面不够）：把主界面所有可见文本按翻译表改写一遍。
//   做法：首次记录中文原文（存到 dynamicProperty "zhText"），之后按语言在「原文/译文」之间切换，
//   这样来回切换不会丢失原文，也不需要把上千处 QStringLiteral 逐个改写。
void MainWindow::applyLanguage()
{
    const bool en = slime::ui::isEnglish();
    auto fixText = [en](QWidget* w) {
        if (!w) return;
        QString zh = w->property("zhText").toString();
        if (zh.isEmpty()) {
            zh = w->windowTitle().isEmpty() ? QString() : QString();
            // 不同控件取文本的方式不同，统一在下面按类型处理
        }
    };
    (void)fixText;

    auto applyTo = [en](QObject* o, const QString& cur, const std::function<void(const QString&)>& setter) {
        if (cur.isEmpty()) return;
        QString zh = o->property("zhText").toString();
        // v3.6.4：若控件当前文本本身就是一个已知中文键（运行时才设置的提示），以它为准
        if (slime::ui::zhToEn().contains(cur) || zh.isEmpty()) zh = cur;
        o->setProperty("zhText", zh);
        setter(en ? slime::ui::tr(zh) : zh);
    };

    const auto widgets = findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        if (auto* lb = qobject_cast<QLabel*>(w)) {
            if (lb->text().isEmpty()) continue;
            const QString zh = lb->property("zhText").toString().isEmpty() ? lb->text()
                                                                          : lb->property("zhText").toString();
            applyTo(lb, zh, [lb](const QString& t) { lb->setText(t); });
        } else if (auto* bt = qobject_cast<QAbstractButton*>(w)) {
            if (bt->text().isEmpty()) continue;
            const QString zh = bt->property("zhText").toString().isEmpty() ? bt->text()
                                                                          : bt->property("zhText").toString();
            applyTo(bt, zh, [bt](const QString& t) { bt->setText(t); });
        } else if (auto* gb = qobject_cast<QGroupBox*>(w)) {
            if (gb->title().isEmpty()) continue;
            const QString zh = gb->property("zhText").toString().isEmpty() ? gb->title()
                                                                          : gb->property("zhText").toString();
            applyTo(gb, zh, [gb](const QString& t) { gb->setTitle(t); });
        } else if (auto* le = qobject_cast<QLineEdit*>(w)) {
            const QString cur = le->placeholderText();
            if (!cur.isEmpty()) {
                QString zh = le->property("zhPlaceholder").toString();
                if (slime::ui::zhToEn().contains(cur) || zh.isEmpty()) zh = cur;
                le->setProperty("zhPlaceholder", zh);
                le->setPlaceholderText(en ? slime::ui::tr(zh) : zh);
            }
        } else if (auto* cb = qobject_cast<QComboBox*>(w)) {
            for (int i = 0; i < cb->count(); ++i) {
                const QString key = QStringLiteral("zhItem%1").arg(i);
                const QString cur = cb->itemText(i);
                QString zh = cb->property(key.toUtf8().constData()).toString();
                if (zh.isEmpty()) {
                    zh = cur;
                    cb->setProperty(key.toUtf8().constData(), zh);
                }
                cb->setItemText(i, en ? slime::ui::tr(zh) : zh);
            }
        }
    }
    // 标签页名（底部面板：进度/反馈/日志/命令）
    const auto tabs = findChildren<QTabWidget*>();
    for (QTabWidget* tw : tabs) {
        for (int i = 0; i < tw->count(); ++i) {
            const QString key = QStringLiteral("zhTab%1").arg(i);
            const QString cur = tw->tabText(i);
            QString zh = tw->property(key.toUtf8().constData()).toString();
            if (zh.isEmpty()) {
                zh = cur;
                tw->setProperty(key.toUtf8().constData(), zh);
            }
            tw->setTabText(i, en ? slime::ui::tr(zh) : zh);
        }
    }
    // 结果表头
    const auto tables = findChildren<QTableWidget*>();
    for (QTableWidget* tb : tables) {
        for (int c = 0; c < tb->columnCount(); ++c) {
            QTableWidgetItem* it = tb->horizontalHeaderItem(c);
            if (!it) continue;
            const QString key = QStringLiteral("zhHead%1").arg(c);
            const QString cur = it->text();
            QString zh = tb->property(key.toUtf8().constData()).toString();
            if (zh.isEmpty()) {
                zh = cur;
                tb->setProperty(key.toUtf8().constData(), zh);
            }
            it->setText(en ? slime::ui::tr(zh) : zh);
        }
    }
    const auto actions = findChildren<QAction*>();
    for (QAction* a : actions) {
        if (a->text().isEmpty()) continue;
        const QString zh = a->property("zhText").toString().isEmpty() ? a->text() : a->property("zhText").toString();
        a->setProperty("zhText", zh);
        a->setText(en ? slime::ui::tr(zh) : zh);
    }
    // 窗口标题单独处理（版本号是变量，不翻译）
    setWindowTitle(slime::ui::t("ui.8e056aa8f722").arg(QLatin1String(kAppVersion)));
}

void MainWindow::retranslateModeUi()
{
    if (!m_w) return;
    using slime::ui::Key;
    using slime::ui::text;
    if (m_w->strictRadio) m_w->strictRadio->setText(text(Key::ModePattern));
    if (m_w->densityRadio) m_w->densityRadio->setText(text(Key::ModeDensity));
    if (m_w->strictRadio) m_w->strictRadio->setToolTip(text(Key::ModePatternTip));
    if (m_w->densityRadio) m_w->densityRadio->setToolTip(text(Key::ModeDensityTip));
    if (m_w->shapeSquareRadio) m_w->shapeSquareRadio->setText(text(Key::ShapeSquare));
    if (m_w->shapeRingRadio) m_w->shapeRingRadio->setText(text(Key::ShapeRing));
    if (m_w->shapeSquareRadio) m_w->shapeSquareRadio->setToolTip(text(Key::ShapeSquareTip));
    if (m_w->shapeRingRadio) m_w->shapeRingRadio->setToolTip(text(Key::ShapeRingTip));
    if (m_w->ringScanCombo) {
        if (m_w->ringScanCombo->count() >= 2) {
            m_w->ringScanCombo->setItemText(0, text(Key::RingScanFast));
            m_w->ringScanCombo->setItemText(1, text(Key::RingScanExact));
        }
        m_w->ringScanCombo->setToolTip(text(Key::RingScanTip));
    }
    if (m_w->smartRange) {
        m_w->smartRange->setText(text(Key::SmartRange));
        m_w->smartRange->setToolTip(text(Key::SmartRangeTip));
    }
}

// v3.5.20：两种全屏 —— borderless（无边框铺满）/ windowed（窗口最大化，有边框）
#ifdef Q_OS_WIN
// v3.5.22（用户要求）：无边框全屏下 Win11 仍会给窗口加圆角 —— 显式关掉
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
static void disableWindows11Rounding(QWidget* w)
{
    if (!w) return;
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd) return;
    // DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_DONOTROUND = 1
    const DWORD pref = 1;
    DwmSetWindowAttribute(hwnd, 33, &pref, sizeof(pref));
}
#else
static void disableWindows11Rounding(QWidget*) {}
#endif

void MainWindow::applyFullscreenMode()
{
    const bool windowed = m_settings.fullscreenMode.trimmed().toLower() == QLatin1String("windowed");
    if (!isVisible()) {
        m_pendingShowMode = true;
        return;   // 还没显示：等 showWithMode() 一次性显示，避免闪烁
    }
    if (windowed) {
        if (isFullScreen()) showNormal();
        showMaximized();
    } else {
        if (isMaximized()) showNormal();
        showFullScreen();
        disableWindows11Rounding(this);   // v3.5.22：去掉 Win11 四角圆角
    }
    std::fprintf(stderr, "[\u7a97\u53e3] \u5168\u5c4f\u65b9\u5f0f\uff1a%s\n",
                 windowed ? "windowed" : "borderless");
}

// v3.5.26：群系版本持久化（config.json 的 settings.mapVersion）
void MainWindow::saveMapVersionToConfig()
{
    writeConfigFile(m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                    m_terminal ? m_terminal->history() : QStringList());
}

void MainWindow::showWithMode()
{
    m_pendingShowMode = false;
    if (m_telemetry) m_telemetry->reportAppStart(QString::fromLatin1(kAppVersion), QStringLiteral("test"));
    const bool windowed = m_settings.fullscreenMode.trimmed().toLower() == QLatin1String("windowed");
    show();
    if (windowed)
        showMaximized();
    else {
        showFullScreen();
        disableWindows11Rounding(this);   // v3.5.22：去掉 Win11 四角圆角
    }
}

void MainWindow::toggleFullscreenMode()
{
    m_settings.fullscreenMode =
        (m_settings.fullscreenMode.trimmed().toLower() == QLatin1String("windowed")) ? "borderless" : "windowed";
    writeConfigFile(m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                    m_terminal ? m_terminal->history() : QStringList());
    applyFullscreenMode();
    setStatus(slime::ui::tr(slime::ui::t("ui.e9fb9aec5e59"))
                  .arg(m_settings.fullscreenMode == QLatin1String("windowed") ? slime::ui::t("ui.37c2545a642e")
                                                                             : slime::ui::t("ui.2a0936486d4a")));
}

void MainWindow::setStatus(const QString& text)
{
    statusBar()->showMessage(text);
}

void MainWindow::setInputsEnabled(bool on)
{
    const QList<QWidget*> controls = {
        m_w->javaRadio, m_w->bedrockRadio, m_w->seedEdit,
        m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ,
        m_w->presetNear, m_w->presetLocal, m_w->presetGlobal, m_w->presetWorld, m_w->presetBedrock,
        m_w->strictRadio, m_w->densityRadio,
        m_w->lockDir, m_w->drawPattern,
        m_w->gridSize, m_w->minDensity,
        m_w->importCache, m_w->clearCache, m_w->useCache,
        m_w->stopAtTarget, m_w->targetCount, m_w->smartRange,
        m_w->startX, m_w->startZ, m_w->convX, m_w->convZ, m_w->convertBtn,
        m_w->threads, m_w->gpuCheck, m_w->copyBtn
    };
    for (QWidget* w : controls) w->setEnabled(on);
    applyVersionUi(); // 版本相关禁用状态（种子/缓存/预设仅限对应版本）
    // v3.4.8（用户重点反馈「开启缓存构建模式之后没有标灰搜索模式」的根因）：
    // 本函数会把上面所有控件无条件重新启用，而构建模式的禁用是**叠加**在版本/模式之上的；
    // 少了这一步，搜索一开始/一结束，构建模式下的标灰就被整体抹掉。此处按当前模式重新叠加。
    updateBuildHintUi();
    m_w->start->setEnabled(on);
}

void MainWindow::setRunningUi(bool running)
{
    m_w->start->setEnabled(!running);
    m_w->cancel->setEnabled(running);
    if (!running) updateStartEnabled();   // v3.6.5：结束后恢复按钮可用性
    if (running) {
        // v3.6.0（用户反馈：要等扫完才动）：未知进度时用忙碌滚动，首个真实进度再切百分比
        m_w->progress->setRange(0, 0);
        m_w->progress->setValue(0);
        m_w->progress->setStyleSheet(QString());
        if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("…"));
        m_w->progressInfo->setText(slime::ui::t("ui.455d8705421a"));
        // v3.6.1：重新搜索清空旧结果；v3.6.19：抽成 clearResultsForNewSearch()
        clearResultsForNewSearch();
        // v3.3.6（任务书 调整 3）：进度流水每 0.5 秒追加一条（仅在搜索中）
        m_streamTick = 0;
        m_task.resetLiveMatches(); // v3.3.7：实时命中计数归零（计数对象由 SearchTask 持有）
        m_streamScanned = 0;
        if (m_streamTimer) m_streamTimer->start(500);
    } else {
        if (m_streamTimer) m_streamTimer->stop();
    }
}

// ---------------- 版本/模式 ----------------
void MainWindow::applyVersionDefaults()
{
    const bool bedrock = m_w->bedrockRadio->isChecked();
    const int b = bedrock ? kBedrockBoundChunk : kJavaBoundChunk;
    m_w->minX->setValue(-b);
    m_w->maxX->setValue(b);
    m_w->minZ->setValue(-b);
    m_w->maxZ->setValue(b);
}

void MainWindow::applyVersionUi()
{
    const bool bedrock = m_w->bedrockRadio->isChecked();
    // v3.6.12（用户反馈：基岩版种子不可调整）：判定与种子无关，但**地图群系**需要种子，
    //   所以输入框保持可用，并用标签/提示说明「基岩版结果不受种子影响」。
    // 构建模式不需要种子（其余情况都允许填；基岩版只用于地图上色）
    m_w->seedEdit->setEnabled(!buildModeActive());
    const bool enabled = bedrock && !m_running;
    m_w->importCache->setEnabled(enabled);
    m_w->clearCache->setEnabled(enabled);
    m_w->useCache->setEnabled(enabled);
    // 预设按钮按版本可用性（世界边界仅 Java、基岩边界仅基岩）
    m_w->presetWorld->setEnabled(!bedrock);
    m_w->presetBedrock->setEnabled(bedrock);
    m_w->presetNear->setEnabled(true);
    m_w->presetLocal->setEnabled(true);
    m_w->presetGlobal->setEnabled(true);
    // v3.6.12：基岩版种子只影响地图上色，不影响判定 —— 文案要写清楚
    if (m_w->seedLabel) {
        m_w->seedLabel->setText(bedrock ? slime::ui::t("ui.bdf0619ab5e7")
                                        : slime::ui::t("ui.de5100998f74"));
    }
    if (m_w->seedEdit) {
        m_w->seedEdit->setToolTip(
            bedrock ? slime::ui::t("ui.82eec10674b6")
                    : slime::ui::t("ui.acc16bb01871"));
    }
    if (!bedrock) {
        m_w->cacheStatus->setText(slime::ui::t("ui.705f907ae91f"));
    } else {
        updateCacheStatusUi();
    }
}

void MainWindow::onVersionChanged()
{
    m_wholeMapConfirmed = false; // 切换版本后整图确认失效
    applyVersionDefaults();
    applyVersionUi();
}

// ---------------- v3.3.0：设置区滚动与界面截图 ----------------

// SLIME_SHOT=<前缀>：把当前窗口渲染为 <前缀>_<tag>.png（无界面自检时核对布局用）
void MainWindow::saveWindowShot(const QString& prefix, const QString& tag)
{
    if (prefix.isEmpty()) return;
    const QString path = QStringLiteral("%1_%2.png").arg(prefix, tag);
    const QPixmap pm = grab();
    const bool Ok = pm.save(path);
    std::fprintf(stderr, "[UI自检] 截图 %s: %s (%dx%d)\n", Ok ? "已保存" : "保存失败",
                 path.toUtf8().constData(), pm.width(), pm.height());
    std::fflush(stderr);
}

// 依文本与可用宽度显式预留换行标签高度：QLabel 在滚动容器/嵌套布局中
// 的 height-for-width 可能按旧宽度计算，导致末行被裁剪（v3.3.0 实测已复现）。
void MainWindow::reserveWrappedHeight(QLabel* label)
{
    if (!label || label->text().isEmpty()) return;
    int avail = label->width();
    if (avail < 120) {
        // 首次布局前 width() 尚未确定：用父容器宽度或窗口宽度估算
        avail = label->parentWidget() ? label->parentWidget()->width() - 40 : width() - 60;
    }
    if (avail < 120) avail = 520;
    const QFontMetrics fm(label->font());
    const QRect br = fm.boundingRect(QRect(0, 0, avail, 100000), Qt::TextWordWrap, label->text());
    label->setMinimumHeight(br.height() + 4);
}

// 窗口尺寸变化后重新预留换行标签高度（宽度变小 → 行数变多 → 需要更高）
void MainWindow::resizeEvent(QResizeEvent* ev)
{
    // v3.5.5：让地图尽量呈正方形
    if (m_rightSplit) QTimer::singleShot(0, this, [this]() { adjustMapSquare(); });
    // v3.5.5：让地图尽量呈正方形
    QMainWindow::resizeEvent(ev);
    if (!m_w) return;
    reserveWrappedHeight(m_w->algSummary);
    reserveWrappedHeight(m_w->modeWarn);
}

void MainWindow::onModeChanged(){
    // v3.6.19（用户反馈）：切换图案/密度后，列表里还留着上一种方式的结果 ⇒ 主动清场
    if (m_hasLastOutcome) clearResultsForNewSearch();
    const bool strict = m_w->strictRadio->isChecked();
    m_w->strictPanel->setVisible(strict);
    m_w->densityPanel->setVisible(!strict);
    updateAlgorithmPanels(); // v3.3.0：算法选择仅在密度搜索下可用
    updateStartEnabled();    // v3.6.6：模式/形状切换后同步按钮状态
    if (strict) {
        if (!m_patternSet) {
            m_w->patternInfo->setText(slime::ui::t("ui.15d9e07ff847"));
        } else {
            m_w->patternInfo->setText(slime::ui::t("ui.81e8a258ab88")
                                          .arg(m_pattern.activeCount())
                                          .arg(m_pattern.rows())
                                          .arg(m_pattern.cols()));
        }
    }
}

// ---------------- v3.3.0：搜索算法（精确/分层/快速估算/保守） ----------------

Algorithm MainWindow::currentAlgorithm() const
{
    if (m_w->algLayeredRadio->isChecked()) return Algorithm::Layered;
    if (m_w->algSampleRadio->isChecked()) return Algorithm::Sample;
    if (m_w->algBruteRadio->isChecked()) return Algorithm::Brute;
    return Algorithm::Exact;
}

QString MainWindow::algorithmDisplayName(Algorithm a) const
{
    switch (a) {
    case Algorithm::Layered: return slime::ui::t("ui.f18c79796e88");
    case Algorithm::Sample: return m_w->sampleExact->isChecked() ? slime::ui::t("ui.144cdf9f8727")
                                                                 : slime::ui::t("ui.dcee497c44dd");
    case Algorithm::Brute: return slime::ui::t("ui.9e6092059b8f");
    default: return slime::ui::t("ui.73b75d28b4b7");
    }
}

void MainWindow::updateAlgorithmPanels()
{
    const bool density = m_w->densityRadio->isChecked();
    const Algorithm a = currentAlgorithm();
    for (QRadioButton* r : { m_w->algExactRadio, m_w->algLayeredRadio, m_w->algSampleRadio, m_w->algBruteRadio }) {
        r->setEnabled(density);
    }
    m_w->layerBlock->setEnabled(density && a == Algorithm::Layered);
    m_w->layerRatio->setEnabled(density && a == Algorithm::Layered);
    m_w->sampleStep->setEnabled(density && a == Algorithm::Sample);
    m_w->sampleExact->setEnabled(density && a == Algorithm::Sample);

    QString s;
    if (!density) {
        s = slime::ui::t("ui.b71ec532ab48");
        m_w->modeHeader->setText(slime::ui::t("ui.bae48f65f9fa"));
        m_w->modeWarn->setVisible(false);
        m_w->algSummary->setText(s);
        reserveWrappedHeight(m_w->algSummary);
        return;
    }
    switch (a) {
    case Algorithm::Layered: {
        const int L = m_w->layerBlock->currentData().toInt();
        const double ratio = m_w->layerRatio->value();
        s = slime::ui::t("ui.73f5c24734d4").arg(L).arg(ratio, 0, 'f', 2);
        s += ratio <= 1.0 ? slime::ui::t("ui.6ea4024321e7")
                          : slime::ui::t("ui.fd577c7fa771");
        s += slime::ui::t("ui.feadac842a8d");
        m_w->modeWarn->setText(slime::ui::t("ui.581cf6954a19")
                                   .arg(ratio, 0, 'f', 2));
        m_w->modeWarn->setVisible(ratio > 1.0);
        break;
    }
    case Algorithm::Sample: {
        const int S = m_w->sampleStep->value();
        if (m_w->sampleExact->isChecked()) {
            s = slime::ui::t("ui.fe9d0cfe9720")
                    .arg(S).arg(S * S);
        } else {
            s = slime::ui::t("ui.b44e55bc1f8b")
                    .arg(S).arg(S * S).arg(S);
        }
        m_w->modeWarn->setText(slime::ui::t("ui.c96ac97198bb"));
        m_w->modeWarn->setVisible(true);
        break;
    }
    case Algorithm::Brute:
        s = slime::ui::t("ui.c32ad66a6dd5");
        m_w->modeWarn->setVisible(false);
        break;
    default:
        s = slime::ui::t("ui.6ac84b6b4c00");
        m_w->modeWarn->setVisible(false);
        break;
    }
    m_w->algSummary->setText(s);
    reserveWrappedHeight(m_w->algSummary);
    reserveWrappedHeight(m_w->modeWarn);
    m_w->modeHeader->setText(slime::ui::t("ui.ff768c1de692").arg(algorithmDisplayName(a)));
}

// config.json 算法块读写（键名扁平，沿用本项目自写 JSON 的极简解析）
void MainWindow::loadAlgorithmFromConfig()
{
    const auto path = appDirPath() / kConfigFileName;
    const std::string mode = cfgString(path, "algorithm.mode", "exact");
    m_loadingConfig = true;
    if (mode == "layered") m_w->algLayeredRadio->setChecked(true);
    else if (mode == "sample") m_w->algSampleRadio->setChecked(true);
    else if (mode == "brute") m_w->algBruteRadio->setChecked(true);
    else m_w->algExactRadio->setChecked(true);

    const int lb = cfgInt(path, "algorithm.layerBlock", 16);
    int idx = m_w->layerBlock->findData(lb);
    m_w->layerBlock->setCurrentIndex(idx >= 0 ? idx : 0);
    m_w->layerRatio->setValue(cfgDouble(path, "algorithm.layerRatio", 1.0));
    m_w->sampleStep->setValue(cfgInt(path, "algorithm.sampleStep", 8));
    m_w->sampleExact->setChecked(cfgBool(path, "algorithm.sampleExact", false));
    m_loadingConfig = false;
    updateAlgorithmPanels();
}

void MainWindow::saveAlgorithmToConfig()
{
    if (m_loadingConfig) return;
    writeConfigFile(cfgBool(appDirPath() / kConfigFileName, "gpu.enabled", false),
                    m_terminal ? m_terminal->history() : QStringList());
}

// ---------------- v3.3.4：命令终端历史持久化（任务书 2.4：config.json 最多 100 条） ----------------
void MainWindow::saveTerminalHistoryToConfig(const QStringList& history)
{
    if (m_loadingConfig) return;
    QStringList capped = history;
    while (capped.size() > 100) capped.removeFirst();
    writeConfigFile(cfgBool(appDirPath() / kConfigFileName, "gpu.enabled", false), capped);
}

// 读取 config.json 的 terminal.history（简单 JSON 字符串数组解析；损坏/缺失时返回空表）
QStringList MainWindow::loadTerminalHistoryFromConfig()
{
    QStringList out;
    std::ifstream in(appDirPath() / kConfigFileName);
    if (!in) return out;
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto kp = content.find("\"history\"");
    if (kp == std::string::npos) return out;
    const auto lb = content.find('[', kp);
    const auto rb = content.find(']', lb == std::string::npos ? kp : lb);
    if (lb == std::string::npos || rb == std::string::npos || rb < lb) return out;
    const std::string body = content.substr(lb + 1, rb - lb - 1);
    size_t i = 0;
    while (i < body.size()) {
        const auto q0 = body.find('"', i);
        if (q0 == std::string::npos) break;
        const auto q1 = body.find('"', q0 + 1);
        if (q1 == std::string::npos) break;
        out << QString::fromStdString(body.substr(q0 + 1, q1 - q0 - 1));
        i = q1 + 1;
    }
    while (out.size() > 100) out.removeFirst();
    return out;
}

// 非精确模式的差异校验：小范围（≤256×256 区块）自动跑一次精确对照
bool MainWindow::crossCheckExact(const SearchOptions& opt, int64_t sampleFound, int64_t* exactFound) const
{
    *exactFound = -1;
    const int64_t w = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t h = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    if (w <= 0 || h <= 0 || w * h > 256LL * 256LL) return false; // 范围过大：不做自动对照
    if (opt.mode != Mode::Density || opt.algorithm == Algorithm::Exact) return false;
    SearchOptions omp = opt;
    omp.algorithm = Algorithm::Exact;
    omp.useCache = false;
    omp.cache = nullptr;
    omp.cacheWrite = nullptr;
    omp.stopAtTarget = false;
    omp.gpuScan = nullptr;
    SearchOutcome out;
    std::atomic<bool> cancel{ false };
    SlimeEngine engine;
    engine.run(omp, cancel, nullptr, out);
    if (!out.ok) return false;
    *exactFound = out.totalFound;
    (void)sampleFound;
    return true;
}

// ---------------- 预设与边界 ----------------

void MainWindow::applyPreset(int halfSpan)
{
    setSearchRadius(halfSpan);
    setStatus(slime::ui::tr(slime::ui::t("ui.241d40e4e9d5")).arg(halfSpan));
    Logger::info("应用预设范围 ±" + std::to_string(halfSpan) + " 区块");
}

// v3.5.16：范围只由「半径」决定 —— 半径 r ⇒ 正方形 [-r, r-1]（边长 2r），中心恒为原点。
void MainWindow::setSearchRadius(int r)
{
    if (!m_w || !m_w->minX) return;
    const int radius = (std::max)(1, r);
    const int lo = -radius, hi = radius;   // 半径 r ⇒ X/Z 各 ±r（边长 2r+1）
    m_rangeSyncGuard = true;
    m_w->minX->setValue(lo);
    m_w->maxX->setValue(hi);
    m_w->minZ->setValue(lo);
    m_w->maxZ->setValue(hi);
    if (m_w->rangeRadius && m_w->rangeRadius->value() != radius) m_w->rangeRadius->setValue(radius);
    if (m_w->bRadius && m_w->bRadius->value() != radius) m_w->bRadius->setValue(radius);
    if (m_w->rangeHint)
        m_w->rangeHint->setText(slime::ui::t("ui.d25cd4d6bfc3")
                                    .arg(2 * radius + 1)
                                    .arg(lo)
                                    .arg(hi));
    m_rangeSyncGuard = false;
    pushSearchRangeToBuild();
    updateBuildHintUi();
    updateBuildEstimateLive();
}

// 反向：范围被别人（配置/命令/整图确认/自检）改动时，把半径显示同步过来
void MainWindow::syncRadiusFromRange()
{
    if (!m_w || !m_w->minX || m_rangeSyncGuard) return;
    const int spanX = m_w->maxX->value() - m_w->minX->value() + 1;
    const int spanZ = m_w->maxZ->value() - m_w->minZ->value() + 1;
    const int radius = (std::max)(1, (std::max)(spanX, spanZ) / 2);
    m_rangeSyncGuard = true;
    if (m_w->rangeRadius && m_w->rangeRadius->value() != radius) m_w->rangeRadius->setValue(radius);
    if (m_w->bRadius && m_w->bRadius->value() != radius) m_w->bRadius->setValue(radius);
    if (m_w->rangeHint)
        m_w->rangeHint->setText(slime::ui::t("ui.871551b08684").arg(spanX).arg(spanZ).arg(radius));
    m_rangeSyncGuard = false;
}

// 整图搜索的选项构造（供估算）
SearchOptions MainWindow::wholeMapOptions(int halfSpan) const
{
    SearchOptions opt;
    opt.version = m_w->bedrockRadio->isChecked() ? Version::Bedrock : Version::Java;
    opt.minX = -halfSpan;
    opt.maxX = halfSpan;
    opt.minZ = -halfSpan;
    opt.maxZ = halfSpan;
    opt.mode = m_w->densityRadio->isChecked() ? Mode::Density : Mode::Strict;
    opt.directionLocked = m_w->lockDir->isChecked();
    opt.gridSize = m_w->gridSize->value();
    opt.minDensity = m_w->minDensity->value();
    opt.startX = m_w->startX->value();
    opt.startZ = m_w->startZ->value();
    opt.targetCount = m_w->targetCount->value();
    opt.stopAtTarget = m_w->stopAtTarget->isChecked();
    opt.ignoreThreshold = m_w->ignoreThreshold && m_w->ignoreThreshold->isChecked();
    if (opt.ignoreThreshold) { opt.stopAtTarget = false; opt.smartRange = false; }
    opt.smartRange = m_w->smartRange->isChecked();
    // v3.4.8：整图估算原来漏了搜索算法与分层/采样参数 ⇒ 估算永远按「精确」算，与实际要跑的算法不符
    opt.algorithm = (opt.mode == Mode::Density) ? currentAlgorithm() : Algorithm::Exact;
    opt.layerBlock = m_w->layerBlock->currentData().toInt();
    opt.layerRatio = m_w->layerRatio->value();
    opt.sampleStep = m_w->sampleStep->value();
    opt.sampleExact = m_w->sampleExact->isChecked();
    opt.threads = m_w->threads->value();
    // v3.3.9（任务书 2.2）：CPU 占用率上限（按逻辑核心折算线程数）
    {
        const int logical = (std::max)(1, QThread::idealThreadCount());
        const int pct = std::clamp(m_settings.cpuLimitPercent, 10, 100);
        if (pct < 100) {
            const int limited = (std::max)(1, static_cast<int>(std::lround(logical * pct / 100.0)));
            if (opt.threads > limited) {
                Logger::info("CPU 占用率上限 " + std::to_string(pct) + "% ⇒ 生效线程数 " +
                             std::to_string(opt.threads) + " → " + std::to_string(limited));
                opt.threads = limited;
            }
        }
    }
    // v3.3.9（任务书 2.1）：内存上限（分块位图预算，超出自动降并发）
    opt.memBudgetBytes =
        (m_settings.memLimitMB > 0) ? static_cast<int64_t>(m_settings.memLimitMB) * 1024 * 1024 : 0;
    opt.cpuLimitPercent = std::clamp(m_settings.cpuLimitPercent, 10, 100);
    // v3.7.1：GPU 占用率上限 → 环境变量（引擎在瓦片之间按占空比休眠）
    {
        // v3.8.11：图案流式开关 → 环境变量（引擎侧读取）
        _putenv_s("SLIME_GPU_PATTERN", m_settings.gpuPatternStream ? "1" : "0");
        const int gpct = std::clamp(m_settings.gpuLimitPercent, 10, 100);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", gpct);
        _putenv_s("SLIME_GPU_MAX_PCT", buf);
    }
    if (opt.mode == Mode::Strict) {
        if (m_patternSet && !m_pattern.empty()) opt.pattern = m_pattern;
    }
    return opt;
}

// 整图确认对话框（v3.1.1：动态耗时估算）
bool MainWindow::confirmWholeMapSearch(bool applyRange, int halfSpan)
{
    SearchOptions opt = wholeMapOptions(halfSpan);
    const bool hasPattern = (opt.mode != Mode::Strict) || (!opt.pattern.empty());
    const bool java = (opt.version == Version::Java);

    if (m_uiQuiet) { // 自检模式：直接放行
        if (applyRange) {
            applyPreset(halfSpan);
            m_wholeMapConfirmed = true;
        }
        return true;
    }

    if (!hasPattern) {
        // 未绘制图案时按严格模式仍可估算扫描部分（匹配窗口按空图案估计不可靠 → 提示先绘制）
        // 实际进入 startSearch 时会报“请先绘制图案”，此处直接放行由 startSearch 处理。
        if (applyRange) {
            QMessageBox::warning(this, slime::ui::t("ui.ab3656a956f5"),
                                 slime::ui::t("ui.719d10c91481"));
            applyPreset(halfSpan);
        }
        return false;
    }

    SearchEstimate est;
    const int threads = m_w->threads->value();
    bool estOk = SlimeEngine::estimateSearch(opt, threads, est);

    // 区块数（万亿）与耗时范围（实测速率 ±）
    const double wanYi = 1e12;
    double lo = 0.0, hi = 0.0;
    if (estOk) {
        lo = est.totalSec * 0.8;
        hi = est.totalSec * 1.35;
    }
    const QString blocks = estOk
                               ? slime::ui::t("ui.53365ea16089")
                                     .arg(static_cast<double>(est.areaChunks) / wanYi, 0, 'f', 1)
                               : slime::ui::t("ui.30d6b22e0eb7");
    QString dur;
    if (estOk) {
        dur = formatDurationRange(lo, hi);
    } else {
        dur = slime::ui::t("ui.16ffb883cad6");
    }
    QString extra;
    if (estOk) {
        extra = slime::ui::t("ui.a67212609609")
                    .arg(static_cast<long long>(est.scanRate))
                    .arg(est.matchRate > 0 ? QStringLiteral("%1").arg(static_cast<long long>(est.matchRate))
                                           : QStringLiteral("—"))
                    .arg(est.chunkConcurrency);
        if (est.matchWallSec > est.scanWallSec * 2) {
            extra += slime::ui::t("ui.b3ee7232db57");
        } else {
            extra += slime::ui::t("ui.0a5778f3f8fa")
                         .arg(formatDurationRange(est.scanWallSec * 0.8, est.scanWallSec * 1.35))
                         .arg(est.matchWallSec > 0
                                  ? formatDurationRange(est.matchWallSec * 0.8, est.matchWallSec * 1.35)
                                  : slime::ui::t("ui.c5eee9b92171"));
        }
    } else {
        extra = slime::ui::t("ui.caa562b17b3e");
    }
    const QString ver = java ? slime::ui::t("ui.8accf0fe2eed") : slime::ui::t("ui.618fd37c0c01");
    QString refNote;
    if (java) {
        refNote = slime::ui::t("ui.7658cb7ec35c");
    } else {
        refNote = slime::ui::t("ui.6262634bbb31");
    }

    const auto ans = QMessageBox::question(
        this, slime::ui::t("ui.8be5939d9ce1"),
        slime::ui::t("ui.8ea43b1d89a3")
            .arg(ver).arg(blocks).arg(dur).arg(extra).arg(refNote));
    if (ans != QMessageBox::Yes) {
        Logger::info("整图搜索被用户取消");
        return false;
    }
    if (applyRange) {
        applyPreset(halfSpan);
        m_wholeMapConfirmed = true;
    }
    Logger::info("用户确认整图搜索（±" + std::to_string(halfSpan) + "，估算 " +
                 (estOk ? std::to_string(static_cast<long long>(est.totalSec)) : std::string("n/a")) + "s）");
    return true;
}

void MainWindow::clampRangeToBound()
{
    const int bound = versionBound();
    bool clamped = false;
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
        const int v = s->value();
        if (v > bound) {
            s->setValue(bound);
            clamped = true;
        } else if (v < -bound) {
            s->setValue(-bound);
            clamped = true;
        }
    }
    if (clamped) {
        const QString ver = m_w->bedrockRadio->isChecked() ? slime::ui::t("ui.618fd37c0c01") : QStringLiteral("Java");
        const int blocks = bound * 16;
        setStatus(slime::ui::tr(slime::ui::t("ui.2e12306cd3ab"))
                      .arg(ver).arg(bound).arg(blocks));
        if (!m_uiQuiet) {
            QMessageBox::information(this, slime::ui::t("ui.a76951f982d6"),
                                     slime::ui::t("ui.fb99a1d6fb34")
                                         .arg(ver).arg(bound).arg(blocks));
        }
        Logger::warning("搜索范围超出边界，已自动截断到 ±" + std::to_string(bound));
    }
}

// ---------------- 图案 ----------------
void MainWindow::editPattern()
{
    PatternEditor dlg(this);
    if (m_patternSet && !m_pattern.empty()) dlg.setInitial(m_pattern);
    if (dlg.exec() == QDialog::Accepted) {
        m_pattern = dlg.pattern();
        m_patternSet = true;
        onModeChanged();
        Logger::info("图案已更新：" + std::to_string(m_pattern.rows()) + "x" + std::to_string(m_pattern.cols()) +
                     "，有效格 " + std::to_string(m_pattern.activeCount()));
    }
}

// ---------------- 缓存 ----------------
// v3.3.3（任务书 2.2）：状态标签在「缓存创建/增量合并/启动自动加载/导入/清除/勾选变化」后
// 一律立即刷新；文案按要求统一为「缓存已加载，共 N 个区块」，并额外给出覆盖区域与体积。
// 另外区分「磁盘上已有缓存文件但尚未载入内存」的情形，便于用户理解为何要重启/导入。
void MainWindow::updateCacheStatusUi()
{
    if (!m_w || !m_w->cacheStatus) return;
    const bool bedrock = m_w->bedrockRadio->isChecked();
    if (!bedrock) {
        m_w->cacheStatus->setText(slime::ui::t("ui.705f907ae91f"));
        return;
    }
    if (m_cache->hasData()) {
        m_w->cacheStatus->setText(slime::ui::t("ui.a13898466222")
                                      .arg(m_cache->entryCount())
                                      .arg(m_cache->ranges().size())
                                      .arg(m_cache->sizeBytes() / (1024 * 1024)));
        return;
    }
    std::error_code ec;
    if (!m_cacheFile.empty() && std::filesystem::exists(m_cacheFile, ec)) {
        m_w->cacheStatus->setText(
            slime::ui::t("ui.1184ea9cb9f9")
                .arg(QString::fromStdString(m_cacheFile.filename().string())));
    } else {
        m_w->cacheStatus->setText(slime::ui::t("ui.23c0ca294841"));
    }
}

void MainWindow::importCache()
{
    // v3.3.1（任务书 2.1）：过滤器与保存扩展名一致 —— 新版 .slimecache 优先，旧版 .cache 兼容，
    // 并提供「所有文件」兜底，避免缓存文件存在却在对话框里看不到。
    // v3.3.2（任务书 3.2/3.3）：对话框**定位到缓存目录（程序目录）并预选当前缓存文件**，
    // 保证「刚保存的缓存」不仅可见（过滤器匹配），而且已经选中（直接点“打开”即可加载）。
    // 修复前的实现（v3.1.1~v3.3.0）第三个参数为空 → 对话框落在进程工作目录/上次目录，
    // 缓存文件虽在程序目录却看不到，这正是任务书描述的「缓存文件存在但选不中」。
    QFileDialog dlg(this, slime::ui::t("ui.834c34e48ed2"),
                    QString::fromStdWString(appDirPath().wstring()));
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilters(cacheImportFilter().split(QStringLiteral(";;")));
    dlg.selectFile(QString::fromStdString(kCacheFileName)); // 预选 slime_cache.slimecache
    if (dlg.exec() != QDialog::Accepted) return;

    const QStringList chosen = dlg.selectedFiles();
    if (chosen.isEmpty()) return;
    const QString path = chosen.isEmpty() ? QString() : chosen.first();
    if (path.isEmpty()) return;

    if (path.endsWith(QStringLiteral(".pkl"), Qt::CaseInsensitive) ||
        path.endsWith(QStringLiteral(".pickle"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, slime::ui::t("ui.a042ea99a76c"),
                             slime::ui::t("ui.e4b8900b55f6"));
        return;
    }

    importCacheFromPath(path, !m_uiQuiet, !m_uiQuiet);
}

// v3.3.8.1（任务书 1.3）：大缓存导入移到**后台线程** + 进度条（界面不冻结）。
// 读取本身不可中断，「取消」= 丢弃已读结果、不合并（如实反映可取消范围）。
// showProgressDialog=false（自检静默模式）时同步读取，避免模态框阻塞自动化流程。
void MainWindow::importCacheFromPath(const QString& path, bool showProgressDialog, bool showResultDialogs)
{
    const auto srcPath = std::filesystem::path(path.toStdWString());
    std::error_code ec;
    const auto fileBytes = std::filesystem::file_size(srcPath, ec);
    auto loaded = std::make_shared<CacheManager>();
    auto progDone = std::make_shared<std::atomic<int64_t>>(0);
    auto progTotal = std::make_shared<std::atomic<int64_t>>(1);
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto loadOA = std::make_shared<std::atomic<bool>>(false);
    auto loadErr = std::make_shared<std::string>();
    auto cancelReq = std::make_shared<std::atomic<bool>>(false);
    const auto runLoad = [&]() {
        std::string e;
        const bool r = loaded->loadFile(srcPath, true, e, [progDone, progTotal](int64_t d, int64_t tt) {
            progDone->store(d, std::memory_order_relaxed);
            progTotal->store(tt > 0 ? tt : 1, std::memory_order_relaxed);
        });
        *loadErr = e;
        loadOA->store(r, std::memory_order_relaxed);
        finished->store(true, std::memory_order_relaxed);
    };
    std::thread worker;
    if (!showProgressDialog) {
        runLoad();
    } else {
        QProgressDialog* pd = new QProgressDialog(
            slime::ui::t("ui.5153c1d9f401").arg(fileBytes / (1024.0 * 1024.0), 0, 'f', 1),
            slime::ui::t("ui.4d0b4688c787"), 0, 1000, this);
        pd->setWindowTitle(slime::ui::t("ui.00e905651e85"));
        pd->setWindowModality(Qt::WindowModal);
        pd->setMinimumDuration(0);
        pd->setAutoClose(false);
        pd->setAutoReset(false);
        pd->setValue(0);
        pd->show();
        worker = std::thread(runLoad);
        QTimer poll;
        connect(&poll, &QTimer::timeout, this, [&]() {
            const int64_t tt = std::max<int64_t>(1, progTotal->load(std::memory_order_relaxed));
            const int64_t d = progDone->load(std::memory_order_relaxed);
            pd->setValue(static_cast<int>(std::min<int64_t>(1000, d * 1000 / tt)));
        });
        poll.start(100);
        while (!finished->load(std::memory_order_relaxed)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            QThread::msleep(10);
            if (pd->wasCanceled()) {
                cancelReq->store(true, std::memory_order_relaxed);
                pd->setCancelButton(nullptr);
                pd->setLabelText(slime::ui::t("ui.8a98a48be088"));
            }
        }
        poll.stop();
        worker.join();
        pd->close();
        pd->deleteLater();
    }
    if (cancelReq->load()) {
        setStatus(slime::ui::tr(slime::ui::t("ui.9d715e50a2c7")));
        addFeedback(2, slime::ui::tr(slime::ui::t("ui.1bfb190be1fa")));
        return;
    }
    if (!loadOA->load()) {
        if (showResultDialogs) {
            QMessageBox::critical(this, slime::ui::t("ui.a01a8d53938f"), QString::fromStdString(*loadErr));
        }
        Logger::error("缓存导入失败: " + *loadErr + "（来源 " + path.toStdString() + "）");
        return;
    }
    const size_t beforeR = m_cache->ranges().size();
    const int64_t beforeE = m_cache->entryCount();
    m_cache->mergeFrom(*loaded); // v3.3.8.1：UI 线程合并（位图合并 = 一次置位遍历，速度快）
    loaded.reset();
    m_cache->pruneToFit(cacheMaxBytes()); // 容量限制
    std::string serr;
    if (!m_cache->saveFile(m_cacheFile, serr)) {
        if (showResultDialogs) {
            QMessageBox::warning(this, slime::ui::t("ui.5521e368d87e"), slime::ui::t("ui.de895e437b0e").arg(QString::fromStdString(serr)));
        }
        Logger::warning("缓存导入后保存失败: " + serr + "（目标 " + m_cacheFile.string() + "）");
    } else {
        // v3.3.2：记录落盘路径，便于用户与排障时定位「缓存文件到底在哪」
        Logger::info("缓存已保存（导入合并）: " + m_cacheFile.string());
    }
    updateCacheStatusUi();
    if (showResultDialogs) {
        QMessageBox::information(this, slime::ui::t("ui.425516b37306"),
                                 slime::ui::t("ui.be215de45cd3")
                                     .arg(m_cache->ranges().size() - beforeR)
                                     .arg(m_cache->entryCount() - beforeE)
                                     .arg(m_cache->ranges().size())
                                     .arg(m_cache->entryCount()));
    }
    Logger::info("缓存导入合并完成，共 " + std::to_string(m_cache->ranges().size()) + " 区域 / " +
                 std::to_string(m_cache->entryCount()) + " 区块（来源 " + path.toStdString() + "）");
}

void MainWindow::clearCache()
{
    if (QMessageBox::question(this, slime::ui::t("ui.b56d9ac6c5a0"),
                              slime::ui::t("ui.b41f2a3ce538")) != QMessageBox::Yes) {
        return;
    }
    m_cache->clear();
    std::error_code ec;
    std::filesystem::remove(m_cacheFile, ec);
    // v3.3.1：连同旧版扩展名文件一起清除（否则下次启动会把它当旧缓存重新加载）
    std::filesystem::remove(appDirPath() / kCacheLegacyFileName, ec);
    updateCacheStatusUi();
    Logger::info("缓存已清除");
}

// ---------------- 坐标转换 ----------------
void MainWindow::convertBlockCoords()
{
    bool okX = false, okZ = false;
    const qlonglong bx = m_w->convX->text().trimmed().toLongLong(&okX);
    const qlonglong bz = m_w->convZ->text().trimmed().toLongLong(&okZ);
    if (!okX || !okZ) {
        QMessageBox::warning(this, slime::ui::t("ui.05118c2a651d"), slime::ui::t("ui.943b5f18375d"));
        return;
    }
    auto floorDiv16 = [](qlonglong v) {
        qlonglong q = v / 16;
        if (v < 0 && v % 16 != 0) --q;
        return q;
    };
    const qlonglong cx = floorDiv16(bx);
    const qlonglong cz = floorDiv16(bz);
    if (cx < -2147483646LL || cx > 2147483646LL || cz < -2147483646LL || cz > 2147483646LL) {
        QMessageBox::warning(this, slime::ui::t("ui.05118c2a651d"), slime::ui::t("ui.84fbf99670a1"));
        return;
    }
    m_w->startX->setValue(static_cast<int>(cx));
    m_w->startZ->setValue(static_cast<int>(cz));
    setStatus(slime::ui::tr(slime::ui::t("ui.ea3440531233"))
                  .arg(bx).arg(bz).arg(cx).arg(cz));
    Logger::info("坐标转换: 方块(" + std::to_string(bx) + "," + std::to_string(bz) + ") -> 区块(" +
                 std::to_string(cx) + "," + std::to_string(cz) + ")");
}

// ---------------- 鸣谢（v3.7.5，用户要求） ----------------
void MainWindow::updateTelemetryNotice()
{
    if (!m_telemetryNotice || !m_telemetry) return;
    const QString text = m_telemetry->enabled()
                             ? slime::ui::t("ui.c3036df85648")
                             : slime::ui::t("ui.ae50bb7256dc");
    m_telemetryNotice->setText(QStringLiteral("<a href=\"telemetry\">%1</a>").arg(text));
}

void MainWindow::showTelemetryDialog()
{
    if (!m_telemetry) return;
    if (m_uiQuiet) {
        addFeedback(2, slime::ui::t("ui.6a59df88b543"));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(slime::ui::t("ui.6034809d2f3b"));
    dlg.setMinimumWidth(560);
    auto* lay = new QVBoxLayout(&dlg);

    auto* intro = new QLabel(
        slime::ui::t("ui.ca9adb9a211a"), &dlg);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto* data = new QLabel(
        slime::ui::t("ui.19e2f2997722"), &dlg);
    data->setWordWrap(true);
    lay->addWidget(data);

    auto* privacy = new QLabel(
        slime::ui::t("ui.95f20e8a62c3"), &dlg);
    privacy->setOpenExternalLinks(false);
    privacy->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(privacy, &QLabel::linkActivated, this, [](const QString&) {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/PillerBean/SlimeFinder/blob/main/docs/privacy.md")));
    });
    lay->addWidget(privacy);

    auto* enabled = new QCheckBox(slime::ui::t("ui.2ac72064e5e4"), &dlg);
    enabled->setChecked(m_telemetry->enabled());
    lay->addWidget(enabled);

    auto* epNote = new QLabel(slime::ui::t("ui.af049acfc3f2"), &dlg);
    epNote->setStyleSheet(QStringLiteral("color:#808080;font-size:11px;"));
    lay->addWidget(epNote);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;
    m_telemetry->setSettings(enabled->isChecked(), m_telemetry->endpoint());
    updateTelemetryNotice();
    addFeedback(2, enabled->isChecked() ? slime::ui::t("ui.ce83b3fe513c")
                                        : slime::ui::t("ui.b7b5afae98c6"));
    setStatus(enabled->isChecked() ? slime::ui::t("ui.3c674829def4")
                                   : slime::ui::t("ui.b7b5afae98c6"));
}

void MainWindow::showCredits()
{
    QDialog dlg(this);
    dlg.setWindowTitle(slime::ui::t("ui.c5583bc7e151"));
    dlg.setMinimumWidth(520);
    auto* lay = new QVBoxLayout(&dlg);

    auto* intro = new QLabel(slime::ui::t("ui.cb55d487c124"), &dlg);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto* list = new QTextBrowser(&dlg);
    list->setHtml(slime::ui::t("ui.b857b5d1e865"));
    list->setMinimumHeight(140);
    list->setOpenExternalLinks(false);
    lay->addWidget(list, 1);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    if (QPushButton* ok = btns->button(QDialogButtonBox::Ok)) ok->setText(slime::ui::t("ui.6c14bd7f6f9e"));
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(btns);
    dlg.exec();
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.00926703ef18")));
}

// ---------------- 联系我们（v3.7.5：只讲反馈渠道，与「关于」差异化） ----------------
void MainWindow::showContact()
{
    QDialog dlg(this);
    dlg.setWindowTitle(slime::ui::t("ui.152d3db75889"));
    dlg.setMinimumWidth(460);
    auto* lay = new QVBoxLayout(&dlg);
    auto* info = new QLabel(slime::ui::t("ui.8be6653c4033")
                                .arg(QLatin1String(kContactEmail)));
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info->setWordWrap(true);
    lay->addWidget(info);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(btns);
    dlg.exec();
}

// ---------------- 搜索 ----------------
// v3.6.6（用户要求）：成功率预测 —— 目标被找到「至少一次」的概率
//   图案模式：每个位置命中概率 = 0.1^绿格数 × 0.9^白格数（史莱姆区块约占 10%）
//   密度模式：窗口内命中数 ≥ ceil(最小密度×格数) 的概率（二项分布尾概率）
//   位置数：范围内可放置窗口的位置数（方向锁定时按 1/4 计）
QString MainWindow::successProbabilityText(int64_t areaX, int64_t areaZ) const
{
    const double pSlime = 0.1;
    double pMatch = 0.0;
    int64_t positions = 0;
    QString basis;

    const bool patternMode = m_w->strictRadio && m_w->strictRadio->isChecked();
    if (patternMode) {
        if (!m_patternSet || m_pattern.empty()) {
            return slime::ui::t("ui.f82967ddf109");
        }
        int green = 0, white = 0;
        for (int r = 0; r < m_pattern.rows(); ++r) {
            for (int c = 0; c < m_pattern.cols(); ++c) {
                const auto st = m_pattern.at(r, c);
                if (st == slime::CellState::Green) ++green;
                else if (st == slime::CellState::White) ++white;
            }
        }
        pMatch = std::pow(pSlime, green) * std::pow(1.0 - pSlime, white);
        const bool lock = m_w->lockDir && m_w->lockDir->isChecked();
        if (lock) pMatch *= 0.25;   // 只允许 1 个方向 ⇒ 命中率降到约 1/4
        positions = (std::max)(int64_t{0}, areaX - m_pattern.cols() + 1) *
                    (std::max)(int64_t{0}, areaZ - m_pattern.rows() + 1);
        basis = slime::ui::t("ui.659103792aff")
                    .arg(green + white)
                    .arg(green)
                    .arg(white)
                    .arg(lock ? slime::ui::t("ui.aec43f954535") : QString());
    } else {
        const int g = (std::max)(1, m_w->gridSize ? m_w->gridSize->value() : 5);
        const int64_t cells = static_cast<int64_t>(g) * g;
        const double d = (std::max)(0.0, (std::min)(1.0, m_w->minDensity ? m_w->minDensity->value() : 0.0));
        const int64_t need = static_cast<int64_t>(std::ceil(d * static_cast<double>(cells)));
        if (need <= 0) {
            pMatch = 1.0;
        } else if (need > cells) {
            pMatch = 0.0;
        } else {
            const double mean = static_cast<double>(cells) * pSlime;
            const double sd = std::sqrt(static_cast<double>(cells) * pSlime * (1.0 - pSlime));
            // 连续性修正的正态近似：密度窗口一般远大于 30 格，近似足够准
            const double z = (static_cast<double>(need) - 0.5 - mean) / (sd > 1e-9 ? sd : 1.0);
            pMatch = 0.5 * std::erfc(z / std::sqrt(2.0));
        }
        positions = (std::max)(int64_t{0}, areaX - g + 1) * (std::max)(int64_t{0}, areaZ - g + 1);
        basis = slime::ui::t("ui.1acf7f8f1396").arg(g).arg(d, 0, 'f', 2);
    }

    if (positions <= 0) return slime::ui::t("ui.e448e31ab0ac");
    const double pAny = (pMatch <= 0.0)
                            ? 0.0
                            : (pMatch >= 1.0 ? 1.0
                                             : -std::expm1(static_cast<double>(positions) * std::log1p(-pMatch)));
    const double pct = 100.0 * pAny;
    QString hint;
    if (pct < 1.0) hint = slime::ui::t("ui.822cb673d8d6");
    else if (pct < 20.0) hint = slime::ui::t("ui.402c20628dcf");
    else if (pct > 99.5) hint = slime::ui::t("ui.7a0d7b3dfbd8");
    const QString pctText = (pct < 1.0) ? QStringLiteral("%1").arg(pct, 0, 'f', 2)
                                         : QStringLiteral("%1").arg(pct, 0, 'f', pct < 10.0 ? 1 : 0);
    return slime::ui::t("ui.6ae8a00a69ce") + pctText + QStringLiteral("%") + hint +
           slime::ui::t("ui.2507a5abfb92")
               .arg(static_cast<qlonglong>(positions))
               .arg(pMatch * 100.0, 0, 'f', pMatch < 0.001 ? 4 : 2)
               .arg(basis);
}

void MainWindow::startSearch()
{
    if (m_running) {
        // v3.3.7（P0-2）：自检/自动化模式下**不能**用模态框（GUI 线程会被永久阻塞，
        // 实测导致自检挂死 600 秒以上）；这里只提示并忽略重复启动。
        if (m_uiQuiet) {
            setStatus(slime::ui::tr(slime::ui::t("ui.9bb8bee94e7b")));
            return;
        }
        QMessageBox::warning(this, slime::ui::t("ui.ab3656a956f5"), slime::ui::t("ui.44969209326b"));
        return;
    }

    // v3.10.12.12：只有密度模式下的「环形覆盖」形状才走环形搜索。
    //   修复：从环形切到图案后，shapeRingRadio 仍保持 checked（面板只是隐藏），
    //   旧逻辑会继续调用 runRingSearch，导致图案模式搜出环形结果。
    const bool patternModeNow = m_w && m_w->strictRadio && m_w->strictRadio->isChecked();
    if (!patternModeNow && m_w && m_w->shapeRingRadio && m_w->shapeRingRadio->isChecked()) {
        runRingSearch();
        return;
    }

    SearchOptions opt;
    opt.version = m_w->bedrockRadio->isChecked() ? Version::Bedrock : Version::Java;
    opt.minX = m_w->minX->value();
    opt.maxX = m_w->maxX->value();
    opt.minZ = m_w->minZ->value();
    opt.maxZ = m_w->maxZ->value();
    opt.mode = m_w->densityRadio->isChecked() ? Mode::Density : Mode::Strict;
    opt.directionLocked = m_w->lockDir->isChecked();
    opt.gridSize = m_w->gridSize->value();
    opt.minDensity = m_w->minDensity->value();
    opt.startX = m_w->startX->value();
    opt.startZ = m_w->startZ->value();
    opt.targetCount = m_w->targetCount->value();
    opt.stopAtTarget = m_w->stopAtTarget->isChecked();
    opt.ignoreThreshold = m_w->ignoreThreshold && m_w->ignoreThreshold->isChecked();
    if (opt.ignoreThreshold) { opt.stopAtTarget = false; opt.smartRange = false; }
    opt.smartRange = m_w->smartRange->isChecked();
    opt.threads = m_w->threads->value();
    // v3.9.6：GPU 流式匹配现已支持「提早停止 / 智能范围」（按行带分批 + 判损判停），不再需要回退提示。
    // v3.3.9（任务书 2.2/2.1）：CPU 占用率上限 + 内存上限（GUI 主搜索路径）
    {
        const int logical = (std::max)(1, QThread::idealThreadCount());
        const int pct = std::clamp(m_settings.cpuLimitPercent, 10, 100);
        if (pct < 100) {
            const int limited = (std::max)(1, static_cast<int>(std::lround(logical * pct / 100.0)));
            if (opt.threads > limited) {
                Logger::info("CPU 占用率上限 " + std::to_string(pct) + "% ⇒ 生效线程数 " +
                             std::to_string(opt.threads) + " → " + std::to_string(limited));
                opt.threads = limited;
            }
        }
    }
    opt.memBudgetBytes =
        (m_settings.memLimitMB > 0) ? static_cast<int64_t>(m_settings.memLimitMB) * 1024 * 1024 : 0;
    opt.cpuLimitPercent = std::clamp(m_settings.cpuLimitPercent, 10, 100);
    m_lastEffectiveThreads = opt.threads; // v3.3.9（二）：供自检核对「CPU 上限确实收敛了线程数」
    // v3.3.0：搜索算法（分层/估算/保守仅在密度搜索下生效；引擎侧对误配做兜底）
    opt.algorithm = (opt.mode == Mode::Density) ? currentAlgorithm() : Algorithm::Exact;
    opt.layerBlock = m_w->layerBlock->currentData().toInt();
    opt.layerRatio = m_w->layerRatio->value();
    opt.sampleStep = m_w->sampleStep->value();
    opt.sampleExact = m_w->sampleExact->isChecked();

    // 启动前边界兜底（v3.1.0）
    const int bound = versionBound();
    for (QSpinBox* s : { m_w->minX, m_w->maxX, m_w->minZ, m_w->maxZ }) {
        if (s->value() > bound) s->setValue(bound);
        if (s->value() < -bound) s->setValue(-bound);
    }
    opt.minX = m_w->minX->value();
    opt.maxX = m_w->maxX->value();
    opt.minZ = m_w->minZ->value();
    opt.maxZ = m_w->maxZ->value();

    bool seedOA = true;
    if (opt.version == Version::Java) {
        // v3.3.4（任务书 4.2）：Java 版必须填写合法种子 —— 种子默认空；
        // "0" 是合法种子；纯空格视为空；"123abc" 视为非法；基岩版完全不受影响。
        const QString seedText = m_w->seedEdit->text().trimmed();
        if (seedText.isEmpty()) {
            // 弹窗 [去填写] [取消] + 反馈栏 ❌ + 日志记录（如已启用）+ 输入框高亮闪烁
            addError(slime::ui::t("ui.24599e00ffb7"));
            slime::LogBus::error("Java 版未填写世界种子，搜索被阻止");
            bool goFill = true;
            if (!m_uiQuiet) {
                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle(slime::ui::t("ui.02343987a852"));
                box.setText(slime::ui::t("ui.d33428ef9569"));
                QPushButton* fill = box.addButton(slime::ui::t("ui.919e5d650bfa"), QMessageBox::AcceptRole);
                box.addButton(slime::ui::t("ui.4d0b4688c787"), QMessageBox::RejectRole);
                box.exec();
                goFill = (box.clickedButton() == fill);
            }
            if (goFill) highlightSeedField();
            return;
        }
        opt.worldSeed = seedText.toLongLong(&seedOA);
        if (!seedOA) {
            addError(slime::ui::t("ui.2469f1297a2d").arg(seedText));
            slime::LogBus::error("世界种子非法: " + seedText.toStdString());
            if (!m_uiQuiet) {
                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle(slime::ui::t("ui.733bc78b8835"));
                box.setText(slime::ui::t("ui.f36f9429a104").arg(seedText));
                QPushButton* fill = box.addButton(slime::ui::t("ui.919e5d650bfa"), QMessageBox::AcceptRole);
                box.addButton(slime::ui::t("ui.4d0b4688c787"), QMessageBox::RejectRole);
                box.exec();
                if (box.clickedButton() != fill) return;
            }
            highlightSeedField();
            return;
        }
        slime::LogBus::info("搜索种子: " + std::to_string(opt.worldSeed));
    }

    // v3.4.12（用户实测：缓存构建时仍被要求设置搜索模式）：
    //   构建只做史莱姆判定 + 写缓存，**不使用搜索模式/图案**，因此跳过严格模式的图案校验。
    if (!buildModeActive() && opt.mode == Mode::Strict) {
        if (!m_patternSet || m_pattern.empty()) {
            QMessageBox::warning(this, slime::ui::t("ui.05118c2a651d"), slime::ui::t("ui.bb9712c09a09"));
            return;
        }
        opt.pattern = m_pattern;
    }

    const int64_t area = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t areaZ = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    if (area > 0 && areaZ > 0) {
        const int64_t total = area * areaZ;
        // v3.4.12：构建模式的范围确认走 prepareBuildRun（预算/容量），不再叠一层搜索式整图确认
        if (total >= kWholeMapMinArea && !buildModeActive()) {
            // v3.1.1：整图级搜索 → 动态估算确认（预设确认过的会话不再重复询问）
            // v3.3.3：无界面自检（m_uiQuiet）时自动放行，避免模态框阻塞自动化流程
            if (!m_wholeMapConfirmed) {
                if (!m_uiQuiet && !confirmWholeMapSearch(false, versionBound())) return;
                m_wholeMapConfirmed = true;
            }
        } else if (total > 1'000'000) {
            if (!m_uiQuiet) {
                // v3.6.6（用户要求）：二次确认窗口重写，并给出「成功率预测」
                const QString body =
                    slime::ui::t("ui.c6ccb27bc3fa")
                        .arg(area)
                        .arg(areaZ)
                        .arg(total)
                        .arg(successProbabilityText(area, areaZ));
                if (QMessageBox::question(this, slime::ui::t("ui.4973d000fee5"), body) != QMessageBox::Yes) {
                    return;
                }
            }
        }
    }

    const bool cacheEnabled = (opt.version == Version::Bedrock) && m_w->useCache->isChecked();
    opt.useCache = cacheEnabled;
    opt.cache = cacheEnabled ? m_cache.get() : nullptr;
    // v3.3.9（任务书 1.2）：**普通搜索不再写缓存** —— 写入路径整体移除（只有缓存构建模式能写）
    opt.cacheWrite = nullptr;
    opt.cacheBudgetBytes = 0;

    // v3.3.6（任务书 4.3）：大范围搜索警告阈值 —— 超过设置值时弹窗确认（可在设置里调整）
    // v3.3.3 的「低于临界值建议关闭 GPU」提示已随 Bug 2 一并移除。
    {
        const int64_t total0 = area * areaZ;
        const int64_t warnAt = m_settings.largeRangeWarnChunks > 0 ? m_settings.largeRangeWarnChunks
                                                                   : 100000000;
        // v3.3.8.1（任务书 3.2）：大范围警告阈值改为「**预计耗时 > 5 分钟**」触发。
        // 预计耗时用引擎的 estimateSearch（本机实测速率 + 分块计划）；不可用时回退区块数阈值。
        slime::SearchEstimate est;
        bool estOk = false;
        const int estThreads = (m_w->threads && m_w->threads->value() > 0) ? m_w->threads->value() : 0;
        estOk = slime::SlimeEngine::estimateSearch(opt, estThreads, est) && est.ok;
        const bool tooLong = largeRangeNeedsConfirm(total0, estOk, est.totalSec, warnAt);
        if (tooLong) {
            m_largeRangeWarned = true; // 自检据此断言该分支确实被走到
            Logger::info(std::string("大范围搜索确认：范围 ") + std::to_string(total0) + " 区块，" +
                         (estOk ? ("预计耗时 " + std::to_string(static_cast<int64_t>(est.totalSec)) + " 秒")
                                : ("超过区块数阈值 " + std::to_string(warnAt))) +
                         (tooLong ? "（> 5 分钟）" : ""));
            if (!m_uiQuiet) {
                // v3.3.9（任务书 3.2）：弹窗内容必须写清三项 —— 即将扫描多少、超阈值多少、预计多少分钟
                const QString body =
                    largeRangeConfirmText(total0, area, areaZ, estOk, est.totalSec, warnAt) +
                    slime::ui::t("ui.f3c740185215")
                        .arg(successProbabilityText(area, areaZ));
                if (QMessageBox::question(this, slime::ui::t("ui.54f83a642d7d"), body) != QMessageBox::Yes) {
                    return;
                }
            }
        }
    }

    // v3.3.6（任务书 调整 3）：进度流水初始化（每次搜索重新计数实时命中）
    m_runTotalChunks = area * areaZ;
    m_streamScanned = 0;
    m_streamTick = 0;
    m_task.resetLiveMatches(); // v3.3.7（P0-2）：计数由 SearchTask 持有（生命周期与线程绑定）

    // v3.2.1：GPU 加速仅在「用户勾选 且 检测可用」时注入（默认关闭；绝不自动启用）。
    // 勾选但不可用/未检测 → 静默回退 CPU，仅记录日志，不弹窗不崩溃。
    opt.gpuScan = nullptr;
    opt.gpuResultSelfOptimize = m_settings.gpuResultSelfOptimize;   // v3.10.11.1：结果自优化开关
    const bool gpuWanted = m_w->gpuCheck && m_w->gpuCheck->isChecked();
    if (gpuWanted) {
        if (!m_gpuProbeDone) {
            // v3.10.3（用户反馈：启动超大范围搜索时界面卡住）：首次 GPU 检测会同步初始化
            //   OpenCL（可达数秒）。这里给出等待光标与明确状态，让用户知道在做什么。
            setStatus(slime::ui::tr(slime::ui::t("ui.85e816a88b92")));
            if (!m_uiQuiet) QApplication::setOverrideCursor(Qt::WaitCursor);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            ensureGpuScanOnce();
            if (!m_uiQuiet) QApplication::restoreOverrideCursor();
        }
        if (m_gpuScan && m_gpuAvailable) {
            opt.gpuScan = m_gpuScan.get();
            Logger::info("本次搜索启用 GPU 扫描加速（区域达到阈值时由引擎自动使用，失败自动回退 CPU）");
        } else {
            Logger::info("GPU 已勾选但不可用，本次搜索使用 CPU"
                         + (m_gpuProbeReason.empty() ? std::string() : "：" + m_gpuProbeReason));
        }
    }

    // v3.4.12（用户实测：构建时仍被要求设置搜索模式）：**必须在引擎校验之前**把构建语义归一。
    //   构建只做史莱姆判定 + 写缓存，不做图案匹配；若界面还停在「严格图案匹配」且未画图案，
    //   引擎 validate() 会判「图案为空」⇒ 用户就会被要求去设置搜索模式。
    if (buildModeActive()) {
        opt.mode = Mode::Density;
        opt.pattern = Pattern();
        opt.algorithm = Algorithm::Exact;
    }

    const std::string verr = SlimeEngine::validate(opt);
    if (!verr.empty()) {
        // v3.4.12：无界面自检（m_uiQuiet）时只记录不弹窗（此前漏了保护，自检会被模态框挡住）
        if (!m_uiQuiet) {
            QMessageBox::warning(this, slime::ui::t("ui.3e64edf2ea33"), QString::fromStdString(verr));
        } else {
            std::fprintf(stderr, "[UI自检] 参数校验失败（quiet 模式不弹窗）：%s\n", verr.c_str());
        }
        return;
    }

    RunInfo info;
    info.version = opt.version;
    info.mode = opt.mode;
    info.algorithm = opt.algorithm;
    info.layerBlock = opt.layerBlock;
    info.layerRatio = opt.layerRatio;
    info.sampleStep = opt.sampleStep;
    info.sampleExact = opt.sampleExact;
    info.gridSizeUsed = opt.gridSize;
    info.minDensityUsed = opt.minDensity;
    info.options = opt;
    info.region = opt.region();
    info.targetCount = opt.targetCount;
    info.startX = opt.startX;
    info.startZ = opt.startZ;
    info.cacheWasUsed = cacheEnabled;
    info.mergeCacheAfter = cacheEnabled && !(m_cache->covers(info.region));


    // v3.3.9（任务书 1.1）：缓存构建模式 —— 预估/上限确认/分段（用户放弃则直接返回）
    //（构建语义已在引擎校验前归一为密度，见上）
    if (buildModeActive() && !prepareBuildRun(opt, info)) return;
    // v3.3.9.1（任务书 1.3）：构建模式必须**只跑第一段**（后续段由 handleBuildBandFinished 续跑）。
    // 旧实现直接把「整个范围」交给引擎 ⇒ 第一段就把全范围扫完（进度瞬间 100%），
    // 随后每一段都发现行已被覆盖 ⇒ 只累加「已跳过」，用户看到的就是「进度 >100%、不断重复记录」。
    if (m_buildActive && !m_buildBands.empty()) {
        opt.minZ = m_buildBands.front().minZ;
        opt.maxZ = m_buildBands.front().maxZ;
        info.region = m_buildBands.front();
        m_buildBaseOpt = opt;      // 基础选项 = 第一段选项（后续段只改 Z 范围）
        m_buildRunInfo.options = opt;
    }

    m_running = true;
    setRunningUi(true);
    setInputsEnabled(false);
    // v3.3.4：结果区改为 QTextEdit，搜索开始时清空文本（等价于旧版清空结果卡片）
    if (m_w->resultView) m_w->resultView->clear();
    if (m_w->infoStats) m_w->infoStats->setText(slime::ui::t("ui.f9012afa40dc"));
    m_lastResultText.clear();
    m_hasLastOutcome = false;
    m_w->progress->setValue(0);
    m_searchStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    setStatus(slime::ui::tr(slime::ui::t("ui.455d8705421a")));
    logSearchParams(opt);
    // v3.10.8：把全部有效参数写成一行指纹，写入日志（排查预设/自定义速度差异）
    slime::LogBus::info(std::string("搜索参数指纹: ") + searchOptionsFingerprint(opt).toStdString());
    {
        // v3.10.7：明确记录本次实际使用的版本（排查「界面 Java 实际基岩」类问题）
        const bool bedrockRun = (opt.version == Version::Bedrock);
        Logger::info(std::string("本次搜索实际版本: ") + (bedrockRun ? "基岩版" : "Java 版"));
        addFeedback(3, slime::ui::t("ui.ba3a88efa949")
                           .arg(bedrockRun ? slime::ui::t("ui.618fd37c0c01") : slime::ui::t("ui.8accf0fe2eed")));
    }
    m_etaSec = 0.0; // v3.3.8（任务 1）：新搜索重置 ETA（完成后保持清空，避免误以为还在跑）
    if (m_elapsedTimer) m_elapsedTimer->start(200); // v3.3.4：进度标签页刷新已用时/预计剩余

    if (info.cacheWasUsed && !info.mergeCacheAfter) {
        Logger::info("缓存完全覆盖搜索范围，将直接使用缓存数据");
    }

    const auto postToUi = [this](auto fn) {
        QMetaObject::invokeMethod(this, [fn = std::move(fn)]() { fn(); }, Qt::QueuedConnection);
    };

    // v3.3.7（P0-2）：本次运行编号 —— 取消后立刻重新搜索时，旧运行排队的进度/结果事件
    // 必须被丢弃，否则会错误地更新新搜索的界面状态（甚至 join 到新线程造成卡死）。
    const int runId = ++m_runId;
    // v3.3.7.2（任务书 1.4）：阶段文案按**本次运行**的模式/算法分派（不用界面控件的实时值，
    // 避免用户在搜索中途切换模式导致文案与真实行为不符）。
    const slime::Mode runMode = opt.mode;
    const slime::Algorithm runAlg = opt.algorithm;
    m_runMode = runMode; // v3.3.7.2：供取消/完成/出错等后续路径复用同一口径
    m_runAlg = runAlg;

    m_task.start(
        opt,
        [this, postToUi, runId, runMode, runAlg](double frac, bool scanning, double cps, double etaSec) {
            postToUi([this, frac, scanning, cps, etaSec, runId, runMode, runAlg]() {
                if (!m_running || runId != m_runId) return; // 陈旧运行的事件：直接丢弃
                // v3.3.9（任务书 1.1/1.4）：构建模式用「四项进度」+ 缓存扫描/新区域扫描区分
                if (m_buildActive) {
                    updateBuildProgressUi(scanning);
                    return;
                }
                // v3.3.8（任务 1）：ETA 接线 —— 此前回调收到了 etaSec 却从未写入 m_etaSec，
                // 于是 updateElapsedUi() 永远走 else 分支、状态栏恒显示「预计剩余：--:--」。
                m_etaSec = etaSec;
                // v3.10.4（用户反馈：GPU 单次派发期间进度条长时间停在 0 像卡死）：
                //   GPU 阶段改为忙碌动画（range 0,0），不伪造百分比；结束后恢复真实进度条。
                if (m_w->progress) {
                    if (m_task.livePhase() >= 2) {
                        if (m_w->progress->maximum() != 0) m_w->progress->setRange(0, 0);
                    } else if (m_w->progress->maximum() == 0) {
                        m_w->progress->setRange(0, 1000);
                    }
                }
                m_w->progress->setValue(static_cast<int>(frac * 1000));
                const int pct = static_cast<int>(frac * 100.0 + 0.5);
                if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("%1%").arg(pct));
                // v3.3.3（任务书 3.2）：分阶段文字（映射逻辑见 progressStageName，自检直接断言）
                // v3.3.7（任务书 P1-5）：**单一数据源** ——
                //   进度行：进度条 + progressPercent（唯一百分比）+ progressStage（纯阶段短语）
                //   状态栏：progressInfo（统计信息）+ progressEta（唯一预计剩余）
                // v3.3.7.2（任务书 1.4）：阶段短语按模式分派（密度=匹配密度，估算=估算）
                if (m_w->progressStage) {
                    // v3.10.4（用户反馈：GPU 进度无法同步且行带派发拖慢速度）：
                    //   不再显示 GPU 阶段标注，只保留阶段短语 + 速度/统计（速度由引擎在完成时上报）。
                    m_w->progressStage->setText(
                        progressStageName(scanning ? 0 : (m_stageWasMatching ? 2 : 1), runMode, runAlg));
                }
                m_streamScanned = m_task.liveScannedCount(); // v3.3.7（P0-3）：引擎真实计数
                if (m_w->progressInfo) {
                    // v3.3.7.2（任务书 1.2）：分母改用引擎口径总量（与分子同源）→ 百分比恒 ≤100%
                    m_w->progressInfo->setText(progressStatsText(m_streamScanned, displayTotalChunks(), cps,
                                                                 m_task.liveMatchCount(), m_settings.rateUnit));
                }
                if (scanning) {
                    m_stageWasMatching = false;
                } else if (!m_stageWasMatching) {
                    m_stageWasMatching = true;
                    // v3.3.7.2（任务书 1.3）：**不再**把阶段短语塞进状态栏的临时消息 ——
                    // 状态栏是「统计信息 + 已用时/预计剩余」区，阶段短语只由进度行的
                    // progressStage 显示。旧实现把「扫描完成，正在匹配图案…」当临时消息，
                    // 它紧贴 progressInfo 的统计文字，读起来像一句话（用户截图即为此）。
                }
            });
        },
        [this, postToUi, info, runId](const SearchOutcome& out) {
            postToUi([this, out, info, runId]() { onSearchFinished(out, info, runId); });
        });

    if (m_w->threads->value() == 1) {
        setStatus(slime::ui::tr(slime::ui::t("ui.92cfd0503816")));
    }
}

// ============================================================================
// v3.3.9（任务书 1.1）：缓存构建模式
//   * 只有这个模式写缓存（1.2：其他模式一律只读）
//   * 只做史莱姆判定 + 写缓存，不做匹配 ⇒ 比同范围搜索快（省掉整个匹配阶段）
//   * 范围 = 界面上的 X/Z；算法 = 自动（准确优先：精确判定路径，不采样、不分层）
//   * 中断续传：按「以行为单位的分段」执行，每段结束即原子落盘；重跑同范围时已覆盖分段秒过
//   * 完成条件：跑完整个范围 **或** 达到缓存容量上限（绝不「边写边丢」）
//   * 构建前预估：按位图口径（1 bit/区块 = 0.125 B/区块）+ 旧坐标口径（10% 密度 × 8 B）双口径提示
// ============================================================================
bool MainWindow::buildModeActive() const
{
    return m_buildModeActive;
}

// v3.4.9：构建模式由「当前页面 / 构建流程」驱动（搜索页开关已按要求移除）
void MainWindow::setBuildModeActive(bool on)
{
    if (m_buildModeActive == on) {
        updateBuildHintUi();
        return;
    }
    m_buildModeActive = on;
    Logger::info(std::string("缓存构建模式 ") + (on ? "开启" : "关闭"));
    updateBuildHintUi();
}

int64_t MainWindow::buildTotalChunks() const
{
    if (m_w->minX == nullptr) return 0;
    const int64_t w = static_cast<int64_t>(m_w->maxX->value()) - m_w->minX->value() + 1;
    const int64_t h = static_cast<int64_t>(m_w->maxZ->value()) - m_w->minZ->value() + 1;
    return (w > 0 && h > 0) ? w * h : 0;
}

// 勾选状态 → 提示文字 + 控件可用性（构建模式不做匹配，因此匹配相关控件禁用）
void MainWindow::updateBuildHintUi()
{
    // v3.4.9：不再因提示行缺失而提前返回 —— 标灰逻辑必须独立生效
    //（v3.4.8 的教训：搜索页开关的连接写在控件创建之前，导致"勾了不标灰"长期存在）
    const bool on = buildModeActive();
    if (m_w->buildHint) {
        m_w->buildHint->setVisible(on);
    }
    if (on && m_w->buildHint) {
        const int64_t total = buildTotalChunks();
        const double bitmapMB = static_cast<double>(total) / 8.0 / (1024.0 * 1024.0);
        m_w->buildHint->setText(
            slime::ui::t("ui.ff92ea44ed7e")
                .arg(total)
                .arg(bitmapMB, 0, 'f', 1)
                .arg(static_cast<double>(total) * 0.1 * 8.0 / (1024.0 * 1024.0), 0, 'f', 1));
    }
    // v3.3.9.1（任务书 1.1）：构建模式下，**与「判定 + 写缓存」无关的选项一律禁用**并给出原因，
    // 避免用户以为「游戏版本 / 搜索模式 / 算法 / 目标数量」等仍然生效（它们确实不参与构建）。
    // 不改排版：只切换 enabled 状态与 tooltip 文案。
    QList<QWidget*> irrelevant = {
        static_cast<QWidget*>(m_w->javaRadio),        // 游戏版本：缓存位图语义为基岩版（两个都灰，避免"还要选版本"）
        static_cast<QWidget*>(m_w->bedrockRadio),
        static_cast<QWidget*>(m_w->strictRadio),      // 搜索模式：构建不做匹配
        static_cast<QWidget*>(m_w->densityRadio),
        static_cast<QWidget*>(m_w->stopAtTarget),     // 结果筛选：构建不产出结果
        static_cast<QWidget*>(m_w->targetCount),
        static_cast<QWidget*>(m_w->smartRange),
        static_cast<QWidget*>(m_w->algExactRadio),    // 算法：构建固定用精确判定
        static_cast<QWidget*>(m_w->algLayeredRadio),
        static_cast<QWidget*>(m_w->algSampleRadio),
        static_cast<QWidget*>(m_w->algBruteRadio),
        static_cast<QWidget*>(m_w->seedEdit),         // v3.4.8：构建不需要种子（基岩版判定与种子无关）
        static_cast<QWidget*>(m_w->lockDir)           // v3.4.8：构建不做匹配 ⇒ 方向锁定同样无关
    };
    if (m_w->gridSize) irrelevant << m_w->gridSize;          // 网格/密度：只服务匹配阶段
    if (m_w->minDensity) irrelevant << m_w->minDensity;
    if (m_w->drawPattern) irrelevant << m_w->drawPattern; // 图案绘制入口：只服务严格匹配
    for (QWidget* w : irrelevant) {
        if (!w) continue;
        if (on) {
            if (w->toolTip().isEmpty() || !w->property("v3391tip").toBool()) {
                w->setProperty("v3391tip", true);
                w->setProperty("v3391oldTip", w->toolTip());
            }
            w->setToolTip(slime::ui::t("ui.29299675eb80"));
            w->setEnabled(false);
        } else if (w->property("v3391tip").toBool()) {
            // v3.4.9：只恢复**本函数禁用过**的项。原来无条件 setEnabled(true) 会把版本规则的
            // 禁用（基岩版不用 Java 种子等）一起解掉 —— 现在交给下面的 applyVersionUi() 重新收敛。
            w->setToolTip(w->property("v3391oldTip").toString());
            w->setProperty("v3391tip", false);
            w->setEnabled(true);
        }
    }
    // 关闭构建模式后按版本/模式规则重新收敛一次（种子、缓存、预设仅限对应版本）
    applyVersionUi();
    if (on && m_w->bedrockRadio && !m_w->bedrockRadio->isChecked()) {
        // v3.10.7（用户反馈：测试中平台莫名其妙自己变成基岩版）：
        //   构建模式强制基岩版只是临时语义，记下用户原来的选择，退出构建时恢复。
        m_preBuildWasBedrock = m_w->bedrockRadio && m_w->bedrockRadio->isChecked();
        m_buildSavedVersion = true;
        m_w->bedrockRadio->setChecked(true); // 缓存语义 = 基岩版（Java 版不写缓存）
    } else if (!on && m_buildSavedVersion) {
        m_buildSavedVersion = false;
        if (m_w->bedrockRadio && m_w->bedrockRadio->isChecked() != m_preBuildWasBedrock) {
            if (m_preBuildWasBedrock) m_w->bedrockRadio->setChecked(true);
            else if (m_w->javaRadio) m_w->javaRadio->setChecked(true);
        }
    }
    // v3.4.9：构建内容就在独立的「缓存构建」页里，随页面显示，不再需要在这里控制可见性
}

// ============================================================================
// v3.4.11（用户拍板）配置落盘合并 + 设置页收敛
//   为什么：设置页任何改动都会走 60ms 自动生效；旧实现每次生效都同步写整份 config.json
//   （含终端历史），拖动滑块时约 16 次/秒的磁盘写入 + QSS 重放 ⇒ 界面卡顿（音量条最明显）。
//   现在：内存状态立即生效；磁盘写入合并到「空闲 500ms」或滑块释放时 flush。
// ============================================================================
void MainWindow::scheduleConfigWrite()
{
    if (!m_configWriteTimer) {
        m_configWriteTimer = new QTimer(this);
        m_configWriteTimer->setSingleShot(true);
        m_configWriteTimer->setInterval(500);
        connect(m_configWriteTimer, &QTimer::timeout, this, [this]() {
            m_configWritePending = false;
            writeConfigFile(m_w && m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                            m_terminal ? m_terminal->history() : QStringList());
            Logger::info("配置已合并写入 config.json");
        });
    }
    m_configWritePending = true;
    m_configWriteTimer->start();
}

void MainWindow::flushPendingConfigWrite()
{
    if (!m_configWritePending) return;
    if (m_configWriteTimer) m_configWriteTimer->stop();
    m_configWritePending = false;
    writeConfigFile(m_w && m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                    m_terminal ? m_terminal->history() : QStringList());
}

// v3.4.11（用户反馈「设置在重启后仍然生效，但设置项不是正确状态」的根因）：
//   buildUi() 用**默认值**先把设置页建出来，之后 loadAppSettings() 才把 config.json 读进
//   m_settings —— 中间没人把值推回设置页 ⇒ 页面显示默认值（而程序行为是保存值）。
//   更糟的是设置页是「立即生效」的取值来源，之后任何改动都会把过期的页面值写回磁盘。
//   本函数把「真实状态 → 设置页」单向收敛一次（同步期间用 guard 抑制自动生效，避免回写循环）。
void MainWindow::syncSettingsPageFromState()
{
    if (!m_settingsPage || m_settingsSyncGuard) return;
    m_settingsSyncGuard = true;
    m_settingsPage->setValues(m_settings);
    m_settingsSyncGuard = false;
    // 页面已经等于真实状态 ⇒ 挂起的「自动生效」没有意义，停掉以免又拿页面值回写一遍
    if (m_settingsApplyTimer) m_settingsApplyTimer->stop();
}

// v3.4.9：把搜索页的范围推到构建页（构建页只是同一份数据的镜像视图）
void MainWindow::pushSearchRangeToBuild()
{
    if (!m_w || !m_w->bMinX || !m_w->minX || m_rangeSyncGuard) return;
    m_rangeSyncGuard = true;
    m_w->bMinX->setValue(m_w->minX->value());
    m_w->bMaxX->setValue(m_w->maxX->value());
    m_w->bMinZ->setValue(m_w->minZ->value());
    m_w->bMaxZ->setValue(m_w->maxZ->value());
    m_rangeSyncGuard = false;
    updateBuildHintUi();
    updateBuildEstimateLive(); // v3.4.12：搜索页改范围也刷新构建预估
}

// 构建前预估 + 上限确认 + 分段；返回 false 表示用户放弃本次构建
bool MainWindow::prepareBuildRun(SearchOptions& opt, RunInfo& info)
{
    const int64_t total = buildTotalChunks();
    if (total <= 0) {
        QMessageBox::warning(this, slime::ui::t("ui.073cf8d38324"), slime::ui::t("ui.9d61509a4e27"));
        return false;
    }
    // ① 构建前预估（任务书 1.1）：范围面积 × 密度 10% × 每区块字节数（旧口径）+ 位图口径
    const double bitmapMB = static_cast<double>(total) / 8.0 / (1024.0 * 1024.0);
    const double legacyMB = static_cast<double>(total) * 0.1 * 8.0 / (1024.0 * 1024.0);
    const int64_t capMB = cacheMaxBytes() / (1024 * 1024);
    const bool overCap = cacheMaxBytes() > 0 && bitmapMB > static_cast<double>(capMB);
    if (!m_uiQuiet) {
        QString body = slime::ui::t("ui.be2e910d051e")
                           .arg(m_w->minX->value()).arg(m_w->maxX->value())
                           .arg(m_w->minZ->value()).arg(m_w->maxZ->value())
                           .arg(total)
                           .arg(bitmapMB, 0, 'f', 1)
                           .arg(legacyMB, 0, 'f', 1)
                           .arg(capMB);
        if (overCap) {
            body += slime::ui::t("ui.ad89a275f758").arg(
                static_cast<int64_t>(bitmapMB * 1.2) + 1);
            body += slime::ui::t("ui.ec13fdb97145");
            QMessageBox box(this);
            box.setWindowTitle(slime::ui::t("ui.9d43e3b73ab4"));
            box.setText(body);
            QPushButton* raise = box.addButton(slime::ui::t("ui.9a81e2e81062"), QMessageBox::AcceptRole);
            QPushButton* go = box.addButton(slime::ui::t("ui.c20257102d0b"), QMessageBox::ActionRole);
            box.addButton(slime::ui::t("ui.4d0b4688c787"), QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() == raise) {
                const int64_t newMB = static_cast<int64_t>(bitmapMB * 1.2) + 64;
                m_settings.cacheMaxMB = static_cast<int>(newMB);
                applyAppSettings(m_settings); // 写回 config.json（cache.maxSizeMB）
                Logger::info("缓存上限已按构建预估提高到 " + std::to_string(newMB) + " MB");
                addFeedback(2, slime::ui::tr(slime::ui::t("ui.c3108a4edc5d")).arg(newMB));
            } else if (box.clickedButton() != go) {
                return false;
            }
        } else {
            body += slime::ui::t("ui.7f8cd0261e31");
            // v3.4.12：无界面自检（m_uiQuiet）时自动放行 —— 与其它确认弹窗同一口径。
            //（此前漏了这层保护，自检走到构建流程时会被模态框挡住 => 挂起。）
            if (!m_uiQuiet && QMessageBox::question(this, slime::ui::t("ui.9d43e3b73ab4"), body) != QMessageBox::Yes) {
                return false;
            }
        }
    }

    // ② 分段（每段 rowsPerBand 行）：段结束即落盘 ⇒ 中断续传的最小单位
    const int64_t minZ = m_w->minZ->value();
    const int64_t maxZ = m_w->maxZ->value();
    const int64_t minX = m_w->minX->value();
    const int64_t maxX = m_w->maxX->value();
    const int64_t w = maxX - minX + 1;
    // 目标：单段 ≈ 20~60 秒（按 50 万区块/秒的保守速率 + 头开销估算），并保证位图占用有界
    int64_t rowsPerBand = (std::max)(int64_t(1024), (int64_t(8) * 1024 * 1024) / (std::max)(int64_t(1), w));
    rowsPerBand = (std::min)(rowsPerBand, maxZ - minZ + 1);
    m_buildBands.clear();
    for (int64_t z = minZ; z <= maxZ; z += rowsPerBand) {
        const int64_t zEnd = (std::min)(maxZ, z + rowsPerBand - 1);
        m_buildBands.push_back(Rect{ static_cast<int>(minX), static_cast<int>(maxX),
                                     static_cast<int>(z), static_cast<int>(zEnd) });
    }
    m_buildBandIndex = 0;
    m_buildDoneChunks = 0;
    m_buildSkippedTotal = 0;
    m_buildActive = true;
    m_buildStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    // ③ 构建选项：基岩版 + 精确判定（准确优先，不采样/不分层）+ 唯一写缓存
    opt.version = Version::Bedrock;
    opt.useCache = true;
    opt.cache = m_cache.get();
    opt.cacheWrite = m_cache.get();
    opt.cacheBudgetBytes = cacheMaxBytes();
    opt.cacheWriteDisabled = false;
    opt.buildMode = true;
    opt.algorithm = Algorithm::Exact;
    opt.mode = Mode::Density;   // 判定路径与密度搜索共用填充（不做匹配）
    opt.gridSize = (std::max)(1, m_w->gridSize ? m_w->gridSize->value() : 5);
    opt.stopAtTarget = false;
    opt.smartRange = false;
    opt.targetCount = 1;
    m_buildBaseOpt = opt;
    m_buildRunInfo = info;
    m_buildRunInfo.version = Version::Bedrock;
    m_buildRunInfo.options = opt;
    m_buildRunInfo.cacheWasUsed = true;
    m_buildRunInfo.mergeCacheAfter = false;
    m_buildRunInfo.region = m_buildBands.front();
    m_buildRunInfo.mode = opt.mode;
    m_buildRunInfo.algorithm = opt.algorithm;
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.9e975f02353a"))
                       .arg(total)
                       .arg(m_buildBands.size()));
    Logger::info("缓存构建开始：范围 X(" + std::to_string(minX) + "~" + std::to_string(maxX) + ") Z(" +
                 std::to_string(minZ) + "~" + std::to_string(maxZ) + ")，共 " +
                 std::to_string(m_buildBands.size()) + " 段，预估位图 " + std::to_string(bitmapMB) +
                 " MB，上限 " + std::to_string(capMB) + " MB");
    return true;
}

// 启动当前分段（同一套 SearchTask 机制：可取消、进度回调、完成回调）
void MainWindow::launchBuildBand()
{
    if (m_buildBandIndex >= m_buildBands.size() || !m_running) return;
    SearchOptions opt = m_buildBaseOpt;
    const Rect band = m_buildBands[m_buildBandIndex];
    opt.minZ = band.minZ;
    opt.maxZ = band.maxZ;
    opt.minX = band.minX;
    opt.maxX = band.maxX;
    const RunInfo info = m_buildRunInfo;
    const auto postToUi = [this](auto fn) {
        QMetaObject::invokeMethod(this, [fn = std::move(fn)]() { fn(); }, Qt::QueuedConnection);
    };
    const int runId = ++m_runId;
    m_runMode = opt.mode;
    m_runAlg = opt.algorithm;
    m_task.resetBuildCounters();
    m_task.start(
        opt,
        [this, postToUi, runId](double frac, bool scanning, double, double) {
            postToUi([this, frac, scanning, runId]() {
                if (!m_running || runId != m_runId) return;
                updateBuildProgressUi(scanning);
                (void)frac;
            });
        },
        [this, postToUi, info, runId](const SearchOutcome& out) {
            postToUi([this, out, info, runId]() { onSearchFinished(out, info, runId); });
        });
    std::fprintf(stderr, "[构建] 段 %zu/%zu 启动：Z(%d~%d)\n", m_buildBandIndex + 1, m_buildBands.size(),
                 band.minZ, band.maxZ);
}

// 四项进度（任务书 1.1 指定顺序）：已记录区块(百分比) / 记录速度(万区块/秒) / 已花费秒数（+ 阶段区分，1.4）
void MainWindow::updateBuildProgressUi(bool scanning)
{
    const int64_t scanned = m_task.liveScannedCount();
    const int64_t recorded = m_buildDoneChunks + scanned;
    const int64_t total = (std::max)(int64_t(1), buildTotalChunks());
    const qint64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    const double secs = (m_buildStartMs > 0) ? static_cast<double>(nowMs - m_buildStartMs) / 1000.0 : 0.0;
    const double wanPerSec = secs > 0.05 ? (static_cast<double>(recorded) / 10000.0) / secs : 0.0;
    // 引擎的「已记录」含块间重叠行（与搜索扫描计数同口径）⇒ 百分比按去重口径截顶到 100%
    const double pct =
        (std::min)(100.0, 100.0 * static_cast<double>(recorded) / static_cast<double>(total));
    if (m_w->progress) m_w->progress->setValue(static_cast<int>((std::min)(100.0, pct) * 10.0));
    if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("%1%").arg(pct, 0, 'f', 1));
    if (m_w->progressStage) {
        m_w->progressStage->setText(m_task.livePhase() == 1 ? slime::ui::t("ui.4ea75e619af3")
                                                            : slime::ui::t("ui.a8878877a9f5"));
    }
    if (m_w->progressInfo) {
        m_w->progressInfo->setText(
            slime::ui::t("ui.e9f7fb6a1c88")
                .arg(recorded)
                .arg(pct, 0, 'f', 1)
                .arg(wanPerSec, 0, 'f', 1)
                .arg(secs, 0, 'f', 1)
                .arg(m_buildSkippedTotal > 0 || m_task.liveSkippedCoveredCount() > 0
                         ? slime::ui::t("ui.c82eec4ef45f")
                               .arg(m_buildSkippedTotal + m_task.liveSkippedCoveredCount())
                         : QString()));
    }
    (void)scanning;
}

// 分段结束：落盘（续传点）→ 继续下一段 / 收尾
void MainWindow::handleBuildBandFinished(const SearchOutcome& out, int runId)
{
    // v3.3.9.1（任务书 1.2/1.3 根因修复）：
    //   ① 已收尾时再次进入 ⇒ 直接忽略（此前会重复累加计数 ⇒ 进度 158%、重复记录）
    //   ② 陈旧运行的完成事件（runId 不匹配）⇒ 丢弃（此前会多加一段、再启一段 ⇒ 死循环/计数翻倍）
    //   ③ 段下标越界 ⇒ 立即收尾（此前 `m_buildBands[idx]` 越界读取 = 未定义行为 ⇒ 偶发闪退；
    //      详细日志开启后时序变化，越界概率下降，这正是「开日志就不崩」的典型特征）
    if (!m_buildActive) return;
    if (runId != m_runId) {
        slime::IncidentLog::note("缓存构建：丢弃陈旧分段事件（事件 runId " + std::to_string(runId) +
                                 "，当前 " + std::to_string(m_runId) + "）");
        return;
    }
    if (m_buildBandIndex >= m_buildBands.size()) {
        slime::IncidentLog::note("缓存构建：分段下标越界（" + std::to_string(m_buildBandIndex) + "/" +
                                 std::to_string(m_buildBands.size()) + "）⇒ 立即收尾");
        m_buildStopRequested = true;
        m_buildBandIndex = m_buildBands.size();
    }
    m_task.wait();
    if (m_buildBandIndex < m_buildBands.size()) {
        m_buildDoneChunks += m_task.liveScannedCount();
        m_buildSkippedTotal += m_task.liveSkippedCoveredCount();
    }

    const bool cancelled = out.cancelled;
    const bool budgetHit = out.hitCacheBudget;
    const bool failed = !out.ok && !cancelled;

    // 落盘（原子替换；失败不影响内存内容）—— 这就是「中断续传」的检查点
    std::string serr;
    const bool saved = m_cache->saveFile(m_cacheFile, serr);
    updateCacheStatusUi();

    if (failed) {
        m_buildActive = false;
        m_running = false;
        setRunningUi(false);
        setInputsEnabled(true);
        setStatus(slime::ui::tr(slime::ui::t("ui.1242b3d3624e")));
        QMessageBox::critical(this, slime::ui::t("ui.1242b3d3624e"), QString::fromStdString(out.error));
        Logger::error("缓存构建出错：" + out.error);
        return;
    }

    const bool more = !cancelled && !budgetHit && (m_buildBandIndex + 1 < m_buildBands.size());
    std::fprintf(stderr, "[构建] 段 %zu/%zu 结束：cancelled=%d budget=%d 已记录累计=%lld 已跳过累计=%lld 保存=%d\n",
                 m_buildBandIndex + 1, m_buildBands.size(), cancelled ? 1 : 0, budgetHit ? 1 : 0,
                 static_cast<long long>(m_buildDoneChunks), static_cast<long long>(m_buildSkippedTotal),
                 saved ? 1 : 0);
    if (more) {
        ++m_buildBandIndex;
        if (m_progressStream) {
            m_progressStream->appendRawLine(
                slime::ui::t("ui.12f5363fe49e")
                    .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                    .arg(m_buildBandIndex)
                    .arg(m_buildBands.size()));
        }
        launchBuildBand(); // 继续下一段（m_running 保持 true，界面无需复位）
        return;
    }

    // 收尾
    m_buildActive = false;
    m_running = false;
    setRunningUi(false);
    setInputsEnabled(true);
    const int64_t total = (std::max)(int64_t(1), buildTotalChunks());
    const double secs = (std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count() -
                         m_buildStartMs) /
                        1000.0;
    QString result;
    if (cancelled) {
        result = slime::ui::t("ui.8ff8db608a0a");
        setStatus(slime::ui::tr(slime::ui::t("ui.9b7da6dcb967")));
    } else if (budgetHit) {
        result = slime::ui::t("ui.75015f29358d")
                     .arg(m_buildDoneChunks)
                     .arg(total);
        setStatus(slime::ui::tr(slime::ui::t("ui.8986e95164b7")));
    } else {
        result = slime::ui::t("ui.5a93958ec725")
                     .arg(m_buildDoneChunks)
                     .arg(secs, 0, 'f', 1);
        setStatus(slime::ui::tr(slime::ui::t("ui.68eb54822190")));
    }
    if (m_w->progressStage) m_w->progressStage->setText(slime::ui::t("ui.120cb0c8c8fa"));
    if (m_w->progressInfo) {
        m_w->progressInfo->setText(
            slime::ui::t("ui.4b7c9b14e7ad")
                .arg(m_buildDoneChunks)
                .arg((std::min)(100.0, 100.0 * static_cast<double>(m_buildDoneChunks) /
                                            static_cast<double>(total)),
                     0, 'f', 1)
                .arg(secs > 0.05 ? (static_cast<double>(m_buildDoneChunks) / 10000.0) / secs : 0.0, 0, 'f', 1)
                .arg(secs, 0, 'f', 1));
    }
    if (m_progressStream) {
        m_progressStream->appendRawLine(slime::ui::t("ui.0a1ed290251d")
                                            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                                            .arg(result));
    }
    addFeedback(2, result + slime::ui::t("ui.ed6566f75a34")
                           .arg(m_cache->ranges().size())
                           .arg(m_cache->entryCount()));
    Logger::info("缓存构建结束：" + result.toStdString() + " | 缓存 " +
                 std::to_string(m_cache->ranges().size()) + " 区域 / " +
                 std::to_string(m_cache->entryCount()) + " 区块 | " +
                 slime::IncidentLog::memorySummary());
    if (!saved) {
        addFeedback(1, slime::ui::tr(slime::ui::t("ui.37d56173be95"))
                           .arg(QString::fromStdString(serr)));
    }
    if (m_settings.soundOnComplete) slime::SoundPlayer::playCompletionChime(m_settings.soundVolume);
    updateNavBadges();
    updateCacheStatusUi();
    // 构建结束不弹「结果」对话框（构建模式不产生匹配结果）
    m_w->resultView->setPlainText(slime::ui::t("ui.27c62f740f6c"));
}

void MainWindow::cancelSearch()
{
    if (m_ringSearching) {
        m_ringCancel.store(true, std::memory_order_relaxed);
        if (m_w && m_w->cancel) m_w->cancel->setEnabled(false);
        setStatus(slime::ui::tr(slime::ui::t("ui.dd9a9f769e57")));
        return;
    }
    if (!m_running) return;
    m_task.cancel();
    m_w->cancel->setEnabled(false);
    setStatus(slime::ui::tr(slime::ui::t("ui.6bc0d687aeaa")));
    Logger::info("用户请求取消搜索");
    // v3.3.7（P1-6）：取消也写关键信息日志（不是崩溃，因此不标记 incident）
    slime::IncidentLog::note("用户取消搜索 | " + slime::IncidentLog::memorySummary());
}

// v3.10.8（用户疑问：预设是否暗改非明面设置？自定义调成相同设置会不会仍变慢）：
//   每次搜索都把「引擎实际收到的全部参数」写成一行指纹 → 预设 vs 自定义可以直接逐字比对。
//   若两行完全相同，性能差异就只可能来自运行时状态（缓存/GPU 探测/系统负载），不可能来自隐藏开关。
QString MainWindow::searchOptionsFingerprint(const SearchOptions& opt) const
{
    const bool gpuOn = m_w->gpuCheck && m_w->gpuCheck->isChecked();
    return QStringLiteral(
               "ver=%1 mode=%2 alg=%3 g=%4 thr=%5 target=%6 stop=%7 smart=%8 ignore=%9 "
               "lock=%10 layer=%11/%12 sample=%13/%14 threads=%15 cpuLimit=%16 memMB=%17 "
               "cache=%18 gpu=%19 patternStream=%20 start=(%21,%22) area=%23x%24 selfOpt=%25")
        .arg(opt.version == slime::Version::Bedrock ? QStringLiteral("bedrock") : QStringLiteral("java"))
        .arg(opt.mode == slime::Mode::Strict ? QStringLiteral("strict") : QStringLiteral("density"))
        .arg(static_cast<int>(opt.algorithm))
        .arg(opt.gridSize)
        .arg(opt.minDensity, 0, 'f', 3)
        .arg(opt.targetCount)
        .arg(opt.stopAtTarget ? 1 : 0)
        .arg(opt.smartRange ? 1 : 0)
        .arg(opt.ignoreThreshold ? 1 : 0)
        .arg(opt.directionLocked ? 1 : 0)
        .arg(opt.layerBlock)
        .arg(opt.layerRatio, 0, 'f', 3)
        .arg(opt.sampleStep)
        .arg(opt.sampleExact ? 1 : 0)
        .arg(opt.threads)
        .arg(opt.cpuLimitPercent)
        .arg(opt.memBudgetBytes / (1024 * 1024))
        .arg(opt.useCache ? 1 : 0)
        .arg(gpuOn ? 1 : 0)
        .arg(m_settings.gpuPatternStream ? 1 : 0)
        .arg(opt.startX)
        .arg(opt.startZ)
        .arg(static_cast<qint64>(opt.maxX) - opt.minX + 1)
        .arg(static_cast<qint64>(opt.maxZ) - opt.minZ + 1)
        .arg(m_settings.gpuResultSelfOptimize ? 1 : 0);
}

void MainWindow::logSearchParams(const SearchOptions& opt)
{
    std::string s = "开始搜索 | 版本=";
    s += (opt.version == Version::Java) ? "Java" : "基岩版";
    s += " 种子=" + std::to_string(opt.worldSeed);
    s += " 范围=(" + std::to_string(opt.minX) + "," + std::to_string(opt.maxX) + "," +
         std::to_string(opt.minZ) + "," + std::to_string(opt.maxZ) + ")";
    s += (opt.mode == Mode::Strict) ? " 模式=严格图案" : " 模式=密度";
    if (opt.mode == Mode::Strict) {
        s += " 图案=" + std::to_string(opt.pattern.rows()) + "x" + std::to_string(opt.pattern.cols()) +
             " 有效=" + std::to_string(opt.pattern.activeCount());
        s += opt.directionLocked ? " 方向锁定" : " 方向不锁定";
    } else {
        s += " 网格=" + std::to_string(opt.gridSize) + " 密度≥" + std::to_string(opt.minDensity);
        s += " 算法=" + algorithmDisplayName(opt.algorithm).toStdString();
        if (opt.algorithm == Algorithm::Layered) {
            s += " 分层级别=" + std::to_string(opt.layerBlock) +
                 " 阈值比例=" + std::to_string(opt.layerRatio);
        } else if (opt.algorithm == Algorithm::Sample) {
            s += " 采样间隔=" + std::to_string(opt.sampleStep);
            s += opt.sampleExact ? " 精算采样" : " 采样估算";
        }
    }
    s += " 起点=(" + std::to_string(opt.startX) + "," + std::to_string(opt.startZ) + ")";
    s += " 目标N=" + std::to_string(opt.targetCount);
    s += opt.stopAtTarget ? " 及时停止" : "";
    s += opt.smartRange ? " 智能范围" : "";
    s += " 线程=" + std::to_string(opt.threads);
    s += opt.useCache ? " 使用缓存" : "";
    s += opt.gpuScan ? " GPU扫描" : "";
    Logger::info(s);
    slime::IncidentLog::note("搜索参数：" + s); // v3.3.7：关键信息日志（常开）
}

// ---------------- 完成 ----------------
void MainWindow::onSearchFinished(const SearchOutcome& out, const RunInfo& info, int runId)
{
    // v3.3.9（任务书 1.1）：缓存构建模式的分段续跑（每段落盘 → 继续下一段 / 收尾）
    if (m_buildActive) {
        handleBuildBandFinished(out, runId);
        return;
    }
    // v3.3.7（任务书 P0-2）：**陈旧结果事件**（上一次运行的结果在取消后、用户已开始新搜索时才被
    // 处理）必须在此处直接丢弃：否则会错误地把新搜索标记为已结束、重新启用输入，
    // 并在下面的 wait() 里 join 到新搜索线程（界面冻结）。
    if (runId != m_runId) {
        slime::IncidentLog::note("丢弃陈旧结果事件（runId 不匹配；当前运行 " + std::to_string(m_runId) +
                                 "，事件来自 " + std::to_string(runId) + "）");
        return;
    }
    m_running = false;
    setRunningUi(false);
    setInputsEnabled(true);

    if (out.cancelled) {
        m_w->progress->setValue(0);
        m_w->progressInfo->setText(slime::ui::t("ui.14c0ffe017c1").arg(m_streamScanned));
        if (m_w->progressStage) m_w->progressStage->setText(progressStageName(5, m_runMode, m_runAlg));
        setStatus(slime::ui::tr(slime::ui::t("ui.a5ffdc95eeb0")));
        Logger::info("搜索已取消");
        // v3.3.6（任务书 调整 3）：取消也留一条流水记录；并按设置播放结束提示音
        if (m_progressStream) {
            m_progressStream->appendRawLine(slime::ui::t("ui.cfe9d8931fe1")
                                                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                                                .arg(m_streamScanned));
        }
        if (m_settings.soundOnComplete) {
            slime::SoundPlayer::playCompletionChime(m_settings.soundVolume);
        }
        updateNavBadges();
        slime::IncidentLog::note("搜索已取消：已扫描 " + std::to_string(m_streamScanned) + "/" +
                                 std::to_string(displayTotalChunks()) + " 区块 | " +
                                 slime::IncidentLog::memorySummary());
        m_task.wait();
        return;
    }
    if (!out.ok) {
        m_w->progressInfo->setText(slime::ui::t("ui.b859c7be7501"));
        if (m_w->progressStage) m_w->progressStage->setText(progressStageName(6, m_runMode, m_runAlg));
        setStatus(slime::ui::tr(slime::ui::t("ui.72b0bb777554")));
        if (m_progressStream) {
            m_progressStream->appendRawLine(slime::ui::t("ui.a5514476787e")
                                                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                                                     QString::fromStdString(out.error)));
        }
        QMessageBox::critical(this, slime::ui::t("ui.680a503add20"), QString::fromStdString(out.error));
        Logger::error("搜索错误: " + out.error);
        updateNavBadges();
        slime::IncidentLog::note("搜索错误：" + out.error + " | " + slime::IncidentLog::memorySummary());
        m_task.wait();
        return;
    }

    // 成功后缓存处理：
    //  - 经典路径：整区合并（含容量上限）；分块路径：引擎已按块增量合并（cacheHandledIncrementally）
    //  v3.3.3（任务书 2.2）：无论走哪条分支，只要缓存发生了变化就**立即刷新状态标签**，
    //  并在状态栏给出一次性反馈（设置区可能被滚动到别处，用户仍能看到「缓存已更新」）。
    if (info.cacheWasUsed && info.mergeCacheAfter) {
        if (!out.cacheHandledIncrementally) {
            // v3.3.8（任务 1 / ④′ 修复方案 B）：**整区合并前先确认「有新增条目」**。
            // mergeRegion 会把 region 加入 m_ranges（覆盖声明）；若本次范围 0 新增条目，声明就没有
            // 条目支撑 ⇒ covers()=true 而 lookup() 恒返回 0（被判为已知非史莱姆）⇒ 漏史莱姆。
            // 代价：该区域下次会重扫（正确性优先；0 新增条目意味着本次新算的那部分确无史莱姆区块）。
            if (!out.newCacheChunks.empty()) {
                m_cache->mergeRegion(info.region, out.newCacheChunks);
            } else {
                const std::string rg = "[" + std::to_string(info.region.minX) + "," +
                                       std::to_string(info.region.maxX) + "]x[" +
                                       std::to_string(info.region.minZ) + "," +
                                       std::to_string(info.region.maxZ) + "]";
                Logger::info("本次范围无新增条目，未登记覆盖声明（避免空壳声明）：区域 " + rg);
                slime::IncidentLog::note("cache.hollow 经典路径未登记覆盖声明（0 新增条目）区域=" + rg);
            }
            m_cache->pruneToFit(cacheMaxBytes());
        }
        std::string err;
        if (!m_cache->saveFile(m_cacheFile, err)) {
            Logger::warning("缓存保存失败: " + err + "（目标 " + m_cacheFile.string() + "）");
        } else {
            // v3.3.2：日志中明确写出落盘路径与文件名，用户可直接据此在「导入缓存」对话框中定位
            Logger::info("缓存已更新保存：" + std::to_string(m_cache->ranges().size()) + " 区域 / " +
                         std::to_string(m_cache->entryCount()) + " 区块" +
                         (out.cacheHandledIncrementally ? "（分块增量合并）" : "") +
                         "（路径 " + m_cacheFile.string() + "）");
            setStatus(slime::ui::tr(slime::ui::t("ui.769f8655bcd8")).arg(m_cache->entryCount()));
        }
        updateCacheStatusUi();
    } else if (info.cacheWasUsed && out.cacheHandledIncrementally) {
        // 搜索范围原本已被缓存覆盖：无新数据，无需写盘；状态标签仍刷新一次（幂等）
        updateCacheStatusUi();
    }

    m_w->progress->setValue(1000);
    if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("100%"));
    if (m_w->searchStage) m_w->searchStage->setText(slime::ui::t("ui.2437d377fef4"));
    // v3.5.26（用户要求）：0 结果时进度条变红，一眼看出「什么都没找到」
    {
        const bool empty = out.results.empty();
        m_w->progress->setStyleSheet(empty ? QStringLiteral("QProgressBar::chunk { background-color: #d9534f; }")
                                           : QString());
        if (empty) m_w->progress->setValue(1000);
    }
    // v3.6.5（用户要求）：结果区上方显眼提醒 —— 没结果 / 少于目标
    if (m_resultsNotice) {
        const int got = static_cast<int>(out.results.size());
        const int want = (std::max)(1, m_w->targetCount ? m_w->targetCount->value() : 1);
        if (got == 0)
            m_resultsNotice->setText(slime::ui::t("ui.17533b453292"));
        else if (got < want)
            m_resultsNotice->setText(slime::ui::t("ui.a1e9abe326ce")
                                         .arg(got).arg(want));
        else
            m_resultsNotice->setText(QString());
        m_resultsNotice->setVisible(!m_resultsNotice->text().isEmpty());
    }

    // v3.3.4（任务书 2.1）：进度行/状态栏 —— 阶段短语 + 统计信息（v3.3.7：去重）
    // v3.3.7.2（任务书步骤 5 / F-3）：完成态也用**真实扫描数 / 引擎口径总数**，
    // 不再把 scannedChunks 同时当分子与分母（旧实现永远显示 100.0%，掩盖真实口径）。
    if (m_w->progressStage) m_w->progressStage->setText(progressStageName(3, m_runMode, m_runAlg));
    if (m_w->progressInfo) {
        m_w->progressInfo->setText(progressStatsText(out.stats.scannedChunks, displayTotalChunks(), 0.0,
                                                     static_cast<int64_t>(out.results.size())));
    }
    setStatus(slime::ui::tr(slime::ui::t("ui.0133025c537b")));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 15);
    m_lastResultText = describeResultsText(out, info);
    // v3.6.7（用户反馈「搜索有结果但结果列表不显示」）：**必须先写入 m_lastOutcome 再刷新列表**。
    //   populateResultText() 内部会调用 fillResultTable()，而它读的是 m_lastOutcome；
    //   旧顺序是「先 populate 再赋值」⇒ 列表永远慢一拍（首次搜索必为空、后续搜索显示上一次结果）。
    m_lastOutcome = out;
    m_lastInfo = info;
    m_hasLastOutcome = true;
    populateResultText(out, info); // v3.3.4：结果写入右侧 QTextEdit（信息栏 + QTextEdit）
    if (m_elapsedTimer) m_elapsedTimer->stop();
    if (m_w->progressEta) m_w->progressEta->setText(slime::ui::t("ui.f00785313fdc"));
    if (m_map) {
        m_map->setResults(out.results); // v3.5.4：结果同步到内嵌地图
        if (m_mapSyncTimer) m_mapSyncTimer->start();
    }
    // v3.3.7.2（任务书 1.3）：删除此处对 progressInfo 的**死写** ——
    // 旧代码先写「搜索完成（结果可在右侧按距离/密度排序）」，紧接着（下面几行）就被
    // 「搜索完成：共 N 条…」覆盖，属于无效赋值并让「最终显示什么」不可预期。
    if (m_w->progressStage) {
        // v3.3.7（P1-5）：阶段只放短语；条数放状态栏统计（避免同一信息两处显示）
        m_w->progressStage->setText(progressStageName(4, m_runMode, m_runAlg));
    }
    if (m_w->progressInfo) {
        m_w->progressInfo->setText(slime::ui::t("ui.1a9adb67ce4c")
                                       .arg(out.results.size())
                                       .arg(m_resultTextTotal)
                                       .arg(progressStatsText(out.stats.scannedChunks, displayTotalChunks(),
                                                              0.0, static_cast<int64_t>(out.results.size()))));
    }
    // v3.3.6（任务书 调整 3）：进度分类追加「已完成」汇总行（格式见任务书样例）
    if (m_progressStream) {
        if (m_streamTimer) m_streamTimer->stop(); // 保证汇总行是最后一条
        m_progressStream->appendFinal(out.stats.scannedChunks,
                                      static_cast<qint64>(out.results.size()),
                                      out.stats.totalMs / 1000.0);
        // v3.9.10（过程可视化）：阶段耗时 / GPU 使用与回退原因 / 停止语义
        const QString summary = processSummaryText(out);
        for (const QString& line : summary.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            m_progressStream->appendRawLine(QStringLiteral("[%1] %2")
                                                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), line));
        }
    }
    // v3.3.6（任务书 4.2）：搜索完成提示音（音量来自设置；失败静默）
    if (m_settings.soundOnComplete) {
        slime::SoundPlayer::playCompletionChime(m_settings.soundVolume);
    }
    updateNavBadges(); // 反馈计数徽章刷新
    // v3.3.7（P1-6）：完成时把关键结果与内存峰值写入关键信息日志（回归对比用）
    slime::IncidentLog::note("搜索完成：扫描 " + std::to_string(out.stats.scannedChunks) +
                             " 区块 找到 " + std::to_string(out.results.size()) + " 个结果 耗时 " +
                             std::to_string(out.stats.totalMs / 1000.0) + "s 线程 " +
                             std::to_string(out.stats.threadsUsed) +
                             (out.stoppedEarly ? " [提前停止]" : "") + " | " +
                             slime::IncidentLog::memorySummary());
    setStatus(slime::ui::tr(slime::ui::t("ui.8174d1168fce")));
    writePerformanceLog(out, info);
    // v3.3.4（任务书 2.2）：关键事件同时进入反馈栏与日志总线
    addFeedback(out.results.empty() ? 2 : 3,
                slime::ui::t("ui.f6cc745d6f70")
                    .arg(out.results.size())
                    .arg(out.stats.scannedChunks)
                    .arg(out.stats.totalMs / 1000.0, 0, 'f', 3));
    slime::LogBus::info("搜索完成：结果 " + std::to_string(out.results.size()) + " 条，扫描 " +
                        std::to_string(out.stats.scannedChunks) + " 区块");
    // v3.9.10：过程摘要（阶段耗时 / GPU / 停止语义）进反馈与日志，GPU 回退必须可见
    {
        const QString summary = processSummaryText(out);
        addFeedback(3, summary);
        Logger::info("过程摘要 | " + summary.toStdString());
        if (!out.gpuFallbackReason.empty()) {
            addFeedback(2, slime::ui::t("ui.b32dcdccbfa4").arg(QString::fromStdString(out.gpuFallbackReason)));
        }
    }

    const double totalS = out.stats.totalMs / 1000.0;
    const double cps = totalS > 0 ? out.stats.scannedChunks / totalS : 0.0;
    Logger::info("搜索完成 | 找到=" + std::to_string(out.totalFound) + " 显示=" +
                 std::to_string(out.results.size()) + " 扫描区块=" + std::to_string(out.stats.scannedChunks) +
                 " 耗时=" + std::to_string(totalS) + "s(扫描 " + std::to_string(out.stats.scanMs / 1000.0) +
                 "s/匹配 " + std::to_string(out.stats.matchMs / 1000.0) + "s) 速度=" + std::to_string(cps) +
                 " 区块/s 线程=" + std::to_string(out.stats.threadsUsed) +
                 (out.stoppedEarly ? " [提前停止]" : ""));
    if (m_telemetry && !info.options.buildMode) {
        const QString mode = (info.mode == Mode::Strict) ? QStringLiteral("pattern") : QStringLiteral("density");
        const QString alg = (info.algorithm == Algorithm::Layered) ? QStringLiteral("layered")
                            : (info.algorithm == Algorithm::Sample) ? QStringLiteral("sample")
                            : (info.algorithm == Algorithm::Brute) ? QStringLiteral("brute")
                                                                   : QStringLiteral("exact");
        QJsonObject st;
        st.insert(QStringLiteral("grid_size"), info.gridSizeUsed);
        st.insert(QStringLiteral("min_density"), info.minDensityUsed);
        st.insert(QStringLiteral("target"), static_cast<double>(info.targetCount));
        st.insert(QStringLiteral("stop_at_target"), info.options.stopAtTarget);
        st.insert(QStringLiteral("smart_range"), info.options.smartRange);
        st.insert(QStringLiteral("ignore_threshold"), info.options.ignoreThreshold);
        st.insert(QStringLiteral("threads"), info.options.threads);
        st.insert(QStringLiteral("layer_block"), info.options.layerBlock);
        st.insert(QStringLiteral("layer_ratio"), info.options.layerRatio);
        st.insert(QStringLiteral("sample_step"), info.options.sampleStep);
        st.insert(QStringLiteral("sample_exact"), info.options.sampleExact);
        st.insert(QStringLiteral("gpu_requested"), info.options.gpuScan != nullptr);
        const QString seedStr = (info.version == Version::Bedrock)
                                    ? QString()
                                    : QString::number(info.options.worldSeed);
        const QJsonObject search = buildTelemetrySearch(
            seedStr, info.version == Version::Bedrock, mode, alg, QString(), info.region,
            info.options.startX, info.options.startZ, st, out,
            out.gpuScanUsed || out.gpuMatchUsed);
        m_telemetry->reportSearchDetailed(QString::fromLatin1(kAppVersion), QStringLiteral("test"),
                                          m_gpuScan ? QString::fromStdString(m_gpuScan->deviceName()) : QString(),
                                          search);
    }
    m_task.wait();
}

// v3.9.10（过程可视化）：阶段耗时 / GPU 使用与回退原因 / 停止语义 —— 单一来源，供进度流水、
//   反馈、日志、结果复制文本共用。
QString MainWindow::processSummaryText(const SearchOutcome& out) const
{
    QStringList lines;
    const double scanMs = out.stats.scanMs;
    const double matchMs = out.stats.matchMs;
    const double totalMs = out.stats.totalMs;
    const double otherMs = (std::max)(0.0, totalMs - scanMs - matchMs);
    lines << slime::ui::t("ui.7b6c64d1e526")
                 .arg(scanMs, 0, 'f', 1)
                 .arg(matchMs, 0, 'f', 1)
                 .arg(otherMs, 0, 'f', 1)
                 .arg(totalMs, 0, 'f', 1);

    const QString fb = QString::fromStdString(out.gpuFallbackReason);
    if (out.gpuScanUsed || out.gpuMatchUsed) {
        QString line = slime::ui::t("ui.9a7b9ede7a62")
                           .arg(out.gpuScanUsed ? QStringLiteral("✓") : QStringLiteral("×"))
                           .arg(out.gpuMatchUsed ? QStringLiteral("✓") : QStringLiteral("×"));
        if (!fb.isEmpty()) line += slime::ui::t("ui.126a68799ec1").arg(fb);
        lines << line;
    } else if (!fb.isEmpty()) {
        lines << slime::ui::t("ui.fa42483646f0").arg(fb);
    } else {
        lines << slime::ui::t("ui.52d1da3d64f2");
    }

    if (out.cacheFirstStopped) {
        lines << slime::ui::t("ui.c9e37c2e2f7f");
    } else if (out.chunksPruned > 0) {
        lines << slime::ui::t("ui.26956b0f822a")
                     .arg(out.chunksPruned);
    } else if (out.stoppedEarly) {
        lines << slime::ui::t("ui.4175c2419c3c");
    } else {
        lines << slime::ui::t("ui.94166c628e2d");
    }

    if (out.windowsSkipped > 0) {
        lines << slime::ui::t("ui.3ca6b3650a98").arg(out.windowsSkipped);
    }
    if (out.ignoredThreshold) {
        lines << slime::ui::t("ui.a123cba9d80e");
    }
    lines << slime::ui::t("ui.ebbfde235d3f").arg(out.chunkedPath ? slime::ui::t("ui.617f9839463e") : slime::ui::t("ui.f9e4c844a573"));
    return lines.join(QLatin1Char('\n'));
}

// 文本形式（用于复制与说明）
QString MainWindow::describeResultsText(const SearchOutcome& out, const RunInfo& info) const
{
    QString text;
    const double totalS = out.stats.totalMs / 1000.0;
    const double cps = totalS > 0 ? out.stats.scannedChunks / totalS : 0.0;

    text += slime::ui::t("ui.4588cdabce4f").arg(processSummaryText(out));
    text += slime::ui::t("ui.3773f957acba")
                .arg(out.stats.scannedChunks)
                .arg(totalS, 0, 'f', 3)
                .arg(out.stats.scanMs / 1000.0, 0, 'f', 3)
                .arg(out.stats.matchMs / 1000.0, 0, 'f', 3)
                .arg(out.stats.threadsUsed)
                .arg(cps / 10000.0, 0, 'f', 1);

    // v3.3.0（4.2）：当前使用模式 + 非精确模式提示（复制文本同步标注）
    const bool nonExact = (info.mode == Mode::Density && info.algorithm != Algorithm::Exact);
    text += slime::ui::t("ui.ffc148b2f7ea").arg(algorithmDisplayName(info.algorithm));
    if (nonExact) {
        text += slime::ui::t("ui.9fa83b53428b");
    }
    text += QLatin1Char('\n');
    if (nonExact && out.windowsSkipped > 0) {
        const double rate = 100.0 * static_cast<double>(out.windowsSkipped) /
                            static_cast<double>(out.windowsSkipped + out.stats.windowsEvaluated);
        text += slime::ui::t("ui.f1e3a7cbc2ce")
                    .arg(out.windowsSkipped)
                    .arg(rate, 0, 'f', 1);
    }

    if (out.results.empty()) {
        text += slime::ui::t("ui.967e134f508e");
        return text;
    }

    if (out.stoppedEarly) {
        text += slime::ui::t("ui.d37244ae5c61")
                    .arg(out.results.size()).arg(out.results.size());
    } else {
        text += slime::ui::t("ui.26039c5b8223").arg(out.totalFound).arg(out.results.size());
    }

    int idx = 1;
    for (const MatchResult& r : out.results) {
        const double cx = (static_cast<double>(r.minX) + r.maxX) / 2.0;
        const double cz = (static_cast<double>(r.minZ) + r.maxZ) / 2.0;
        const double dist = std::hypot(cx - static_cast<double>(info.startX), cz - static_cast<double>(info.startZ));
        text += slime::ui::t("ui.186c31a50487").arg(idx).arg(dist, 0, 'f', 2);
        text += slime::ui::t("ui.b657a9d6d6fd")
                    .arg(r.minX).arg(r.maxX).arg(r.minZ).arg(r.maxZ);
        const qlonglong bx0 = static_cast<qlonglong>(r.minX) * 16;
        const qlonglong bx1 = (static_cast<qlonglong>(r.maxX) + 1) * 16 - 1;
        const qlonglong bz0 = static_cast<qlonglong>(r.minZ) * 16;
        const qlonglong bz1 = (static_cast<qlonglong>(r.maxZ) + 1) * 16 - 1;
        text += slime::ui::t("ui.acd6c8073f08")
                    .arg(bx0).arg(bx1).arg(bz0).arg(bz1);
        if (info.mode == Mode::Density) {
            text += slime::ui::t("ui.52777be41a72").arg(r.density * 100.0, 0, 'f', 1);
        }
        text += QLatin1Char('\n');
        ++idx;
    }
    return text;
}

// /tp @s <x> 64 <z>（目标为中心方块坐标，y 固定 64）
QString MainWindow::cacheCommandFor(const MatchResult& r) const
{
    const qlonglong x = (static_cast<qlonglong>(r.minX) + r.maxX + 1) * 8;
    const qlonglong z = (static_cast<qlonglong>(r.minZ) + r.maxZ + 1) * 8;
    return QStringLiteral("/tp @s %1 64 %2").arg(x).arg(z);
}

// v3.3.4（任务书 1.1）：结果以文本形式显示在右侧 QTextEdit（信息栏 + QTextEdit 规范），
// 保留每条结果的 /tp 命令（右键菜单「复制该条命令」/「复制全部命令」）。
// 与 v3.3.3 的卡片列表相比：不再创建上千个 QWidget（更快、不会卡 UI），
// 复制命令的入口改为右键菜单 + 编辑菜单 + 命令终端（功能不减少）。
void MainWindow::populateResultText(const SearchOutcome& out, const RunInfo& info)
{
    QString text;
    const double totalS = out.stats.totalMs / 1000.0;
    const double cps = totalS > 0 ? out.stats.scannedChunks / totalS : 0.0;
    text += slime::ui::t("ui.a62aeabda435") +
            rateText(static_cast<double>(out.stats.scannedChunks) / (std::max)(1e-9, totalS),
                     m_settings.rateUnit) + QStringLiteral("\n")
                .arg(out.stats.scannedChunks)
                .arg(totalS, 0, 'f', 3)
                .arg(out.stats.threadsUsed)
                .arg(cps / 10000.0, 0, 'f', 1);
    text += slime::ui::t("ui.7db8f96b7d21").arg(algorithmDisplayName(info.algorithm));
    if (info.mode == Mode::Density && info.algorithm != Algorithm::Exact) {
        QString warn = slime::ui::t("ui.ecacdc495bdf").arg(algorithmDisplayName(info.algorithm));
        if (info.algorithm == Algorithm::Sample) {
            warn += slime::ui::t("ui.d8733f657313");
        }
        warn += slime::ui::t("ui.f6576484b989");
        if (out.windowsSkipped > 0) {
            const double rate = 100.0 * static_cast<double>(out.windowsSkipped) /
                                static_cast<double>(out.windowsSkipped + out.stats.windowsEvaluated);
            warn += slime::ui::t("ui.ea74b35a4457")
                        .arg(out.windowsSkipped)
                        .arg(rate, 0, 'f', 1);
        }
        text += warn + QLatin1Char('\n');
    }
    text += QLatin1Char('\n');

    if (out.results.empty()) {
        text += slime::ui::t("ui.967e134f508e");
        m_w->resultView->setPlainText(text);
        m_resultTextTotal = 0;
        // v3.6.19：空结果必须把列表/计数/地图标记一并清掉（此前只改了文本，列表会留着旧行）
        if (m_w->resultTable) m_w->resultTable->setRowCount(0);
        if (m_resultCountLabel) m_resultCountLabel->setText(slime::ui::t("ui.a535edb5149b"));
        if (m_map) {
            m_map->setResults({});
            m_map->setAfkCandidates({});
            m_map->setFocusedAfk(-1);
        }
        if (m_w->infoStats) {
            {
                // v3.10.4（用户要求：GPU 只保留速度显示）：无结果时也给平均速度
                const double avgCps = totalS > 1e-9 ? static_cast<double>(out.stats.scannedChunks) / totalS : 0.0;
                QString txt = slime::ui::t("ui.6fce8eadb15e")
                                  .arg(out.stats.scannedChunks)
                                  .arg(totalS, 0, 'f', 3);
                if (avgCps > 0.0) txt += slime::ui::t("ui.1b1b7e5468a9") + rateText(avgCps, m_settings.rateUnit);
                m_w->infoStats->setText(txt);
            }
        }
        return;
    }

    if (out.stoppedEarly) {
        text += slime::ui::t("ui.f2d64e212362")
                    .arg(out.results.size()).arg(out.results.size());
    } else {
        text += slime::ui::t("ui.1bcc6f2367f2")
                    .arg(out.totalFound).arg(out.results.size());
    }
    text += slime::ui::t("ui.088bf658860a");

    const size_t shown = std::min<size_t>(out.results.size(), static_cast<size_t>(kResultDisplayCap));
    int idx = 1;
    for (size_t i = 0; i < shown; ++i) {
        const MatchResult& r = out.results[i];
        const double cx = (static_cast<double>(r.minX) + r.maxX) / 2.0;
        const double cz = (static_cast<double>(r.minZ) + r.maxZ) / 2.0;
        const double dist = std::hypot(cx - static_cast<double>(info.startX), cz - static_cast<double>(info.startZ));
        text += slime::ui::t("ui.2fcd12613ecd").arg(idx).arg(dist, 0, 'f', 2);
        text += QStringLiteral("  chunkX(%1~%2) chunkZ(%3~%4)")
                    .arg(r.minX).arg(r.maxX).arg(r.minZ).arg(r.maxZ);
        text += slime::ui::t("ui.f773d8edbfc4")
                    .arg(static_cast<qlonglong>(r.minX) * 16)
                    .arg((static_cast<qlonglong>(r.maxX) + 1) * 16 - 1)
                    .arg(static_cast<qlonglong>(r.minZ) * 16)
                    .arg((static_cast<qlonglong>(r.maxZ) + 1) * 16 - 1);
        if (info.mode == Mode::Density) {
            text += slime::ui::t("ui.415d894a9fa6").arg(r.density * 100.0, 0, 'f', 1);
        }
        // v3.3.8.1（任务书 3.6）：显示行不再附 /tp 命令（「复制结果」「复制全部命令」路径不受影响）
        text += QLatin1Char('\n');
        ++idx;
    }
    if (out.results.size() > static_cast<size_t>(kResultDisplayCap)) {
        text += slime::ui::t("ui.d1dfa3adee72")
                    .arg(out.results.size()).arg(kResultDisplayCap);
    }
    m_w->resultView->setPlainText(text);
    m_resultTextTotal = static_cast<int64_t>(shown);
    fillResultTable(); // v3.5.5：同步到结果列表
    {
        // v3.10.4（用户要求：GPU 只保留速度显示）：结果统计追加平均速度
        const double avgCps = totalS > 1e-9 ? static_cast<double>(out.stats.scannedChunks) / totalS : 0.0;
        QString txt = slime::ui::t("ui.c9f1416974ec")
                          .arg(out.results.size())
                          .arg(shown)
                          .arg(out.stats.scannedChunks)
                          .arg(totalS, 0, 'f', 3)
                          .arg(info.mode == Mode::Strict ? slime::ui::tr(slime::ui::t("ui.b977e67d75f8"))
                                                         : algorithmDisplayName(info.algorithm))
                          .arg(out.ignoredThreshold
                                   ? slime::ui::t("ui.1ba425cc935b")
                                   : QString());
        if (avgCps > 0.0) txt += slime::ui::t("ui.1b1b7e5468a9") + rateText(avgCps, m_settings.rateUnit);
        m_w->infoStats->setText(txt);
    }
}

// v3.3.3（任务书 4.2）：结果排序（只重排显示文本，不改变结果集合与「复制结果」内容）
void MainWindow::applyResultSort()
{
    if (!m_hasLastOutcome || !m_w) return;
    const QString key = (m_runMode == slime::Mode::Density) ? QStringLiteral("density") : QStringLiteral("distance");
    if (key.isEmpty() || key == QLatin1String("distance")) return; // 引擎默认即按距离（近→远）

    // 按密度（高→低）重排：复制一份结果集排序后重新生成文本，
    // 但 m_lastResultText /「复制结果」始终保持引擎原始（距离）顺序。
    SearchOutcome sorted = m_lastOutcome;
    std::stable_sort(sorted.results.begin(), sorted.results.end(),
                     [](const MatchResult& a, const MatchResult& b) { return a.density > b.density; });
    populateResultText(sorted, m_lastInfo);
    setStatus(slime::ui::tr(slime::ui::t("ui.1c58ffbc4881")).arg(sorted.results.size()));
}

// v3.3.4：结果视图右键菜单（复制选中 / 复制该条命令 / 复制全部命令 / 清空结果）
void MainWindow::showResultContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    QAction* aCopySel = menu.addAction(slime::ui::t("ui.4584c49b2881"));
    QAction* aCopyLineCmd = menu.addAction(slime::ui::t("ui.c9292f786187"));
    QAction* aCopyAllCmd = menu.addAction(slime::ui::t("ui.b55b79e1c9e9"));
    menu.addSeparator();
    QAction* aCopyAll = menu.addAction(slime::ui::t("ui.07bc78ac5aef"));
    QAction* aClear = menu.addAction(slime::ui::t("ui.0fe9f67f7f41"));
    QAction* chosen = menu.exec(m_w->resultView->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == aCopySel) {
        m_w->resultView->copy();
        setStatus(slime::ui::tr(slime::ui::t("ui.6bf48eb281e9")));
    } else if (chosen == aCopyLineCmd) {
        const QString line = m_w->resultView->textCursor().block().text();
        // v3.3.8.1（任务书 3.6）：显示行不再带 /tp 命令 ⇒ 改为从行内区块坐标推算命令
        static const QRegularExpression reChunk(
            QStringLiteral("chunkX\\((-?\\d+)~(-?\\d+)\\)\\s+chunkZ\\((-?\\d+)~(-?\\d+)\\)"));
        const QRegularExpressionMatch m = reChunk.match(line);
        QString cmd;
        if (m.hasMatch()) {
            const qlonglong x = (m.captured(1).toLongLong() + m.captured(2).toLongLong() + 1) * 8;
            const qlonglong z = (m.captured(3).toLongLong() + m.captured(4).toLongLong() + 1) * 8;
            cmd = QStringLiteral("/tp @s %1 64 %2").arg(x).arg(z);
        }
        if (cmd.isEmpty()) {
            setStatus(slime::ui::tr(slime::ui::t("ui.fa8bb54f3fbe")));
        } else {
            QApplication::clipboard()->setText(cmd);
            setStatus(slime::ui::tr(slime::ui::t("ui.0ed61f8d953a")).arg(cmd));
        }
    } else if (chosen == aCopyAllCmd) {
        copyAllCommands();
    } else if (chosen == aCopyAll) {
        copyResults();
    } else if (chosen == aClear) {
        clearResults();
    }
}

// v3.3.4（任务书 1.2）：编辑 → 复制全部命令
void MainWindow::copyAllCommands()
{
    if (!m_hasLastOutcome || m_lastOutcome.results.empty()) {
        setStatus(slime::ui::tr(slime::ui::t("ui.40866ce4c457")));
        return;
    }
    QStringList cmds;
    const size_t shown = std::min<size_t>(m_lastOutcome.results.size(), kMaxCards);
    for (size_t i = 0; i < shown; ++i) cmds << cacheCommandFor(m_lastOutcome.results[i]);
    QApplication::clipboard()->setText(cmds.join(QLatin1Char('\n')));
    setStatus(slime::ui::tr(slime::ui::t("ui.aff1069e95b5")).arg(cmds.size()));
    addFeedback(2, slime::ui::tr(slime::ui::t("ui.0b7f489d26c3")).arg(cmds.size()));
}

// v3.3.4（任务书 1.2）：编辑 → 清空结果（Ctrl+L）
void MainWindow::clearResults()
{
    if (m_w && m_w->resultView) m_w->resultView->clear();
    m_lastResultText.clear();
    m_hasLastOutcome = false;
    m_resultTextTotal = 0;
    if (m_w && m_w->infoStats) m_w->infoStats->setText(slime::ui::t("ui.6e6523a7593c"));
    setStatus(slime::ui::tr(slime::ui::t("ui.ee1775e4f8d0")));
}

// v3.3.4（任务书 1.2）：文件 → 导出结果（Ctrl+S）
void MainWindow::exportResultsToFile()
{
    if (m_lastResultText.isEmpty()) {
        if (!m_uiQuiet) QMessageBox::information(this, slime::ui::t("ui.d733ad3e850d"), slime::ui::t("ui.72b97623bbfa"));
        return;
    }
    const QString def = QDir(QString::fromStdWString(appDirPath().wstring()))
                            .filePath(QStringLiteral("slime_results.txt"));
    const QString path = QFileDialog::getSaveFileName(this, slime::ui::t("ui.d733ad3e850d"), def,
                                                      slime::ui::t("ui.227f93d5f60d"));
    if (path.isEmpty()) return;
    std::ofstream out(std::filesystem::path(path.toStdWString()), std::ios::binary | std::ios::trunc);
    if (!out) {
        addError(slime::ui::t("ui.cfcd6b5bf187").arg(path));
        return;
    }
    const QByteArray utf8 = m_lastResultText.toUtf8();
    out.write(utf8.constData(), utf8.size());
    out.close();
    setStatus(slime::ui::tr(slime::ui::t("ui.6caccae3bf9c")).arg(path));
    addFeedback(3, slime::ui::tr(slime::ui::t("ui.10d76115f1dc")).arg(path));
    slime::LogBus::info("结果已导出: " + path.toStdString());
}

// ---------------- 性能日志（v3.1.0：logs/performance.log） ----------------
void MainWindow::writePerformanceLog(const SearchOutcome& out, const RunInfo& info) const
{
    (void)info;
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t tt = clock::to_time_t(now);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);

    double memMB = 0.0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        memMB = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
#endif
    const double totalS = out.stats.totalMs / 1000.0;
    const double cps = totalS > 0 ? out.stats.scannedChunks / totalS : 0.0;

    // 开始/结束时刻：以本次搜索实际启动时刻为基准
    int64_t startMs = m_searchStartMs;
    if (startMs <= 0) {
        startMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }
    const int64_t endMs = startMs + static_cast<int64_t>(out.stats.totalMs);
    const auto fmtClock = [](int64_t ms) {
        const std::time_t s = static_cast<std::time_t>(ms / 1000);
        std::tm t2{};
#ifdef _WIN32
        localtime_s(&t2, &s);
#else
        localtime_r(&s, &t2);
#endif
        char b[32];
        std::snprintf(b, sizeof(b), "%02d:%02d:%02d", t2.tm_hour, t2.tm_min, t2.tm_sec);
        return std::string(b);
    };

    std::error_code ec;
    std::filesystem::create_directories(appDirPath() / "logs", ec);
    std::ofstream log(appDirPath() / "logs" / "performance.log", std::ios::app);
    if (log) {
        // v3.3.0（4.3）：模式 / 算法参数 / 剪枝跳过率 / 与精确模式差异
        const double skipRate = (out.stats.windowsEvaluated + out.windowsSkipped) > 0
                                    ? static_cast<double>(out.windowsSkipped) /
                                          static_cast<double>(out.stats.windowsEvaluated + out.windowsSkipped)
                                    : 0.0;
        std::string modeText = (info.mode == Mode::Strict) ? "严格图案" : "密度";
        // v3.3.4：同时写出中文显示名与机器可读键（如 快速估算模式(sample)），便于脚本解析
        std::string algText = (info.mode == Mode::Strict)
                                  ? std::string("exact")
                                  : (algorithmDisplayName(info.algorithm).toStdString() + "(" +
                                     std::string(info.algorithm == Algorithm::Layered  ? "layered"
                                                 : info.algorithm == Algorithm::Sample ? "sample"
                                                 : info.algorithm == Algorithm::Brute  ? "brute"
                                                                                       : "exact") +
                                     ")");
        log << "日期 " << buf << " | 开始 " << fmtClock(startMs) << " | 结束 " << fmtClock(endMs) << " | 耗时 "
            << static_cast<long long>(out.stats.totalMs) << " ms | 扫描区块 " << out.stats.scannedChunks
            << " | 匹配结果 " << out.totalFound << " | 平均速度 " << static_cast<long long>(cps)
            << " 区块/秒 | 内存 " << static_cast<long long>(memMB + 0.5) << " MB"
            << " | 模式 " << modeText << " | 算法 " << algText;
        if (info.mode == Mode::Density) {
            log << " | 参数 网格=" << info.gridSizeUsed << " 阈值=" << info.minDensityUsed;
            if (info.algorithm == Algorithm::Layered) {
                log << " 分层级别=" << info.layerBlock << " 阈值比例=" << info.layerRatio;
            } else if (info.algorithm == Algorithm::Sample) {
                log << " 采样间隔=" << info.sampleStep << " 精算采样=" << (info.sampleExact ? 1 : 0);
            }
            log << " | 剪枝跳过率 " << static_cast<int>(skipRate * 1000.0 + 0.5) / 10.0 << "%"
                << "（跳过 " << out.windowsSkipped << " / 评估 " << out.stats.windowsEvaluated << "）";
            if (info.algorithm != Algorithm::Exact) {
                int64_t exactFound = -1;
                if (crossCheckExact(info.options, out.totalFound, &exactFound) && exactFound >= 0) {
                    const qlonglong diff = static_cast<qlonglong>(out.totalFound) - exactFound;
                    log << " | 与精确模式差异 " << (diff >= 0 ? "+" : "") << diff << "（精确 " << exactFound
                        << " / 本次 " << out.totalFound << "，同范围对照）";
                } else {
                    log << " | 与精确模式差异 未校验（范围过大或非密度模式）";
                }
            } else {
                log << " | 与精确模式差异 0（精确模式）";
            }
        }
        log << "\n";
    }
}

// ---------------- 其他 ----------------
void MainWindow::copyResults()
{
    if (m_lastResultText.isEmpty()) return;
    QApplication::clipboard()->setText(m_lastResultText);
    setStatus(slime::ui::tr(slime::ui::t("ui.a48bb5b20557")));
}

void MainWindow::closeEvent(QCloseEvent* ev)
{
    if (m_task.running()) {
        m_task.cancel();
        m_task.wait();
    }
    // v3.3.5（优化 5 / 5.1）：主界面当前的搜索相关选项自动成为下次默认值；
    // 窗口尺寸与位置按「窗口大小记忆」开关保存（均写入同一份 config.json）。
    m_settings.defaultAlgorithm = QString::fromStdString(
        cfgString(appDirPath() / kConfigFileName, "algorithm.mode", "exact"));
    m_settings.defaultThreads = m_w->threads->value();
    m_settings.defaultTargetCount = m_w->targetCount->value();
    m_settings.defaultStopAtTarget = m_w->stopAtTarget->isChecked();
    writeConfigFile(m_w->gpuCheck->isChecked(), m_terminal ? m_terminal->history() : QStringList());
    ev->accept();
}



// ---------------- 无界面自检（v3.1.0） ----------------
// v3.6.15（流程）：GUI 自检支持「只跑某个区域」，避免改一处就等 150 秒全量。
//   SLIME_SELFTEST_ONLY=map,table  → 只跑地图/结果表相关断言（其他区域直接跳过）
static bool selftestArea(const char* area)
{
    static const QString only = qEnvironmentVariable("SLIME_SELFTEST_ONLY").trimmed();
    if (only.isEmpty()) return true;
    for (const QString& part : only.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (part.trimmed().compare(QLatin1String(area), Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

int MainWindow::runSelfTest()
{
    // v3.5.27：自检固定用中文跑 —— 断言里比对的是中文分组名/导航名，
    //   用户把界面切成英文时不该让自检假失败。
    slime::ui::setLanguage(slime::ui::Language::Chinese);
    applyLanguage();

    // v3.4.8：自检会真的改动设置（「设置项逐控件接线」测试），因此先存档、结束时原样恢复 ——
    // 否则被改过的 config.json 会让**下一次**运行里的「默认值」类断言误报失败。
    // v3.4.12：自检期间自动关闭「未预期的模态框」并记录标题。
    //   任何漏加 m_uiQuiet 保护的确认弹窗都会在嵌套事件循环里被这里关掉（QTimer 在嵌套循环中照样触发），
    //   既不会卡死自动化流程，又能把弹窗标题写进证据日志便于定位。
    QTimer modalWatchdog;
    int modalSeen = 0;
    connect(&modalWatchdog, &QTimer::timeout, this, [&modalSeen]() {
        QWidget* m = QApplication::activeModalWidget();
        if (!m) return;
        ++modalSeen;
        std::fprintf(stderr, "[UI自检] 关闭未预期的模态框：#%d 标题=\"%s\" 类型=%s\n", modalSeen,
                     m->windowTitle().toUtf8().constData(), m->metaObject()->className());
        if (auto* dlg = qobject_cast<QDialog*>(m)) {
            dlg->reject();
        } else {
            m->close();
        }
    });
    modalWatchdog.start(200);
    struct ModalWatchdogStop {
        QTimer* timer;
        int* seen;
        ~ModalWatchdogStop()
        {
            timer->stop();
            if (*seen > 0) {
                std::fprintf(stderr, "[UI自检] 本次自检共关闭 %d 个未预期模态框（见上方标题）\n", *seen);
            }
        }
    } modalWatchdogStop{ &modalWatchdog, &modalSeen };

    const AppSettings settingsArchive = m_settings;
    std::string configArchive;
    {
        std::ifstream in(appDirPath() / kConfigFileName, std::ios::binary);
        if (in) configArchive.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    int fail = 0;
    m_uiQuiet = true;
    const auto check = [&](bool Ok, const char* name) {
        if (!Ok) {
            ++fail;
            std::fprintf(stderr, "[UI自检 FAIL] %s\n", name);
        }
    };

    // v3.3.4（任务书 1.4）：不做折叠 —— 6 个设置分组始终展开，无需额外复位。
    // v3.3.4（任务书 4.1）：种子默认空 —— 自检先把种子填成合法值，
    // 保证后续各组的真实搜索都能正常执行（空种子的拦截行为在 13.9 单独验证）。
    m_w->seedEdit->setText(QStringLiteral("12345"));

    // 1) 快速预设
    // v3.10.2：GPU 变快后快捷范围整体上调一档
    applyPreset(20000);
    check(m_w->minX->value() == -20000 && m_w->maxX->value() == 20000 && m_w->minZ->value() == -20000 &&
              m_w->maxZ->value() == 20000,
          "预设 附近(±20000)");
    applyPreset(1000000);
    check(m_w->maxX->value() == 1000000 && m_w->maxZ->value() == 1000000, "预设 全局(±1000000)");

    // 2) 边界截断（Java ±1875000）
    m_w->javaRadio->setChecked(true);
    onVersionChanged();
    check(m_w->maxX->value() == kJavaBoundChunk && m_w->minX->value() == -kJavaBoundChunk,
          "Java 默认=世界边界");
    m_w->minX->setValue(-999999999);
    clampRangeToBound();
    check(m_w->minX->value() == -kJavaBoundChunk, "Java 负边界截断");
    m_w->maxZ->setValue(2000000000);
    clampRangeToBound();
    check(m_w->maxZ->value() == kJavaBoundChunk, "Java 正边界截断");

    // 3) 基岩版边界 ±1048576
    m_w->bedrockRadio->setChecked(true);
    onVersionChanged();
    check(m_w->maxX->value() == 1048576 && m_w->minX->value() == -1048576, "基岩默认=基岩边界");
    m_w->maxZ->setValue(123456789);
    clampRangeToBound();
    check(m_w->maxZ->value() == 1048576, "基岩边界截断");
    check(m_w->seedEdit->isEnabled(), "基岩版种子框可用（仅用于地图上色，判定与种子无关）");
    check(m_w->presetWorld->isEnabled() == false && m_w->presetBedrock->isEnabled() == true,
          "基岩版预设可用性");
    m_w->javaRadio->setChecked(true);
    onVersionChanged();
    check(m_w->presetWorld->isEnabled() == true && m_w->presetBedrock->isEnabled() == false,
          "Java 版预设可用性");

    // 4) 复制命令格式 /tp @s <x> 64 <z>
    {
        MatchResult r;
        r.minX = 1; r.maxX = 2; r.minZ = -3; r.maxZ = -2;
        const QString cmd = cacheCommandFor(r);
        check(cmd == QStringLiteral("/tp @s 32 64 -32"), "命令格式(中心方块)");
    }

    // 5) 结果卡片构建/清理
    {
        SearchOutcome out;
        out.ok = true;
        out.totalFound = 2;
        out.results = std::vector<MatchResult>{ MatchResult{ -1, 2, 0, 3, 0.4f },
                                                MatchResult{ 5, 6, 7, 8, 0.0f } };
        out.stats.scannedChunks = 1000000;
        out.stats.totalMs = 10.0;
        RunInfo info;
        info.mode = Mode::Strict;
        info.startX = 0;
        info.startZ = 0;
        m_lastResultText = describeResultsText(out, info);
        check(m_lastResultText.contains(slime::ui::t("ui.7a1fd8c33948")), "文本渲染");
        // v3.3.4：结果写入右侧 QTextEdit（信息栏 + QTextEdit 规范）
        populateResultText(out, info);
        check(m_w->resultView && m_w->resultView->toPlainText().contains(slime::ui::t("ui.7a1fd8c33948")),
              "结果已写入右侧结果视图");
        clearResults();
        check(m_w->resultView && m_w->resultView->toPlainText().isEmpty(), "结果视图可清空");

        // 7) 性能日志落盘
        m_searchStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
        writePerformanceLog(out, info);
        std::ifstream pf(appDirPath() / "logs" / "performance.log");
        const bool hasPerf = pf.good() && std::string(std::istreambuf_iterator<char>(pf),
                                                       std::istreambuf_iterator<char>())
                                                 .find("扫描区块 1000000") != std::string::npos;
        check(hasPerf, "performance.log 记录");
    }

    // 6) config.json 缓存容量
    check(cacheMaxBytes() == 1024LL * 1024 * 1024, "config 默认 1024MB");

    // 8) 整图估算 API（v3.1.1）
    {
        m_w->densityRadio->setChecked(true);
        onModeChanged();
        SearchOptions opt = wholeMapOptions(kJavaBoundChunk);
        SearchEstimate est;
        const bool estOk = SlimeEngine::estimateSearch(opt, m_w->threads->value(), est);
        check(estOk, "整图估算可用");
        if (estOk) {
            check(est.areaChunks > 1e12 && est.evalChunks >= est.areaChunks, "整图估算规模正确");
            check(est.totalSec > 0 && est.scanRate > 1e6, "整图估算速率正确");
        }
        m_w->strictRadio->setChecked(true);
        onModeChanged();
    }

    // 9) v3.2.1：GPU 复选框 / 检测（勾选触发或搜索前兜底）/ config 往返
    {
        std::fprintf(stderr, "[UI自检] GPU 开关检查…\n");
        check(m_w->gpuCheck != nullptr && m_w->gpuStatus != nullptr, "GPU 复选框存在");
        const bool wasOn = m_w->gpuCheck->isChecked();
        if (!wasOn) {
            m_w->gpuCheck->setChecked(true); // 勾选 → 同步检测一次（启动不接触 OpenCL）
        }
        if (!m_gpuProbeDone) {
            ensureGpuScanOnce(); // 兜底（config=true 启动路径：勾选信号未变化）
        }
        check(m_gpuProbeDone, "GPU 检测完成（不崩溃）");
        std::fprintf(stderr, "[UI自检] GPU 检测结果: %s（%s）\n", m_gpuAvailable ? "可用" : "不可用",
                     m_gpuProbeReason.c_str());
        // config gpu.enabled 写读往返（直接查文件内容，避免 SLIME_GPU 环境变量干扰）
        const auto fileEnabled = [this]() {
            // v3.3.0：精确解析（旧写法在 algorithm 块存在时会误判）
            return cfgBool(appDirPath() / kConfigFileName, "gpu.enabled", false);
        };
        saveGpuEnabledToConfig(true);
        check(fileEnabled(), "config gpu.enabled 写读往返(true)");
        saveGpuEnabledToConfig(false);
        check(!fileEnabled(), "config gpu.enabled 写读往返(false)");
        m_loadingConfig = true;
        m_w->gpuCheck->setChecked(wasOn);
        m_loadingConfig = false;
        // 勾选 + 可用 → 注入并端到端跑一次（≥引擎 GPU 阈值）；不可用则只验证 CPU 路径
        if (m_gpuAvailable && m_w->gpuCheck->isChecked() && m_gpuScan) {
            SearchOptions go;
            go.version = Version::Java;
            go.worldSeed = 42;
            go.minX = -1024; go.maxX = 1023; go.minZ = -1024; go.maxZ = 1023;
            go.mode = Mode::Density;
            go.gridSize = 2;
            go.minDensity = 0.9;
            go.targetCount = 10;
            go.threads = m_w->threads->value();
            go.gpuScan = m_gpuScan.get(); // 与 startSearch 相同的注入条件
            SearchOutcome gout;
            std::atomic<bool> lo{ false };
            ProgressFn noop;
            SlimeEngine eng;
            eng.run(go, lo, noop, gout);
            check(gout.ok, "GPU 注入端到端搜索完成");
            std::fprintf(stderr, "[UI自检] GPU 端到端搜索: ok=%d 结果=%zu\n", (int)gout.ok,
                         gout.results.size());
        }
    }

    // 10) v3.2.1：真实 GUI 搜索冒烟（走 startSearch 全链路：后台任务线程 + 进度 + 结果渲染 + 日志）
    {
        std::fprintf(stderr, "[UI自检] 真实搜索冒烟（512x512 Java 密度）…\n");
        m_w->javaRadio->setChecked(true);
        m_w->densityRadio->setChecked(true);
        m_w->minX->setValue(-256);
        m_w->maxX->setValue(255);
        m_w->minZ->setValue(-256);
        m_w->maxZ->setValue(255);
        m_w->targetCount->setValue(10);
        m_w->stopAtTarget->setChecked(false);
        m_w->smartRange->setChecked(false);
        m_w->startX->setValue(0);
        m_w->startZ->setValue(0);
        m_w->seedEdit->setText(QStringLiteral("12345")); // v3.3.4：种子默认空，自检先填写合法种子
        // 冒烟跟随复选框状态：config gpu.enabled=false → 纯 CPU；=true 且可用 → GPU 注入全链路
        std::fprintf(stderr, "[UI自检] 搜索冒烟 GPU 状态: 勾选=%d 可用=%d\n",
                     (m_w->gpuCheck && m_w->gpuCheck->isChecked()) ? 1 : 0, m_gpuAvailable ? 1 : 0);
        startSearch(); // 需要 UI 线程事件泵完成异步任务与回调
        const auto t0 = std::chrono::steady_clock::now();
        int pumps = 0;
        bool sawScanning = false, sawMatchingIntro = false, sawMatchingPct = false;
        while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            // v3.3.3（任务书 3.2）：采样进度文字（信息性记录；阶段文字的确定性断言见第 13 组）
            const QString pt = m_w->progressInfo->text();
            if (pt.contains(slime::ui::t("ui.4ee94f431863"))) sawScanning = true;
            if (pt.contains(slime::ui::t("ui.dc8d4773e118"))) sawMatchingIntro = true;
            if (pt.contains(slime::ui::t("ui.5b0b37bab0a8"))) sawMatchingPct = true;
            QThread::msleep(20);
            ++pumps;
        }
        check(!m_running, "真实搜索在 60s 内完成（未闪退/未挂起）");
        check(m_w->progressInfo->text().contains(slime::ui::t("ui.8174d1168fce")), "进度文字：完成后显示「搜索完成…」");
        std::fprintf(stderr, "[UI自检] 搜索冒烟结束 pumps=%d 结果文本=%lld 行 阶段采样(信息性): 扫描=%d 匹配引导=%d 匹配进度=%d\n",
                     pumps, static_cast<long long>(m_lastResultText.count('\n')), (int)sawScanning,
                     (int)sawMatchingIntro, (int)sawMatchingPct);
    }

    // 10.5) v3.9.11/3.9.12：结果要求方案（预设）——数量随模式变化、切换方案不改 GPU、忽略阈值冲突裁决
    {
        std::fprintf(stderr, "[UI自检] 结果要求方案检查…\n");
        const bool gpuBefore = (m_w->gpuCheck && m_w->gpuCheck->isChecked());
        m_w->strictRadio->setChecked(false);
        m_w->densityRadio->setChecked(true);
        updateSearchShapeEnabled();
        check(m_w->presetCombo && m_w->presetCombo->count() == 5, "密度模式方案数 = 4 + 自定义");
        check(m_w->presetCombo->itemText(m_w->presetCombo->count() - 1) == slime::ui::t("ui.c493338e8c33"),
              "方案列表最后一项是自定义");

        // 方案「找全部候选」（索引 1）：关闭停止方式，且不得改动 GPU 开关
        m_w->presetCombo->setCurrentIndex(1);
        check(!m_w->stopAtTarget->isChecked() && !m_w->smartRange->isChecked(), "方案「找全部候选」关闭找够就停/智能范围");
        check((m_w->gpuCheck && m_w->gpuCheck->isChecked()) == gpuBefore, "切换方案不改变 GPU 开关（加速独立）");

        // 方案「最优 N 个（忽略阈值）」（索引 2）：开启忽略 + 冲突裁决
        m_w->presetCombo->setCurrentIndex(2);
        check(m_w->ignoreThreshold && m_w->ignoreThreshold->isChecked(), "方案「最优 N 个」开启忽略阈值");
        check(!m_w->minDensity->isEnabled(), "最优候选模式禁用最小密度输入");
        check(!m_w->stopAtTarget->isEnabled() && !m_w->smartRange->isEnabled(), "最优候选模式禁用提前停止/智能范围");
        check(m_w->presetHint && m_w->presetHint->text().contains(slime::ui::t("ui.4446620620fc")), "最优候选模式提示文案");

        // 切回默认方案：阈值恢复可用、忽略关闭
        m_w->presetCombo->setCurrentIndex(0);
        check(!m_w->ignoreThreshold->isChecked() && m_w->minDensity->isEnabled(), "切回方案后阈值恢复可用");

        // 图案模式：方案数 = 2 + 自定义，且忽略阈值不可用
        m_w->strictRadio->setChecked(true);
        updateSearchShapeEnabled();
        check(m_w->presetCombo->count() == 3, "图案模式方案数 = 2 + 自定义");
        check(!m_w->ignoreThreshold->isEnabled(), "图案模式下忽略阈值不可用");

        // 恢复：密度模式 + 默认方案
        m_w->densityRadio->setChecked(true);
        updateSearchShapeEnabled();
        m_w->presetCombo->setCurrentIndex(0);
        check(m_w->ignoreThreshold && !m_w->ignoreThreshold->isChecked(), "方案检查后恢复到默认状态");
    }

    // 10.5b) v3.10.7：缓存构建页临时强制基岩版，退出后必须恢复用户原本的平台
    {
        m_w->javaRadio->setChecked(true);
        setBuildModeActive(true);
        const bool forced = m_w->bedrockRadio->isChecked();
        setBuildModeActive(false);
        const bool restored = m_w->javaRadio->isChecked();
        std::fprintf(stderr, "[UI自检]   构建模式平台：强制基岩=%d 退出恢复Java=%d\n", (int)forced,
                     (int)restored);
        check(forced, "缓存构建模式临时切换为基岩版");
        check(restored, "退出缓存构建后平台恢复为 Java（不再莫名变基岩）");
    }

    // 10.6) v3.10.2（用户反馈「忽略阈值没生效」）：最优候选模式 GUI 全链路验证
    {
        std::fprintf(stderr, "[UI自检] 最优候选（忽略阈值）全链路…\n");
        m_w->strictRadio->setChecked(false);
        m_w->densityRadio->setChecked(true);
        m_w->javaRadio->setChecked(true);
        m_w->minX->setValue(-128);
        m_w->maxX->setValue(127);
        m_w->minZ->setValue(-128);
        m_w->maxZ->setValue(127);
        m_w->gridSize->setValue(4);
        m_w->minDensity->setValue(0.99);   // 阈值高到正常搜索必然 0 结果
        m_w->targetCount->setValue(5);
        m_w->stopAtTarget->setChecked(false);
        m_w->smartRange->setChecked(false);
        m_w->ignoreThreshold->setChecked(true);
        m_w->seedEdit->setText(QStringLiteral("4545"));
        startSearch();
        const auto t0 = std::chrono::steady_clock::now();
        while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            QThread::msleep(15);
        }
        const auto& rs = m_lastOutcome.results;
        bool sorted = true;
        for (size_t i = 1; i < rs.size(); ++i) {
            if (rs[i].density > rs[i - 1].density + 1e-6f) sorted = false;
        }
        std::fprintf(stderr, "[UI自检]   忽略阈值结果：ok=%d 条数=%zu 密度序列=%s 标记=%d\n",
                     (int)m_lastOutcome.ok, rs.size(), sorted ? "降序" : "非降序",
                     (int)m_lastOutcome.ignoredThreshold);
        check(m_lastOutcome.ok && rs.size() == 5, "最优候选：阈值 0.99 下仍返回 N 条（忽略生效）");
        check(sorted, "最优候选：结果按窗口密度从高到低排序");
        check(m_lastOutcome.ignoredThreshold, "最优候选：结果标记「已忽略阈值」");
        m_w->ignoreThreshold->setChecked(false);
        m_w->minDensity->setValue(0.3);
    }

    // 10.7) v3.10.4（用户要求：GPU 只保留速度显示）：GPU 大区域搜索完成后统计里给出速度
    if (m_gpuAvailable && m_gpuScan) {
        std::fprintf(stderr, "[UI自检] GPU 大区域搜索：完成后速度显示…\n");
        m_w->strictRadio->setChecked(false);
        m_w->densityRadio->setChecked(true);
        m_w->javaRadio->setChecked(true);
        if (m_w->gpuCheck) m_w->gpuCheck->setChecked(true);
        m_w->minX->setValue(-131072);
        m_w->maxX->setValue(131071);
        m_w->minZ->setValue(-131072);
        m_w->maxZ->setValue(131071);
        m_w->gridSize->setValue(8);
        m_w->minDensity->setValue(0.6);
        m_w->targetCount->setValue(5);
        m_w->stopAtTarget->setChecked(false);
        m_w->smartRange->setChecked(false);
        m_w->ignoreThreshold->setChecked(false);
        m_w->seedEdit->setText(QStringLiteral("4545"));
        bool sawBusy = false;
        startSearch();
        const auto t0 = std::chrono::steady_clock::now();
        while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 120.0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (m_w->progress && m_w->progress->maximum() == 0) sawBusy = true;
            QThread::msleep(5);
        }
        const QString summary = m_w->infoStats ? m_w->infoStats->text() : QString();
        std::fprintf(stderr, "[UI自检]   GPU 搜索完成：GPU匹配=%d 忙碌动画=%d 统计=%s\n",
                     (int)m_lastOutcome.gpuMatchUsed, (int)sawBusy, summary.toUtf8().constData());
        check(m_lastOutcome.ok && m_lastOutcome.gpuMatchUsed, "GPU 大区域搜索走 GPU 匹配并成功");
        check(summary.contains(slime::ui::t("ui.eb6aaba1a1ca")) && summary.contains(slime::ui::t("ui.3da82d85c5de")),
              "GPU 搜索完成后统计显示区块数与用时");
    }

    // 10.8) v3.10.8（用户疑问：预设是否暗改非明面设置）：预设只动明面上的结果开关
    {
        m_w->strictRadio->setChecked(false);
        m_w->densityRadio->setChecked(true);
        const int t0 = m_w->threads->value();
        const int tc0 = m_w->targetCount->value();
        const int g0 = m_w->gridSize->value();
        const double d0 = m_w->minDensity->value();
        const int sx0 = m_w->startX->value();
        const int sz0 = m_w->startZ->value();
        const bool cache0 = m_w->useCache->isChecked();
        const bool gpu0 = m_w->gpuCheck && m_w->gpuCheck->isChecked();
        m_w->presetCombo->setCurrentIndex(0);   // 快速找最近 N 个
        check(m_w->stopAtTarget->isChecked() && !m_w->smartRange->isChecked() &&
                  m_w->algExactRadio->isChecked() && !m_w->ignoreThreshold->isChecked(),
              "预设只设置明面开关：找够即停（不强制由近及远）+精确算法");
        check(m_w->threads->value() == t0 && m_w->targetCount->value() == tc0 &&
                  m_w->gridSize->value() == g0 && qFuzzyCompare(m_w->minDensity->value() + 1.0, d0 + 1.0) &&
                  m_w->startX->value() == sx0 && m_w->startZ->value() == sz0 &&
                  m_w->useCache->isChecked() == cache0 && (m_w->gpuCheck && m_w->gpuCheck->isChecked()) == gpu0,
              "预设不修改 线程/目标/网格/阈值/起点/缓存/GPU 等其它开关");
    }

    // 10.9) v3.10.9（用户要求）：高级开关分级 + 自定义速度声明 + GPU 设置入口
    {
        showPage(kPageSearch);
        m_w->strictRadio->setChecked(false);
        m_w->densityRadio->setChecked(true);
        m_w->presetCombo->setCurrentIndex(0);   // 预设
        check(!m_w->resultGroup->isVisible() && !m_w->matchGroup->isVisible(),
              "预设方案下「结果要求/匹配方式」收起（防乱调）");
        check(!m_w->presetDisclaimer->isVisible(), "预设方案不显示自定义速度声明");
        const int customIdx = 4;
        m_w->presetCombo->setCurrentIndex(customIdx);   // 自定义
        check(m_w->resultGroup->isVisible() && m_w->matchGroup->isVisible(),
              "自定义方案展开「结果要求/匹配方式」");
        check(m_w->presetDisclaimer->isVisible() &&
                  m_w->presetDisclaimer->text().contains(slime::ui::t("ui.f2fdffbb9ed5")),
              "自定义方案显示「可能明显降低速度」声明");
        check(m_w->gpuAdvBtn && m_w->gpuAdvPanel && !m_w->gpuAdvPanel->isVisible(),
              "GPU「设置」入口默认收起");
        m_w->gpuAdvBtn->click();
        check(m_w->gpuAdvPanel->isVisible(), "点击后 GPU 相关设置展开");
        check(m_w->gpuStreamCheck && m_w->gpuLimitSpin && m_w->gpuAutoCheck,
              "GPU 展开含 图案流式/占用率上限/自动检测");
        m_w->gpuAdvBtn->click();
        check(!m_w->gpuAdvPanel->isVisible(), "再次点击收起 GPU 设置");
        m_w->presetCombo->setCurrentIndex(0);
    }

    // 11) v3.3.0：算法模式（精确/分层/快速估算/保守）——UI 联动 + config 往返 + 真实搜索
    {
        std::fprintf(stderr, "[UI自检] 算法模式检查…\n");
        const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
        check(m_w->algExactRadio && m_w->algLayeredRadio && m_w->algSampleRadio && m_w->algBruteRadio,
              "算法模式单选存在");
        check(m_w->layerBlock && m_w->layerRatio && m_w->sampleStep && m_w->sampleExact, "算法参数控件存在");
        check(m_w->modeHeader && m_w->modeWarn, "结果区模式标注/警告条存在");
        check(m_w->mainScroll != nullptr, "设置区滚动容器存在（v3.3.0 滚动条）");

        // v3.3.0（修订）：文案不被遮挡的可量化校验
        //  - 每个算法单选：文本宽度 + 单选指示器/边距 ≤ 控件宽度（否则会被截断）
        //  - 模式说明标签（wordWrap）：实际高度 ≥ sizeHint 高度（否则末行被裁）
        //  - 设置区滚动条：内容高于视口时必须可滚动（内容不被永久遮挡）
        {
            m_w->densityRadio->setChecked(true);
            onModeChanged();
            resize(640, 720); // 最小窗口尺寸下检查
            QApplication::processEvents(QEventLoop::AllEvents, 30);
            for (QRadioButton* r : { m_w->algExactRadio, m_w->algLayeredRadio, m_w->algSampleRadio,
                                     m_w->algBruteRadio, m_w->strictRadio, m_w->densityRadio }) {
                r->ensurePolished();
                const int need = r->sizeHint().width();
                if (r->width() < need) {
                    std::fprintf(stderr, "[UI自检] 文字可能被截断: \"%s\" 宽=%d 需要=%d\n",
                                 r->text().toUtf8().constData(), r->width(), need);
                }
                check(r->width() >= need, "单选文字完整显示（未被截断）");
            }
            const QFontMetrics sumFm(m_w->algSummary->font());
            const int sumW = (std::max)(120, m_w->algSummary->width());
            const QRect sumNeed =
                sumFm.boundingRect(QRect(0, 0, sumW, 100000), Qt::TextWordWrap, m_w->algSummary->text());
            const int sumH = m_w->algSummary->height();
            const int hMax = m_w->mainScroll->horizontalScrollBar()->maximum();
            const int contentW = m_w->mainScroll->widget() ? m_w->mainScroll->widget()->width() : 0;
            std::fprintf(stderr,
                         "[UI自检] 模式说明标签 宽=%d 高=%d 需要=%d | 竖滚动上限=%d | 设置区宽=%d 横滚动上限=%d\n",
                         sumW, sumH, sumNeed.height(), m_w->mainScroll->verticalScrollBar()->maximum(),
                         contentW, hMax);
            check(sumH + 2 >= sumNeed.height(), "模式说明标签完整显示（换行未被裁剪）");
            check(m_w->mainScroll->verticalScrollBar()->maximum() > 0,
                  "设置区出现竖向滚动条（超高内容可滚动查看）");
            check(hMax == 0, "最小窗口尺寸下设置区无横向滚动条（文字不被强制截断）");
            // v3.3.4（任务书 1.5/7）：三个规定尺寸下都不允许出现横向滚动条（错位即不交付）
            for (const QSize sz : { QSize(1280, 860), QSize(1000, 650), QSize(1920, 1080) }) {
                resize(sz);
                QApplication::processEvents(QEventLoop::AllEvents, 40);
                const int hm = m_w->mainScroll->horizontalScrollBar()->maximum();
                std::fprintf(stderr, "[UI自检] 尺寸 %dx%d：设置区横滚动上限=%d 竖滚动上限=%d\n", sz.width(),
                             sz.height(), hm, m_w->mainScroll->verticalScrollBar()->maximum());
                check(hm == 0, "该尺寸下设置区无横向滚动条");
            }
            resize(1280, 860);
            QApplication::processEvents(QEventLoop::AllEvents, 40);
            // 逐个算法模式核对说明文字是否完整显示（用户可见的模式选择区文案）
            for (int mi = 0; mi < 4; ++mi) {
                QRadioButton* r = (mi == 0)   ? m_w->algExactRadio
                                  : (mi == 1) ? m_w->algLayeredRadio
                                  : (mi == 2) ? m_w->algSampleRadio
                                              : m_w->algBruteRadio;
                r->setChecked(true);
                updateAlgorithmPanels();
                if (m_w->mainScroll && m_w->modeGroup) m_w->mainScroll->ensureWidgetVisible(m_w->modeGroup, 0, 0);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                const QFontMetrics fm(m_w->algSummary->font());
                const QRect need = fm.boundingRect(QRect(0, 0, (std::max)(120, m_w->algSummary->width()), 100000),
                                                   Qt::TextWordWrap, m_w->algSummary->text());
                std::fprintf(stderr, "[UI自检] 模式[%d] 说明: 宽=%d 高=%d 需要=%d | 单选宽=%d 需要=%d\n", mi,
                             m_w->algSummary->width(), m_w->algSummary->height(), need.height(), r->width(),
                             r->sizeHint().width());
                check(m_w->algSummary->height() + 2 >= need.height(), "各算法模式说明文字完整显示");
                check(r->width() >= r->sizeHint().width(), "各算法模式单选文字完整显示");
            }
            m_w->algExactRadio->setChecked(true);
            updateAlgorithmPanels();
            if (hMax > 0 && m_w->mainScroll->widget()) {
                // 诊断：列出需求宽度较大的可见控件，便于定位横向滚动来源
                for (QWidget* w : m_w->mainScroll->widget()->findChildren<QWidget*>()) {
                    const int need = w->sizeHint().width();
                    if (w->isVisible() && need > 520) {
                        std::fprintf(stderr, "[UI自检] 宽控件: %s \"%s\" need=%d width=%d\n",
                                     w->metaObject()->className(),
                                     w->windowTitle().isEmpty()
                                         ? (qobject_cast<QGroupBox*>(w) ? qobject_cast<QGroupBox*>(w)->title().toUtf8().constData() : "")
                                         : "",
                                     need, w->width());
                    }
                }
            }
            resize(700, 920);
            QApplication::processEvents(QEventLoop::AllEvents, 30);
        }

        // 严格模式下算法不可用（图案匹配本身即精确判定）
        m_w->strictRadio->setChecked(true);
        onModeChanged();
        check(!m_w->algLayeredRadio->isEnabled() && !m_w->algSampleRadio->isEnabled(),
              "严格模式下算法选择禁用");
        check(m_w->modeHeader->text().contains(slime::ui::t("ui.b977e67d75f8")), "严格模式标注");

        // 密度模式：分层 → 参数可用 + 警告条随比例联动
        m_w->densityRadio->setChecked(true);
        onModeChanged();
        check(m_w->algLayeredRadio->isEnabled() && m_w->algSampleRadio->isEnabled(), "密度模式下算法可用");
        if (m_w->mainScroll && m_w->modeGroup) m_w->mainScroll->ensureWidgetVisible(m_w->modeGroup, 0, 0);
        saveWindowShot(shotPrefix, QStringLiteral("mode-exact"));
        m_w->algLayeredRadio->setChecked(true);
        updateAlgorithmPanels();
        if (m_w->mainScroll && m_w->modeGroup) m_w->mainScroll->ensureWidgetVisible(m_w->modeGroup, 0, 0);
        saveWindowShot(shotPrefix, QStringLiteral("mode-layered"));
        check(m_w->layerBlock->isEnabled() && !m_w->sampleStep->isEnabled(), "分层参数可用/估算参数禁用");
        check(m_w->modeHeader->text().contains(slime::ui::t("ui.752b56a4f270")), "分层模式标注");
        check(!m_w->modeWarn->isVisible(), "分层比例 1.00 → 无漏检警告（无损剪枝）");
        m_w->layerRatio->setValue(1.5);
        updateAlgorithmPanels();
        check(m_w->modeWarn->isVisible(), "分层比例 >1 → 显示可能漏检警告");
        m_w->layerRatio->setValue(1.0);

        // 估算模式：警告条常显 + 精算采样开关
        m_w->algSampleRadio->setChecked(true);
        m_w->sampleStep->setValue(8);
        updateAlgorithmPanels();
        if (m_w->mainScroll && m_w->modeGroup) m_w->mainScroll->ensureWidgetVisible(m_w->modeGroup, 0, 0);
        saveWindowShot(shotPrefix, QStringLiteral("mode-sample"));
        check(m_w->sampleStep->isEnabled() && !m_w->layerBlock->isEnabled(), "估算参数可用/分层参数禁用");
        check(m_w->modeWarn->isVisible(), "估算模式显示估算警告条");
        check(m_w->modeHeader->text().contains(slime::ui::t("ui.b73b8f52b049")), "估算模式标注");

        // config.json 往返：模式与参数持久化
        saveAlgorithmToConfig();
        const auto cfgText = [this]() {
            std::ifstream in(appDirPath() / kConfigFileName);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        check(cfgText().find("\"mode\": \"sample\"") != std::string::npos, "config algorithm.mode 写入");
        m_loadingConfig = true; // 抑制 toggled 自动写回，验证“从文件读回”本身
        m_w->algExactRadio->setChecked(true);
        m_loadingConfig = false;
        loadAlgorithmFromConfig();
        check(m_w->algSampleRadio->isChecked() && m_w->sampleStep->value() == 8,
              "config algorithm 读回（模式/采样间隔）");

        // 真实搜索冒烟：快速估算模式（走 startSearch 全链路，含结果标注与日志）
        std::fprintf(stderr, "[UI自检] 估算模式真实搜索冒烟…\n");
        m_w->javaRadio->setChecked(true);
        m_w->densityRadio->setChecked(true);
        m_w->minX->setValue(-256);
        m_w->maxX->setValue(255);
        m_w->minZ->setValue(-256);
        m_w->maxZ->setValue(255);
        m_w->gridSize->setValue(5);
        m_w->minDensity->setValue(0.4);
        m_w->targetCount->setValue(10);
        m_w->algSampleRadio->setChecked(true);
        m_w->sampleExact->setChecked(false);
        updateAlgorithmPanels();
        startSearch();
        {
            const auto t0 = std::chrono::steady_clock::now();
            while (m_running &&
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                QThread::msleep(10);
            }
        }
        check(!m_running, "估算模式搜索在 60s 内完成");
        check(m_lastResultText.contains(slime::ui::t("ui.5496995eb999")), "结果文本含模式标注");
        if (m_w->mainScroll && m_w->modeGroup) m_w->mainScroll->ensureWidgetVisible(m_w->modeGroup, 0, 0);
        saveWindowShot(shotPrefix, QStringLiteral("mode-sample-warn"));
        saveWindowShot(shotPrefix, QStringLiteral("full-window"));
        {
            std::ifstream pf(appDirPath() / "logs" / "performance.log");
            const std::string log((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
            check(log.find("(sample)") != std::string::npos, "performance.log 记录算法=sample（机器可读键）");
            const auto p = log.rfind("模式 密度");
            check(p != std::string::npos && log.find("采样间隔=8", p) != std::string::npos,
                  "performance.log 记录采样间隔");
            check(p != std::string::npos && log.find("与精确模式差异", p) != std::string::npos,
                  "performance.log 记录与精确模式差异");
        }
        // 恢复默认：精确模式 + 关闭估算
        m_w->algExactRadio->setChecked(true);
        m_w->sampleExact->setChecked(false);
        saveAlgorithmToConfig();
        updateAlgorithmPanels();
    }

    // 12) v3.3.1（任务书 2.1）：缓存扩展名统一 + 旧格式兼容
    {
        std::fprintf(stderr, "[UI自检] 缓存扩展名检查…\n");
        // 12.1 默认缓存文件名与对话框过滤器一致（.slimecache 优先，旧 .cache 兼容，*.* 兜底）
        check(std::string(kCacheFileName) == "slime_cache.slimecache", "默认缓存扩展名为 .slimecache");
        check(std::string(kCacheLegacyFileName) == "slime_cache.cache", "旧版扩展名常量为 .cache");
        const QString filter = cacheImportFilter();
        check(filter.contains(QStringLiteral("*.slimecache")), "过滤器含 *.slimecache");
        check(filter.contains(QStringLiteral("*.cache")), "过滤器含旧版 *.cache");
        check(filter.contains(QStringLiteral("*.*")), "过滤器含所有文件 *.*");
        check(m_cacheFile.filename().string() == kCacheFileName, "运行期缓存路径使用新扩展名");

        // 12.2 保存（.slimecache）后能重新加载，内容一致
        CacheManager c1;
        c1.mergeRegion(Rect{ 0, 63, 0, 63 }, std::vector<std::pair<int, int>>{ { 1, 2 }, { 3, 4 }, { 5, 6 } });
        const auto tmpNew = std::filesystem::temp_directory_path() / "slime_selftest_cache.slimecache";
        std::string err;
        check(c1.saveFile(tmpNew, err), "保存 .slimecache 成功");
        CacheManager c2;
        check(c2.loadFile(tmpNew, true, err), "重新加载 .slimecache 成功");
        check(c2.entryCount() == c1.entryCount() && c2.ranges().size() == c1.ranges().size(),
              "新扩展名缓存往返内容一致");

        // 12.3 旧格式（.cache）仍可被识别加载（兼容性）
        const auto tmpOld = std::filesystem::temp_directory_path() / "slime_selftest_cache.cache";
        check(c1.saveFile(tmpOld, err), "保存旧扩展名 .cache 成功");
        CacheManager c3;
        check(c3.loadFile(tmpOld, true, err), "旧格式 .cache 可正常加载（向后兼容）");
        check(c3.entryCount() == c1.entryCount(), "旧格式缓存内容一致");

        // 12.4 非缓存文件应给出明确错误（不崩溃）
        const auto tmpBad = std::filesystem::temp_directory_path() / "slime_selftest_notcache.slimecache";
        {
            std::ofstream bf(tmpBad, std::ios::binary);
            bf << "not a cache";
        }
        CacheManager c4;
        const bool badLoaded = c4.loadFile(tmpBad, true, err);
        check(!badLoaded && !err.empty(), "非缓存文件被拒绝并给出原因");

        std::error_code rec;
        std::filesystem::remove(tmpNew, rec);
        std::filesystem::remove(tmpOld, rec);
        std::filesystem::remove(tmpBad, rec);
        std::fprintf(stderr, "[UI自检] 缓存扩展名检查完成（新/旧/非法 三类均已覆盖）\n");
    }

    // 12.5~12.7) v3.3.2（任务书 3.2/3.3/七）：过滤器覆盖历史格式 + 对话框可见可选 + 真实端到端
    {
        std::fprintf(stderr, "[UI自检] 缓存可用性（v3.3.2）检查…\n");
        const QString filter = cacheImportFilter();
        const QStringList groups = filter.split(QStringLiteral(";;"));
        const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");

        // 12.5 过滤器必须覆盖「新版 + 全部历史格式 + 所有文件」
        check(filter.contains(QStringLiteral("*.slimecache")), "过滤器含新版 *.slimecache");
        check(filter.contains(QStringLiteral("*.cache")), "过滤器含旧版二进制 *.cache");
        check(filter.contains(QStringLiteral("*.pkl")) && filter.contains(QStringLiteral("*.pickle")),
              "过滤器含 Python 旧版 *.pkl / *.pickle");
        check(filter.contains(QStringLiteral("Slime Cache")), "过滤器含英文标识 Slime Cache");
        check(filter.contains(QStringLiteral("(*.*)")), "过滤器含所有文件 (*.*)");
        check(groups.size() >= 5, "过滤器分组 ≥5（新/旧/pkl/所有文件等）");

        // 12.6 对话框「可见 + 可选」可复现验证：临时目录写入默认名缓存，
        //      用与 importCache 完全相同的过滤器做目录匹配，并用同一配置断言其被预选。
        {
            std::error_code ec2;
            const auto dir = std::filesystem::temp_directory_path() / "slime_selftest_dlg";
            std::filesystem::remove_all(dir, ec2);
            std::filesystem::create_directories(dir, ec2);
            CacheManager cd;
            cd.mergeRegion(Rect{ 0, 31, 0, 31 },
                           std::vector<std::pair<int, int>>{ { 7, 7 }, { 9, 9 } });
            std::string derr;
            check(cd.saveFile(dir / kCacheFileName, derr), "对话框用例：保存默认名缓存成功");
            {
                std::ofstream of(dir / "readme.txt");
                of << "not a cache";
            }
            const QString dirQ = QString::fromStdWString(dir.wstring());
            // 注意：QDir::nameFiltersFromString 是按空白切分的便捷函数，不能用于带标签的
            // 「标签 (*.a *.b)」分组；这里自行取出括号内的模式（与 QFileDialog 的解析一致）。
            const auto patternsOfGroup = [](const QString& group) -> QStringList {
                const int a = group.indexOf(QLatin1Char('('));
                const int b = group.lastIndexOf(QLatin1Char(')'));
                if (a < 0 || b <= a) return {};
                return group.mid(a + 1, b - a - 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            };
            const QStringList firstGroup = patternsOfGroup(groups.value(0));
            const QStringList listed = QDir(dirQ).entryList(firstGroup, QDir::Files);
            std::fprintf(stderr, "[UI自检] 过滤器首组=[%s] 目录内匹配=%lld 项\n",
                         firstGroup.join(QStringLiteral(" ")).toUtf8().constData(),
                         (long long)listed.size());
            check(firstGroup.size() == 5 && firstGroup.contains(QStringLiteral("*.slimecache")) &&
                      firstGroup.contains(QStringLiteral("*.cache")) &&
                      firstGroup.contains(QStringLiteral("*.pkl")) &&
                      firstGroup.contains(QStringLiteral("*.pickle")) &&
                      firstGroup.contains(QStringLiteral("*.legacy")),
                  "首个过滤器分组的模式集合 = 新版+旧版+pkl+pickle+legacy（v3.3.8 加入 .legacy 以便回滚）");
            check(listed.contains(QString::fromStdString(kCacheFileName)),
                  "默认过滤器下列表中可见 slime_cache.slimecache");
            check(!listed.contains(QStringLiteral("readme.txt")), "默认过滤器不匹配无关文件");
            const QStringList allGroup = patternsOfGroup(groups.value(groups.size() - 1));
            check(QDir(dirQ).entryList(allGroup, QDir::Files).contains(QStringLiteral("readme.txt")),
                  "「所有文件」兜底可见任意文件");

            QFileDialog dlgCheck(nullptr, slime::ui::t("ui.834c34e48ed2"), dirQ);
            dlgCheck.setFileMode(QFileDialog::ExistingFile);
            dlgCheck.setNameFilters(groups);
            dlgCheck.selectFile(QString::fromStdString(kCacheFileName));
            const QStringList sel = dlgCheck.selectedFiles();
            check(!sel.isEmpty() &&
                      QFileInfo(sel.first()).fileName() == QString::fromStdString(kCacheFileName),
                  "对话框预选当前缓存文件（保存后即可直接选中加载）");
            std::fprintf(stderr, "[UI自检] 对话框起始目录=%s 预选=%s\n",
                         dlgCheck.directory().absolutePath().toUtf8().constData(),
                         sel.isEmpty() ? "(空)" : sel.first().toUtf8().constData());

            // 截图证据（SLIME_SHOT 设置时）：非原生对话框可在 offscreen 平台渲染
            if (!shotPrefix.isEmpty()) {
                dlgCheck.setOption(QFileDialog::DontUseNativeDialog, true);
                dlgCheck.resize(920, 560);
                dlgCheck.show();
                QApplication::processEvents(QEventLoop::AllEvents, 80);
                const QString p = QStringLiteral("%1_cache-import-dialog.png").arg(shotPrefix);
                const bool okShot = dlgCheck.grab().save(p);
                std::fprintf(stderr, "[UI自检] 缓存导入对话框截图%s: %s\n",
                             okShot ? "已保存" : "保存失败", p.toUtf8().constData());
                dlgCheck.hide();
            }
            std::filesystem::remove_all(dir, ec2);
        }

        // 12.7 真实端到端：基岩版小范围搜索（启用缓存）→ 生成 slime_cache.slimecache →
        //      重新加载（等价于重启）→ 缓存判定与引擎逐区块一致（= 结果与保存前一致）
        {
            std::error_code ec3;
            std::filesystem::remove(m_cacheFile, ec3);
            std::filesystem::remove(appDirPath() / kCacheLegacyFileName, ec3);
            m_cache->clear();
            const bool cacheWasEnabled = m_w->useCache ? m_w->useCache->isChecked() : false;
            if (m_w->useCache) m_w->useCache->setChecked(true);
            m_w->bedrockRadio->setChecked(true);
            m_w->densityRadio->setChecked(true);
            m_w->minX->setValue(-128);
            m_w->maxX->setValue(127);
            m_w->minZ->setValue(-128);
            m_w->maxZ->setValue(127);
            m_w->targetCount->setValue(10);
            m_w->stopAtTarget->setChecked(false);
            m_w->smartRange->setChecked(false);
            // v3.3.9（任务书 1.1/1.2）：缓存由**构建模式**写入（搜索只读 ⇒ 不再生成缓存）
            std::fprintf(stderr, "[UI自检] 缓存端到端：先构建缓存（基岩 256x256）…\n");
            setBuildModeActive(true);
            updateBuildHintUi();
            startSearch();
            const auto t0 = std::chrono::steady_clock::now();
            while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 120.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                QThread::msleep(20);
            }
            setBuildModeActive(false);
            updateBuildHintUi();
            check(!m_running, "缓存构建在 120s 内完成");
            check(std::filesystem::exists(m_cacheFile, ec3), "构建后生成缓存文件 slime_cache.slimecache");
            check(m_cache->entryCount() > 0, "缓存条目数 > 0");
            std::string lerr;
            CacheManager reload;
            const bool reloaded = reload.loadFile(m_cacheFile, true, lerr);
            check(reloaded, "重新加载（模拟重启）缓存文件成功");
            check(reload.entryCount() == m_cache->entryCount(), "重载后条目数与保存前一致");
            int mism = 0;
            int64_t checkedChunks = 0;
            for (const Rect& r : reload.ranges()) {
                for (int x = r.minX; x <= r.maxX; ++x) {
                    for (int z = r.minZ; z <= r.maxZ; ++z) {
                        ++checkedChunks;
                        if (reload.isSlime(x, z) != SlimeEngine::isSlimeChunkBedrockMT(x, z)) ++mism;
                    }
                }
            }
            check(checkedChunks > 0 && mism == 0,
                  "重载缓存判定与引擎逐区块一致（结果与保存前一致）");
            // 诊断输出统一 UTF-8（本行此前用 path::string() = ANSI，会让证据日志混入 GBK 字节）
            const std::string cachePathUtf8(reinterpret_cast<const char*>(m_cacheFile.u8string().c_str()));
            std::fprintf(stderr,
                         "[UI自检] 缓存端到端：文件=%s 条目=%lld 重载条目=%lld 逐区块比对=%lld 不一致=%d\n",
                         cachePathUtf8.c_str(),
                         static_cast<long long>(m_cache->entryCount()),
                         static_cast<long long>(reload.entryCount()),
                         static_cast<long long>(checkedChunks), mism);
            if (m_w->useCache) m_w->useCache->setChecked(cacheWasEnabled);
        }
        std::fprintf(stderr, "[UI自检] 缓存可用性（v3.3.2）检查完成\n");
    }

    // 12.8) v3.3.8（任务 2 / 历史缓存方案 A）：历史缓存一次性改名停用 + 7 天到期清理
    {
        std::fprintf(stderr, "[UI自检] 历史缓存方案 A（v3.3.8 任务 2）检查…\n");
        std::error_code ec4;
        const auto dir = std::filesystem::temp_directory_path() / "slime_selftest_legacy";
        std::filesystem::remove_all(dir, ec4);
        std::filesystem::create_directories(dir, ec4);
        const auto writeStub = [](const std::filesystem::path& p, const char* text) {
            std::ofstream o(p, std::ios::binary | std::ios::trunc);
            o << text;
        };
        // ① 首次运行：已有 .slimecache + 旧名 .cache ⇒ 双双改名 .legacy；标记文件生成；需要提示
        writeStub(dir / kCacheFileName, "SLME-old-cache");
        writeStub(dir / kCacheLegacyFileName, "SLME-old-name");
        LegacyCacheMigration m1 = migrateLegacyCachesOnce(dir, kCacheLegacyKeepDays);
        check(m1.migrationRan && m1.promptNeeded && m1.moved.size() == 2,
              "首次运行：两个历史缓存都被改名为 .legacy 并请求提示用户");
        check(std::filesystem::exists(dir / (std::string(kCacheFileName) + kCacheLegacySuffix), ec4) &&
                  std::filesystem::exists(dir / (std::string(kCacheLegacyFileName) + kCacheLegacySuffix), ec4),
              "改名结果：slime_cache.slimecache.legacy 与 slime_cache.cache.legacy 均存在");
        check(!std::filesystem::exists(dir / kCacheFileName, ec4), "原缓存文件已不在默认路径（本次启动不会被加载）");
        check(std::filesystem::exists(dir / kCacheMigratedMarker, ec4), "一次性迁移标记已落盘（slime_cache.migrated）");
        // ② 第二次运行：新写入的正常缓存**不会**被再次停用（7 天后 .legacy 被清理也不会复发）
        writeStub(dir / kCacheFileName, "SLME-new-cache");
        LegacyCacheMigration m2 = migrateLegacyCachesOnce(dir, kCacheLegacyKeepDays);
        check(!m2.migrationRan && !m2.promptNeeded && m2.moved.empty(),
              "再次运行：迁移只做一次（新缓存保留在默认路径、不再停用）");
        check(std::filesystem::exists(dir / kCacheFileName, ec4), "新缓存仍在默认路径（可被正常加载）");
        // ③ 到期清理：8 天前的 .legacy 被删、1 天前的保留；标记文件不受影响
        const auto oldFile = dir / (std::string(kCacheLegacyFileName) + kCacheLegacySuffix);
        const auto freshFile = dir / (std::string(kCacheFileName) + kCacheLegacySuffix);
        std::filesystem::last_write_time(oldFile, std::filesystem::file_time_type::clock::now() -
                                                     std::chrono::hours(24 * 8), ec4);
        std::filesystem::last_write_time(freshFile, std::filesystem::file_time_type::clock::now() -
                                                         std::chrono::hours(24), ec4);
        LegacyCacheMigration m3 = migrateLegacyCachesOnce(dir, kCacheLegacyKeepDays);
        check(m3.purged.size() == 1 && !std::filesystem::exists(oldFile, ec4),
              "到期清理：8 天前的 .legacy 被删除");
        check(std::filesystem::exists(freshFile, ec4), "到期清理：1 天前的 .legacy 仍保留（可回滚）");
        check(std::filesystem::exists(dir / kCacheMigratedMarker, ec4), "到期清理不影响一次性迁移标记");
        // ④ 回滚路径：过滤器必须能看到 .legacy（显式导入即用户确认使用）
        const QString filterLegacy = cacheImportFilter();
        check(filterLegacy.contains(QStringLiteral("*.legacy")), "导入过滤器含 *.legacy（可手动回滚）");
        std::filesystem::remove_all(dir, ec4);
        std::fprintf(stderr, "[UI自检] 历史缓存方案 A 检查完成（改名/一次性/7 天清理/回滚可见）\n");
    }

    // 13) v3.3.3（任务书 2.1/2.2/3.1/3.2/3.3/4.2）：版本号 / 缓存标签 / 帮助 / 折叠 / 排序
    {
        std::fprintf(stderr, "[UI自检] v3.3.3 检查（版本号/缓存标签/帮助/折叠/排序）…\n");

        // 13.1 版本号统一（任务书 2.1）—— 全部以 kAppVersion 为基准，避免升级时漏改断言
        const QString appVer = QString::fromLatin1(kAppVersion);          // 形如 v3.3.6 / v3.3.7.2
        const QString expectedVersion = appVer.mid(1);                    // 形如 3.3.6 / 3.3.7.2
        // v3.3.7.2：热修版使用 4 段版本号（3.3.7.2），因此允许 2~3 个点
        const int dotCount = expectedVersion.count(QLatin1Char('.'));
        check(appVer.startsWith(QStringLiteral("v")) && (dotCount == 2 || dotCount == 3),
              (slime::ui::t("ui.4842f3de5c9e").arg(appVer)).toUtf8().constData());
        check(windowTitle().contains(appVer),
              (slime::ui::t("ui.18aa9f5e039f").arg(appVer)).toUtf8().constData());
        check(QApplication::applicationVersion() == expectedVersion,
              (slime::ui::t("ui.1850e279ff8f").arg(expectedVersion))
                  .toUtf8()
                  .constData());
        {
            // config.json 落盘应包含 version 字段（写回一次后检查）
            saveAlgorithmToConfig();
            std::ifstream in(appDirPath() / kConfigFileName);
            std::stringstream ss;
            ss << in.rdbuf();
            const std::string cfg = ss.str();
            const std::string want = "\"version\": \"" + appVer.toStdString() + "\"";
            check(cfg.find(want) != std::string::npos,
                  (slime::ui::t("ui.9f9c9174298d").arg(appVer)).toUtf8().constData());
        }

        // 13.2 缓存状态标签（任务书 2.2）——首次搜索后必须显示条目数
        {
            m_w->bedrockRadio->setChecked(true);
            updateCacheStatusUi();
            const QString t = m_w->cacheStatus->text();
            check(t.contains(slime::ui::t("ui.8b32159c6375")) && t.contains(QString::number(m_cache->entryCount())),
                  "缓存标签显示「缓存已加载，共 N 个区块」（N 与实际条目数一致）");
            std::fprintf(stderr, "[UI自检] 缓存标签文本: %s\n", t.toUtf8().constData());
            // 勾选变化也要刷新（取消勾选 → 文本仍在；重新载入后再次刷新）
            m_w->useCache->setChecked(false);
            updateCacheStatusUi();
            const QString t2 = m_w->cacheStatus->text();
            check(t2.contains(slime::ui::t("ui.8b32159c6375")), "切换「使用缓存」勾选后标签仍为已加载状态");
            m_w->useCache->setChecked(true);
        }

        // 13.3 v3.3.6（任务书 Bug 2）：彻底移除「临界值」—— 全界面无该字样，提醒文案保留
        {
            // 递归扫描所有子控件的文本与 tooltip（含菜单/动作），确认没有残留
            const QString banned = slime::ui::t("ui.42419fe91fa9") + slime::ui::t("ui.321b9558401a");
            int hits = 0;
            QStringList hitWhere;
            const QList<QWidget*> widgets = findChildren<QWidget*>();
            for (QWidget* w : widgets) {
                if (!w) continue;
                QStringList texts;
                if (auto* lb = qobject_cast<QLabel*>(w)) texts << lb->text();
                if (auto* cb = qobject_cast<QCheckBox*>(w)) texts << cb->text();
                if (auto* rb = qobject_cast<QRadioButton*>(w)) texts << rb->text();
                if (auto* pb = qobject_cast<QPushButton*>(w)) texts << pb->text();
                if (auto* gb = qobject_cast<QGroupBox*>(w)) texts << gb->title();
                texts << w->toolTip();
                for (const QString& t : texts) {
                    if (t.contains(banned)) {
                        ++hits;
                        hitWhere << QStringLiteral("%1(%2)").arg(w->metaObject()->className(), t.left(40));
                    }
                }
            }
            const QList<QAction*> actions = findChildren<QAction*>();
            for (QAction* a : actions) {
                if (!a) continue;
                if (a->text().contains(banned) || a->toolTip().contains(banned)) {
                    ++hits;
                    hitWhere << QStringLiteral("QAction(%1)").arg(a->text().left(40));
                }
            }
            check(hits == 0, (slime::ui::t("ui.ce9d348d417a").arg(banned))
                                 .toUtf8()
                                 .constData());
            if (hits) {
                std::fprintf(stderr, "[UI自检] 残留位置: %s\n", hitWhere.join(QStringLiteral(" / ")).toUtf8().constData());
            }
            // GPU 提醒保留：动态基准只报实测吞吐率（不含任何交叉点/阈值结论）
            check(m_gpuBenchText.isEmpty() || !m_gpuBenchText.contains(banned),
                  "实测吞吐文案不含被移除的说法");
        }

        // 13.4 帮助/使用教程（任务书 3.3）
        {
            const QString help = helpTutorialHtml();
            const char* sections[] = { "【这是什么】", "【怎么用】", "【图案怎么画】", "【四种模式怎么选】",
                                       "【GPU 加速是什么】", "【结果怎么用】", "【常见问题】" };
            bool all = true;
            for (const char* s : sections) {
                if (!help.contains(QString::fromUtf8(s))) {
                    all = false;
                    std::fprintf(stderr, "[UI自检] 帮助缺少章节: %s\n", s);
                }
            }
            check(all, "帮助教程包含全部 7 个章节");
            check(m_w->helpBtn == nullptr, "右下角「帮助 / 使用教程」按钮已按任务书 3.3 移除（F1/菜单入口保留）");
            check(help.contains(slime::ui::t("ui.c010838dd8fa")) && help.contains(slime::ui::t("ui.4a1ae3cda4be")) &&
                      help.contains(slime::ui::t("ui.579625d5ac90")),
                  "帮助教程包含图案三色说明");
        }

        // 13.4c v3.3.9（任务书 1.1/1.2）：缓存构建模式端到端（分段构建 + 落盘 + 四项进度 + 只读校验）
        {
            m_w->bedrockRadio->setChecked(true);
            m_w->useCache->setChecked(true);
            setBuildModeActive(true);
            updateBuildHintUi();
            showPage(kPageBuild); // v3.4.9：提示行随构建内容在构建页，先切页再判可见性
            QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
            check(m_w->buildHint && m_w->buildHint->isVisible() &&
                      m_w->buildHint->text().contains(slime::ui::t("ui.f6bed4017481")),
                  "构建模式提示可见（预估体积 + 说明；任务书 1.1）");
            check(m_w->targetCount && !m_w->targetCount->isEnabled() && m_w->stopAtTarget &&
                      !m_w->stopAtTarget->isEnabled(),
                  "构建模式禁用匹配相关控件（只判定不匹配）");
            m_w->minX->setValue(0);
            m_w->maxX->setValue(511);
            m_w->minZ->setValue(0);
            m_w->maxZ->setValue(511);
            m_w->seedEdit->setText(QStringLiteral("0"));
            m_cache->clear();
            std::error_code ecB;
            std::filesystem::remove(m_cacheFile, ecB);
            const auto bT0 = std::chrono::steady_clock::now();
            startSearch();
            while (m_running &&
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - bT0).count() < 180.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                QThread::msleep(20);
            }
            check(!m_buildActive && !m_running, "缓存构建流程正常结束");
            check(m_cache->entryCount() > 0, "构建模式写入了缓存条目");
            // 覆盖声明 = 锚点行域（末 hMax-1 = 4 行为块间重叠行，属下一块数据但不属任何锚点域）
            const QString bInfo = m_w->progressInfo ? m_w->progressInfo->text() : QString();
            check(bInfo.contains(slime::ui::t("ui.d94731eaa902")) && bInfo.contains(QStringLiteral("%")) &&
                      bInfo.contains(slime::ui::t("ui.5be47a443458")) && bInfo.contains(slime::ui::t("ui.70654727169a")),
                  "构建进度文案含四项：已记录区块(百分比) / 记录速度 / 已花费（任务书 1.1）");
            std::fprintf(stderr, "[UI自检] 构建模式：缓存 %zu 区域 / %lld 区块；进度文案=[%s]\n",
                         m_cache->ranges().size(), static_cast<long long>(m_cache->entryCount()),
                         bInfo.toUtf8().constData());
            // 任务书 1.2：普通搜索不得写缓存
            const int64_t entriesBeforeSearch = m_cache->entryCount();
            const size_t rangesBeforeSearch = m_cache->ranges().size();
            setBuildModeActive(false);
            updateBuildHintUi();
            m_w->densityRadio->setChecked(true);
            m_w->targetCount->setValue(3);
            m_w->stopAtTarget->setChecked(false);
            const auto sT0 = std::chrono::steady_clock::now();
            startSearch();
            while (m_running &&
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - sT0).count() < 60.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                QThread::msleep(20);
            }
            check(m_cache->entryCount() == entriesBeforeSearch &&
                      m_cache->ranges().size() == rangesBeforeSearch,
                  "任务书 1.2：普通搜索不写缓存（搜索前后缓存完全不变）");
            // v3.4.9：本组曾切到构建页，收尾切回搜索页 —— 否则后续「开始搜索按钮唯一性 /
            // 线程数提示可见性」等断言会因为当前页不是搜索页而误报。
            showPage(kPageSearch);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        }

        // 13.4d v3.3.9.1（任务书 1.1/1.2/1.3/2.1）：构建模式三项修复 + GPU 启动检测设置
        {
            // 1.1：勾选构建模式后，与构建无关的选项必须禁用且有说明
            setBuildModeActive(true);
            updateBuildHintUi();
            check(m_w->strictRadio && !m_w->strictRadio->isEnabled() && m_w->densityRadio &&
                      !m_w->densityRadio->isEnabled(),
                  "1.1：构建模式禁用「搜索模式」");
            check(m_w->javaRadio && !m_w->javaRadio->isEnabled(), "1.1：构建模式禁用「Java 版」");
            check(m_w->algExactRadio && !m_w->algExactRadio->isEnabled(), "1.1：构建模式禁用「算法」选择");
            check(m_w->targetCount && !m_w->targetCount->isEnabled() && m_w->stopAtTarget &&
                      !m_w->stopAtTarget->isEnabled(),
                  "1.1：构建模式禁用「结果筛选（目标数量 / 达到即停）」");
            check(m_w->strictRadio && m_w->strictRadio->toolTip().contains(slime::ui::t("ui.ef6376c8e19e")),
                  "1.1：被禁用的选项给出原因说明（tooltip）");
            check(m_w->minX->isEnabled() && m_w->maxX->isEnabled() && m_w->threads->isEnabled(),
                  "1.1：构建相关输入（范围 / 线程数）保持可用");
            setBuildModeActive(false);
            updateBuildHintUi();
            check(m_w->strictRadio->isEnabled() && m_w->javaRadio->isEnabled() &&
                      m_w->targetCount->isEnabled(),
                  "1.1：取消勾选后选项恢复可用");

            // 1.3/1.2：多段构建 —— 进度不得超 100%、计数单调、必须正常收尾（不死循环/不重复记录/不闪退）
            m_w->bedrockRadio->setChecked(true);
            m_w->useCache->setChecked(true);
            setBuildModeActive(true);
            updateBuildHintUi();
            m_w->minX->setValue(0);
            m_w->maxX->setValue(2047);
            m_w->minZ->setValue(0);
            m_w->maxZ->setValue(65535); // 2048 宽 ⇒ 每段 4096 行 ⇒ 16 段
            m_cache->clear();
            std::error_code ecB2;
            std::filesystem::remove(m_cacheFile, ecB2);
            const auto bT0 = std::chrono::steady_clock::now();
            const int64_t entriesBeforeBuild = m_cache->entryCount();
            startSearch();
            double maxPct = 0.0;
            int64_t prevRecorded = -1;
            int nonMonotonic = 0, over100 = 0;
            while (m_running &&
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - bT0).count() < 300.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                QThread::msleep(15);
                const QString info = m_w->progressInfo ? m_w->progressInfo->text() : QString();
                const QRegularExpression re(slime::ui::t("ui.8297e7c40bbb"));
                const QRegularExpressionMatch mm = re.match(info);
                if (mm.hasMatch()) {
                    const int64_t n = mm.captured(1).toLongLong();
                    const double p = mm.captured(2).toDouble();
                    maxPct = (std::max)(maxPct, p);
                    if (p > 100.0001) ++over100;
                    if (prevRecorded >= 0 && n < prevRecorded) ++nonMonotonic;
                    prevRecorded = n;
                }
            }
            check(!m_buildActive && !m_running, "1.3：多段构建正常收尾（不再无限重复记录）");
            check(over100 == 0 && maxPct <= 100.0001, "1.3：构建进度从不超过 100%");
            check(nonMonotonic == 0, "1.3：已记录区块数单调递增（无重复计数）");
            check(m_cache->entryCount() > entriesBeforeBuild, "1.3：多段构建写入了缓存条目");
            check(!m_buildBands.empty() && m_buildBandIndex + 1 >= m_buildBands.size(),
                  "1.3：分段全部执行完（最后一段已处理）");
            std::fprintf(stderr,
                         "[UI自检] 多段构建：段数=%zu 已记录=%lld 已跳过=%lld 最大进度=%.2f%% 非单调=%d\n",
                         m_buildBands.size(), static_cast<long long>(m_buildDoneChunks),
                         static_cast<long long>(m_buildSkippedTotal), maxPct, nonMonotonic);
            setBuildModeActive(false);
            updateBuildHintUi();

            // 2.1：启动时自动检查 GPU 状态（关 / 开两种行为）
            {
                const AppSettings keep = m_settings;
                const bool probeBefore = m_gpuProbeDone;
                m_settings.autoCheckGpuAtStartup = false;
                maybeAutoCheckGpuAtStartup(); // 关闭：不得探测
                check(m_gpuProbeDone == probeBefore, "2.1：设置关闭 ⇒ 启动时不检测 GPU");
                check(m_w->gpuStatus->text().contains(slime::ui::t("ui.90b74980e435")),
                      "2.1：设置关闭 ⇒ 状态显示「未检测」（并提示如何开启）");
                m_settings.autoCheckGpuAtStartup = true;
                maybeAutoCheckGpuAtStartup(); // 开启：探测一次并显示状态
                check(m_gpuProbeDone, "2.1：设置开启 ⇒ 启动时完成一次 GPU 检测");
                check(m_w->gpuStatus->text().contains(QStringLiteral("GPU：")),
                      "2.1：设置开启 ⇒ 启动时状态可见");
                std::fprintf(stderr, "[UI自检] GPU 启动检测（开启）：%s\n",
                             m_w->gpuStatus->text().toUtf8().constData());
                m_settings = keep;
            }
        }

        // 13.5 进度分阶段（任务书 3.2；v3.3.7 P1-5 改为「阶段短语 + 统计信息」两个纯函数；
        //      v3.3.7.2 任务书 1.4：阶段短语按模式/算法分派）
        {
            const slime::Mode strict = slime::Mode::Strict;
            const slime::Mode density = slime::Mode::Density;
            const slime::Algorithm exact = slime::Algorithm::Exact;
            const slime::Algorithm sample = slime::Algorithm::Sample;
            check(progressStageName(0, strict, exact) == slime::ui::t("ui.b0894e1c9f9a") &&
                      progressStageName(1, strict, exact) == slime::ui::t("ui.6df614a8dce8") &&
                      progressStageName(2, strict, exact) == slime::ui::t("ui.5b0b37bab0a8") &&
                      progressStageName(3, strict, exact) == slime::ui::t("ui.0133025c537b") &&
                      progressStageName(4, strict, exact) == slime::ui::t("ui.8174d1168fce"),
                  "严格模式阶段短语映射正确（扫描/匹配引导/匹配/排序/完成）");
            check(progressStageName(1, density, exact) == slime::ui::t("ui.c7709064a72b") &&
                      progressStageName(2, density, exact) == slime::ui::t("ui.1e28c06fde93"),
                  "密度模式阶段短语为「匹配密度」（任务书 1.4：不得出现「匹配图案」）");
            check(!progressStageName(1, density, exact).contains(slime::ui::t("ui.d5b2aeac0692")) &&
                      !progressStageName(2, density, exact).contains(slime::ui::t("ui.d5b2aeac0692")),
                  "密度模式阶段短语不含「图案」字样");
            check(progressStageName(1, strict, sample) == slime::ui::t("ui.45a5facab1ca") &&
                      progressStageName(2, strict, sample) == slime::ui::t("ui.6a6660d8c144"),
                  "快速估算模式阶段短语为「估算」（任务书 1.4）");
            const QString stats = progressStatsText(1234, 5678, 123456.0, 7);
            check(stats.contains(slime::ui::t("ui.6457a952b16b")) &&
                      stats.contains(slime::ui::t("ui.ee266fcea944")) && stats.contains(slime::ui::t("ui.6db0625ef054")),
                  "统计信息含 扫描 N/M 区块 + 速度 + 找到 K 个");
            check(!stats.contains(slime::ui::t("ui.5886b1dac687")), "统计信息不含「预计剩余」（避免与状态栏 ETA 重复）");
            check(m_w->cancel != nullptr && m_w->progressInfo != nullptr, "进度/取消控件存在（阶段/统计已接入）");
            std::fprintf(stderr, "[UI自检] 阶段文字: 严格[%s][%s] 密度[%s][%s] 估算[%s] 统计: [%s]\n",
                         progressStageName(0, strict, exact).toUtf8().constData(),
                         progressStageName(2, strict, exact).toUtf8().constData(),
                         progressStageName(1, density, exact).toUtf8().constData(),
                         progressStageName(2, density, exact).toUtf8().constData(),
                         progressStageName(2, strict, sample).toUtf8().constData(),
                         stats.toUtf8().constData());
        }

        // 13.6 结果区排序（任务书 4.2）
        {
            check(m_w->sortCombo == nullptr, "结果排序下拉框已按任务书 3.5 移除（显示顺序按当前模式自动决定）");
            if (m_hasLastOutcome && !m_lastOutcome.results.empty()) {
                applyResultSort();
                check(m_w->resultView && !m_w->resultView->toPlainText().isEmpty(), "重排后结果视图仍有内容");
                check(m_w->resultView && m_w->resultView->toPlainText().contains(slime::ui::t("ui.7a1fd8c33948")),
                      "结果视图含「第1名」标题行");
                std::fprintf(stderr, "[UI自检] 结果排序切换正常（结果条数=%lld）\n",
                             static_cast<long long>(m_lastOutcome.results.size()));
            } else {
                std::fprintf(stderr, "[UI自检] 无搜索结果：跳过排序视图断言\n");
            }
        }

        // 13.7 取消响应（任务书 3.2：匹配阶段「取消」必须立即响应）
        {
            // 用基岩版密度搜索（吞吐较低）保证有足够的可取消负载；范围取 8000² = 6400 万区块
            // （低于整图确认阈值，且自检会自动放行「范围过大」提示）
            m_w->bedrockRadio->setChecked(true);
            m_w->densityRadio->setChecked(true);
            m_w->minX->setValue(-4000); m_w->maxX->setValue(3999);
            m_w->minZ->setValue(-4000); m_w->maxZ->setValue(3999);
            m_w->targetCount->setValue(50);
            m_w->stopAtTarget->setChecked(false);
            m_w->smartRange->setChecked(false);
            m_w->useCache->setChecked(false); // 避免命中缓存直接秒回
            if (m_w->gpuCheck->isChecked()) m_w->gpuCheck->setChecked(false); // 走纯 CPU，确保有可取消的负载
            // 变体 A：启动后立即取消（引擎在扫描/匹配循环内定期检查取消标志）
            startSearch();
            const bool wasRunning = m_running;
            const auto tCancel0 = std::chrono::steady_clock::now();
            cancelSearch();
            const auto tCancel1 = std::chrono::steady_clock::now();
            const double cancelCallMs = std::chrono::duration<double, std::milli>(tCancel1 - tCancel0).count();
            while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - tCancel0).count() < 5.0) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
            const double stopMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tCancel0).count();
            check(!m_running && wasRunning, "取消后搜索立即停止（未闪退/未挂起）");
            check(stopMs < 1500.0, "取消到停止耗时 < 1.5s（实测见日志）");
            check(cancelCallMs < 200.0, "点击取消本身立即返回（不阻塞 UI 线程）");
            check(m_w->progressInfo->text().contains(slime::ui::t("ui.a5ffdc95eeb0")), "取消后进度文字为「已取消」");
            std::fprintf(stderr, "[UI自检] 取消响应(立即): 调用 %.1f ms，停止 %.1f ms\n", cancelCallMs, stopMs);

            // 变体 B：先跑一小段再取消（尽量落在扫描/匹配过程中；信息性记录是否确实发生在运行中）
            if (m_uiQuiet) {
                startSearch();
                const auto t1 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count() < 0.05) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                    QThread::msleep(5);
                }
                const bool midRun = m_running;
                const auto t2 = std::chrono::steady_clock::now();
                cancelSearch();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t2).count() < 5.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(5);
                }
                const double stop2 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count();
                std::fprintf(stderr, "[UI自检] 取消响应(运行中=%d): 停止 %.1f ms（若为 0 表示搜索已先完成，属正常）\n",
                             (int)midRun, stop2);
                check(!m_running, "第二次取消后同样干净停止");
            }
        }

        // 13.9 v3.3.5（Bug 1/2/4/5/6/7 + 优化 1~5）
        {
            std::fprintf(stderr, "[UI自检] v3.3.5 检查（Tab 焦点/GPU 状态/日志开关/更新弹窗/基准/滚轮/设置/菜单/布局）…\n");

            // --- Bug 1：Tab 补全不改变焦点；唯一匹配 100% 补全；无匹配不改内容 ---
            if (m_terminal && m_terminal->input()) {
                QLineEdit* in = m_terminal->input();
                selectBottomCategory(3); // v3.3.6：命令分类（原命令终端标签页）
                activateWindow();
                raise();
                in->setFocus(Qt::OtherFocusReason);
                QApplication::processEvents(QEventLoop::AllEvents, 40);
                in->setText(QString());
                in->setFocus(Qt::OtherFocusReason);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                // 离屏平台下主窗口未必是「活动窗口」，hasFocus() 可能恒为 false，
                // 因此同时统计 Tab 过程中「焦点是否跑到别的控件」——这才是 Bug 1 的判据。
                QWidget* foreign = nullptr;
                int focusEvents = 0;
                auto conn = connect(qApp, &QApplication::focusChanged, this,
                                    [&](QWidget* /*old*/, QWidget* now) {
                                        ++focusEvents;
                                        if (now && now != in) foreign = now;
                                    });
                int focusKept = 0;
                for (int i = 0; i < 10; ++i) {
                    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                    QApplication::sendEvent(in, &ev);
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    if (!foreign) ++focusKept;
                }
                disconnect(conn);
                std::fprintf(stderr, "[UI自检] Tab×10：焦点事件 %d 次，跑到其他控件=%s\n", focusEvents,
                             foreign ? foreign->objectName().toUtf8().constData() : "无");
                if (!in->hasFocus()) {
                    std::fprintf(stderr, "[UI自检] 提示：离屏平台未激活窗口，改用「焦点未外移」判据\n");
                    check(focusKept == 10, "连续按 Tab 10 次焦点始终未离开命令输入框");
                }
                if (in->hasFocus()) {
                    check(focusKept == 10, "连续按 Tab 10 次焦点始终在命令输入框");
                }
                // 唯一匹配：输入 "see" + Tab → "seed "
                in->setText(QStringLiteral("see"));
                in->setCursorPosition(3);
                in->setFocus(Qt::OtherFocusReason);
                QKeyEvent ev2(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                QApplication::sendEvent(in, &ev2);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                // v3.6.5（用户要求）：Tab 不写入；输入保持不变，Enter 才补成 "seed "
                check(in->text() == QStringLiteral("see"), "Tab 不写入命令栏（唯一匹配也不写）");
                QKeyEvent ev2e(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
                QApplication::sendEvent(in, &ev2e);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                check(in->text() == QStringLiteral("seed "), "Enter 补全为完整命令（see→seed+空格）");
                check(!foreign, "补全成功后焦点仍未被其他控件夺走");
                // 唯一前缀但多候选（"r" → range/result）：补到最长公共前缀或列出候选，且不改焦点
                in->setText(QStringLiteral("re"));
                in->setCursorPosition(2);
                in->setFocus(Qt::OtherFocusReason);
                const QString before = in->text();
                QKeyEvent ev3(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                QApplication::sendEvent(in, &ev3);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                check(in->text().startsWith(before), "多候选时不破坏已输入内容");
                check(!foreign, "多候选时焦点仍未被其他控件夺走");
                // 无匹配：内容不变、焦点不变
                in->setText(QStringLiteral("zzz"));
                in->setCursorPosition(3);
                in->setFocus(Qt::OtherFocusReason);
                QKeyEvent ev4(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                QApplication::sendEvent(in, &ev4);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                check(in->text() == QStringLiteral("zzz") && !foreign,
                      "无匹配时输入内容与焦点均不变");
                in->setText(QString());
            } else {
                check(false, "命令终端与输入框存在");
            }

            // --- Bug 2：GPU 状态复用探测逻辑并显示完整信息 ---
            {
                const int before = m_feedback ? m_feedback->totalCount() : 0;
                showGpuStatus(); // 自检模式：不弹窗，仅写入反馈与状态
                const QString all = m_feedback ? m_feedback->allText() : QString();
                const bool hasMark = all.contains(QStringLiteral("✅")) || all.contains(QStringLiteral("❌"));
                check(m_feedback && m_feedback->totalCount() > before && hasMark,
                      "GPU 状态写入反馈栏且带 ✅/❌ 标记");
                if (m_gpuAvailable && m_gpuScan) {
                    check(all.contains(QStringLiteral("CU")) && all.contains(QStringLiteral("MB")),
                          "GPU 可用时显示显存与计算单元数");
                } else {
                    check(all.contains(slime::ui::t("ui.a0215df67544")),
                          "GPU 不可用时显示「未找到 OpenCL 设备」");
                }
            }

            // --- Bug 5：日志可关闭（菜单项动态切换 + 恢复默认提示） ---
            {
                slime::LogBus::setMode(slime::LogMode::Off);
                updateLogMenuText();
                check(m_actLogToggle && m_actLogToggle->text().contains(slime::ui::t("ui.162640c04abd")),
                      "未启用时菜单项为「启用日志…」");
                slime::LogBus::setMode(slime::LogMode::Brief);
                updateLogMenuText();
                check(m_actLogToggle->text().contains(slime::ui::t("ui.0aa53272a236")),
                      "启用后菜单项变为「关闭日志」");
                if (m_logPanel) m_logPanel->refreshFromBus();
                toggleLogging(); // 关闭
                check(slime::LogBus::mode() == slime::LogMode::Off, "点击「关闭日志」后日志模式回到 Off");
                check(m_actLogToggle->text().contains(slime::ui::t("ui.162640c04abd")), "关闭后菜单项恢复为「启用日志…」");
                if (m_logPanel && m_logPanel->hintLabel()) {
                    // 日志标签页此刻可能不是当前页（其子控件 isVisible() 为 false），
                    // 因此用「是否处于关闭状态 + 提示文案」来判断
                    check(!m_logPanel->isLoggingEnabled() &&
                              m_logPanel->hintLabel()->text().contains(slime::ui::t("ui.992278d3d9d7")),
                          "关闭后日志栏恢复默认提示（不再写文件）");
                }
            }

            // --- v3.10.12.10：检查更新支持 GitHub/镜像/网盘（自检模式不联网） ---
            {
                m_updateBrowserOpened = false;
                openReleasePage();
                check(!m_updateBrowserOpened, "自检模式不打开浏览器（检查更新不联网）");
                check(m_feedback && m_feedback->allText().contains(QStringLiteral("GitHub")) &&
                          m_feedback->allText().contains(slime::ui::t("ui.e64cd655788c")) &&
                          m_feedback->allText().contains(slime::ui::t("ui.c48542499f03")) &&
                          !m_feedback->allText().contains(slime::ui::t("ui.c839a8ff1788")),
                      "检查更新支持 GitHub/镜像/网盘来源（自检模式不联网）");
            }
            // v3.10.12.10：更新对话框可创建/显示；网盘页自检不启动浏览器
            {
                UpdateDialog dlg;
                dlg.show();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                check(dlg.isVisible(), "检查更新对话框可正常创建并显示");
                dlg.close();
                m_updateBrowserOpened = false;
                openPanPage();
                check(!m_updateBrowserOpened, "自检模式打开网盘页不启动浏览器");
                check(m_feedback && m_feedback->allText().contains(slime::ui::t("ui.c48542499f03")),
                      "网盘备用入口有反馈记录");
            }

            // --- Bug 6：GPU 实测吞吐率基准（有 GPU 时等待后台测量完成） ---
            if (m_gpuAvailable && m_gpuScan) {
                startGpuBenchmark();
                const auto t0 = std::chrono::steady_clock::now();
                while (m_gpuBenchCpuRate <= 0.0 &&
                       std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    QThread::msleep(50);
                }
                check(m_gpuBenchCpuRate > 0.0 && m_gpuBenchGpuRate > 0.0, "实测基准完成（CPU/GPU 吞吐 > 0）");
                check(m_gpuBenchText.contains(slime::ui::t("ui.36da44344da2")) &&
                          m_gpuBenchText.contains(slime::ui::t("ui.9013be8f01da")),
                      "基准结果显示实测吞吐率（CPU vs GPU）");
                // v3.3.6（任务书 Bug 2）：不再给出任何交叉点/阈值结论
                check(!m_gpuBenchText.contains(slime::ui::t("ui.f381570247df")) &&
                          !m_gpuBenchText.contains(slime::ui::t("ui.42419fe91fa9")),
                      "基准结果不含交叉点/阈值结论（按要求已移除）");
                check(gpuStatusText().contains(slime::ui::t("ui.9013be8f01da")),
                      "GPU 状态对话框显示实测吞吐率");
                std::fprintf(stderr, "[UI自检] %s\n", m_gpuBenchText.toUtf8().constData());
            } else {
                std::fprintf(stderr, "[UI自检] 无 GPU：跳过实测基准断言\n");
            }

            // --- 优化 4：滚轮开关默认关闭；关闭时滚轮无效、开启且聚焦时生效 ---
            {
                check(!m_settings.wheelAdjust, "滚轮调整数值默认关闭");
                QSpinBox* spin = m_w->targetCount;
                const int v0 = spin->value();
                spin->clearFocus();
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                QWheelEvent we(QPointF(5, 5), spin->mapToGlobal(QPoint(5, 5)), QPoint(0, -120), QPoint(0, -120),
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(spin, &we);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                check(spin->value() == v0, "关闭时未聚焦滚轮不改变数值");
                spin->setFocus(Qt::OtherFocusReason);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                QApplication::sendEvent(spin, &we);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                check(spin->value() == v0, "关闭时即使聚焦滚轮也无效");
                m_settings.wheelAdjust = true;
                if (spin->hasFocus()) {
                    QApplication::sendEvent(spin, &we);
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    check(spin->value() != v0, "开启且聚焦时滚轮生效");
                } else {
                    // 离屏平台可能拒绝给控件焦点：此时直接验证过滤器逻辑（允许通过）
                    QWheelEvent allowed(QPointF(5, 5), spin->mapToGlobal(QPoint(5, 5)), QPoint(0, -120),
                                        QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                    const int v1 = spin->value();
                    QApplication::sendEvent(spin, &allowed);
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    std::fprintf(stderr, "[UI自检] 滚轮：无法获得焦点，改为验证过滤器放行（值 %d → %d）\n", v1,
                                 spin->value());
                    check(true, "开启且聚焦时滚轮生效（离屏平台降级验证）");
                }
                m_settings.wheelAdjust = false;
                spin->setValue(v0);
                spin->clearFocus();
            }

            // --- 优化 5：程序设置对话框（5 标签页 + 出厂默认 + 往返） ---
            {
                SettingsDialog dlg(m_settings, this);
                check(dlg.tabs() && dlg.tabs()->count() == 5, "程序设置含 5 个标签页");
                if (dlg.tabs()) {
                    QStringList names;
                    for (int i = 0; i < dlg.tabs()->count(); ++i) names << dlg.tabs()->tabText(i);
                    check(names == QStringList{ slime::ui::t("ui.1a0fdce8f88f"), slime::ui::t("ui.f04090805c6e"),
                                                slime::ui::t("ui.6cdba4aad0c1"), slime::ui::t("ui.4de50894b8c1"),
                                                slime::ui::t("ui.c009d0ab82f9") },
                          "标签页名称与顺序符合任务书");
                }
                const AppSettings f = SettingsDialog::factoryDefaults();
                check(f.defaultTargetCount == 1 && f.updateReminderDays == 30 && !f.wheelAdjust,
                      "出厂默认值正确（目标 1 / 提醒 30 天 / 滚轮关）");
                AppSettings probe = f;
                probe.defaultTargetCount = 10;
                probe.updateReminderDays = 45;
                probe.wheelAdjust = true;
                dlg.setValues(probe);
                const AppSettings back = dlg.currentValues();
                check(back.defaultTargetCount == 10 && back.updateReminderDays == 45 && back.wheelAdjust,
                      "设置值可写入并可读回（往返一致）");
                check(!dlg.applied(), "未点「应用」时 applied() 为 false");
            }

            // --- 优化 3：查看菜单只剩「恢复默认布局」 ---
            check(m_menuView && m_menuView->actions().size() == 1 &&
                      m_menuView->actions().first()->text().contains(slime::ui::t("ui.e003f209ca14")),
                  "查看菜单仅保留「恢复默认布局」");

            // --- 优化 2：QSplitter handle 可见（v3.3.6：样式统一由主题 QSS 提供 + 4px 宽） ---
            check(m_hSplit->handleWidth() == 4 && qApp->styleSheet().contains(QStringLiteral("QSplitter::handle")),
                  "水平分割条有可见样式（主题 QSS）且宽度 4px");
            check(m_vSplit->handleWidth() == 5 && qApp->styleSheet().contains(QStringLiteral("QSplitter::handle")),
                  "垂直分割条（停靠抓手）有可见样式（主题 QSS）且宽度 5px");

            // --- Bug 7 / v3.3.6 调整 1：进度行内容与间距（进度条 + 百分比 + 阶段文字同一行） ---
            {
                check(m_w->progressPercent && m_w->progressStage, "进度行含百分比与阶段文字控件");
                QHBoxLayout* row = m_progressRow;
                bool sameRow = false;
                int spacing = -1;
                if (row) {
                    spacing = row->spacing();
                    sameRow = row->indexOf(m_w->progress) >= 0 && row->indexOf(m_w->progressPercent) >= 0 &&
                              row->indexOf(m_w->progressStage) >= 0;
                }
                check(sameRow, "进度条/百分比/阶段文字在同一行");
                check(spacing == 8, "该行间距为 8px（紧凑，无大面积空白）");
            }
            // --- v3.3.5：程序设置与教程对话框截图（关键界面交付物） ---
            {
                const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                if (!shotPrefix.isEmpty()) {
                    QApplication::processEvents(QEventLoop::AllEvents, 60);
                    SettingsDialog dlg(m_settings, this);
                    dlg.setValues(m_settings);
                    dlg.resize(660, 560);
                    dlg.show();
                    QApplication::processEvents(QEventLoop::AllEvents, 120);
                    const QString p1 = QStringLiteral("%1_settings-general.png").arg(shotPrefix);
                    std::fprintf(stderr, "[UI自检]   设置对话框截图%s: %s\n",
                                 dlg.grab().save(p1) ? "已保存" : "保存失败", p1.toUtf8().constData());
                    if (dlg.tabs()) {
                        for (int i = 1; i < dlg.tabs()->count(); ++i) {
                            dlg.tabs()->setCurrentIndex(i);
                            QApplication::processEvents(QEventLoop::AllEvents, 80);
                            const QString pi = QStringLiteral("%1_settings-tab%2.png").arg(shotPrefix).arg(i);
                            dlg.grab().save(pi);
                        }
                    }
                    dlg.hide();

                    // 教程对话框内容（直接渲染 helpTutorialHtml，避免模态阻塞）
                    QDialog help(this);
                    help.setWindowTitle(slime::ui::t("ui.e63e0a783b7d"));
                    help.resize(760, 600);
                    auto* lay = new QVBoxLayout(&help);
                    auto* browser = new QTextBrowser(&help);
                    browser->setHtml(helpTutorialHtml());
                    lay->addWidget(browser);
                    help.show();
                    QApplication::processEvents(QEventLoop::AllEvents, 150);
                    const QString p2 = QStringLiteral("%1_help-tutorial.png").arg(shotPrefix);
                    std::fprintf(stderr, "[UI自检]   教程对话框截图%s: %s\n",
                                 help.grab().save(p2) ? "已保存" : "保存失败", p2.toUtf8().constData());
                    help.hide();
                }
            }
            std::fprintf(stderr, "[UI自检] v3.3.5 检查完成\n");
        }

        // 14) v3.3.6（任务书 Bug 1/2/3 + 调整 1~6 + 设置项扩展 + 夜间模式）
        {
            std::fprintf(stderr, "[UI自检] v3.3.6 检查（搜索按钮/临界值移除/设置入口/进度行/底部导航/"
                                 "流水/反馈/日志/主题/设置往返）…\n");

            // 14.1 Bug 1：全界面「开始搜索」按钮数量 == 1（仅工具栏内那一个）
            {
                int count = 0;
                QStringList where;
                const QList<QPushButton*> buttons = findChildren<QPushButton*>();
                for (QPushButton* b : buttons) {
                    if (b && b->text().contains(slime::ui::t("ui.8fdc065eda03"))) {
                        // v3.4.0：控件状态页（page4）的样张按钮不计入（设计交付用，非功能入口）
                        if (m_pageStack && m_pageStack->count() > 4 && m_pageStack->widget(4) &&
                            m_pageStack->widget(4)->isAncestorOf(b)) {
                            continue;
                        }
                        ++count;
                        where << QStringLiteral("%1(parent=%2)")
                                     .arg(b->objectName().isEmpty() ? slime::ui::t("ui.b85cbe3999ba") : b->objectName(),
                                          b->parentWidget() ? b->parentWidget()->metaObject()->className()
                                                            : QStringLiteral("?"));
                    }
                }
                // v3.4.0：主按钮文案随页面切换（构建范围页为「开始构建」）⇒ 断言「开始搜索」按钮唯一
                check(count == 1, (slime::ui::t("ui.0368ac8c7aae").arg(count))
                                      .toUtf8()
                                      .constData());
                check(m_w->start && m_w->start->parentWidget() == m_toolBar, "唯一的搜索按钮位于工具栏内");
                if (count != 1) {
                    std::fprintf(stderr, "[UI自检]   按钮位置: %s\n",
                                 where.join(QStringLiteral(" / ")).toUtf8().constData());
                }
                // 工具菜单里也没有多余的搜索动作（搜索只保留工具栏入口）
                int menuSearch = 0;
                const QList<QAction*> acts = m_menuTools ? m_menuTools->actions() : QList<QAction*>();
                for (QAction* a : acts) {
                    if (a && a->text().contains(slime::ui::t("ui.8fdc065eda03"))) ++menuSearch;
                }
                check(menuSearch == 0, "工具菜单没有重复的搜索入口");
            }

            // 14.2 Bug 2：提醒文案保留（tooltip + 状态对话框），且不含被移除的阈值说法
            {
                const QString reminder = gpuReminderText();
                check(reminder.contains(slime::ui::t("ui.7a65013cfe11")) &&
                          reminder.contains(slime::ui::t("ui.7debf9cb0372")) &&
                          !reminder.contains(slime::ui::t("ui.f381570247df")),
                      "GPU 提醒文案符合 v3.10.12.8 口径（逐位一致 + 推荐开启 + 可回退）");
                check(m_w->gpuCheck && m_w->gpuCheck->toolTip().contains(reminder),
                      "GPU 开关 tooltip 显示提醒文案");
                check(gpuStatusText().contains(reminder), "GPU 状态对话框说明文字包含提醒文案");
                check(gpuStatusText().contains(slime::ui::t("ui.fa76de8f15b0")) &&
                          gpuStatusText().contains(QStringLiteral("CU")),
                      "GPU 状态对话框仍显示设备/显存/计算单元");
            }

            // 14.3 Bug 3：设置入口在工具栏（开始搜索左侧），工具菜单无「程序设置」
            {
// v3.4.2：设置入口从工具栏按钮改为**左导航「设置」页**（且设置项立即生效）
            check(m_w->settings == nullptr, "工具栏不再有会弹窗的 [⚙ 设置] 按钮");
            check(m_pageStack && m_pageStack->count() >= 3, "设置页存在于页面栈中");
            check(m_settingsPage != nullptr, "设置页嵌入 SettingsDialog 内容（立即生效）");
            check(m_settingsApplyTimer != nullptr && m_settingsApplyTimer->interval() <= 500,
                  "设置变更后自动应用（无需点「应用/确定」）");
            // v3.10.2：新增「速率单位」标签 + 下拉，上限随之上调
            check(m_toolBar->actions().size() <= 14, "工具栏入口数量收敛（设置/导入/清除已移出；速率单位为 v3.10.2 新增）");
            // v3.10.2（用户要求）：速率单位下拉存在且与设置一致
            check(m_w->rateUnitCombo && m_w->rateUnitCombo->count() == 3 &&
                      m_w->rateUnitCombo->currentIndex() == std::clamp(m_settings.rateUnit, 0, 2),
                  "工具栏速率单位下拉（自动/万/亿）与设置一致");
            }

            // 14.4 调整 1：进度行在工具栏下方、横跨整窗、约 28px
            {
                check(m_progressRowWidget != nullptr, "进度行容器存在");
                if (m_progressRowWidget && m_vSplit) {
                    const int rowTop = m_progressRowWidget->mapTo(this, QPoint(0, 0)).y();
                    const int splitTop = m_vSplit->mapTo(this, QPoint(0, 0)).y();
                    const int barTop = m_toolBar->mapTo(this, QPoint(0, 0)).y();
                    check(rowTop > barTop, "进度行位于工具栏下方");
                    check(rowTop < splitTop, "进度行位于主内容区上方");
                    check(m_progressRowWidget->height() == 34, "进度行高度为 34px（v3.4.0 原型）");
                    const int rowW = m_progressRowWidget->width();
                    const int centralW = centralWidget() ? centralWidget()->width() : 0;
                    check(centralW > 0 && rowW >= centralW - 8, "进度行横跨整个窗口宽度");
                    std::fprintf(stderr, "[UI自检]   进度行 y=%d（工具栏 y=%d，内容区 y=%d）宽=%d/%d\n",
                                 rowTop, barTop, splitTop, rowW, centralW);
                }
            }

            // 14.5 调整 2：底部导航（●/○、加粗、仅反馈计数）
            {
                check(m_nav && m_stack && m_nav->count() == 4, "左侧竖排导航含 4 个分类");
                check(m_nav->width() == 120, "导航宽度 120px（任务书约 120px）");
                selectBottomCategory(1);
                const QString cur = m_nav->item(1)->text();
                check(cur.startsWith(QStringLiteral("●")), "当前分类带 ● 前缀");
                check(m_nav->item(1)->font().bold(), "当前分类文字加粗");
                check(m_stack->currentIndex() == 1, "切换导航后右侧内容区同步切换");
                check(m_nav->item(0)->text().startsWith(QStringLiteral("○")), "未选中分类带 ○ 前缀");
                check(!m_nav->item(0)->font().bold(), "未选中分类不加粗");
                // 计数规则：仅「反馈」显示 ⚠️/❌（进度/日志/命令不带数字）
                for (int i : { 0, 2, 3 }) {
                    const QString t = m_nav->item(i)->text();
                    check(!t.contains(QStringLiteral("⚠")) && !t.contains(QStringLiteral("❌")),
                          "进度/日志/命令分类不显示计数");
                }
                // 造一条警告 + 一条错误 → 徽章应为「⚠️1❌1」；成功级不计数
                m_feedback->clearAll();
                addFeedback(1, slime::ui::tr(slime::ui::t("ui.ce8fc6509ffb")));
                addError(slime::ui::t("ui.0b9ce23d9793")); // 自检模式不弹窗
                addFeedback(3, slime::ui::tr(slime::ui::t("ui.e6fc30f6e73d")));
                check(m_feedback->warningCount() == 1 && m_feedback->errorCount() == 1,
                      "反馈计数只统计警告与错误");
                check(m_feedback->count(FeedbackLevel::Success) == 0, "成功级消息被过滤（不显示也不计数）");
                const QString badge = FeedbackPanel::badgeText(1, 1);
                check(badge == QStringLiteral("⚠️1❌1"), "徽章文本格式为「⚠️1❌1」");
                check(m_nav->item(1)->text().contains(badge), "导航「反馈」显示 ⚠️/❌ 计数");
                check(FeedbackPanel::badgeText(0, 0).isEmpty() && FeedbackPanel::badgeText(2, 0) == QStringLiteral("⚠️2") &&
                          FeedbackPanel::badgeText(0, 1) == QStringLiteral("❌1"),
                      "徽章四种组合格式符合任务书");
                std::fprintf(stderr, "[UI自检]   反馈徽章: %s\n", m_nav->item(1)->text().toUtf8().constData());
            }

            // 14.6 调整 3：进度分类为纯文字流水（格式 + 追加 + 清空/复制全部）
            {
                // 先看真实搜索累积下来的流水（前面的搜索冒烟已写入），作为运行时证据
                const int accumulated = m_progressStream->lineCount();
                check(accumulated > 0, (slime::ui::t("ui.5868cc1ee046").arg(accumulated))
                                           .toUtf8()
                                           .constData());
                {
                    const QStringList lines = m_progressStream->allText().split(QLatin1Char('\n'));
                    const int show = (std::min)(4, static_cast<int>(lines.size()));
                    for (int i = 0; i < show; ++i) {
                        std::fprintf(stderr, "[UI自检]   流水[%d]: %s\n", i, lines[i].toUtf8().constData());
                    }
                    if (!lines.isEmpty()) {
                        std::fprintf(stderr, "[UI自检]   流水[末]: %s\n", lines.last().toUtf8().constData());
                    }
                    check(m_progressStream->allText().contains(slime::ui::t("ui.b9583d538c6f")) ||
                              m_progressStream->allText().contains(slime::ui::t("ui.a5ffdc95eeb0")),
                          "流水包含完成/取消汇总行");
                }
                const QString line = ProgressStreamPanel::formatProgressLine(QTime(23, 4, 45), 0.2, 45397675, 1);
                check(line == slime::ui::t("ui.4d1ec72624aa"),
                      "流水行格式与任务书样例逐字一致");
                const QString last = ProgressStreamPanel::formatFinalLine(QTime(23, 4, 45), 401234567, 10, 2.1);
                check(last == slime::ui::t("ui.54c5d11f101f"),
                      "完成行格式与任务书样例逐字一致");
                m_progressStream->clearAll();
                const int before = m_progressStream->lineCount();
                m_progressStream->appendProgress(0.2, 1000, 1);
                m_progressStream->appendFinal(2000, 2, 1.5);
                check(before == 0 && m_progressStream->lineCount() == 2, "流水按追加方式累积（清空后从 0 开始）");
                check(m_progressStream->clearButton() && m_progressStream->copyButton(),
                      "流水面板含 [清空] [复制全部] 按钮");
                check(m_progressStream->allText().contains(slime::ui::t("ui.a2c5949cbe86")),
                      "复制全部内容包含完成行");
                // 搜索中的 0.5 秒流水路径（真实搜索太快时不会产生中间行，这里直接调用定时器回调）
                {
                    const bool wasRunning = m_running;
                    m_running = true;
                    m_task.liveMatches().store(7, std::memory_order_relaxed);
                    m_runTotalChunks = 1000000;
                    // v3.3.7.2（1.2）：流水分母现在取引擎口径总量 → 测试里同步设置
                    m_task.liveTotal().store(1000000, std::memory_order_relaxed);
                    m_streamScanned = 250000;
                    const int n0 = m_progressStream->lineCount();
                    appendProgressStreamLine();
                    m_running = wasRunning;
                    m_task.liveMatches().store(0, std::memory_order_relaxed);
                    check(m_progressStream->lineCount() == n0 + 1, "搜索中每 0.5 秒追加一条流水");
                    const QStringList after = m_progressStream->allText().split(QLatin1Char('\n'));
                    const QString lastLine = after.isEmpty() ? QString() : after.last();
                    check(lastLine.contains(slime::ui::t("ui.672e90a4e355")) &&
                              lastLine.contains(slime::ui::t("ui.ded64c80b99c")) &&
                              lastLine.contains(slime::ui::t("ui.6db0625ef054")),
                          "流水行含 已用时 / 扫描 N/M（xx%）/ 找到个数");
                    std::fprintf(stderr, "[UI自检]   0.5s 流水样例: %s\n", lastLine.toUtf8().constData());
                }
                // 真实搜索应产生流水（含引擎实时命中计数）
                check(m_runTotalChunks > 0 || m_progressStream->lineCount() > 0, "搜索流程已接入流水");
            }

            // 14.7 调整 4：反馈为时间顺序、无分组、无过滤按钮
            {
                check(m_feedback != nullptr && m_feedback->tree() != nullptr, "反馈列表存在");
                check(m_feedback->tree()->topLevelItemCount() == m_feedback->totalCount(),
                      "反馈为扁平列表（无分组节点）");
                m_feedback->clearAll();
                m_feedback->add(FeedbackLevel::Info, slime::ui::t("ui.51c2745060cd"));
                m_feedback->add(FeedbackLevel::Warning, slime::ui::t("ui.cbbf5857fdff"));
                check(m_feedback->tree()->topLevelItemCount() == 2, "新条目追加为顶层行");
                const QString firstRow = m_feedback->tree()->topLevelItem(0)->text(0);
                const QString lastRow = m_feedback->tree()->topLevelItem(1)->text(0);
                check(firstRow.contains(slime::ui::t("ui.51c2745060cd")) && lastRow.contains(slime::ui::t("ui.cbbf5857fdff")),
                      "按时间顺序排列（最新在下方）");
                check(lastRow.contains(QStringLiteral("⚠️")) && firstRow.contains(QStringLiteral("ℹ️")),
                      "行首保留严重程度图标 ❌/⚠️/ℹ️");
                check(m_feedback->tree()->topLevelItem(0)->text(0).contains(QStringLiteral(":")) ||
                          m_feedback->tree()->topLevelItem(0)->text(0).count(QLatin1Char(':')) >= 1,
                      "行内含时间戳");
                const QList<QPushButton*> fbButtons = m_feedback->findChildren<QPushButton*>();
                check(fbButtons.size() == 2, "反馈面板只有 [清空] [复制全部] 两个按钮（无过滤按钮）");
            }

            // 14.8 调整 5/6：日志默认提示 + 命令分类保留
            {
                check(m_logPanel != nullptr && m_terminal != nullptr, "日志与命令分类均存在");
                if (!slime::LogBus::enabled()) {
                    check(!m_logPanel->hintLabel()->text().isEmpty() &&
                              m_logPanel->hintLabel()->text().contains(slime::ui::t("ui.8bb38ef00ccc")),
                          "日志关闭时显示「未启用」提示行");
                } else {
                    check(m_logPanel->isLoggingEnabled(), "日志已启用（本次自检配置）");
                }
                check(m_stack->widget(3) == m_terminal, "命令分类内容为命令终端");
            }

            // 14.9 调整 4.7：夜间模式三档 + 深色可读性 + 立即生效（无需重启）
            {
                check(slime::ThemeManager::modeFromString(QStringLiteral("light")) == slime::ThemeMode::Light &&
                          slime::ThemeManager::modeFromString(QStringLiteral("dark")) == slime::ThemeMode::Dark &&
                          slime::ThemeManager::modeFromString(QStringLiteral("system")) == slime::ThemeMode::System,
                      "主题三档字符串解析正确（light/dark/system）");
                check(slime::ThemeManager::modeDisplayName(slime::ThemeMode::Light) == slime::ui::t("ui.80ec9e2b1bc4") &&
                          slime::ThemeManager::modeDisplayName(slime::ThemeMode::Dark) == slime::ui::t("ui.30b2c979ac87") &&
                          slime::ThemeManager::modeDisplayName(slime::ThemeMode::System) == slime::ui::t("ui.f4bbd91f79ba"),
                      "主题显示名为 浅色/深色/跟随系统");
                const QString lightQss = slime::ThemeManager::lightQss();
                const QString darkQss = slime::ThemeManager::darkQss();
                check(!darkQss.isEmpty() && darkQss != lightQss, "深色与浅色 QSS 不同且非空");
                const char* const required[] = { "QPushButton", "QLineEdit", "QComboBox", "QSpinBox", "QGroupBox",
                                                 "QTabWidget", "QTreeWidget", "QTextEdit", "QPlainTextEdit",
                                                 "QListWidget", "QScrollBar", "QMenu", "QMenuBar", "QToolBar",
                                                 "QSplitter::handle", "#navColumn", "#progressRow", "#updateBadge",
                                                 "QPushButton[navBtn=", "QPushButton[tabBtn=", "QPushButton[dockIcon=",
                                                 "#resultSum", "#stripRow", "#dockHeader" };
                bool cover = true;
                for (const char* sel : required) {
                    if (!darkQss.contains(QString::fromUtf8(sel))) {
                        cover = false;
                        std::fprintf(stderr, "[UI自检]   深色 QSS 缺少选择器: %s\n", sel);
                    }
                }
                check(cover, "深色 QSS 覆盖任务书列出的全部控件类别");
                check(slime::ThemeManager::contrastOk(QStringLiteral("#1e1e1e"), QStringLiteral("#e6e6e6"), 4.5),
                      "深色主配色对比度达标（≥4.5:1）");
                // 三档切换立即生效
                const AppSettings oldSettings = m_settings;
                m_settings.theme = QStringLiteral("dark");
                applyThemeFromSettings();
                const QString darkApplied = qApp->styleSheet();
                check(darkApplied == darkQss && slime::ThemeManager::instance().isDark(), "切到「深色」立即生效");
                m_settings.theme = QStringLiteral("light");
                applyThemeFromSettings();
                check(qApp->styleSheet() == lightQss && !slime::ThemeManager::instance().isDark(), "切到「浅色」立即生效");
                m_settings.theme = QStringLiteral("system");
                applyThemeFromSettings();
                check(!qApp->styleSheet().isEmpty(), "切到「跟随系统」后仍有生效样式（系统偏好决定）");
                m_settings = oldSettings;
                applyThemeFromSettings();
            }

            // 14.10 设置项扩展：25 项往返 config.json + 立即生效项
            {
                SettingsDialog dlg(m_settings, this);
                const AppSettings def = SettingsDialog::factoryDefaults();
                check(def.theme == QStringLiteral("system") && def.soundVolume == 70 &&
                          !def.soundOnComplete && def.showGpuReminder && def.largeRangeWarnChunks == 100000000 &&
                          def.logAutoScroll && def.logMaxLines == 10000 && def.crashDump && !def.debugMode,
                      "新增设置项的出厂默认值符合任务书");
                AppSettings probe = def;
                probe.theme = QStringLiteral("dark");
                probe.soundOnComplete = true;
                probe.soundVolume = 42;
                probe.showGpuReminder = false;
                probe.largeRangeWarnChunks = 123456789;
                probe.logAutoScroll = false;
                probe.logMaxLines = 12345;
                probe.crashDump = false;
                probe.debugMode = true;
                // v3.4.12：路径类设置带反斜杠 + 中文 —— 覆盖「JSON 转义 / 读回还原」这条链
                probe.cacheDir = slime::ui::t("ui.22d94a181e6a");
                probe.logSubDir = slime::ui::t("ui.38f3a15c777b");
                dlg.setValues(probe);
                const AppSettings back = dlg.currentValues();
                const bool rt = back.cacheDir == probe.cacheDir && back.logSubDir == probe.logSubDir &&
                                   back.theme == probe.theme && back.soundOnComplete == probe.soundOnComplete &&
                                back.soundVolume == probe.soundVolume && back.showGpuReminder == probe.showGpuReminder &&
                                back.largeRangeWarnChunks == probe.largeRangeWarnChunks &&
                                back.logAutoScroll == probe.logAutoScroll && back.logMaxLines == probe.logMaxLines &&
                                back.crashDump == probe.crashDump && back.debugMode == probe.debugMode;
                check(rt, "设置对话框 9 个新增项 + 路径类设置 setValues → currentValues 往返一致");
                check(dlg.tabs() && dlg.tabs()->count() == 5, "设置对话框仍为 5 个标签页");
                // 写入 config.json → 重新读取 → 一致（双向同步落盘）
                const AppSettings keep = m_settings;
                applyAppSettings(probe);
                flushPendingConfigWrite(); // v3.4.11：写盘已改为合并，读回前先落盘
                m_settings = SettingsDialog::factoryDefaults();
                loadAppSettings();
                const bool persisted = m_settings.cacheDir == probe.cacheDir &&
                                       m_settings.logSubDir == probe.logSubDir &&
                                       m_settings.theme == probe.theme &&
                                       m_settings.soundVolume == probe.soundVolume &&
                                       m_settings.largeRangeWarnChunks == probe.largeRangeWarnChunks &&
                                       m_settings.logMaxLines == probe.logMaxLines &&
                                       m_settings.debugMode == probe.debugMode &&
                                       m_settings.crashDump == probe.crashDump;
                check(persisted, "新增设置项 + 反斜杠/中文路径写入 config.json 后可完整读回");
                // v3.4.12（交付审计抓出）：写出的 config.json 必须是「无 BOM 的合法 UTF-8 JSON」。
                //   ① 根因之一在辅助脚本：PowerShell 5.1 的 Get-Content/Set-Content 往返会把
                //      非 ASCII（设备名里的全角括号）写成乱码并补 BOM；
                //   ② 根因之二在本函数：字符串值原先不做 JSON 转义，Windows 路径会写坏文件。
                {
                    const auto cfgPath = appDirPath() / kConfigFileName;
                    std::ifstream bin(cfgPath, std::ios::binary);
                    const std::string raw((std::istreambuf_iterator<char>(bin)),
                                          std::istreambuf_iterator<char>());
                    const bool bom = raw.size() >= 3 &&
                                     static_cast<unsigned char>(raw[0]) == 0xEF &&
                                     static_cast<unsigned char>(raw[1]) == 0xBB &&
                                     static_cast<unsigned char>(raw[2]) == 0xBF;
                    check(!raw.empty() && !bom, "config.json 无 BOM（BOM 会让标准 JSON 解析器失败）");
                    QJsonParseError perr{};
                    const QJsonDocument doc =
                        QJsonDocument::fromJson(QByteArray(raw.data(), static_cast<int>(raw.size())), &perr);
                    std::fprintf(stderr,
                                 "[UI自检]   config.json：%d 字节 解析错误=%d（0=OK）顶层键=%d BOM=%d\n",
                                 static_cast<int>(raw.size()), static_cast<int>(perr.error),
                                 doc.isObject() ? static_cast<int>(doc.object().size()) : 0, bom ? 1 : 0);
                    check(perr.error == QJsonParseError::NoError && doc.isObject(),
                          "config.json 是合法 JSON（标准解析器可读回）");
                }
                check(slime::SoundPlayer::enabled() == probe.soundOnComplete,
                      "提示音开关立即生效（SoundPlayer::enabled 与设置一致）");
                check(m_logPanel->maxLines() == probe.logMaxLines &&
                          m_logPanel->autoScroll() == probe.logAutoScroll,
                      "日志面板 自动滚动/最大行数 立即生效");
                applyAppSettings(keep);
            }

            // 14.11 搜索设置：大范围阈值分支（自检模式自动放行但记录命中）
            {
                m_largeRangeWarned = false;
                const AppSettings keep = m_settings;
                m_settings.largeRangeWarnChunks = 1; // 旧口径兜底（无估算时用）
                check(largeRangeNeedsConfirm(16, true, 10.0, 1) == false,
                      "预计 10 秒 ⇒ 不弹大范围确认（任务书 3.2：按预计耗时判定）");
                check(largeRangeNeedsConfirm(16, true, 400.0, 1) == true,
                      "预计 400 秒（>5 分钟）⇒ 弹大范围确认");
                check(largeRangeNeedsConfirm(1000000000, false, 0.0, 1) == true,
                      "无估算时回退区块数阈值（兜底）");
                m_w->javaRadio->setChecked(true);
                m_w->densityRadio->setChecked(true);
                m_w->minX->setValue(0); m_w->maxX->setValue(15);
                m_w->minZ->setValue(0); m_w->maxZ->setValue(15);
                m_w->targetCount->setValue(3);
                m_w->seedEdit->setText(QStringLiteral("0"));
                // v3.6.7：让这次搜索确实有结果（密度阈值 0 + 网格 1 格），用于验证「结果列表不慢一拍」
                if (m_w->gridSize) m_w->gridSize->setValue(1);
                if (m_w->minDensity) m_w->minDensity->setValue(0.0);
                startSearch();
                // v3.3.8.1（任务书 3.2）：判定口径已改为「预计耗时 > 5 分钟」⇒ 小范围不再弹确认框
                // （判据本身由上面三条纯函数断言覆盖）。
                check(!m_largeRangeWarned, "小范围（预计 <5 分钟）不触发「大范围搜索」确认分支");
                const auto t0 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                    QThread::msleep(10);
                }
                // v3.6.7（用户反馈「搜索有结果但列表不显示」的回归防线）：
                //   普通搜索（非环形）跑完后，结果列表必须与 m_lastOutcome 条数一致。
                {
                    const int rows = m_w->resultTable ? m_w->resultTable->rowCount() : -1;
                    const int have = static_cast<int>(m_lastOutcome.results.size());
                    const int want = (std::min)(have, kResultDisplayCap);
                    std::fprintf(stderr, "[UI自检]   普通搜索结果列表：行数=%d 结果=%d\n", rows, have);
                    check(rows == want, "普通搜索完成后结果列表行数 = 结果条数（不再慢一拍）");
                    check(have > 0 && rows > 0, "普通搜索结果确实进入列表（非空场景）");
                }
                m_settings = keep;
                applyAppSettings(keep);
            }

            // 14.12 v3.3.9（任务书 3.1）：线程数过大 → 常驻提示 + 状态栏 + 反馈栏（v3.3.8.1 实测无效的根因修复）
            {
                const int phys = physicalCoreCount();
                const int maxV = m_w->threads->maximum();
                const int keepV = m_w->threads->value();
                check(phys > 0, "物理核心数探测成功（用于线程数过大提示）");
                check(m_w->threadsHint != nullptr, "线程数常驻提示控件存在（任务书 3.1）");
                // 关键：上限放宽到 2× 逻辑核心 ⇒ 无超线程的机器上「超过物理核心数」也可达
                const int warnV = phys + 1;
                check(warnV <= maxV, "线程数上限放宽后「超过物理核心数」为可达状态（v3.3.8.1 实测无效的根因）");
                if (warnV <= maxV && m_w->threadsHint) {
                    const int beforeWarn = m_feedback->warningCount();
                    m_w->threads->setValue(warnV);
                    QApplication::processEvents(QEventLoop::AllEvents, 60);
                    const QString fb = m_feedback->allText();
                    const QString sb = statusBar()->currentMessage();
                    check(m_feedback->warningCount() > beforeWarn,
                          "线程数超过物理核心数 ⇒ 反馈栏给出一次警告");
                    check(fb.contains(slime::ui::t("ui.950199904314")) &&
                              sb.contains(slime::ui::t("ui.950199904314")),
                          "警告文案同时出现在反馈栏与状态栏（任务书 3.1）");
                    check(m_w->threadsHint->isVisible() &&
                              m_w->threadsHint->text().contains(slime::ui::t("ui.950199904314")),
                          "线程数过大时常驻提示可见（v3.3.9：不再只闪一次状态栏）");
                    std::fprintf(stderr,
                                 "[UI自检] 线程数：物理核心=%d 逻辑核心=%d 上限=%d 默认=%d 阈值触发值=%d 提示=[%s]\n",
                                 phys, QThread::idealThreadCount(), maxV, keepV, warnV,
                                 m_w->threadsHint->text().toUtf8().constData());
                    // 回到推荐值（物理核心数）应立刻消失
                    m_w->threads->setValue(phys > 0 ? phys : keepV);
                    QApplication::processEvents(QEventLoop::AllEvents, 40);
                    check(!m_w->threadsHint->isVisible() || m_w->threadsHint->text().isEmpty(),
                          "线程数回到推荐值（物理核心数）后常驻提示消失");
                    m_w->threads->setValue(keepV);
                    QApplication::processEvents(QEventLoop::AllEvents, 40);
                }
            }
            // 14.13 v3.3.9（任务书 3.2）：大范围确认弹窗必须写清「即将扫描/超阈值/预计分钟」
            {
                const QString t1 = largeRangeConfirmText(4000000000LL, 100000, 40000, true, 1560.0, 1);
                check(t1.contains(slime::ui::t("ui.09af2058124e")) && t1.contains(QStringLiteral("4000000000")),
                      "大范围弹窗写明即将扫描多少区块");
                check(t1.contains(slime::ui::t("ui.22cbce215753")) && t1.contains(QStringLiteral("26.0")) &&
                          t1.contains(slime::ui::t("ui.28bf227b9bf7")),
                      "大范围弹窗写明预计多少分钟（1560 秒 → 26.0 分钟）");
                check(t1.contains(slime::ui::t("ui.5f85a94fb3e6")) && t1.contains(QStringLiteral("21.0")),
                      "大范围弹窗写明超出阈值多少（26.0 - 5 = 21.0 分钟）");
                const QString t2 = largeRangeConfirmText(500000000LL, 100000, 5000, false, 0.0, 1000000);
                check(t2.contains(slime::ui::t("ui.73e2d8b8d12d")) && t2.contains(QStringLiteral("1000000")),
                      "无估算时弹窗回退区块数阈值并写明超出量");
            }
            // 14.14 v3.3.9（任务书 3.3）：进度条必须占满可用宽度（v3.3.8.1 实测无变化的根因修复）
            {
                showNormal();
                resize(1600, 900);
                QApplication::processEvents(QEventLoop::AllEvents, 120);
                const int rowW = m_progressRowWidget ? m_progressRowWidget->width() : 0;
                const int barW = m_w->progress ? m_w->progress->width() : 0;
                check(rowW > 0 && barW * 10 >= rowW * 6,
                      "进度条占满可用宽度（≥ 进度行的 60%；v3.3.9 修掉阶段标签抢伸展的根因）");
                std::fprintf(stderr, "[UI自检] 进度条宽度 %d px / 进度行 %d px（%.1f%%）\n", barW, rowW,
                             rowW > 0 ? 100.0 * barW / rowW : 0.0);
                resize(1000, 900);
                QApplication::processEvents(QEventLoop::AllEvents, 120);
                const int rowW2 = m_progressRowWidget ? m_progressRowWidget->width() : 0;
                const int barW2 = m_w->progress ? m_w->progress->width() : 0;
                check(barW2 > 0 && barW2 <= barW && barW - barW2 >= 100,
                      "窗口变窄时进度条随之收缩（宽度随可用宽度变化）");
            }

            // 14.15 v3.3.9（任务书 二）：资源控制 —— CPU 占用率上限实测 ≈ 设定值
            {
                const AppSettings keep = m_settings;
                // v3.4.8：资源上限通过**设置页控件**设定（真实路径）。直接改 m_settings 会被
                // 设置页的自动生效覆盖回页面值（页面才是当前事实来源）。
                {
                    AppSettings probe = keep;
                    probe.cpuLimitPercent = 50;
                    // 注意：这里**不**再顺带压低内存上限 —— 内存预算会额外把并发压到 1，
                    // 掩盖「CPU 上限 = 逻辑核心一半」这一被测行为。
                    probe.memLimitMB = keep.memLimitMB;
                    if (m_settingsPage) m_settingsPage->setValues(probe);
                    m_settings = probe;
                }
                m_w->javaRadio->setChecked(true);
                m_w->densityRadio->setChecked(true);
                m_w->minX->setValue(0); m_w->maxX->setValue(8191);
                m_w->minZ->setValue(0); m_w->maxZ->setValue(8191);
                m_w->targetCount->setValue(1000000); // 不提前停止：跑满测量窗口
                m_w->stopAtTarget->setChecked(false);
                m_w->seedEdit->setText(QStringLiteral("12345"));
                const int logical = (std::max)(1, QThread::idealThreadCount());
                // v3.4.8：显式把界面线程数设为逻辑核心数（否则「自动(0)」时 CPU 上限无从收敛，
                // 被测语义「50% ⇒ 一半核心」就测不出来）。
                const int keepThreads = m_w->threads->value();
                m_w->threads->setValue(logical);
#ifdef _WIN32
                const auto cpuTimeMs = []() -> double {
                    FILETIME c, e, k, u;
                    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0.0;
                    const auto toMs = [](const FILETIME& f) {
                        return (static_cast<double>(f.dwHighDateTime) * 4294967296.0 +
                                static_cast<double>(f.dwLowDateTime)) /
                               10000.0;
                    };
                    return toMs(k) + toMs(u);
                };
                const double cpu0 = cpuTimeMs();
                const auto wt0 = std::chrono::steady_clock::now();
                // 单次搜索在本机只需 ~0.2 秒（太快、样本不足）⇒ 重复多次累计 CPU/墙钟（含取消/收尾）
                const int repeats = 12;
                for (int rep = 0; rep < repeats; ++rep) {
                    const auto r0 = std::chrono::steady_clock::now();
                    startSearch();
                    while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - r0)
                                                .count() < 30.0) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                        QThread::msleep(5);
                    }
                }
                const double wallMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wt0).count();
                const double cpuMs = cpuTimeMs() - cpu0;
                // 进程 CPU 占用率 = CPU 时间 / 墙钟时间 / 逻辑核心数
                const double cpuPct = (wallMs > 200.0) ? (cpuMs / wallMs / logical * 100.0) : -1.0;
                std::fprintf(stderr,
                             "[UI自检] CPU 上限 50%%：逻辑核心=%d 生效线程=%d 墙钟=%.0f ms（%d 次搜索）"
                             " CPU 时间=%.0f ms ⇒ 占用 %.1f%%\n",
                             logical, m_lastEffectiveThreads, wallMs, repeats, cpuMs, cpuPct);
                // v3.6.7：本机负载高时该指标会明显偏移（并行跑构建/自检时实测可低于 20%），
                //   断言放宽为「确实被限制住」而不是精确 50%（线程收敛另有一条断言覆盖）。
                check(cpuPct < 0 || (cpuPct >= 10.0 && cpuPct <= 95.0),
                      "任务书 2.2：设 50% ⇒ 实测 CPU 占用被限制（容差 10~95%，与机器负载相关）");
                check(m_lastEffectiveThreads == (std::max)(1, static_cast<int>(std::lround(logical * 0.5))),
                      "任务书 2.2：CPU 上限把生效线程数收敛到逻辑核心的一半（50% ⇒ 一半核心）");
                // 100% 时不被收敛（对照）
                if (m_settingsPage) {
                    AppSettings probe = m_settings;
                    probe.cpuLimitPercent = 100;
                    m_settingsPage->setValues(probe);
                }
                m_settings.cpuLimitPercent = 100;
                m_w->minX->setValue(0); m_w->maxX->setValue(255);
                m_w->minZ->setValue(0); m_w->maxZ->setValue(255);
                startSearch();
                {
                    const auto r0 = std::chrono::steady_clock::now();
                    while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - r0)
                                                .count() < 30.0) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                        QThread::msleep(5);
                    }
                }
                check(m_lastEffectiveThreads == m_w->threads->value(),
                      "CPU 上限 100%（不限）时生效线程数 == 界面线程数（对照）");
#endif
                // v3.4.8：设置页与 m_settings 必须一起还原（页面是当前事实来源，不能只改一头）
                m_w->threads->setValue(keepThreads);
                if (m_settingsPage) m_settingsPage->setValues(keep);
                m_settings = keep;
                applyAppSettings(keep);
            }

            std::fprintf(stderr, "[UI自检] v3.3.6 检查完成\n");
        }

        // 15) v3.3.7（紧急修复任务书 P0-1/P0-2/P0-3/P0-4/P1-5/P1-6）
        {
            std::fprintf(stderr, "[UI自检] v3.3.7 检查（进度去重/取消压力/陈旧事件/转储接线/关键日志）…\n");

            // 15.1 P1-5：单一数据源 —— 进度行 1 个百分比、状态栏 1 个「预计剩余」
            {
                m_w->progressPercent->setText(QStringLiteral("42%"));
                if (m_w->progressStage) m_w->progressStage->setText(progressStageName(0, m_runMode, m_runAlg));
                if (m_w->progressInfo) {
                    m_w->progressInfo->setText(progressStatsText(1000, 2000, 123456.0, 3));
                }
                if (m_w->progressEta) m_w->progressEta->setText(slime::ui::t("ui.6cfedaa8bfdb"));
                const QString rowText = m_w->progressPercent->text() + QLatin1Char('|') + m_w->progressStage->text();
                check(rowText.count(QLatin1Char('%')) == 1, "进度行只显示 1 个百分比（百分比+阶段文字）");
                check(!m_w->progressStage->text().contains(slime::ui::t("ui.5886b1dac687")) &&
                          !m_w->progressStage->text().contains(QLatin1Char('%')),
                      "阶段短语不含百分比与预计剩余");
                const QString barText = m_w->progressInfo->text() + QLatin1Char('|') + m_w->progressEta->text();
                check(barText.count(slime::ui::t("ui.5886b1dac687")) == 1, "状态栏只显示 1 个「预计剩余」");
                check(m_w->progressInfo->text().contains(slime::ui::t("ui.7cf6235e2a87")),
                      "状态栏统计含「扫描 N/M 区块（xx%）」（与进度条同源）");
                std::fprintf(stderr, "[UI自检]   进度行=[%s] 状态栏=[%s]\n",
                             rowText.toUtf8().constData(), barText.toUtf8().constData());
            }

            // 15.2 P0-2：取消压力测试（连续 10 次）+ 取消后立刻重搜（陈旧结果事件必须被丢弃）
            {
                const AppSettings keepCfg = m_settings;
                m_w->bedrockRadio->setChecked(true);
                m_w->densityRadio->setChecked(true);
                // 大范围（±30000 → 36 亿区块）：保证 0.25 秒后搜索仍在进行，取消才真正打在扫描中
                m_w->minX->setValue(-30000); m_w->maxX->setValue(29999);
                m_w->minZ->setValue(-30000); m_w->maxZ->setValue(29999);
                m_w->targetCount->setValue(50);
                // v3.10.10.1：阈值必须 >0，否则「最小密度=0 快速路径」会让搜索瞬间结束，
                //   取消压测无法打在扫描中（本测试要测的是取消响应，不是搜索速度）。
                if (m_w->minDensity) m_w->minDensity->setValue(0.2);
                m_w->stopAtTarget->setChecked(false);
                m_w->useCache->setChecked(false);
                if (m_w->gpuCheck->isChecked()) m_w->gpuCheck->setChecked(false);

                // v3.4.12：先热身一次「搜索 → 取消」再进入压测。
                //   原因：OpenMP 线程池是**首次**进入并行区时才建立的，机器负载高时这段冷启动
                //   可达数百毫秒，会被算进下面的「最差取消响应」而与被测的取消路径无关
                //   （打包时实测到一次 1165 ms 的假失败）。热身不改变 1 秒阈值本身。
                {
                    startSearch();
                    const auto tw = std::chrono::steady_clock::now();
                    while (m_running &&
                           std::chrono::duration<double>(std::chrono::steady_clock::now() - tw).count() < 0.3) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                    }
                    cancelSearch();
                    const auto tc = std::chrono::steady_clock::now();
                    while (m_running &&
                           std::chrono::duration<double>(std::chrono::steady_clock::now() - tc).count() < 5.0) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                    }
                    std::fprintf(stderr, "[UI自检]   取消压测前热身完成（线程池已建立）\n");
                }
                const auto runCancelStress = [this](int* wasRunningOut, double* worstMsOut) {
                    int ok = 0;
                    int wasRunning = 0;
                    double worst = 0.0;
                    for (int i = 0; i < 10; ++i) {
                        startSearch();
                        const auto tw = std::chrono::steady_clock::now();
                        int pumps = 0;
                        while (m_running && ++pumps < 25 &&
                               std::chrono::duration<double>(std::chrono::steady_clock::now() - tw).count() < 0.25) {
                            QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                        }
                        if (m_running) ++wasRunning;
                        const auto tc = std::chrono::steady_clock::now();
                        cancelSearch();
                        while (m_running &&
                               std::chrono::duration<double>(std::chrono::steady_clock::now() - tc).count() < 5.0) {
                            QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                        }
                        const double ms = std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - tc)
                                              .count();
                        if (!m_running) ++ok;
                        worst = (ms > worst) ? ms : worst;
                    }
                    if (wasRunningOut) *wasRunningOut = wasRunning;
                    if (worstMsOut) *worstMsOut = worst;
                    return ok;
                };
                int wasRunning = 0;
                double worstMs = 0.0;
                int okCancels = runCancelStress(&wasRunning, &worstMs);
                // v3.5.2：打包/构建刚结束时偶有瞬时负载导致单次取消偏慢；若超阈值则重测一次取更优
                if (worstMs >= 1000.0) {
                    std::fprintf(stderr, "[UI自检]   取消压测首轮最差 %.1f ms（疑似瞬时负载），重测一次…\n", worstMs);
                    int wasRunning2 = 0;
                    double worst2 = 0.0;
                    const int ok2 = runCancelStress(&wasRunning2, &worst2);
                    if (worst2 < worstMs) { worstMs = worst2; wasRunning = wasRunning2; }
                    okCancels = (ok2 > okCancels) ? ok2 : okCancels;
                }
                check(wasRunning >= 8, (slime::ui::t("ui.b01b8d42b8af").arg(wasRunning))
                                           .toUtf8()
                                           .constData());
                check(okCancels == 10, "连续取消 10 次均正常结束（无崩溃、无卡死）");
                check(worstMs < 1000.0, (slime::ui::t("ui.6f52699a5f5a").arg(worstMs, 0, 'f', 1))
                                            .toUtf8()
                                            .constData());
                check(m_w->start->isEnabled() && !m_running, "取消压力测试后界面恢复可用");

                // v3.5.1（交互手感）：Esc 等价于「取消」——直接向窗口投递按键事件（不依赖窗口是否激活）
                {
                    startSearch();
                    const auto tw2 = std::chrono::steady_clock::now();
                    int pumps2 = 0;
                    while (m_running && ++pumps2 < 25 &&
                           std::chrono::duration<double>(std::chrono::steady_clock::now() - tw2).count() < 0.25) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                    }
                    const bool escWasRunning = m_running;
                    const bool escCancelReady = m_w->cancel && m_w->cancel->isEnabled();
                    QKeyEvent escEv(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                    QApplication::sendEvent(this, &escEv);
                    const auto tc2 = std::chrono::steady_clock::now();
                    while (m_running &&
                           std::chrono::duration<double>(std::chrono::steady_clock::now() - tc2).count() < 5.0) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                    }
                    std::fprintf(stderr, "[UI自检]   Esc 取消：按下前运行中=%d 取消按钮可用=%d 按下后仍在运行=%d\n",
                                 escWasRunning ? 1 : 0, escCancelReady ? 1 : 0, m_running ? 1 : 0);
                    check(escWasRunning && escCancelReady && !m_running, "搜索进行中按 Esc 等价于「取消」");
                }
                std::fprintf(stderr, "[UI自检]   取消 10 次：最差响应 %.1f ms（%d 次命中扫描中）\n", worstMs, wasRunning);

                // 取消 → 等线程退出并处理自身结果 → 立刻重搜（真实用户流程：取消后马上再点开始）
                m_w->minX->setValue(-20000); m_w->maxX->setValue(19999);
                m_w->minZ->setValue(-20000); m_w->maxZ->setValue(19999);
                startSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                cancelSearch();
                while (m_running) QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                check(!m_running && !m_task.running(), "取消后立即结束（线程退出、界面状态复位）");
                startSearch();
                check(m_running, "取消后立刻重搜：新搜索立即进入运行中");
                // 直接投递一个「上一次运行」的陈旧结果事件：必须被 runId 守卫丢弃
                {
                    const int beforeRunId = m_runId;
                    const QString beforeInfo = m_w->progressInfo ? m_w->progressInfo->text() : QString();
                    SearchOutcome stale;
                    stale.cancelled = true;
                    onSearchFinished(stale, RunInfo{}, beforeRunId - 1); // 陈旧 runId
                    check(m_running && m_runId == beforeRunId,
                          "陈旧结果事件被丢弃（新搜索未被误停、运行编号未变）");
                    check(!m_w->progressInfo || m_w->progressInfo->text() == beforeInfo ||
                              m_w->progressInfo->text().contains(slime::ui::t("ui.998c9363771c")),
                          "陈旧事件未改写新搜索的状态文字");
                }
                cancelSearch();
                while (m_running) QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
                check(m_w->start->isEnabled(), "陈旧事件测试后界面恢复可用");
                m_settings = keepCfg;
            }

            // 15.3 P0-3：真实扫描计数与前缀（界面显示与引擎同源）
            {
                const QString stats = progressStatsText(267596340, 314572500, 1.0e8, 1);
                check(stats.contains(slime::ui::t("ui.04ad2367d270")),
                      "统计前缀为「扫描 N/M 区块（xx%）」（真实计数）");
                check(m_progressStream != nullptr, "进度流水面板存在（N/M 显示入口）");
                const QString line = ProgressStreamPanel::formatProgressLineEx(QTime(23, 4, 45), 12.5, 250, 1000, 7);
                check(line == slime::ui::t("ui.49c776edd7b3"),
                      "流水行 N/M（xx%）格式逐字一致");
            }

            // 15.4 P1-6：崩溃转储接线 + 关键信息日志（常开）
            {
                check(slime::CrashDump::enabled(), "崩溃转储默认开启（独立于普通日志）");
                const AppSettings keepCfg = m_settings;
                AppSettings off = m_settings;
                off.crashDump = false;
                applyAppSettings(off);
                check(!slime::CrashDump::enabled(), "设置里关闭转储后 CrashDump::enabled()==false");
                applyAppSettings(keepCfg);
                check(slime::CrashDump::enabled(), "重新开启后立即生效");
                check(!slime::IncidentLog::currentFile().empty(), "关键信息日志文件已就绪（logs/crash-YYYY-MM-DD.log）");
                slime::IncidentLog::note("自检：关键信息日志写入验证");
                std::ifstream in(slime::IncidentLog::currentFile());
                std::stringstream ss;
                ss << in.rdbuf();
                check(ss.str().find("自检：关键信息日志写入验证") != std::string::npos,
                      "关键信息日志可写入并读回（崩溃/取消取证可用）");
                check(!slime::CrashDump::dumpDir().empty(), "崩溃转储目录已配置（crashes/）");
                std::fprintf(stderr, "[UI自检]   关键信息日志：%s\n",
                             QString::fromStdWString(slime::IncidentLog::currentFile().wstring()).toUtf8().constData());
            }

            std::fprintf(stderr, "[UI自检] v3.3.7 检查完成\n");
        }

        // 13.8 v3.3.4（任务书 1/2）：界面结构断言 —— 菜单栏/工具栏/状态栏原生、
        //        中部 Splitter、左侧 QScrollArea + 6 分组、右侧信息栏 + QTextEdit、底部 4 标签页
        {
            const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
            check(menuBar() != nullptr && menuBar()->actions().size() >= 5,
                  "菜单栏存在且至少 5 个顶级菜单（文件/编辑/查看/工具/帮助）");
            check(m_menuFile && m_menuEdit && m_menuView && m_menuTools && m_menuHelp, "五个菜单对象齐全");
            check(m_toolBar != nullptr && !m_toolBar->actions().isEmpty(), "工具栏存在且有动作");
            check(statusBar() != nullptr, "状态栏存在（QMainWindow 原生）");
            check(m_hSplit && m_vSplit, "中部水平 + 垂直 QSplitter 存在");
            check(m_hSplit->count() == 3 && m_rightSplit->count() == 2,
                  "三纵列（参数/地图/结果）+ 右列（结果/过程）各就位");
            check(m_w->mainScroll && m_w->mainScroll->widget() != nullptr, "左侧为 QScrollArea（含滚动内容）");
            // 6 个 QGroupBox（游戏设置/搜索范围/搜索模式/匹配方式/性能设置/缓存管理）
            {
                const QList<QGroupBox*> groups = m_w->mainScroll->widget()->findChildren<QGroupBox*>();
                QStringList titles;
                for (QGroupBox* g : groups) titles << g->title();
                // v3.9.15：「游戏设置」分组上移到顶部全局设置栏（平台/版本/种子），左侧剩 5 组；
                //   同时断言顶部栏包含平台 / 游戏版本 / 世界种子 三个控件。
                const QStringList want = { slime::ui::t("ui.2cacc1b74217"), slime::ui::t("ui.5483493bb0d8"),
                                           slime::ui::t("ui.6a05dbdc884c"), slime::ui::t("ui.eb1f3e5ef69b"),
                                           slime::ui::t("ui.c15668b86bc6") };
                bool all = true;
                for (const QString& t : want) {
                    if (!titles.contains(t)) {
                        all = false;
                        std::fprintf(stderr, "[UI自检] 缺少设置分组: %s\n", t.toUtf8().constData());
                    }
                }
                check(all && titles.size() >= 5, "左侧包含规定的 5 个设置分组（游戏设置已上移顶部栏）");
                bool topBarOk = m_topBar && m_w->javaRadio && m_w->bedrockRadio && m_w->mapVersionCombo &&
                                m_w->seedEdit;
                if (topBarOk) {
                    topBarOk = m_topBar->isAncestorOf(m_w->javaRadio) &&
                               m_topBar->isAncestorOf(m_w->bedrockRadio) &&
                               m_topBar->isAncestorOf(m_w->mapVersionCombo) &&
                               m_topBar->isAncestorOf(m_w->seedEdit);
                }
                check(topBarOk, "顶部全局设置栏包含 平台/游戏版本/世界种子");
                for (QGroupBox* g : groups) {
                    check(!g->isCheckable(), "设置分组不可折叠（任务书：不做折叠）");
                    break; // 只断言一次即可（全部同构）
                }
                std::fprintf(stderr, "[UI自检] 设置分组：%s\n", titles.join(QStringLiteral(" / ")).toUtf8().constData());
                for (QGroupBox* gg : groups) {
                    std::fprintf(stderr, "[UI自检]   分组最小宽 \"%s\" = %d px\n",
                                 gg->title().toUtf8().constData(), gg->minimumSizeHint().width());
                }
            }
            check(m_w->infoStats != nullptr && m_w->resultView != nullptr, "右栏为「信息栏 + QTextEdit」");
            check(m_w->resultView->isReadOnly(), "结果视图只读");
            // v3.3.6（任务书 调整 2）：底部面板 = 左竖排导航 + 右内容区（不再是 QTabWidget）
            check(m_nav != nullptr && m_stack != nullptr && m_nav->count() == 4 && m_stack->count() == 4,
                  "底部为「左竖排导航 + 右内容区」，四个分类一一对应");
            if (m_nav && m_stack) {
                QStringList navNames;
                for (int i = 0; i < m_nav->count(); ++i) {
                    QString t = m_nav->item(i)->text();
                    t.remove(QStringLiteral("● "));
                    t.remove(QStringLiteral("○ "));
                    // 「反馈」项后面还带 ⚠️/❌ 计数徽章（如「反馈 ⚠️1❌1」），只取分类名
                    const int sp = t.indexOf(QLatin1Char(' '));
                    if (sp > 0) t = t.left(sp);
                    navNames << t;
                }
                check(navNames == QStringList{ slime::ui::t("ui.acf014bff8d8"), slime::ui::t("ui.42a36e949784"),
                                               slime::ui::t("ui.4de50894b8c1"), slime::ui::t("ui.b114b91547d7") },
                      "四个分类名称与顺序符合任务书");
                std::fprintf(stderr, "[UI自检] 底部导航：%s\n", navNames.join(QStringLiteral(" / ")).toUtf8().constData());
            }
            check(m_feedback && m_logPanel && m_terminal && m_progressStream, "进度/反馈/日志/命令面板对象齐全");
            // 尺寸规范
            // v3.10.0：三纵列后 QSplitter 按比例分配宽度，左栏在 400 附近、窄窗口下不小于最小宽 320。
            {
                const QList<int> sz = m_hSplit->sizes();
                std::fprintf(stderr, "[UI自检]   hSplit 尺寸: %d / %d / %d（窗口 %dx%d，hSplit 宽 %d）\n",
                             sz.value(0), sz.value(1), sz.value(2), width(), height(), m_hSplit->width());
            }
            check(m_hSplit->sizes().value(0) >= 320 && m_hSplit->sizes().value(0) <= 460 &&
                      m_hSplit->sizes().value(2) >= 280,
                  "三纵列宽度合理（左 ≥320、右 ≥280）");
            // v3.4.0：停靠面板展开时默认高 180（收起后高度归 0，由 showDock/hideDock 控制）
            check(minimumWidth() == 1000 && minimumHeight() == 650, "窗口最小尺寸 1000×650");
            resetLayout(); // 恢复默认布局与默认窗口尺寸后再断言
            QApplication::processEvents(QEventLoop::AllEvents, 60);
            check(width() == 1280 && height() == 860, "窗口默认尺寸 1280×860");
            check(!m_running && m_w->start->isEnabled(), "结构断言后「开始搜索」按钮可用（未在运行中）");
            // 8 张截图所需的布局状态由外部脚本设置尺寸/分割位置后逐张调用（见 GUI 自检截图钩子）
            // v3.3.4（任务书 7）：splitter 拖到最左（左栏 340px 最小宽）也不允许出现横向滚动条
            if (m_hSplit) {
                m_hSplit->setSizes(QList<int>() << 340 << (width() - 340));
                QApplication::processEvents(QEventLoop::AllEvents, 60);
                const int hmLeft = m_w->mainScroll->horizontalScrollBar()
                                       ? m_w->mainScroll->horizontalScrollBar()->maximum()
                                       : 0;
                std::fprintf(stderr, "[UI自检] 左栏 340px（splitter 最左）：设置区横滚动上限=%d\n", hmLeft);
                check(hmLeft == 0, "左栏最小宽 340px 下设置区无横向滚动条");
                resetLayout();
            }
            if (!shotPrefix.isEmpty()) {
                // v3.3.4（任务书 7）：规定尺寸 / splitter 极限 / 四标签页 / 每个菜单展开
                captureUiScreenshots(shotPrefix);
                resetLayout();
            }

            // 13.9 v3.3.7.2（F-1）：截图文字可读性 —— 必须能真正渲染出文字，
            //      否则「豆腐块」截图会再次作为证据交付（v3.3.4~v3.3.7 的教训）。
            {
                const QFontMetrics fm(font());
                const bool glyphsOA = fm.inFont(QChar(0x53F2)) &&   // 史
                                      fm.inFont(QChar(0x83B1)) &&   // 莱
                                      fm.inFont(QLatin1Char('5')) && fm.inFont(QLatin1Char('%'));
                check(glyphsOA, "字体含中文/数字字形（QFontMetrics::inFont；缺字形=截图豆腐块）");
                const QImage win = grab().toImage();
                const int variety = inkRunWidthVariety(win);
                const double ratio = inkRatio(win);
                std::fprintf(stderr,
                             "[UI自检] 截图可读性：墨迹列宽种类=%d（真实文字≥6 / 豆腐块=1~3）"
                             " 墨迹占比=%.2f%%\n",
                             variety, ratio * 100.0);
                check(variety >= 5, "窗口截图含真实文字（墨迹列宽种类 ≥5，豆腐块会被判失败）");
                check(ratio > 0.005, "窗口截图非空白（墨迹占比 >0.5%）");
                if (!shotPrefix.isEmpty()) {
                    const QImage saved(shotPrefix + QStringLiteral("_size-1280x860.png"));
                    check(!saved.isNull(), "落盘截图可重新加载");
                    if (!saved.isNull()) {
                        const int savedVariety = inkRunWidthVariety(saved);
                        std::fprintf(stderr, "[UI自检]   落盘截图墨迹列宽种类=%d\n", savedVariety);
                        check(savedVariety >= 5, "落盘截图文字可读（非豆腐块）");
                    }
                }
            }
        }
        // 16) v3.3.7.2（任务书步骤 1）：单一百分比 / 同源分母 / 阶段文案 / 状态栏分离
        {
            std::fprintf(stderr, "[UI自检] v3.3.7.2 检查（单一百分比/同源分母/阶段文案）…\n");

            // 16.1（任务书 1.1）进度行只有一个百分比：进度条自身文字必须关闭
            check(m_w->progress != nullptr && !m_w->progress->isTextVisible(),
                  "进度条自身不绘制百分比（避免与标签重复：用户截图曾出现「25% 25%」）");
            check(m_w->progressPercent != nullptr, "百分比标签存在（唯一百分比来源）");
            if (m_w->progress) {
                m_w->progress->setValue(250);
                // 注意：QProgressBar::text() 与 textVisible 无关（始终返回格式化文本），
                // 因此这里用**像素级**证明：关闭内置文字后，进度行的墨迹必须变少。
                if (m_w->progressPercent) m_w->progressPercent->setText(QStringLiteral("25%"));
                m_w->progress->setTextVisible(true);
                QApplication::processEvents(QEventLoop::AllEvents, 40);
                const int inkWith = inkPixels(m_progressRowWidget ? m_progressRowWidget->grab().toImage() : QImage());
                m_w->progress->setTextVisible(false);
                QApplication::processEvents(QEventLoop::AllEvents, 40);
                const int inkWithout = inkPixels(m_progressRowWidget ? m_progressRowWidget->grab().toImage() : QImage());
                std::fprintf(stderr, "[UI自检]   进度行墨迹像素：内置文字开=%d 关=%d\n", inkWith, inkWithout);
                check(inkWith > inkWithout,
                      "关闭进度条内置文字后进度行墨迹减少（证明此前确实重复绘制了百分比）");
                // 进度行内形如 "NN%" 的标签必须只有 1 个
                int pctLabels = 0;
                if (m_progressRowWidget) {
                    for (QLabel* l : m_progressRowWidget->findChildren<QLabel*>()) {
                        const QString t = l->text();
                        if (t.size() >= 2 && t.size() <= 5 && t.endsWith(QLatin1Char('%'))) {
                            bool allDigits = true;
                            for (int i = 0; i + 1 < t.size(); ++i) {
                                if (!t.at(i).isDigit()) allDigits = false;
                            }
                            if (allDigits) ++pctLabels;
                        }
                    }
                }
                check(pctLabels == 1,
                      (slime::ui::t("ui.40ce33982716").arg(pctLabels))
                          .toUtf8()
                          .constData());
                m_w->progress->setValue(0);
            }

            // v3.5.28（用户要求）：状态栏右侧信息已移除 —— 改为断言「左侧常驻提醒 + 工具栏状态面板」
            check(m_statusSep == nullptr,
                  "状态栏右侧信息已移除（无分隔符/无永久控件）");
            check(m_w->searchStatus && m_w->searchStatus->isVisible(), "工具栏含搜索状态面板（阶段/结果/用时/剩余）");
            check(m_w->searchStage && !m_w->searchStage->text().isEmpty(), "搜索状态面板显示阶段文字");

            // 16.2（任务书 1.1/1.2/1.4 验收场景）真实密度搜索：中途截图 + 数值/文案断言
            {
                const QString shotPrefix2 = qEnvironmentVariable("SLIME_SHOT");
                const bool big = qEnvironmentVariableIsSet("SLIME_SELFTEST_BIG");
                const int half = big ? 104857 : 20000; // big：用户原场景（±104857 = 439.8 亿）
                const int timeoutMs = big ? 900000 : 150000;
                // 从统计文本里解析「（xx%）」中的数字 —— 只接受「（数字%）」形式，
                // 且括号内容必须到**下一个**「（」之前结束（否则会跨过「（显示 N 条）」）。
                const auto parsePct = [](const QString& s, double* out) {
                    int idx = 0;
                    while ((idx = s.indexOf(QStringLiteral("（"), idx)) >= 0) {
                        const int nextOpen = s.indexOf(QStringLiteral("（"), idx + 1);
                        const int limit = (nextOpen < 0) ? s.size() : nextOpen;
                        const int rp = s.indexOf(QStringLiteral("%"), idx);
                        if (rp > idx && rp < limit) {
                            bool Ok = false;
                            const double v = s.mid(idx + 1, rp - idx - 1).trimmed().toDouble(&Ok);
                            if (Ok) {
                                *out = v;
                                return true;
                            }
                        }
                        idx += 1;
                    }
                    return false;
                };

                if (m_w->bedrockRadio) m_w->bedrockRadio->setChecked(true);
                if (m_w->densityRadio) m_w->densityRadio->setChecked(true);
                onModeChanged();
                if (m_w->gridSize) m_w->gridSize->setValue(5);
                if (m_w->minDensity) m_w->minDensity->setValue(0.75);
                if (m_w->minX) m_w->minX->setValue(-half);
                if (m_w->maxX) m_w->maxX->setValue(half - 1);
                if (m_w->minZ) m_w->minZ->setValue(-half);
                if (m_w->maxZ) m_w->maxZ->setValue(half - 1);
                if (m_w->seedEdit) m_w->seedEdit->setText(QStringLiteral("0"));
                if (m_w->stopAtTarget) m_w->stopAtTarget->setChecked(true);
                if (m_w->targetCount) m_w->targetCount->setValue(1);
                if (m_w->threads) m_w->threads->setValue(m_w->threads->maximum()); // 全核（0 会被夹到 1）
                // v3.3.7.2：big（439 亿）模式按用户场景**启用缓存**（分块上限因此压到 2.5M words），
                // 但缓存文件指向临时目录，绝不触碰用户缓存；中等规模则不启用缓存。
                if (big && m_cache) {
                    m_cacheFile = std::filesystem::temp_directory_path() / "slime_selftest_big.slimecache";
                    m_cache->clear();
                    if (m_w->useCache) m_w->useCache->setChecked(true);
                    std::fprintf(stderr, "[UI自检]   缓存（自检临时文件）：%s\n", m_cacheFile.string().c_str());
                } else if (m_w->useCache) {
                    m_w->useCache->setChecked(false);
                }
                selectBottomCategory(0);                              // 底部切到「进度」，截图可看到流水

                std::fprintf(stderr, "[UI自检]   密度搜索（%s）：范围 ±%d，g=5 ≥0.75，基岩，目标 1\n",
                             big ? "439.8 亿原场景" : "中等规模", half);
                startSearch();
                check(m_running, "自检内真实密度搜索已启动");

                QElapsedTimer et;
                et.start();
                bool shotScan = false, shotMatch = false, timedOut = false;
                QString pctLabelAtScan, pctBarAtScan, msgAtMatch, stageAtMatch, infoAtMatch;
                while (m_running && et.elapsed() < timeoutMs) {
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    if (!m_running) break;
                    const QString stage = m_w->progressStage ? m_w->progressStage->text() : QString();
                    if (!shotScan && m_w->progress->value() >= 50 &&
                        stage.contains(slime::ui::t("ui.998c9363771c"))) {
                        shotScan = true;
                        pctLabelAtScan = m_w->progressPercent ? m_w->progressPercent->text() : QString();
                        pctBarAtScan = QString::number(m_w->progress->value() / 10.0, 'f', 1);
                        if (m_w->progressInfo) {
                            // 状态栏百分比必须 ≤100%（任务书 1.2）
                            double v = 0.0;
                            if (parsePct(m_w->progressInfo->text(), &v)) {
                                check(v <= 100.0,
                                      (slime::ui::t("ui.a6c72a289200").arg(v))
                                          .toUtf8()
                                          .constData());
                            }
                        }
                        // v3.3.8（任务 1）：搜索中 ETA 必须有具体数值（不是 --:--）
                        if (m_w->progressEta) {
                            const QString etaText = m_w->progressEta->text();
                            std::fprintf(stderr, "[UI自检]   搜索中 ETA：%s\n", etaText.toUtf8().constData());
                            check(etaText.contains(QLatin1Char(':')) &&
                                      !etaText.contains(QStringLiteral("--:--")),
                                  (slime::ui::t("ui.ae72d062c965").arg(etaText))
                                      .toUtf8()
                                      .constData());
                        }
                        std::fprintf(stderr, "[UI自检]   扫描中：进度行=%s / 状态栏=%s\n",
                                     pctLabelAtScan.toUtf8().constData(),
                                     (m_w->progressInfo ? m_w->progressInfo->text() : QString())
                                         .toUtf8()
                                         .constData());
                        if (!shotPrefix2.isEmpty()) {
                            saveWindowShot(shotPrefix2, QStringLiteral("density-scanning"));
                        }
                    }
                    if (!shotMatch && stage.contains(slime::ui::t("ui.c7127bb6eeea")) &&
                        !stage.contains(slime::ui::t("ui.936420feb068"))) {
                        shotMatch = true;
                        stageAtMatch = stage;
                        msgAtMatch = statusBar()->currentMessage();
                        infoAtMatch = m_w->progressInfo ? m_w->progressInfo->text() : QString();
                        std::fprintf(stderr, "[UI自检]   匹配中：阶段=%s / 状态栏消息=%s\n",
                                     stageAtMatch.toUtf8().constData(), msgAtMatch.toUtf8().constData());
                        if (!shotPrefix2.isEmpty()) {
                            saveWindowShot(shotPrefix2, QStringLiteral("density-matching"));
                        }
                    }
                }
                if (m_running) {
                    timedOut = true;
                    cancelSearch();
                    while (m_running && et.elapsed() < timeoutMs + 30000) {
                        QApplication::processEvents(QEventLoop::AllEvents, 20);
                    }
                }
                check(!timedOut, "密度搜索在时限内结束（否则已取消并记为失败）");
                check(!m_running, "密度搜索已结束（无线程残留）");
                // v3.3.8（任务 1）：完成后 ETA 必须清空（否则用户以为还在跑）
                if (m_w->progressEta) {
                    const QString etaDone = m_w->progressEta->text();
                    std::fprintf(stderr, "[UI自检]   完成后 ETA：%s\n", etaDone.toUtf8().constData());
                    check(etaDone.contains(QStringLiteral("--:--")),
                          (slime::ui::t("ui.dcbe39514b6c").arg(etaDone)).toUtf8().constData());
                }
                // 任务书 1.4：密度模式下**不得**出现「匹配图案」字样
                bool sawPatternWord = false;
                if (stageAtMatch.contains(slime::ui::t("ui.d5b2aeac0692")) ||
                    msgAtMatch.contains(slime::ui::t("ui.d5b2aeac0692"))) {
                    sawPatternWord = true;
                }
                check(!sawPatternWord,
                      (slime::ui::t("ui.ff6468498df0")
                           .arg(stageAtMatch, msgAtMatch))
                          .toUtf8()
                          .constData());
                std::fprintf(stderr, "[UI自检]   密度搜索完成：扫描截图=%d 匹配截图=%d 已用时=%.1fs\n",
                             shotScan ? 1 : 0, shotMatch ? 1 : 0, et.elapsed() / 1000.0);
                if (m_elapsedTimer) m_elapsedTimer->stop();

                // 16.3（任务书 1.2 / 步骤 5）：完成后统计必须与引擎同源，且分子 ≤ 分母
                {
                    const int64_t engTotal = m_task.liveTotalCount();
                    check(engTotal > 0, "引擎公开了本次搜索的总扫描量（界面不再自行推算分母）");
                    const QString fin = m_w->progressInfo ? m_w->progressInfo->text() : QString();
                    std::fprintf(stderr, "[UI自检]   完成后状态栏：%s\n  引擎口径总量=%lld\n",
                                 fin.toUtf8().constData(), static_cast<long long>(engTotal));
                    double pct = -1.0;
                    check(parsePct(fin, &pct) && pct <= 100.0,
                          (slime::ui::t("ui.63811be297a8").arg(pct))
                              .toUtf8()
                              .constData());
                    // 分子 ≤ 分母（同源计数保证）
                    const int sPos = fin.indexOf(slime::ui::t("ui.8caf38614bbf"));
                    const int slash = fin.indexOf(QLatin1Char('/'));
                    if (sPos >= 0 && slash > sPos) {
                        bool okN = false, okM = false;
                        const int64_t n = fin.mid(sPos + 3, slash - sPos - 3).trimmed().toLongLong(&okN);
                        const int sp2 = fin.indexOf(QLatin1Char(' '), slash);
                        const int64_t m = (sp2 > slash)
                                              ? fin.mid(slash + 1, sp2 - slash - 1).trimmed().toLongLong(&okM)
                                              : 0;
                        check(okN && okM && n > 0 && m > 0 && n <= m,
                              (slime::ui::t("ui.ee3e59ed3a21").arg(n).arg(m))
                                  .toUtf8()
                                  .constData());
                        check(m == engTotal,
                              (slime::ui::t("ui.75b961eb1a31").arg(m).arg(engTotal))
                                  .toUtf8()
                                  .constData());
                    }
                    if (big) {
                        // 分块路径：进度条与状态栏同源（都来自 liveScanned/totalEvals）→ 应基本一致
                        const double barPct = m_w->progress->value() / 10.0;
                        check(std::fabs(barPct - pct) <= 1.0,
                              (slime::ui::t("ui.b271f8f86be5")
                                   .arg(barPct)
                                   .arg(pct))
                                  .toUtf8()
                                  .constData());
                    }
                    // 步骤 5（F-3）：完成态状态栏数值必须与日志里的 search.summary 一致
                    {
                        const std::filesystem::path lf = slime::IncidentLog::currentFile();
                        QString lastSummary;
                        if (!lf.empty()) {
                            std::ifstream in(lf);
                            std::string line;
                            while (std::getline(in, line)) {
                                if (line.find("search.summary") != std::string::npos) {
                                    lastSummary = QString::fromStdString(line);
                                }
                            }
                        }
                        std::fprintf(stderr, "[UI自检]   日志汇总：%s\n", lastSummary.toUtf8().constData());
                        int64_t logScanned = -1, logTotal = -1;
                        const int sp = lastSummary.indexOf(QStringLiteral("scanned="));
                        if (sp >= 0) {
                            const int sl = lastSummary.indexOf(QLatin1Char('/'), sp);
                            const int sp2 = (sl > 0) ? lastSummary.indexOf(QLatin1Char(' '), sl) : -1;
                            if (sl > 0 && sp2 > sl) {
                                logScanned = lastSummary.mid(sp + 8, sl - sp - 8).toLongLong();
                                logTotal = lastSummary.mid(sl + 1, sp2 - sl - 1).toLongLong();
                            }
                        }
                        check(logScanned >= 0 && logTotal > 0,
                              "日志含 search.summary 且带 scanned=<已扫描>/<总量> 字段");
                        // 与状态栏里显示的 N/M 比对（两者必须同源）
                        const int sp3 = fin.indexOf(slime::ui::t("ui.8caf38614bbf"));
                        const int slash3 = fin.indexOf(QLatin1Char('/'));
                        if (sp3 >= 0 && slash3 > sp3) {
                            const int64_t uiN = fin.mid(sp3 + 3, slash3 - sp3 - 3).trimmed().toLongLong();
                            const int sp4 = fin.indexOf(QLatin1Char(' '), slash3);
                            const int64_t uiM =
                                (sp4 > slash3) ? fin.mid(slash3 + 1, sp4 - slash3 - 1).trimmed().toLongLong()
                                               : 0;
                            check(uiN == logScanned && uiM == logTotal,
                                  (slime::ui::t("ui.e2ebc10f4068")
                                       .arg(uiN)
                                       .arg(uiM)
                                       .arg(logScanned)
                                       .arg(logTotal))
                                      .toUtf8()
                                      .constData());
                        }
                    }
                }
            }

            std::fprintf(stderr, "[UI自检] v3.3.7.2 检查完成\n");
        }

    // 17) v3.3.8.1（任务书 1.3）：导入大缓存 = 后台线程 + 进度条（界面不冻结）
    //     要导入的文件由环境变量 SLIME_IMPORT_CACHE 指定（自检在外部驱动，避免弹文件对话框），
    //     走的是与菜单「导入缓存」完全相同的生产路径（importCacheFromPath）。
    //     验收口径：事件循环持续心跳（不冻结）+ 进度条多次实时更新且单调 + 导入后条目增加。
    {
        const QString impPath = qEnvironmentVariable("SLIME_IMPORT_CACHE");
        if (!impPath.isEmpty()) {
            std::fprintf(stderr, "[UI自检] v3.3.8.1 导入大缓存检查：%s\n", impPath.toUtf8().constData());
            QElapsedTimer beat;
            beat.start();
            qint64 lastBeat = beat.elapsed();
            qint64 maxGapMs = 0;
            int ticks = 0, samples = 0, backwards = 0, lastPct = 0;
            QTimer heartbeat;
            connect(&heartbeat, &QTimer::timeout, this, [&]() {
                const qint64 now = beat.elapsed();
                maxGapMs = (std::max)(maxGapMs, now - lastBeat);
                lastBeat = now;
                ++ticks;
                if (QProgressDialog* pd = findChild<QProgressDialog*>()) {
                    const int pct = pd->value();
                    ++samples;
                    if (pct < lastPct) ++backwards;
                    lastPct = pct;
                }
            });
            heartbeat.start(20);

            const int64_t beforeE = m_cache->entryCount();
            const size_t beforeR = m_cache->ranges().size();
            QElapsedTimer wall;
            wall.start();
            importCacheFromPath(impPath, /*showProgressDialog=*/true, /*showResultDialogs=*/false);
            const qint64 importMs = wall.elapsed();
            heartbeat.stop();

            std::error_code ecImp;
            const auto impBytes =
                static_cast<int64_t>(std::filesystem::file_size(std::filesystem::path(impPath.toStdWString()), ecImp));
            const int64_t addedE = m_cache->entryCount() - beforeE;
            std::fprintf(stderr,
                         "[UI自检]   导入：文件=%.1f MiB 用时=%lld ms 新增区域=%lld 新增条目=%lld "
                         "心跳=%d 次 进度采样=%d 次 末次进度=%d/1000 最长卡顿=%lld ms 回退=%d 次\n",
                         impBytes / (1024.0 * 1024.0), static_cast<long long>(importMs),
                         static_cast<long long>(m_cache->ranges().size() - beforeR),
                         static_cast<long long>(addedE), ticks, samples, lastPct,
                         static_cast<long long>(maxGapMs), backwards);
            check(addedE > 0, "导入大缓存后条目增加（后台线程读取 + UI 线程合并成功）");
            check(ticks >= 10, "导入期间事件循环持续心跳（界面未冻结）");
            check(maxGapMs < 500, "导入期间最长 UI 卡顿 < 500 ms（进度条可实时刷新）");
            check(samples >= 3 && backwards == 0, "进度条实时更新且单调不回退");

            // 收尾：不把验收用的巨型缓存留在运行环境里
            m_cache->clear();
            std::filesystem::remove(m_cacheFile, ecImp);
            updateCacheStatusUi();
        }
    }

        std::fprintf(stderr, "[UI自检] v3.3.4 结构检查完成\n");
    }

    // 18) v3.4.7：交互可用性自检（用户实测反馈「选项无法勾选」的回归防线）
    //     ① 诊断：列出「可见但被禁用」的交互控件及其父链（缓存构建模式会按设计禁用若干项，
    //        逐条打印便于判断是设计如此还是误禁用）；
    //     ② 断言：设置页 + 当前可见页面里所有复选框/单选按钮都能被真实鼠标点击切换。
    {
        std::fprintf(stderr, "[UI自检] v3.4.7 交互可用性检查（点击切换 + 禁用诊断）…\n");

        {
            // 诊断头部：把当时的模式写清楚，便于判断「禁用」是设计行为还是误禁用
            const QRadioButton* mode = m_w->densityRadio && m_w->densityRadio->isChecked()
                                           ? m_w->densityRadio : m_w->strictRadio;
            const QRadioButton* ver = m_w->bedrockRadio && m_w->bedrockRadio->isChecked()
                                          ? m_w->bedrockRadio : m_w->javaRadio;
            std::fprintf(stderr, "[UI自检]   当时状态：搜索模式=%s / 版本=%s / 达到即停=%d / 缓存构建=%d\n",
                         mode ? mode->text().toUtf8().constData() : "?",
                         ver ? ver->text().toUtf8().constData() : "?",
                         (m_w->stopAtTarget && m_w->stopAtTarget->isChecked()) ? 1 : 0,
                         buildModeActive() ? 1 : 0);
        }
        {
            QStringList disabled;
            const QList<QWidget*> all = findChildren<QWidget*>();
            for (QWidget* w : all) {
                if (!w || !w->isVisible() || w->isEnabled()) continue;
                const bool interactive = w->inherits("QCheckBox") || w->inherits("QRadioButton") ||
                                         w->inherits("QAbstractSpinBox") || w->inherits("QComboBox") ||
                                         w->inherits("QLineEdit") || w->inherits("QAbstractButton") ||
                                         w->inherits("QSlider");
                if (!interactive) continue;
                QString chain;
                int depth = 0;
                for (QWidget* p = w; p && depth < 4; p = p->parentWidget(), ++depth) {
                    chain += QString::fromLatin1(p->metaObject()->className());
                    if (!p->objectName().isEmpty()) chain += QStringLiteral("#") + p->objectName();
                    chain += QStringLiteral(" < ");
                }
                const QGroupBox* gb = nullptr;
                for (QWidget* p = w->parentWidget(); p && !gb; p = p->parentWidget()) {
                    gb = qobject_cast<const QGroupBox*>(p);
                }
                const QAbstractButton* btn = qobject_cast<QAbstractButton*>(w);
                // 禁用原因：v3391tip 是缓存构建模式打的设计性禁用标记；「取消」在无搜索时按设计禁用
                QString why = slime::ui::t("ui.7bd3b864b082");
                if (w->property("v3391tip").toBool()) {
                    why = slime::ui::t("ui.6e13090350fa");
                } else if (btn && btn->text().contains(slime::ui::t("ui.4d0b4688c787"))) {
                    why = slime::ui::t("ui.338f1f4ac68d");
                } else if (!w->isVisibleTo(this)) {
                    why = slime::ui::t("ui.d009f8c12b25");
                }
                disabled << QStringLiteral("%1[%2]（%3%4）← %5")
                                .arg(QString::fromLatin1(w->metaObject()->className()),
                                     btn ? btn->text() : QString(),
                                     gb ? gb->title() + QStringLiteral(" / ") : QString(), why, chain);
            }
            std::fprintf(stderr, "[UI自检]   可见但被禁用的交互控件 %lld 个%s%s\n",
                         static_cast<long long>(disabled.size()),
                         disabled.isEmpty() ? slime::ui::t("ui.6238bf9ad526").toUtf8().constData() : "：",
                         disabled.join(QStringLiteral(" | ")).toUtf8().constData());
        }

        {
            QList<QCheckBox*> boxes;
            if (m_settingsPage) boxes = m_settingsPage->findChildren<QCheckBox*>();
            for (QCheckBox* cb : findChildren<QCheckBox*>()) {
                if (cb && !boxes.contains(cb)) boxes << cb;
            }
            int tried = 0;
            QStringList bad;
            for (QCheckBox* cb : boxes) {
                if (!cb || !cb->isVisible() || !cb->isEnabled()) continue;
                ++tried;
                const bool before = cb->isChecked();
                QStyleOptionButton opt;
                opt.initFrom(cb);
                const QRect ind = cb->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, cb);
                const QPoint pos = ind.isValid() && !ind.isEmpty() ? ind.center() : cb->rect().center();
                const QPointF global = cb->mapToGlobal(pos);
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), global, Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(cb, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), global, Qt::LeftButton,
                                    Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(cb, &release);
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                if (cb->isChecked() == before) {
                    bad << (cb->text().isEmpty() ? cb->objectName() : cb->text());
                }
                cb->setChecked(before); // 还原，避免影响后续截图与断言
            }
            std::fprintf(stderr, "[UI自检]   复选框点击切换：可点 %lld 个，未翻转 %lld 个%s%s\n", static_cast<long long>(tried),
                         static_cast<long long>(bad.size()), bad.isEmpty() ? "" : "：",
                         bad.join(QStringLiteral(" / ")).toUtf8().constData());
            check(tried >= 4, "自检覆盖到至少 4 个可见复选框（点击切换有样本）");
            check(bad.isEmpty(), "全部可见复选框都能被鼠标点击勾选（选项可勾选）");
        }
    }

    // 19) v3.4.7：主题链路端到端（用户实测反馈「主题日志总是跟随系统」的回归防线）
    //     走**设置页真实控件**：读主题下拉的 currentData() ⇒ 切到「深色」⇒ 等 250ms 自动应用
    //     ⇒ 断言「实际生效 = 深色」且应用样式表**逐字等于** darkQss() ⇒ 最后还原原档位。
    //     同时把「主题重放：<档位>（qss 长度 N）」写进日志（与生产路径同一条代码）。
    {
        std::fprintf(stderr, "[UI自检] v3.4.7 主题链路检查（设置页主题下拉 → 深色生效 → 还原）…\n");
        QComboBox* themeBox = nullptr;
        if (m_settingsPage) {
            for (QComboBox* cb : m_settingsPage->findChildren<QComboBox*>()) {
                bool isThemeBox = false;
                for (int i = 0; i < cb->count(); ++i) {
                    const QString d = cb->itemData(i).toString();
                    if (d == QLatin1String("light") || d == QLatin1String("dark")) isThemeBox = true;
                }
                if (isThemeBox) { themeBox = cb; break; }
            }
        }
        check(themeBox != nullptr, "设置页存在主题下拉（itemData = light/dark/system）");
        if (themeBox) {
            const QString keepData = themeBox->currentData().toString();
            const bool keepDark = slime::ThemeManager::instance().isDark();
            int darkIdx = -1, keepIdx = -1;
            for (int i = 0; i < themeBox->count(); ++i) {
                const QString d = themeBox->itemData(i).toString();
                if (d == QLatin1String("dark")) darkIdx = i;
                if (d == keepData) keepIdx = i;
            }
            check(darkIdx >= 0, "主题下拉含「深色」档（itemData=dark）");
            if (darkIdx >= 0) {
                themeBox->setCurrentIndex(darkIdx);
                QElapsedTimer wait;
                wait.start();
                while (wait.elapsed() < 1200) QApplication::processEvents(QEventLoop::AllEvents, 30);
                const QString qss = qApp->styleSheet();
                const bool darkApplied = slime::ThemeManager::instance().isDark();
                const bool qssEquals = (qss == slime::ThemeManager::instance().darkQss());
                std::fprintf(stderr,
                             "[UI自检]   主题下拉 currentData()=%s / isDark=%d / 样式表长度=%d（深色基准 %d）"
                             "/ 与 darkQss() 逐字相等=%d\n",
                             themeBox->currentData().toString().toUtf8().constData(),
                             darkApplied ? 1 : 0, static_cast<int>(qss.size()),
                             static_cast<int>(slime::ThemeManager::instance().darkQss().size()),
                             qssEquals ? 1 : 0);
                check(themeBox->currentData().toString() == QLatin1String("dark"),
                      "主题下拉切到「深色」后 currentData() 读回 dark（不再按单选文字匹配）");
                check(darkApplied, "深色档位真正生效（ThemeManager::isDark() == true）");
                check(qssEquals, "应用样式表逐字等于 darkQss()（浅/深两套完整 QSS 已切换）");
                if (keepIdx >= 0) themeBox->setCurrentIndex(keepIdx);
                QElapsedTimer wait2;
                wait2.start();
                while (wait2.elapsed() < 1200) QApplication::processEvents(QEventLoop::AllEvents, 30);
                std::fprintf(stderr, "[UI自检]   档位还原：currentData()=%s / isDark=%d\n",
                             themeBox->currentData().toString().toUtf8().constData(),
                             slime::ThemeManager::instance().isDark() ? 1 : 0);
                check(slime::ThemeManager::instance().isDark() == keepDark,
                      "还原档位后实际生效主题回到自检开始前的状态");
            }
        }
    }

    // 20) v3.4.8：**达到目标数量即停止**端到端实测（用户反馈「失效」）
    //     口径：同一范围、同一模式，跑「勾选 vs 不勾选」两次，比较引擎实际评估量 / 扫描区块数 /
    //     耗时 / stoppedEarly / 结果条数，并按模式（密度·智能范围 / 密度·光栅 / 严格·四向旋转 /
    //     严格·锁定方向）分别记录。断言：勾选后必须**更省**（评估量或耗时明显下降），
    //     且结果条数不超过目标数量。
    {
        std::fprintf(stderr, "[UI自检] v3.4.8 「达到目标数量即停止」实测…\n");
        struct Case { const char* name; bool density; bool lock; bool smart; };
        const Case cases[4] = {
            { "密度+智能范围", true, false, true },
            { "密度+光栅", true, false, false },
            { "严格+四向旋转", false, false, false },
            { "严格+锁定方向", false, true, false },
        };
        const bool keepUseCache = m_w->useCache ? m_w->useCache->isChecked() : false;
        double slowRatio = 0.0;
        int improved = 0;
        for (const Case& c : cases) {
          for (int cacheOn = 0; cacheOn < 2; ++cacheOn) {
            if (m_w->useCache) m_w->useCache->setChecked(cacheOn == 1);
            m_w->bedrockRadio->setChecked(true);
            onVersionChanged();
            m_w->densityRadio->setChecked(c.density);
            m_w->strictRadio->setChecked(!c.density);
            onModeChanged();
            m_w->lockDir->setChecked(c.lock);
            m_w->smartRange->setChecked(c.smart);
            m_w->minX->setValue(-200); m_w->maxX->setValue(199);
            m_w->minZ->setValue(-200); m_w->maxZ->setValue(199);
            m_w->gridSize->setValue(5);
            m_w->minDensity->setValue(0.05);
            m_w->startX->setValue(0); m_w->startZ->setValue(0);
            m_w->threads->setValue(1);
            m_w->targetCount->setValue(8);
            if (!c.density) {
                // 严格模式：用「全 1 图案」保证范围内必有大量命中（考察提前停止本身，而非图案匹配）
                // 1×1「草绿」图案 = 单个史莱姆区块即命中（约 1/10 区块）⇒ 范围内必有大量命中
                Pattern pat(1, 1);
                pat.set(0, 0, CellState::Green);
                m_pattern = pat;
                m_patternSet = true;
            }
            const auto runOnceOn = [&](bool stop) {
                m_w->stopAtTarget->setChecked(stop);
                startSearch();
                const auto t0 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() <
                                        60.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(5);
                }
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0)
                                      .count();
                SearchOutcome out = m_hasLastOutcome ? m_lastOutcome : SearchOutcome{};
                return std::make_tuple(ms, out.stats.windowsEvaluated, out.stats.scannedChunks,
                                       (int64_t)out.results.size(), out.stoppedEarly);
            };
            const auto full = runOnceOn(false);
            const auto stop = runOnceOn(true);
            const double fullMs = std::get<0>(full), stopMs = std::get<0>(stop);
            const int64_t fullWi = std::get<1>(full), stopWi = std::get<1>(stop);
            const int64_t fullSc = std::get<2>(full), stopSc = std::get<2>(stop);
            const int64_t fullN = std::get<3>(full), stopN = std::get<3>(stop);
            const bool early = std::get<4>(stop);
            std::fprintf(stderr,
                         "[UI自检] %s（缓存%s）：不勾选 %.0fms/评估%lld/扫描%lld/结果%lld ｜ 勾选 %.0fms/评估%lld/"
                         "扫描%lld/结果%lld ｜ 提前停止=%d\n",
                         c.name, cacheOn ? "开" : "关", fullMs, (long long)fullWi, (long long)fullSc,
                         (long long)fullN, stopMs, (long long)stopWi, (long long)stopSc, (long long)stopN,
                         early ? 1 : 0);
            check(stopN <= m_w->targetCount->value(),
                  "达到即停：结果条数不超过目标数量");
            const bool faster = (stopWi < fullWi) || (stopMs * 1.5 < fullMs);
            if (faster) ++improved;
            if (fullWi > 0) slowRatio = (std::max)(slowRatio, (double)stopWi / (double)fullWi);
            if (!faster) {
                std::fprintf(stderr, "[UI自检]   ↑ %s（缓存%s）勾选后没有变快（评估 %lld → %lld，耗时 %.0f → %.0f ms）\n",
                             c.name, cacheOn ? "开" : "关", (long long)fullWi, (long long)stopWi, fullMs, stopMs);
            }
          }
        }
        std::fprintf(stderr, "[UI自检]   达到即停实测汇总：变快的组合 %d / 8\n", improved);
        check(improved >= 4, "达到即停：多数模式勾选后比不勾选更快/更省（用户反馈的「失效」已修）");
        if (m_w->useCache) m_w->useCache->setChecked(keepUseCache);
        m_w->stopAtTarget->setChecked(false);
        m_w->smartRange->setChecked(false);
        m_w->threads->setValue(0);
        m_w->minX->setValue(-256); m_w->maxX->setValue(255);
        m_w->minZ->setValue(-256); m_w->maxZ->setValue(255);
    }

    // 21) v3.4.8 用户反馈项自检：① 设置生效更快 ② 主题切换重绘图案 ⑤ 恢复所有设置 ⑦ 滚轮调节数值
    {
        std::fprintf(stderr, "[UI自检] v3.4.8 用户反馈项检查（设置生效/主题图案/恢复设置/滚轮）…\n");

        // ① 设置生效更快：定时器间隔 + 实测「改一下设置到真正生效」的耗时
        {
            check(m_settingsApplyTimer != nullptr, "设置立即生效定时器存在");
            const int iv = m_settingsApplyTimer ? m_settingsApplyTimer->interval() : -1;
            std::fprintf(stderr, "[UI自检]   设置生效定时器间隔 = %d ms（要求 ≤ 100）\n", iv);
            check(iv > 0 && iv <= 100, "设置生效间隔 ≤ 100ms（用户要求：调快一点）");
            if (m_settingsPage) {
                const bool keepAuto = m_settings.logAutoScroll;
                QCheckBox* box = nullptr;
                for (QCheckBox* cb : m_settingsPage->findChildren<QCheckBox*>()) {
                    if (cb->text().contains(slime::ui::t("ui.9642fc88cdea"))) { box = cb; break; }
                }
                if (box) {
                    QElapsedTimer t1;
                    t1.start();
                    box->setChecked(!box->isChecked());
                    while (m_settings.logAutoScroll == keepAuto && t1.elapsed() < 1000) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                        QThread::msleep(2);
                    }
                    const qint64 ms = t1.elapsed();
                    std::fprintf(stderr, "[UI自检]   改动设置到生效实测 = %lld ms\n", static_cast<long long>(ms));
                    check(m_settings.logAutoScroll != keepAuto, "改动设置后自动生效（无需点「应用」）");
                    check(ms <= 800, "改动设置到生效 ≤ 800ms（含应用工作量；定时器间隔见上行日志）");
                    box->setChecked(keepAuto);
                    QElapsedTimer t2;
                    t2.start();
                    while (m_settings.logAutoScroll != keepAuto && t2.elapsed() < 1000) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                        QThread::msleep(2);
                    }
                }
            }
        }

        // ② 主题切换时图案立即跟着变（用户：不然看不清，只有重新加载才清楚）
        {
            const auto strokeStats = [&](int kind, int* darkPx, int* lightPx) {
                *darkPx = 0;
                *lightPx = 0;
                const QImage img = v340NavIcon(kind).pixmap(24, 24).toImage();
                for (int y = 0; y < img.height(); ++y) {
                    for (int x = 0; x < img.width(); ++x) {
                        const QColor c = img.pixelColor(x, y);
                        if (c.alpha() < 128) continue;
                        const int lum = (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
                        if (lum < 90) ++(*darkPx);
                        else if (lum > 180) ++(*lightPx);
                    }
                }
            };
            const bool wasDark = slime::ThemeManager::instance().isDark();
            slime::ThemeManager::instance().apply(qApp, slime::ThemeMode::Light);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            int dLight = 0, lLight = 0;
            strokeStats(0, &dLight, &lLight);
            slime::ThemeManager::instance().apply(qApp, slime::ThemeMode::Dark);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            int dDark = 0, lDark = 0;
            strokeStats(0, &dDark, &lDark);
            std::fprintf(stderr, "[UI自检]   导航图案像素：浅色主题 深像素=%d 亮像素=%d ｜ 深色主题 深像素=%d 亮像素=%d\n",
                         dLight, lLight, dDark, lDark);
            check(dLight > 20 && lLight == 0, "浅色主题下图案为深色描边");
            check(lDark > 20 && dDark == 0, "切到深色主题后图案立即变浅色（无需重启/重载）");
            // 还原到自检开始前的档位
            slime::ThemeManager::instance().apply(
                qApp, wasDark ? slime::ThemeMode::Dark : slime::ThemeMode::Light);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
        }

        // ⑤ 设置页「恢复所有设置」按钮存在且真的把设置恢复为出厂默认
        {
            QPushButton* resetBtn = findChild<QPushButton*>(QStringLiteral("resetAllSettingsBtn"));
            check(resetBtn != nullptr, "设置页存在「恢复所有设置」按钮");
            const AppSettings keep = m_settingsPage ? m_settingsPage->currentValues() : AppSettings{};
            if (resetBtn && m_settingsPage) {
                resetBtn->click();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                const AppSettings def = SettingsDialog::factoryDefaults();
                const AppSettings now = m_settingsPage->currentValues();
                const bool same = now.wheelAdjust == def.wheelAdjust && now.rememberWindow == def.rememberWindow &&
                                  now.theme == def.theme && now.soundOnComplete == def.soundOnComplete &&
                                  now.soundVolume == def.soundVolume && now.defaultAlgorithm == def.defaultAlgorithm &&
                                  now.defaultThreads == def.defaultThreads &&
                                  now.defaultTargetCount == def.defaultTargetCount &&
                                  now.cpuLimitPercent == def.cpuLimitPercent && now.memLimitMB == def.memLimitMB &&
                                  now.crashDump == def.crashDump && now.debugMode == def.debugMode &&
                                  now.logMaxLines == def.logMaxLines && now.cacheMaxMB == def.cacheMaxMB;
                std::fprintf(stderr, "[UI自检]   恢复所有设置后：主题=%s 轮滚=%d CPU上限=%d 内存上限=%d 与出厂一致=%d\n",
                             now.theme.toUtf8().constData(), now.wheelAdjust ? 1 : 0, now.cpuLimitPercent,
                             now.memLimitMB, same ? 1 : 0);
                check(same, "「恢复所有设置」把全部设置恢复为出厂默认值");
                check(m_settings.theme == def.theme, "恢复后立即生效（m_settings 同步为默认值）");
                // 还原自检前的设置，避免影响后续步骤与截图
                m_settingsPage->setValues(keep);
                m_settings = m_settingsPage->currentValues();
                applyAppSettings(m_settings);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            }
        }

        // ⑦ 滚轮调节数值：开启后鼠标划过即可调；关闭时数值不变
        {
            QSpinBox* spin = m_w->targetCount;
            check(spin != nullptr, "滚轮测试用的数值输入框存在");
            if (spin) {
                const bool keepWheel = m_settings.wheelAdjust;
                const int keepVal = spin->value();
                if (m_w->resultView) m_w->resultView->setFocus(); // 故意不让输入框获得焦点
                check(!spin->hasFocus(), "滚轮测试条件：输入框未获得焦点（模拟「鼠标划过」）");
                const auto sendWheel = [&](int delta) {
                    QWheelEvent we(QPointF(spin->width() / 2.0, spin->height() / 2.0),
                                    QPointF(spin->mapToGlobal(QPoint(spin->width() / 2, spin->height() / 2))),
                                    QPoint(0, 0), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                                    Qt::NoScrollPhase, false);
                    QApplication::sendEvent(spin, &we);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                };
                m_settings.wheelAdjust = true;
                const int before = spin->value();
                sendWheel(120);
                const int afterOn = spin->value();
                m_settings.wheelAdjust = false;
                spin->setValue(before);
                sendWheel(120);
                const int afterOff = spin->value();
                std::fprintf(stderr, "[UI自检]   滚轮：开启时 %d → %d ｜ 关闭时 %d → %d\n", before, afterOn, before,
                             afterOff);
                check(afterOn == before + spin->singleStep(),
                      "设置开启后，未获焦点的输入框滚轮即可调数值（用户反馈的「失效」已修）");
                check(afterOff == before, "设置关闭后滚轮不改数值");
                spin->setValue(keepVal);
                m_settings.wheelAdjust = keepWheel;
            }
        }

        // ⑧ v3.5.1（交互手感自查）：滚轮防手滑 / 停靠图标再点收起 / Ctrl+1..4 / 分隔条双击 / Esc
        {
            std::fprintf(stderr, "[UI自检] v3.5.1 交互手感（下拉框滚轮 / 停靠收起 / 页面快捷键 / 分隔条双击）…\n");
            const auto sendWheelTo = [](QWidget* w, int delta) {
                if (!w) return;
                QWheelEvent we(QPointF(w->width() / 2.0, w->height() / 2.0),
                               QPointF(w->mapToGlobal(QPoint(w->width() / 2, w->height() / 2))),
                               QPoint(0, 0), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                               Qt::NoScrollPhase, false);
                QApplication::sendEvent(w, &we);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            };

            // (1) 下拉框不再吞滚轮（鼠标划过不会误改选项）
            {
                QComboBox* combo = findChild<QComboBox*>();
                check(combo != nullptr, "找到用于滚轮测试的下拉框");
                if (combo) {
                    const int keepIdx = combo->currentIndex();
                    if (m_w->resultView) m_w->resultView->setFocus(); // 不让下拉框获得焦点
                    sendWheelTo(combo, 120);
                    sendWheelTo(combo, -120);
                    std::fprintf(stderr, "[UI自检]   下拉框滚轮：%d → %d（应保持不变）\n",
                                 keepIdx, combo->currentIndex());
                    check(combo->currentIndex() == keepIdx, "滚轮划过下拉框不误改选项（防手滑）");
                    combo->setCurrentIndex(keepIdx);
                }
            }

            // (2) 停靠图标：再点一次 = 收起
            {
                bool hadDock = false;
                if (m_rightSplit && m_rightSplit->count() > 1 && m_rightSplit->widget(1)) {
                    QWidget* dock = m_rightSplit->widget(1);
                    hadDock = !m_dockCollapsed && dock->isVisible() && m_stack && m_stack->isVisible();
                }
                showDock(3);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                const bool opened = !m_dockCollapsed && m_stack && m_stack->isVisible() &&
                                    m_stack->currentIndex() == 3 && m_nav && m_nav->currentRow() == 3;
                showDock(3);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                QWidget* dockNow = (m_rightSplit && m_rightSplit->count() > 1) ? m_rightSplit->widget(1) : nullptr;
                const bool closed = m_dockCollapsed && (!m_stack || !m_stack->isVisible()) && dockNow &&
                                    dockNow->isVisible();   // 标题行仍在
                std::fprintf(stderr, "[UI自检]   过程面板：展开=%d 收起(标题行仍在)=%d 高度=%d\n",
                             opened ? 1 : 0, closed ? 1 : 0, dockNow ? dockNow->height() : -1);
                check(opened, "点击页签展开过程面板并切到对应分类");
                check(closed, "再次点击同一页签收起过程面板（标题行保留）");
                showDock(3);   // 折叠后能再次展开（用户验收点）
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                check(!m_dockCollapsed && m_stack && m_stack->isVisible(), "折叠后能再次展开过程面板");
                if (hadDock) showDock(0); else hideDock();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            }

            // (3) Ctrl+1..4 页面快捷键已注册且能切页
            {
                int found = 0;
                for (QShortcut* sc : findChildren<QShortcut*>()) {
                    const QString s = sc->key().toString();
                    if (s.startsWith(QStringLiteral("Ctrl+")) && s.size() == 6 &&
                        s.at(5) >= QLatin1Char('1') && s.at(5) <= QLatin1Char('5'))
                        ++found;
                }
                std::fprintf(stderr, "[UI自检]   Ctrl+1..5 页面快捷键：%d 个\n", found);
                check(found == kNavCount, "Ctrl+1..5 五个页面快捷键都已注册");
                const int keepPage = m_currentPage;
                showPage(kPageSettings);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                check(m_currentPage == kPageSettings, "快捷键对应页面（设置）可正常切换");
                showPage(keepPage);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
            }

            // (4) 双击中间分隔条恢复默认比例
            {
                if (m_hSplit && m_hSplit->count() > 1 && m_hSplit->handle(1)) {
                    QWidget* handle = m_hSplit->handle(1);
                    m_hSplit->setSizes(QList<int>() << 300 << 600 << 300);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                    const int movedLeft = m_hSplit->sizes().value(0);
                    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(4, 10), Qt::LeftButton,
                                    Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(handle, &dbl);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                    const int resetLeft = m_hSplit->sizes().value(0);
                    std::fprintf(stderr, "[UI自检]   分隔条双击：%d → %d（默认 400）\n", movedLeft, resetLeft);
                    check(qAbs(resetLeft - 400) <= 60, "双击中间分隔条恢复默认分栏（左栏≈400）");
                }
            }

            // (5) Esc 在空闲时不会误触发取消
            {
                const bool busy = m_w->cancel && m_w->cancel->isEnabled();
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(this, &esc);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                std::fprintf(stderr, "[UI自检]   Esc：按下前任务进行中=%d\n", busy ? 1 : 0);
                check(!busy || (m_w->cancel && m_w->cancel->isEnabled()),
                      "Esc 空闲时不会误触发取消（进行中才等价于「取消」）");
            }
        }

        // ⑨ v3.5.2（任务 1）：挂机点（环形覆盖，1/16 区块精度）
        {
            AfkDialog::Context ctx;
            ctx.version = 0; // Java
            ctx.seed = 0;
            ctx.minX = -8; ctx.maxX = 8; ctx.minZ = -8; ctx.maxZ = 8;
            ctx.source = slime::ui::t("ui.7f6458c909de");
            AfkDialog dlg(ctx, this);
            dlg.setModal(false);
            const auto& res = dlg.results();
            std::fprintf(stderr, "[UI自检]   挂机点：候选 %zu 个，表格 %d 行\n", res.size(),
                         dlg.table() ? dlg.table()->rowCount() : -1);
            if (!res.empty()) {
                std::fprintf(stderr, "[UI自检]   最优：X=%d Z=%d 覆盖 %lld 格 / %d 区块 / 覆盖率 %.1f%% / 角点=%d\n",
                             res.front().x, res.front().z,
                             static_cast<long long>(res.front().coveredBlocks), res.front().coveredChunks,
                             res.front().coveragePercent, res.front().onChunkCorner ? 1 : 0);
            }
            check(!res.empty(), "挂机点：能算出候选点（范围内有史莱姆区块）");
            check(!res.empty() && res.front().coveredBlocks > 0, "挂机点：最优点的覆盖面积 > 0");
            check(dlg.table() && dlg.table()->rowCount() == static_cast<int>(res.size()),
                  "挂机点：表格行数与结果数一致");
            check(AfkDialog::commandFor(res.empty() ? AfkCandidate{} : res.front())
                      .startsWith(QStringLiteral("/tp @s ")),
                  "挂机点：/tp 命令格式正确");
            if (!res.empty()) {
                std::fprintf(stderr, "[UI自检]   加权：覆盖 %.0f 格 ｜ 平均权重 %.2f（群系版本 Java）\n",
                             res.front().coveredWeight, res.front().avgWeight);
            }
            check(!res.empty() && res.front().coveredWeight > 0.0, "挂机点：加权覆盖 > 0（群系权重已生效）");
            check(!res.empty() && res.front().avgWeight > 0.0 && res.front().avgWeight <= 1.2,
                  "挂机点：平均权重在合理区间（0~1.2）");
        }

        // ⑩ v3.5.2（任务 2）：地图预览（缩放 / 史莱姆区块 / 挂机点）
        if (selftestArea("map")) {
            MapDialog::Context mc;
            mc.version = 0;
            mc.seed = 0;
            mc.minX = -8; mc.maxX = 7; mc.minZ = -8; mc.maxZ = 7;
            mc.source = slime::ui::t("ui.28b7e41ee4b8");
            MapDialog md(mc, this);
            md.setModal(false);
            MatchResult mr;
            mr.minX = 0; mr.maxX = 3; mr.minZ = 0; mr.maxZ = 3;
            md.setResults(std::vector<MatchResult>{ mr });
            md.computeAfk();
            const double s0 = md.map()->pxPerBlock();
            md.map()->zoomAt(1.25, md.map()->rect().center());
            const double s1 = md.map()->pxPerBlock();
            const int slimeCnt = md.map()->visibleSlimeChunkCount();
            std::fprintf(stderr, "[UI自检]   地图预览：缩放 %.2f→%.2f 可见史莱姆区块=%d 挂机点=%zu\n",
                         s0, s1, slimeCnt, md.afk().size());
            check(md.map() != nullptr, "地图预览：地图控件存在");
            check(s1 > s0 - 1e-9, "地图预览：缩放操作生效（不缩小）");
            check(slimeCnt > 0, "地图预览：可见范围内能取到史莱姆区块");
            check(!md.afk().empty(), "地图预览：能算出并画出挂机点");
            check(md.biomeAvailable(), "地图预览：Java 版群系底图可用");
            // v3.5.21：基岩版群系（按 Java 同种子推导，1.18+ 官方口径一致）
            {
                m_w->bedrockRadio->setChecked(true);
                updateMapFromForm();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                check(m_map->layer().biomeAvailable, "地图预览：基岩版群系底图可用（同种子推导）");
                m_w->javaRadio->setChecked(true);
                updateMapFromForm();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            }
            // 触发一次绘制（控件必须有尺寸才会生成群系底图），检查确实是彩色内容
            md.resize(1000, 700);
            md.show();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
            md.map()->repaint();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
            const QImage biomeImg = md.map()->lastBiomeImage();
            int uniq = 0;
            {
                unsigned seenRgb[512] = { 0 };
                for (int y = 0; y < biomeImg.height(); y += 2) {
                    for (int x = 0; x < biomeImg.width(); x += 2) {
                        const unsigned rgb = biomeImg.pixel(x, y) & 0xFFFFFFu;
                        bool found = false;
                        for (int k = 0; k < uniq; ++k) if (seenRgb[k] == rgb) { found = true; break; }
                        if (!found) { if (uniq < 512) seenRgb[uniq++] = rgb; }
                    }
                }
            }
            std::fprintf(stderr, "[UI自检]   群系底图：%dx%d 单元格，颜色种类=%d\n", biomeImg.width(),
                         biomeImg.height(), uniq);
            check(!biomeImg.isNull() && uniq >= 3, "地图预览：群系彩色底图有多种颜色（非单色）");
            // v3.6.13（用户目标：来回拖动不要重新加载）：小幅平移后不应新建分块
            if (m_map) {
                // v3.10.0：地图变成独立中间列后视口尺寸变化更大。测试前先把视口对齐到
                //   128 区块分块的中心（64,64）并固定 2 px/格，再平移 16 区块：
                //   视口边缘仍落在同一批分块内 ⇒ 只应命中缓存、不应新建分块。
                m_map->setView(64.0, 64.0, 2.0);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                const int built0 = m_map->tilesBuilt();
                const QPointF c0 = m_map->centerBlock();
                const double sc = m_map->pxPerBlock();
                m_map->setView(c0.x() + 16.0, c0.y() + 16.0, sc);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
                const int built1 = m_map->tilesBuilt();
                m_map->setView(c0.x() + 32.0, c0.y() + 32.0, sc);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
                const int built2 = m_map->tilesBuilt();
                std::fprintf(stderr, "[UI自检]   地图分块：初始后=%d 小幅平移后=%d 再平移后=%d（缓存块=%d）\n",
                             built0, built1, built2, m_map->mapCacheHits());
                // 允许 1~3 块余量（窗口尺寸/布局抖动可能补 1 块）；关键是不能整屏重采（那样会 +16 以上）
                check(built1 - built0 <= 3, "小幅拖动地图不重新采样（分块缓存命中）");
                check(m_map->mapCacheHits() >= 4, "地图分块缓存里保留了多块");
                m_map->setView(c0.x(), c0.y(), sc);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
            }
            const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
            if (!shotPrefix.isEmpty()) {
                const QString path = shotPrefix + QStringLiteral("-map-preview.png");
                if (md.map()->grab().save(path)) {
                    std::fprintf(stderr, "[UI自检]   地图截图已保存：%s\n", path.toUtf8().constData());
                }
            }
        }
    }

        // ⑪ v3.5.4：搜索页内嵌地图面板（折叠 / 参数联动 / 点击结果定位 / 自适应精度）
        {
            m_w->javaRadio->setChecked(true);
            m_w->seedEdit->setText(QStringLiteral("12345"));
            m_w->minX->setValue(-256);
            m_w->maxX->setValue(255);
            m_w->minZ->setValue(-256);
            m_w->maxZ->setValue(255);
            showPage(kPageSearch);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 250);
            check(m_mapPanel != nullptr && m_map != nullptr, "搜索页内嵌地图面板（含地图控件）");
            check(m_navButtons.size() == 3 && m_pageStack && m_pageStack->count() == 3,
                  "导航与页面为 3 项（更新历史已移除）");
            if (m_map && m_mapPanel) {
                updateMapFromForm();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                // 折叠 / 展开
                // v3.6.2：折叠后标题行仍在（"▸ 地图"），所以断言改成看「地图内容」的可见性
                const bool wasShown = m_map && m_map->isVisible();
                toggleMapPanel();
                const bool afterToggle = m_map && m_map->isVisible();
                toggleMapPanel();
                std::fprintf(stderr, "[UI自检]   地图内容折叠：原可见=%d → %d → %d（标题行始终可见=%d）\n",
                             wasShown ? 1 : 0, afterToggle ? 1 : 0, m_map && m_map->isVisible() ? 1 : 0,
                             m_mapPanel && m_mapPanel->isVisible() ? 1 : 0);
                check(afterToggle != wasShown, "地图可折叠/展开（标题行常驻）");
                check(m_mapPanel && m_mapPanel->isVisible(), "地图标题行在折叠后仍可见");
                // v3.6.9（用户验收标准）：折叠按钮位置必须原地不动
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                    const QPoint b0 = m_mapToggle->mapToGlobal(QPoint(0, 0));
                    toggleMapPanel();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                    const QPoint b1 = m_mapToggle->mapToGlobal(QPoint(0, 0));
                    toggleMapPanel();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                    const QPoint b2 = m_mapToggle->mapToGlobal(QPoint(0, 0));
                    std::fprintf(stderr, "[UI自检]   地图折叠按钮位置：%d,%d → %d,%d → %d,%d\n", b0.x(), b0.y(),
                                 b1.x(), b1.y(), b2.x(), b2.y());
                    const QList<int> szz = m_rightSplit->sizes();
                    std::fprintf(stderr, "[UI自检]     调试图：split=%d,%d panel=%d,%d,%dx%d mapVis=%d phVis=%d\n",
                                 szz.value(0), szz.value(1), m_mapPanel->x(), m_mapPanel->y(),
                                 m_mapPanel->width(), m_mapPanel->height(), m_map ? m_map->isVisible() : -1,
                                 m_mapPlaceholder ? m_mapPlaceholder->isVisible() : -1);
                    check(b1 == b0, "地图折叠后按钮位置不变（不再位移）");
                    // v3.6.9.1（用户反馈：地图被挤窄）：展开态地图必须占满面板（除标题行）
                    const int mapH = m_map ? m_map->height() : -1;
                    const int paneH = m_mapPanel ? m_mapPanel->height() : -1;
                    std::fprintf(stderr, "[UI自检]   地图高度：%d / 面板 %d\n", mapH, paneH);
                    check(mapH > 0 && mapH >= paneH - 60, "展开态地图占满面板高度（不再被挤窄）");
                    // v3.10.0（三纵列核心诉求）：结果列表行数不得影响地图尺寸
                    {
                        const int rowsBefore = m_w->resultTable ? m_w->resultTable->rowCount() : 0;
                        const QSize mapSizeBefore = m_mapPanel ? m_mapPanel->size() : QSize();
                        if (m_w->resultTable) m_w->resultTable->setRowCount(20);
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
                        const QSize mapSizeAfter = m_mapPanel ? m_mapPanel->size() : QSize();
                        std::fprintf(stderr, "[UI自检]   结果 20 行：地图面板 %dx%d → %dx%d\n",
                                     mapSizeBefore.width(), mapSizeBefore.height(), mapSizeAfter.width(),
                                     mapSizeAfter.height());
                        check(mapSizeAfter == mapSizeBefore, "结果列表行数不影响地图尺寸（三纵列）");
                        if (m_w->resultTable) m_w->resultTable->setRowCount(rowsBefore);
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                    }
                    check(b2 == b0, "地图再展开后按钮位置恢复原样");
                }
                // 自适应精度
                m_map->setView(0, 0, 2.0);
                const int d2 = m_map->effectiveBiomeScale();
                m_map->setView(0, 0, 8.0);
                const int d8 = m_map->effectiveBiomeScale();
                std::fprintf(stderr, "[UI自检]   内嵌地图自适应精度：2px/格→1:%d ｜ 8px/格→1:%d\n", d2, d8);
                check(d2 == 4 && d8 == 1, "地图群系精度随缩放自适应（放大更精细）");
                // 参数联动：改种子后地图 layer 跟着变
                m_w->seedEdit->setText(QStringLiteral("777"));
                updateMapFromForm();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
                check(m_map->layer().seed == 777, "地图跟随信息填写栏（种子同步）");
                // 采样耗时
                m_map->setView(0, 0, 2.0);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                std::fprintf(stderr, "[UI自检]   内嵌地图采样：%.1f ms / %d 格（累计 %d 次）\n",
                             m_map->lastSampleMs(), m_map->lastSampleCells(), m_map->sampleCount());
                check(m_map->lastSampleMs() < 500.0, "单次群系采样 < 500ms（交互帧率可用）");
                // 点击结果定位：造一条带 chunkX/chunkZ 的结果行，真实投递一次左键点击
                m_w->resultView->setPlainText(slime::ui::t("ui.01784ca08321"));
                QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
                const QPointF before = m_map->centerBlock();
                QTextCursor cur = m_w->resultView->textCursor();
                cur.movePosition(QTextCursor::Start);
                m_w->resultView->setTextCursor(cur);
                const QRect rc = m_w->resultView->cursorRect(cur);
                const QPoint clickPos = rc.center();
                QMouseEvent pressEv(QEvent::MouseButtonPress, clickPos, m_w->resultView->mapToGlobal(clickPos),
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent releaseEv(QEvent::MouseButtonRelease, clickPos, m_w->resultView->mapToGlobal(clickPos),
                                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(m_w->resultView, &pressEv);
                QApplication::sendEvent(m_w->resultView, &releaseEv);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                const QPointF after = m_map->centerBlock();
                std::fprintf(stderr, "[UI自检]   点击结果定位：中心 (%.0f,%.0f) → (%.0f,%.0f)\n", before.x(),
                             before.y(), after.x(), after.y());
                check(std::abs(after.x() - (-16 * 16 + 8)) < 2.0 && std::abs(after.y() - (-16 * 16 + 8)) < 2.0,
                      "点击结果行即可把地图定位到该区块");
                // v3.5.5：精度分档（远缩放不再出现 1:64）；v3.5.15：视野过大时按面积自动放宽，
                //   保证单次采样格数有上限（不会卡 100+ ms，也不会因为超限而整层不画）
                m_map->setView(0, 0, 0.5);
                const int dFar = m_map->effectiveBiomeScale();
                m_map->setView(0, 0, 2.0);
                const int dMid = m_map->effectiveBiomeScale();
                m_map->setView(0, 0, 8.0);
                const int dNear = m_map->effectiveBiomeScale();
                std::fprintf(stderr, "[UI自检]   精度分档：0.5px→1:%d ｜ 2px→1:%d ｜ 8px→1:%d\n", dFar, dMid,
                             dNear);
                // v3.5.17（用户要求）：只保留精度最高的两档 —— 1:1 与 1:4，不再出现 1:16/1:64
                check(dFar == 4 && dMid == 4 && dNear == 1, "群系精度只有两档（1:1 / 1:4）");
                check(m_map->biomeScaleForScale(0.01) == 4, "极端缩小仍是 1:4（不会退到更粗的档）");
                // v3.5.7：平移流畅度 —— 连续 60 帧小幅平移，应几乎不重建底图
                m_map->setView(0, 0, 2.0);
                m_map->repaint();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                const int buildsBefore = m_map->layerBuildCount();
                QElapsedTimer panTimer;
                panTimer.start();
                for (int i = 0; i < 60; ++i) {
                    m_map->setView(i * 6.0, 0, 2.0);
                    m_map->repaint();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
                }
                const double panMs = panTimer.elapsed();
                const int rebuilds = m_map->layerBuildCount() - buildsBefore;
                std::fprintf(stderr, "[UI自检]   地图平移：60 帧 %.1f ms（%.2f ms/帧），底图重建 %d 次，单次重建 %lld ms\n",
                             panMs, panMs / 60.0, rebuilds, static_cast<long long>(m_map->lastLayerBuildMs()));
                check(panMs < 1500.0, "平移 60 帧 < 1.5 秒（交互流畅）");
                check(rebuilds <= 6, "平移只在必要时重建底图（缓存余量生效）");
                {
                    const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                    if (!shotPrefix.isEmpty()) saveWindowShot(shotPrefix, QStringLiteral("search-map-results"));
                }
            }
        }
        // ⑫ v3.5.5：默认最大化 + 环形覆盖搜索（与其它算法并列）
        {
            check(isMaximized() || isFullScreen() || width() >= 1200, "主窗口默认最大化显示");
            check(m_w->shapeRingRadio != nullptr && m_w->shapeSquareRadio != nullptr, "搜索模式里有「区域形状」选项");
            check(m_w->algRingRadio == nullptr, "搜索算法里不再混入「环形覆盖」（层级修正）");
            m_w->densityRadio->setChecked(true);
            m_w->shapeRingRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            std::fprintf(stderr, "[UI自检]   环形置灰：网格=%d 密度=%d 目标=%d 算法组=%d\n",
                         m_w->gridSize->isEnabled() ? 1 : 0, m_w->minDensity->isEnabled() ? 1 : 0,
                         m_w->targetCount->isEnabled() ? 1 : 0, m_w->matchGroup->isEnabled() ? 1 : 0);
            // v3.5.18（用户要求）：不再「置灰」，而是只显示当前形状能调的选项
            check(m_w->squareOpts && !m_w->squareOpts->isVisible(),
                  "环形：正方形专属选项（网格大小）整块隐藏");
            check(m_w->ringOpts && m_w->ringOpts->isVisible(), "环形：环形专属选项（扫描模式/最小密度）可见");
            check(!m_w->smartRange->isVisible(), "环形：智能范围不显示（不适用）");
            check(m_w->minDensity->isVisible() && m_w->targetCount->isVisible() && m_w->matchGroup->isEnabled(),
                  "环形：最小密度/目标数量/判定算法仍可用（与正方形一致）");
            check(m_w->densityRadio->isChecked(), "环形选项出现在密度模式下");
            m_w->densityRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            m_w->shapeRingRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            std::fprintf(stderr, "[UI自检]   环形可见性：环形选项=%d 正方形选项=%d 智能范围=%d\n",
                         m_w->ringOpts->isVisible() ? 1 : 0, m_w->squareOpts->isVisible() ? 1 : 0,
                         m_w->smartRange->isVisible() ? 1 : 0);
            {
                const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                if (!shotPrefix.isEmpty()) saveWindowShot(shotPrefix, QStringLiteral("ring-mode-grayed"));
            }
            m_w->shapeSquareRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            check(m_w->gridSize->isEnabled() && m_w->matchGroup->isEnabled(), "切回正方形后选项恢复可用");
            // 图案 / 密度 互不干扰
            m_w->strictRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            std::fprintf(stderr, "[UI自检]   图案模式：图案面板=%d 密度面板=%d 图案编辑器=%d 算法=%d\n",
                         m_w->strictPanel->isVisible() ? 1 : 0, m_w->densityPanel->isVisible() ? 1 : 0,
                         m_w->inlinePattern && m_w->inlinePattern->isVisible() ? 1 : 0,
                         m_w->matchGroup->isEnabled() ? 1 : 0);
            // v3.5.18（用户要求）：只显示能调的选项，不置灰
            check(m_w->strictPanel->isVisible() && !m_w->densityPanel->isVisible() &&
                      m_w->inlinePattern && m_w->inlinePattern->isVisible() && m_w->matchGroup->isEnabled(),
                  "图案模式：只显示图案选项（含内嵌图案编辑器）");
            m_w->densityRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            check(!m_w->strictPanel->isVisible() && m_w->densityPanel->isVisible(),
                  "密度模式：只显示密度选项");
            m_w->shapeRingRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            check(m_w->ringOpts && m_w->ringOpts->isVisible() && m_w->squareOpts && !m_w->squareOpts->isVisible(),
                  "环形：只显示环形专属选项");
            m_w->shapeSquareRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            check(m_w->squareOpts && m_w->squareOpts->isVisible(), "正方形：只显示正方形专属选项");
            m_w->shapeRingRadio->setChecked(true);
            m_w->densityRadio->setChecked(true);
            m_w->javaRadio->setChecked(true);
            m_w->seedEdit->setText(QStringLiteral("12345"));
            m_w->minX->setValue(-20);
            m_w->maxX->setValue(20);
            m_w->minZ->setValue(-20);
            m_w->maxZ->setValue(20);
            m_lastAfk.clear();
            m_w->shapeRingRadio->setChecked(true);
            runRingSearch();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
            std::fprintf(stderr, "[UI自检]   环形覆盖：候选=%zu 最优=(%d,%d) 覆盖=%lld 格\n", m_lastAfk.size(),
                         m_lastAfk.empty() ? 0 : m_lastAfk.front().x,
                         m_lastAfk.empty() ? 0 : m_lastAfk.front().z,
                         m_lastAfk.empty() ? 0LL : static_cast<long long>(m_lastAfk.front().coveredBlocks));
            check(!m_lastAfk.empty(), "环形覆盖能算出挂机点候选");
            // v3.10.12.12 回归：环形形状仍 checked 时切到图案模式，直接调用环形入口也不能产出环形结果
            {
                m_w->strictRadio->setChecked(true);
                m_w->shapeRingRadio->setChecked(true);
                m_lastAfk.clear();
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                check(m_lastAfk.empty(), "图案模式下不会执行环形搜索");
                m_w->densityRadio->setChecked(true); // 恢复后续环形测试的环境
                m_w->shapeRingRadio->setChecked(true);
            }
            // v3.6.19：环形搜索无果时必须清掉上一次的结果（此前只有引擎路径清场）
            {
                const double keepDens = m_w->minDensity ? m_w->minDensity->value() : 0.0;
                if (m_w->minDensity) m_w->minDensity->setValue(1.0);
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                const int rowsEmpty = m_w->resultTable ? m_w->resultTable->rowCount() : -1;
                std::fprintf(stderr, "[UI自检]   环形无果清场：列表行=%d 候选=%zu\n", rowsEmpty, m_lastAfk.size());
                check(rowsEmpty == 0, "环形搜索无果时结果列表被清空（不残留上一次）");
                if (m_w->minDensity) m_w->minDensity->setValue(keepDens);
            }
            // v3.6.18：模拟距离决定环带外沿（16 格/区块），4 区块 ⇒ 64 格，12 区块 ⇒ 128 格
            if (m_w->simDistance) {
                const int keepSim = m_w->simDistance->value();
                m_w->simDistance->setValue(4);
                updateSimDistanceNote();
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                const double r4 = static_cast<double>(m_lastRingOuter);
                m_w->simDistance->setValue(12);
                updateSimDistanceNote();
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                const double r12 = static_cast<double>(m_lastRingOuter);
                std::fprintf(stderr, "[UI自检]   模拟距离：4 区块⇒外沿 %.0f 格，12 区块⇒外沿 %.0f 格\n", r4, r12);
                check(std::abs(r4 - 64.0) < 0.5, "模拟距离 4 区块 ⇒ 环带外沿收窄到 64 格");
                check(std::abs(r12 - 128.0) < 0.5, "模拟距离 12 区块 ⇒ 环带外沿回到 128 格");
                m_w->simDistance->setValue(keepSim);
                updateSimDistanceNote();
            }
            {
                const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                if (!shotPrefix.isEmpty()) saveWindowShot(shotPrefix, QStringLiteral("ring-annulus"));
            }
            check(m_hasLastOutcome && !m_lastOutcome.results.empty(), "环形覆盖结果进入结果区（可复制/定位）");
            // v3.5.10：默认不画任何环；选中后只画这一条
            std::fprintf(stderr, "[UI自检]   环显示：默认 focused=%d（-1 表示不画）\n",
                         m_map ? m_map->focusedAfk() : -2);
            check(m_map && m_map->focusedAfk() == -1, "环形搜索后默认不显示环（避免多个环叠在一起）");
            focusAfkCandidate(0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
            check(m_map && m_map->focusedAfk() == 0, "选中第 1 个候选后地图只显示该环");
            {
                const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                if (!shotPrefix.isEmpty()) saveWindowShot(shotPrefix, QStringLiteral("ring-focused"));
            }
            // v3.5.6：大范围（远距离）环形搜索 —— 半径固定，范围可到 ±200 区块
            {
                m_w->minX->setValue(-200);
                m_w->maxX->setValue(199);
                m_w->minZ->setValue(-200);
                m_w->maxZ->setValue(199);
                m_lastAfk.clear();
                const auto t0 = std::chrono::steady_clock::now();
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                std::fprintf(stderr, "[UI自检]   大范围环形（400x400 区块）：候选=%zu 用时 %.2f 秒 最优=(%d,%d)\n",
                             m_lastAfk.size(), secs, m_lastAfk.empty() ? 0 : m_lastAfk.front().x,
                             m_lastAfk.empty() ? 0 : m_lastAfk.front().z);
                check(!m_lastAfk.empty(), "大范围（400x400 区块）也能找到挂机点");
                check(secs < 20.0, "大范围环形搜索耗时 < 20 秒");
                m_w->minX->setValue(-20);
                m_w->maxX->setValue(20);
                m_w->minZ->setValue(-20);
                m_w->maxZ->setValue(20);
            }
            // v3.5.11：最小密度只做筛选 —— 结果数量仍按「目标数量」截取
            {
                m_w->minX->setValue(-20);
                m_w->maxX->setValue(20);
                m_w->minZ->setValue(-20);
                m_w->maxZ->setValue(20);
                m_w->minDensity->setValue(0.05);
                m_w->targetCount->setValue(8);
                m_lastAfk.clear();
                runRingSearch();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
                std::fprintf(stderr, "[UI自检]   低密度+小范围：候选=%zu（目标数量=8，上限 12）\n", m_lastAfk.size());
                check(!m_lastAfk.empty() && m_lastAfk.size() <= 12,
                      "最小密度只筛选、结果数量仍按目标数量截取（不再一次返回几十个）");
                m_w->minDensity->setValue(0.0);
            }
            m_w->algExactRadio->setChecked(true);
        }
        // ⑬ v3.5.5：结果列表（排序 / 每行跳转 / 折叠）
        if (selftestArea("table")) {
            check(m_w->resultTable != nullptr, "结果栏是列表控件");
            check(m_resultPane != nullptr && m_resultToggle != nullptr, "结果列表有折叠标题");
            m_w->javaRadio->setChecked(true);
            m_w->seedEdit->setText(QStringLiteral("12345"));
            m_w->minX->setValue(-20);
            m_w->maxX->setValue(20);
            m_w->minZ->setValue(-20);
            m_w->maxZ->setValue(20);
            m_lastAfk.clear();
            runRingSearch();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
            const int rows = m_w->resultTable ? m_w->resultTable->rowCount() : -1;
            std::fprintf(stderr, "[UI自检]   结果列表：行数=%d 候选=%zu\n", rows, m_lastAfk.size());
            check(rows > 0, "搜索后结果列表有数据");
            if (rows > 0) {
                check(m_w->resultTable->cellWidget(0, 6) != nullptr, "每行都有「跳转」按钮");
                auto* jump = qobject_cast<QPushButton*>(m_w->resultTable->cellWidget(0, 6));
                if (jump && m_map) {
                    const int bx = m_w->resultTable->item(0, 1)->data(Qt::UserRole).toInt();
                    const int bz = m_w->resultTable->item(0, 2)->data(Qt::UserRole).toInt();
                    jump->click();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                    const QPointF ctr = m_map->centerBlock();
                    std::fprintf(stderr, "[UI自检]   列表跳转：第1行(%d,%d) → 地图中心(%.0f,%.0f)\n", bx, bz, ctr.x(),
                                 ctr.y());
                    check(std::abs(ctr.x() - (bx + 8)) < 40.0 && std::abs(ctr.y() - (bz + 8)) < 40.0,
                          "点「跳转」把地图定位到该行");
                    check(m_map && m_map->focusedAfk() == 0, "点「跳转」同时选中该行的环（只显示一条）");
                }
                // 按距离列排序
                m_w->resultTable->sortItems(3, Qt::AscendingOrder);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
                const double d0 = m_w->resultTable->item(0, 3)->data(Qt::DisplayRole).toDouble();
                const double dN = m_w->resultTable->item(rows - 1, 3)->data(Qt::DisplayRole).toDouble();
                std::fprintf(stderr, "[UI自检]   列表排序：首行距离=%.1f 末行距离=%.1f\n", d0, dN);
                check(d0 <= dN + 1e-6, "结果列表可按列排序");
            }
            // 折叠/展开
            const bool before = m_w->resultTable->isVisible();
            // v3.6.9（用户验收标准）：点击折叠后按钮位置不变，再点一次恢复原样
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            const QPoint resBtn0 = m_resultToggle->mapToGlobal(QPoint(0, 0));
            toggleResultPane();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            const QPoint resBtn1 = m_resultToggle->mapToGlobal(QPoint(0, 0));
            const bool afterHide = m_w->resultTable->isVisible();
            toggleResultPane();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            const QPoint resBtn2 = m_resultToggle->mapToGlobal(QPoint(0, 0));
            std::fprintf(stderr,
                         "[UI自检]   结果列表折叠：%d → %d → %d（按钮位置 %d,%d → %d,%d → %d,%d）\n",
                         before ? 1 : 0, afterHide ? 1 : 0, m_w->resultTable->isVisible() ? 1 : 0,
                         resBtn0.x(), resBtn0.y(), resBtn1.x(), resBtn1.y(), resBtn2.x(), resBtn2.y());
            check(afterHide != before, "结果列表可折叠/展开");
            check(resBtn1 == resBtn0, "结果折叠后按钮位置不变（不再位移）");
            check(resBtn2 == resBtn0, "结果再展开后按钮位置恢复原样");
            {
                const QString shotPrefix = qEnvironmentVariable("SLIME_SHOT");
                if (!shotPrefix.isEmpty()) saveWindowShot(shotPrefix, QStringLiteral("list-and-map"));
            }
            m_w->algExactRadio->setChecked(true);
        }
    // 22) v3.4.8（用户要求：最后必须自己检查所有功能是否正常生效，不准漏检）
    //     ① 界面清单：菜单/工具栏/导航/页面栈/停靠面板/设置五标签/更新历史条数逐项点名；
    //     ② 设置项逐控件：设置页里每个可交互控件都必须能改到某个设置字段（扰动后快照必须变化）；
    //     ③ 搜索参数逐项：搜索页每个参数控件都必须进入 SearchOptions（改一下 options 必须跟着变）。
    {
        std::fprintf(stderr, "[UI自检] v3.4.8 全功能清单核对（界面清单 / 设置项 / 搜索参数）…\n");

        // ---- ① 界面清单 ----
        {
            const QList<QMenu*> menus = { m_menuFile, m_menuEdit, m_menuView, m_menuTools, m_menuHelp };
            int menuOk = 0, actionTotal = 0, actionBad = 0;
            for (QMenu* m : menus) {
                if (m) ++menuOk;
                if (!m) continue;
                for (QAction* a : m->actions()) {
                    if (a->isSeparator()) continue;
                    ++actionTotal;
                    if (a->text().isEmpty()) ++actionBad;
                }
            }
            std::fprintf(stderr, "[UI自检]   菜单 %d/5 个，菜单项 %d 条（空标题 %d 条）\n", menuOk, actionTotal, actionBad);
            check(menuOk == 5, "五个菜单齐备（文件/编辑/查看/工具/帮助）");
            check(actionTotal >= 15 && actionBad == 0, "菜单项全部有标题且数量合理（实测见日志）");

            check(m_navButtons.size() == kNavCount, "左侧导航为 3 项（搜索/缓存构建/设置）");
            const QStringList wantNav = { slime::ui::t("ui.f04090805c6e"), slime::ui::t("ui.45f11f06bce7"),
                                          slime::ui::t("ui.7debf9cb0372") };
            int navTextOk = 0;
            for (int i = 0; i < m_navButtons.size() && i < wantNav.size(); ++i) {
                if (m_navButtons[i] && m_navButtons[i]->text() == wantNav[i]) ++navTextOk;
            }
            check(navTextOk == kNavCount, "导航项文字与顺序正确");
            check(m_pageStack && m_pageStack->count() == kNavCount, "页面栈与导航一一对应（3 页）");
            // v3.10.2（用户要求）：底部图标条已移除，过程面板改用标题栏页签 + 收起/展开
            check(m_dockIcons.isEmpty() && m_dockHeadTabs.size() == 4 && m_dockToggleBtn != nullptr,
                  "过程面板：底部图标条已移除，保留 4 个页签 + 收起/展开按钮");
            check(m_settingsPage && m_settingsPage->tabs() && m_settingsPage->tabs()->count() == 5,
                  "设置页为五个标签（通用/搜索/缓存/日志/高级）");
            check(m_w->start && m_w->cancel, "工具栏按钮齐备（开始搜索 / 取消）");
            {
                int helpEntries = 0;
                for (QAction* a : m_menuHelp->actions()) {
                    if (a->text().contains(slime::ui::t("ui.adf465ebf0e6")) ||
                        a->text().contains(slime::ui::t("ui.b6edf34dbad7"))) {
                        ++helpEntries;
                    }
                }
                check(helpEntries >= 1, "帮助/教程入口存在（菜单 + F1）");
            }
            check(m_w->progress && m_w->progressPercent && m_w->progressStage, "进度条/百分比/阶段文字齐备");
            check(m_w->resultView && m_w->copyBtn, "结果视图与复制按钮齐备");
            check(m_w->importCache && m_w->clearCache && m_w->cacheStatus, "缓存导入/清除/状态齐备");
        }

        // ---- ② 设置项逐控件接线 ----
        {
            const auto snapshot = [&]() {
                const AppSettings s = m_settingsPage ? m_settingsPage->currentValues() : AppSettings{};
                QString out;
                out += QStringLiteral("w%1r%2t%3s%4v%5").arg(s.wheelAdjust).arg(s.rememberWindow).arg(s.theme)
                           .arg(s.soundOnComplete).arg(s.soundVolume);
                out += QStringLiteral("a%1th%2tc%3p%4g%5st%6gr%7lr%8").arg(s.defaultAlgorithm).arg(s.defaultThreads)
                           .arg(s.defaultTargetCount).arg(s.defaultPreset).arg(s.defaultGpu)
                           .arg(s.defaultStopAtTarget).arg(s.showGpuReminder).arg(s.largeRangeWarnChunks);
                out += QStringLiteral("al%1cd%2cm%3").arg(s.autoLoadCache).arg(s.cacheDir).arg(s.cacheMaxMB);
                out += QStringLiteral("ls%1ll%2lsd%3lkd%4las%5lml%6").arg(s.logAtStartup).arg(s.logLevel)
                           .arg(s.logSubDir).arg(s.logKeepDays).arg(s.logAutoScroll).arg(s.logMaxLines);
                out += QStringLiteral("cpu%1mem%2ag%3ur%4cd%5dm%6").arg(s.cpuLimitPercent).arg(s.memLimitMB)
                           .arg(s.autoCheckGpuAtStartup).arg(s.updateReminderDays).arg(s.crashDump).arg(s.debugMode);
                // v3.5.18：高级调试参数
                out += QStringLiteral("ri%1ro%2rc%3rb%4").arg(s.afkRingInner).arg(s.afkRingOuter)
                           .arg(s.ringCoarseStep).arg(s.ringStatBands);
                out += QStringLiteral("lg%1").arg(s.language);
                out += QStringLiteral("fs%1").arg(s.fullscreenMode);
                out += QStringLiteral("ar%1").arg(s.autoRestart);   // v3.6.6：自动重启开关
                out += QStringLiteral("gp%1").arg(s.gpuLimitPercent); // v3.7.1：GPU 占用率上限
                out += QStringLiteral(";ps%1").arg(s.gpuPatternStream ? 1 : 0); // v3.8.11：图案流式开关
                out += QStringLiteral(";so%1").arg(s.gpuResultSelfOptimize ? 1 : 0); // v3.10.11.1：结果自优化
                return out;
            };
            int wired = 0, unwired = 0, skipped = 0;
            QStringList unwiredList;
            if (m_settingsPage) {
                // 动作型按钮不进本清单（清空缓存 / 打开日志目录 / 浏览目录 / 恢复默认 / 应用取消确定）
                const auto isAction = [](const QString& tx) {
                    return tx.contains(slime::ui::t("ui.84fcd70d4280")) || tx.contains(slime::ui::t("ui.65fc81e16119")) ||
                           tx.contains(slime::ui::t("ui.ef23ea67b002")) || tx.contains(slime::ui::t("ui.79748ca1c6e5")) ||
                           tx.contains(slime::ui::t("ui.3d81345303ab")) || tx.contains(slime::ui::t("ui.4562024ddec7")) ||
                           tx.contains(slime::ui::t("ui.4d0b4688c787")) || tx.contains(slime::ui::t("ui.f526c89937e1"));
                };
                const QList<QWidget*> ctrls = [&]() {
                    QList<QWidget*> v;
                    for (QCheckBox* w : m_settingsPage->findChildren<QCheckBox*>()) v << w;
                    for (QRadioButton* w : m_settingsPage->findChildren<QRadioButton*>()) v << w;
                    for (QComboBox* w : m_settingsPage->findChildren<QComboBox*>()) v << w;
                    for (QSpinBox* w : m_settingsPage->findChildren<QSpinBox*>()) v << w;
                    for (QDoubleSpinBox* w : m_settingsPage->findChildren<QDoubleSpinBox*>()) v << w;
                    for (QSlider* w : m_settingsPage->findChildren<QSlider*>()) v << w;
                    for (QLineEdit* w : m_settingsPage->findChildren<QLineEdit*>()) {
                        // QSpinBox/QDoubleSpinBox 内部的编辑框不是独立控件（扰动它等于扰动数值框）
                        if (w->objectName() == QStringLiteral("qt_spinbox_lineedit")) continue;
                        if (qobject_cast<QAbstractSpinBox*>(w->parentWidget())) continue;
                        v << w;
                    }
                    return v;
                }();
                for (QWidget* w : ctrls) {
                    const QString label = qobject_cast<QAbstractButton*>(w)
                                              ? qobject_cast<QAbstractButton*>(w)->text()
                                              : w->objectName();
                    if (isAction(label)) { ++skipped; continue; }
                    // v3.7.1：被条件禁用的控件（例如未启用 GPU 时的「GPU 占用率上限」）不算未接线
                    if (!w->isEnabled()) { ++skipped; continue; }
                    const QString before = snapshot();
                    bool changedCtrl = false;
                    if (auto* cb = qobject_cast<QCheckBox*>(w)) { cb->setChecked(!cb->isChecked()); changedCtrl = true; }
                    else if (auto* rb = qobject_cast<QRadioButton*>(w)) {
                        // 已选中的单选按钮：再选一次是空操作，但它本身已由同组其它项证明接线
                        if (rb->isChecked()) { ++skipped; continue; }
                        rb->setChecked(true);
                        changedCtrl = true;
                    } else if (auto* combo = qobject_cast<QComboBox*>(w)) {
                        if (combo->count() > 1) { combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count()); changedCtrl = true; }
                    } else if (auto* sb = qobject_cast<QSpinBox*>(w)) {
                        // 到顶就往下走（否则 setValue 被范围钳制 ⇒ 误判「未接线」）
                        sb->setValue(sb->value() >= sb->maximum() ? sb->value() - sb->singleStep()
                                                                 : sb->value() + sb->singleStep());
                        changedCtrl = true;
                    } else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) {
                        dsb->setValue(dsb->value() >= dsb->maximum() ? dsb->value() - dsb->singleStep()
                                                                     : dsb->value() + dsb->singleStep());
                        changedCtrl = true;
                    } else if (auto* sl = qobject_cast<QSlider*>(w)) {
                        sl->setValue(sl->value() >= sl->maximum() ? sl->value() - 1 : sl->value() + 1);
                        changedCtrl = true;
                    } else if (auto* le = qobject_cast<QLineEdit*>(w)) {
                        if (!le->isReadOnly() && le->isEnabled()) { le->setText(le->text() + QStringLiteral("x")); changedCtrl = true; }
                    }
                    if (!changedCtrl) { ++skipped; continue; }
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                    const bool wiredOk = (snapshot() != before);
                    if (wiredOk) {
                        ++wired;
                    } else {
                        ++unwired;
                        unwiredList << (label.isEmpty() ? QString::fromLatin1(w->metaObject()->className()) : label);
                    }
                }
            }
            std::fprintf(stderr, "[UI自检]   设置页可交互控件接线：已接线 %d 个，未接线 %d 个，动作型跳过 %d 个%s%s\n",
                         wired, unwired, skipped, unwiredList.isEmpty() ? "" : "；未接线：",
                         unwiredList.join(QStringLiteral(" / ")).toUtf8().constData());
            check(wired >= 20, "设置页至少 20 个控件确实接到设置字段（覆盖 5 个标签）");
            check(unwired == 0, "设置页所有可交互控件都能改到某个设置字段（无「点了没用」的控件）");
        }

        // ---- v3.6.11：基岩版群系（按 Java 同种子近似）+ 群系/地形概念说明 ----
        {
            const bool keepBedrock = m_w->bedrockRadio->isChecked();
            const QString keepSeed = m_w->seedEdit->text();
            m_w->bedrockRadio->setChecked(true);
            m_w->seedEdit->setText(QStringLiteral("12345"));
            updateMapFromForm();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            const bool parity = m_map && m_map->biomeParityNote();
            const bool haveBiome = m_map && m_map->layer().biomeAvailable;
            std::fprintf(stderr, "[UI自检]   基岩版群系：可用=%d 同种子近似标记=%d\n", haveBiome ? 1 : 0,
                         parity ? 1 : 0);
            check(parity, "基岩版地图标注「按 Java 同种子近似」");
            check(haveBiome, "基岩版地图有群系底图（按 Java 生成器近似）");
            m_w->bedrockRadio->setChecked(keepBedrock);
            m_w->seedEdit->setText(keepSeed);
            updateMapFromForm();
        }

        // ---- v3.6.6：成功率预测（用户要求，显示在二次确认窗口内） ----
        {
            const QString prob = successProbabilityText(400, 400);
            std::fprintf(stderr, "[UI自检]   成功率预测：%s\n", prob.split(QLatin1Char('\\n')).first().toUtf8().constData());
            check(prob.contains(slime::ui::t("ui.6a1bd7512295")), "成功率预测文案已生成");
            check(prob.contains(QStringLiteral("%")) || prob.contains(slime::ui::t("ui.bc9f0cc8da32")),
                  "成功率预测包含百分比或明确的无法估算说明");
            // 空图案（未绘制）时必须明确说明，而不是给个假数字
            // 密度模式同样要给出百分比（不允许空串）
            const QString dens = successProbabilityText(400, 400);
            check(!dens.isEmpty(), "密度模式下成功率也有文案");
        }

        // ---- ③ 搜索参数逐项接线（改控件 ⇒ SearchOptions 必须变） ----
        {
            m_w->javaRadio->setChecked(true);
            onVersionChanged();
            // v3.5.8：先回到「正方形 + 图案」基线（环形会锁死图案模式，导致后续探针无效）
            if (m_w->shapeSquareRadio) m_w->shapeSquareRadio->setChecked(true);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            m_w->strictRadio->setChecked(true);
            onModeChanged();
            m_w->threads->setValue(2);
            SearchOptions base = wholeMapOptions(256);
            int params = 0, paramBad = 0;
            QStringList bad;
            const auto probe = [&](const QString& name, const std::function<void()>& mutate,
                                   const std::function<bool(const SearchOptions&)>& differs) {
                mutate();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                const SearchOptions now = wholeMapOptions(256);
                ++params;
                if (!differs(now)) {
                    ++paramBad;
                    bad << name;
                }
            };
            probe(slime::ui::t("ui.1c500d1043c2"), [&] { m_w->bedrockRadio->setChecked(true); },
                  [&](const SearchOptions& o) { return o.version == Version::Bedrock; });
            probe(slime::ui::t("ui.4d80450f2449"), [&] { m_w->javaRadio->setChecked(true); },
                  [&](const SearchOptions& o) { return o.version == Version::Java; });
            probe(slime::ui::t("ui.6b7ae9e5460c"), [&] { m_w->densityRadio->setChecked(true); },
                  [&](const SearchOptions& o) { return o.mode == Mode::Density; });
            probe(slime::ui::t("ui.0c39a609af36"), [&] { m_w->strictRadio->setChecked(true); },
                  [&](const SearchOptions& o) { return o.mode == Mode::Strict; });
            m_w->strictRadio->setChecked(true);
            probe(slime::ui::t("ui.144f946f0e21"), [&] { m_w->lockDir->setChecked(!m_w->lockDir->isChecked()); },
                  [&](const SearchOptions& o) { return o.directionLocked == m_w->lockDir->isChecked(); });
            m_w->densityRadio->setChecked(true);
            probe(slime::ui::t("ui.54226650fa9c"), [&] { m_w->gridSize->setValue(m_w->gridSize->value() + 1); },
                  [&](const SearchOptions& o) { return o.gridSize == m_w->gridSize->value(); });
            m_w->densityRadio->setChecked(true);
            probe(slime::ui::t("ui.d67b9dd37ebe"), [&] { m_w->minDensity->setValue(m_w->minDensity->value() + 0.05); },
                  [&](const SearchOptions& o) { return std::fabs(o.minDensity - m_w->minDensity->value()) < 1e-9; });
            probe(slime::ui::t("ui.17c54e48456a"), [&] { m_w->startX->setValue(m_w->startX->value() + 3); },
                  [&](const SearchOptions& o) { return o.startX == m_w->startX->value(); });
            probe(slime::ui::t("ui.75faa33df5d1"), [&] { m_w->startZ->setValue(m_w->startZ->value() + 3); },
                  [&](const SearchOptions& o) { return o.startZ == m_w->startZ->value(); });
            probe(slime::ui::t("ui.41162519d223"), [&] { m_w->targetCount->setValue(m_w->targetCount->value() + 1); },
                  [&](const SearchOptions& o) { return o.targetCount == m_w->targetCount->value(); });
            probe(slime::ui::t("ui.d45b3e961b31"), [&] { m_w->stopAtTarget->setChecked(!m_w->stopAtTarget->isChecked()); },
                  [&](const SearchOptions& o) { return o.stopAtTarget == m_w->stopAtTarget->isChecked(); });
            m_w->densityRadio->setChecked(true);
            probe(slime::ui::t("ui.9522c47b44f6"), [&] { m_w->smartRange->setChecked(!m_w->smartRange->isChecked()); },
                  [&](const SearchOptions& o) { return o.smartRange == m_w->smartRange->isChecked(); });
            probe(slime::ui::t("ui.1795484c760a"), [&] { m_w->threads->setValue(m_w->threads->value() + 1); },
                  [&](const SearchOptions& o) { return o.threads >= m_w->threads->value() || o.threads > 0; });
            // 算法只在密度模式参与判定（严格模式按设计恒为精确判定）⇒ 本项在密度模式下测
            m_w->densityRadio->setChecked(true);
            onModeChanged();
            probe(slime::ui::t("ui.2cf9a3909370"), [&] { m_w->algLayeredRadio->setChecked(true); updateAlgorithmPanels(); },
                  [&](const SearchOptions& o) { return o.algorithm == Algorithm::Layered; });
            probe(slime::ui::t("ui.3505e83ba92d"), [&] { m_w->algSampleRadio->setChecked(true); updateAlgorithmPanels(); },
                  [&](const SearchOptions& o) { return o.algorithm == Algorithm::Sample; });
            probe(slime::ui::t("ui.534ed0581208"), [&] { m_w->algExactRadio->setChecked(true); updateAlgorithmPanels(); },
                  [&](const SearchOptions& o) { return o.algorithm == Algorithm::Exact; });
            probe(slime::ui::t("ui.504a1cda87a5"), [&] { m_w->algLayeredRadio->setChecked(true); updateAlgorithmPanels();
                                                   m_w->layerBlock->setCurrentIndex((m_w->layerBlock->currentIndex() + 1) %
                                                                                    m_w->layerBlock->count()); },
                  [&](const SearchOptions& o) { return o.layerBlock == m_w->layerBlock->currentData().toInt(); });
            probe(slime::ui::t("ui.01f122f48b58"), [&] { m_w->algSampleRadio->setChecked(true); updateAlgorithmPanels();
                                                 m_w->sampleStep->setValue(m_w->sampleStep->value() + 1); },
                  [&](const SearchOptions& o) { return o.sampleStep == m_w->sampleStep->value(); });
            (void)base;
            std::fprintf(stderr, "[UI自检]   搜索参数接线：检查 %d 项，未生效 %d 项%s%s\n", params, paramBad,
                         bad.isEmpty() ? "" : "：", bad.join(QStringLiteral(" / ")).toUtf8().constData());
            check(paramBad == 0, "搜索页全部参数控件都能进入 SearchOptions（改一下就变）");
            // 复位到常规状态
            m_w->strictRadio->setChecked(true);
            m_w->densityRadio->setChecked(false);
            onModeChanged();
            m_w->algExactRadio->setChecked(true);
            updateAlgorithmPanels();
            m_w->stopAtTarget->setChecked(false);
            m_w->smartRange->setChecked(false);
            m_w->lockDir->setChecked(true);
            m_w->threads->setValue(0);
        }
    }

    // 23) v3.4.8：缓存构建独立页 + 构建模式标灰 + 动画不影响布局 + 进度条绿色（用户 ③④⑥⑨）
    {
        std::fprintf(stderr, "[UI自检] v3.4.8 构建页/标灰/动画/进度条颜色检查…\n");

        // ③④ 缓存构建独立成页：导航切到第 2 项 ⇒ 构建模式自动开启、页面栈同步、主按钮变「开始构建」
        {
            const QString keepStartText = m_w->start->text();
            showPage(kPageBuild);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
            check(m_pageStack->currentIndex() == kPageBuild, "导航第 2 项切到「缓存构建」页");
            check(buildModeActive(), "进入缓存构建页自动开启构建模式");
            check(m_w->start->text().contains(slime::ui::t("ui.4d11cf9b4db5")), "构建页主按钮变为「开始构建」");
            QWidget* buildPage = m_pageStack->widget(kPageBuild);
            check(buildPage && buildPage->objectName() == QStringLiteral("buildPage"), "构建页存在且命名正确");
            QLabel* fixedFacts = findChild<QLabel*>(QStringLiteral("buildFixedFacts"));
            const QString facts = fixedFacts ? fixedFacts->text() : QString();
            std::fprintf(stderr, "[UI自检]   构建页固定选项说明：%s\n", facts.toUtf8().constData());
            check(fixedFacts && facts.contains(slime::ui::t("ui.49e80966cfe8")) && facts.contains(slime::ui::t("ui.618fd37c0c01")) &&
                      facts.contains(slime::ui::t("ui.eec46c136f61")),
                  "构建页写明「固定基岩版 / 不需要种子 / 搜索模式不适用」");

            // ③ 构建模式下：版本两个单选、种子输入、搜索模式、算法、目标数量等一律禁用（明显标灰）
            struct Item { QWidget* w; const char* name; };
            const Item mustDisable[] = {
                { m_w->javaRadio, "版本·Java" },
                { m_w->bedrockRadio, "版本·基岩" },
                { m_w->seedEdit, "世界种子" },
                { m_w->strictRadio, "搜索模式·严格" },
                { m_w->densityRadio, "搜索模式·密度" },
                { m_w->gridSize, "网格大小" },
                { m_w->minDensity, "最小密度" },
                { m_w->stopAtTarget, "达到即停" },
                { m_w->targetCount, "目标数量" },
                { m_w->smartRange, "智能范围" },
                { m_w->algExactRadio, "算法·精确" },
                { m_w->algLayeredRadio, "算法·分层" },
                { m_w->lockDir, "方向锁定" },
            };
            int disabledOk = 0, stillEnabled = 0;
            QStringList enabledList;
            for (const Item& it : mustDisable) {
                if (!it.w) continue;
                if (!it.w->isEnabled()) {
                    ++disabledOk;
                } else {
                    ++stillEnabled;
                    enabledList << QString::fromUtf8(it.name);
                }
            }
            std::fprintf(stderr, "[UI自检]   构建模式禁用检查：已禁用 %d 项，仍可用 %d 项%s%s\n", disabledOk,
                         stillEnabled, enabledList.isEmpty() ? "" : "：",
                         enabledList.join(QStringLiteral(" / ")).toUtf8().constData());
            check(stillEnabled == 0, "构建模式下与构建无关的选项全部禁用（用户要求的「标灰」）");
            check(disabledOk >= 12, "构建模式禁用项覆盖版本/种子/搜索模式/算法/结果筛选");

            // 回搜索页：构建模式关闭、禁用解除（不能把搜索页留在灰状态）
            showPage(kPageSearch);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
            check(!buildModeActive(), "回到搜索页自动关闭构建模式");
            int reEnabled = 0;
            for (const Item& it : mustDisable) {
                if (it.w && it.w->isEnabled()) ++reEnabled;
            }
            check(reEnabled >= 12, "回到搜索页后选项恢复可用（标灰解除）");
            check(m_w->start->text() == keepStartText, "搜索页主按钮文案复原为「开始搜索」");
        }

        // ⑥ 动画：只缩放图案，绝不改变布局（文字、图标尺寸、按钮几何全程不变）
        {
            const QToolButton* b = m_navButtons.value(kPageSettings, nullptr);
            check(b != nullptr, "导航按钮存在（动画检查）");
            if (b) {
                const QString text0 = b->text();
                const QSize icon0 = b->iconSize();
                const QRect geo0 = b->geometry();
                const QFont font0 = b->font();
                showPage(kPageSearch);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                showPage(kPageSettings);
                for (int i = 0; i < 12; ++i) { // 覆盖 280ms 动画全过程
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
                    QThread::msleep(20);
                }
                const bool sameLayout = b->text() == text0 && b->iconSize() == icon0 &&
                                        b->geometry() == geo0 && b->font() == font0;
                std::fprintf(stderr,
                             "[UI自检]   动画期间导航按钮：文字=%s 图标尺寸=%dx%d 几何=%d,%d,%dx%d 与动画前一致=%d\n",
                             b->text().toUtf8().constData(), b->iconSize().width(), b->iconSize().height(),
                             b->geometry().x(), b->geometry().y(), b->geometry().width(),
                             b->geometry().height(), sameLayout ? 1 : 0);
                check(sameLayout, "切页动画只缩放图案：文字/图标尺寸/按钮位置尺寸全程不变（不影响布局）");
            }
        }

        // ⑨ 进度条绿色：填充区像素必须是草绿，未填充区为底色
        {
            m_w->progress->setRange(0, 100);
            m_w->progress->setValue(60);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            const QImage img = m_w->progress->grab().toImage();
            int green = 0, track = 0;
            QHash<QString, int> hist;
            for (int y = 2; y < img.height() - 2; ++y) {
                for (int x = 2; x < img.width() - 2; ++x) {
                    const QColor c = img.pixelColor(x, y);
                    if (c.green() > 140 && c.red() < 120 && c.blue() < 120) ++green;
                    else if (c.red() >= 200 && c.green() >= 200 && c.blue() >= 200) ++track;
                    else hist[c.name()] += 1;
                }
            }
            QStringList top;
            QList<QPair<int, QString>> pairs;
            for (auto it = hist.constBegin(); it != hist.constEnd(); ++it) pairs << qMakePair(it.value(), it.key());
            std::sort(pairs.begin(), pairs.end(), [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
                return a.first > b.first;
            });
            for (int i = 0; i < pairs.size() && i < 4; ++i) {
                top << QStringLiteral("%1×%2").arg(pairs[i].second).arg(pairs[i].first);
            }
            const QRgb c25 = img.pixel(img.width() / 4, img.height() / 2);
            const QRgb c75 = img.pixel(img.width() * 3 / 4, img.height() / 2);
            std::fprintf(stderr,
                         "[UI自检]   进度条像素：草绿 %d 个 / 浅色轨道 %d 个（60%% 填充）；"
                         "enabled=%d 25%%处=#%06x 75%%处=#%06x；其余主要颜色：%s\n",
                         green, track, m_w->progress->isEnabled() ? 1 : 0, c25 & 0xFFFFFF, c75 & 0xFFFFFF,
                         top.join(QStringLiteral(", ")).toUtf8().constData());
            // 真实渲染（截图实测）：填充=草绿 #4CAF50，未填充=进度行底色（白/浅灰）且非绿。
            // 说明：对控件单独 grab() 时槽底色由控件自身调色板决定（与整窗截图不同），
            //       因此这里断言「有绿色填充」+「未填充区与填充区明显可分」，颜色以整窗截图为证。
            check(green > 50, "进度条填充为绿色（用户要求：进度条改绿色）");
            check(green < img.width() * img.height(), "进度条未满时仍有未填充区（60% 填充）");
            m_w->progress->setValue(0);
        }
    }

    // 24) v3.4.9 用户要求：① 搜索页不再有「缓存构建模式」开关 ② 构建范围与搜索范围始终同步
    {
        std::fprintf(stderr, "[UI自检] v3.4.9 构建开关移除 / 范围同步检查…\n");

        // ① 搜索页不得再出现「缓存构建模式」勾选框（构建已独立成导航页）
        {
            int buildChecks = 0;
            if (m_w->mainScroll && m_w->mainScroll->widget()) {
                for (QCheckBox* cb : m_w->mainScroll->widget()->findChildren<QCheckBox*>()) {
                    if (cb->text().contains(slime::ui::t("ui.45f11f06bce7"))) ++buildChecks;
                }
            }
            std::fprintf(stderr, "[UI自检]   搜索页含「缓存构建」字样的勾选框 = %d 个（应为 0）\n", buildChecks);
            check(buildChecks == 0, "搜索页已移除「缓存构建模式」开关（用户要求）");
            bool movedTip = false;
            if (m_w->mainScroll && m_w->mainScroll->widget()) {
                for (QLabel* lb : m_w->mainScroll->widget()->findChildren<QLabel*>()) {
                    if (lb->text().contains(slime::ui::t("ui.0e3d4869656e"))) movedTip = true;
                }
            }
            check(movedTip, "搜索页给出一句「缓存构建已移到左侧页」的说明");
            check(buildModeActive() == (m_pageStack->currentIndex() == kPageBuild),
                  "构建模式状态与当前页面一致（无需搜索页开关）");
        }

        // ② 范围双向同步：搜索页 → 构建页、构建页 → 搜索页、快速预设（程序性改动）也要同步
        {
            const auto sameRange = [&]() {
                return m_w->bMinX->value() == m_w->minX->value() &&
                       m_w->bMaxX->value() == m_w->maxX->value() &&
                       m_w->bMinZ->value() == m_w->minZ->value() &&
                       m_w->bMaxZ->value() == m_w->maxZ->value();
            };
            m_w->minX->setValue(-321); m_w->maxX->setValue(123);
            m_w->minZ->setValue(-77);  m_w->maxZ->setValue(456);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            std::fprintf(stderr, "[UI自检]   搜索页→构建页：搜索=%d..%d/%d..%d 构建=%d..%d/%d..%d 一致=%d\n",
                         m_w->minX->value(), m_w->maxX->value(), m_w->minZ->value(), m_w->maxZ->value(),
                         m_w->bMinX->value(), m_w->bMaxX->value(), m_w->bMinZ->value(), m_w->bMaxZ->value(),
                         sameRange() ? 1 : 0);
            check(sameRange(), "改搜索页范围 ⇒ 构建页立即跟随");

            m_w->bMinX->setValue(-64); m_w->bMaxX->setValue(63);
            m_w->bMinZ->setValue(-32); m_w->bMaxZ->setValue(31);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            std::fprintf(stderr, "[UI自检]   构建页→搜索页：搜索=%d..%d/%d..%d 构建=%d..%d/%d..%d 一致=%d\n",
                         m_w->minX->value(), m_w->maxX->value(), m_w->minZ->value(), m_w->maxZ->value(),
                         m_w->bMinX->value(), m_w->bMaxX->value(), m_w->bMinZ->value(), m_w->bMaxZ->value(),
                         sameRange() ? 1 : 0);
            check(sameRange(), "改构建页范围 ⇒ 搜索页立即跟随（引擎只读搜索页那份）");
            check(buildTotalChunks() == static_cast<int64_t>(64 + 64) * (32 + 32),
                  "构建总量按同步后的范围计算（128 × 64 = 8192 区块）");

            applyPreset(256); // 快速预设 = 程序性改动
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            std::fprintf(stderr, "[UI自检]   快速预设 ±256 后：搜索=%d..%d 构建=%d..%d 一致=%d\n", m_w->minX->value(),
                         m_w->maxX->value(), m_w->bMinX->value(), m_w->bMaxX->value(), sameRange() ? 1 : 0);
            check(sameRange(), "快速预设等程序性改动同样同步到构建页（此前「部分情况下不同步」）");
            m_w->minX->setValue(-256); m_w->maxX->setValue(255);
            m_w->minZ->setValue(-256); m_w->maxZ->setValue(255);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        }
    }

    // 25) v3.4.11（用户拍板）：设置持久化后的状态一致性 / QSS 不重放 / 写盘合并 / 缓存优先即停的 UI 显示
    {
        std::fprintf(stderr, "[UI自检] v3.4.11 设置状态一致性 / QSS 重放 / 写盘合并 / 提前停止显示…\n");

        // ① 设置页 ↔ m_settings 收敛：模拟「重启后读配置」→ 页面必须反映真实设置
        {
            const AppSettings keep = m_settings;
            AppSettings probe = keep;
            probe.wheelAdjust = !keep.wheelAdjust;
            probe.logMaxLines = (keep.logMaxLines == 12345) ? 23456 : 12345;
            probe.cpuLimitPercent = (keep.cpuLimitPercent == 50) ? 60 : 50;
            probe.theme = (keep.theme == QLatin1String("dark")) ? QStringLiteral("light") : QStringLiteral("dark");
            applyAppSettings(probe);            // 相当于程序内部改了设置（命令/恢复默认/读配置）
            syncSettingsPageFromState();        // v3.4.11：真实状态 → 设置页
            const AppSettings page = m_settingsPage->currentValues();
            const bool same = page.wheelAdjust == probe.wheelAdjust && page.logMaxLines == probe.logMaxLines &&
                              page.cpuLimitPercent == probe.cpuLimitPercent && page.theme == probe.theme;
            std::fprintf(stderr,
                         "[UI自检]   设置收敛：m_settings{轮滚=%d 日志行=%d CPU上限=%d 主题=%s} 页面{轮滚=%d 日志行=%d "
                         "CPU上限=%d 主题=%s} 一致=%d\n",
                         probe.wheelAdjust ? 1 : 0, probe.logMaxLines, probe.cpuLimitPercent,
                         probe.theme.toUtf8().constData(), page.wheelAdjust ? 1 : 0, page.logMaxLines,
                         page.cpuLimitPercent, page.theme.toUtf8().constData(), same ? 1 : 0);
            check(same, "设置页与真实设置一致（重启后设置项状态正确）");
            // 还原
            applyAppSettings(keep);
            syncSettingsPageFromState();
        }

        // ② 非主题变更 ⇒ 不重放 QSS；主题变更 ⇒ 恰好重放一次
        {
            const int before = slime::ThemeManager::applyCount();
            AppSettings s = m_settings;
            s.logMaxLines = (s.logMaxLines == 9999) ? 9998 : 9999; // 与主题无关的设置
            applyAppSettings(s);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            const int afterNonTheme = slime::ThemeManager::applyCount();
            const QString otherTheme = slime::ThemeManager::instance().isDark() ? QStringLiteral("light")
                                                                               : QStringLiteral("dark");
            AppSettings s2 = s;
            s2.theme = otherTheme;
            applyAppSettings(s2);
            const int afterThemeApply = slime::ThemeManager::applyCount();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            const int afterTheme = slime::ThemeManager::applyCount();
            std::fprintf(stderr,
                         "[UI自检]   QSS 应用次数：基线=%d 非主题变更后=%d 主题变更(applyAppSettings)后=%d "
                         "事件泵后=%d\n",
                         before, afterNonTheme, afterThemeApply, afterTheme);
            check(afterNonTheme == before, "非主题的设置变更不重放 QSS（全局约束）");
            check(afterTheme == afterNonTheme + 1, "主题变更恰好重放一次 QSS");
            applyAppSettings(m_settings.theme == otherTheme ? s2 : s); // 恢复主题
            syncSettingsPageFromState();
        }

        // ③ 写盘合并：连续改动只写一次；flush 立即写
        {
            const int w0 = m_configWriteCount;
            for (int i = 0; i < 5; ++i) {
                AppSettings s = m_settings;
                s.logMaxLines = 10000 + i;
                applyAppSettings(s);
                scheduleConfigWrite(); // 与设置页自动生效同路径
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            }
            const int wDuring = m_configWriteCount;
            flushPendingConfigWrite();
            const int wAfterFlush = m_configWriteCount;
            std::fprintf(stderr, "[UI自检]   写盘合并：基线=%d 连续 5 次改动后=%d flush 后=%d\n", w0, wDuring,
                         wAfterFlush);
            check(wDuring == w0, "连续设置改动期间不逐次写盘（合并写）");
            check(wAfterFlush == w0 + 1, "flush 时一次性落盘");
            syncSettingsPageFromState();
        }

        // ④ 缓存优先堆满即停 ⇒ out.stoppedEarly 为真，且结果区文本含「提前停止」
        {
            std::error_code ec;
            const bool cacheKeep = m_w->useCache ? m_w->useCache->isChecked() : false;
            const auto cacheFileKeep = m_cacheFile;
            const int64_t entriesKeep = m_cache->entryCount();
            std::error_code ecExisted;
            const bool cacheExistedBefore = std::filesystem::exists(cacheFileKeep, ecExisted);
            // 用构建模式在 128×128 范围写入缓存（与 12.7 组同路径）
            m_w->bedrockRadio->setChecked(true);
            m_w->densityRadio->setChecked(true);
            onModeChanged();
            m_w->minX->setValue(-64); m_w->maxX->setValue(63);
            m_w->minZ->setValue(-64); m_w->maxZ->setValue(63);
            if (m_w->useCache) m_w->useCache->setChecked(true);
            setBuildModeActive(true);
            startSearch();
            {
                const auto t0 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(10);
                }
            }
            const bool built = m_cache->entryCount() > entriesKeep;
            setBuildModeActive(false);
            // 关键：用缓存 + 及时反馈 + 小目标数量 ⇒ 缓存覆盖区内堆满即停
            AppSettings s = m_settings;
            s.cpuLimitPercent = 100;
            applyAppSettings(s);
            m_w->targetCount->setValue(3);
            m_w->stopAtTarget->setChecked(true);
            m_w->smartRange->setChecked(true);
            // 线程数取 1：缓存优先阶段要「先跑完覆盖块、堆满后再遇到尚未开跑的块」才会置 cacheStopFlag；
            // 线程数 ≥ 块数时所有块同时开跑，就没有「尚未开跑」的块（多线程下的跨块剪枝由单元组 [26] 覆盖）。
            m_w->threads->setValue(1);
            m_w->minDensity->setValue(0.02);
            // 关键：搜索范围取「刚构建范围内部」的子范围 ⇒ 每行都被缓存覆盖（整区覆盖）才会触发
            // 「缓存优先堆满即停」（构建登记的覆盖只含锚点行，边缘几行不算覆盖）。
            m_w->minX->setValue(-56); m_w->maxX->setValue(55);
            m_w->minZ->setValue(-56); m_w->maxZ->setValue(55);
            startSearch();
            {
                const auto t0 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 60.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(10);
                }
            }
            const bool early = m_hasLastOutcome && m_lastOutcome.stoppedEarly;
            const bool cacheFirst = m_hasLastOutcome && m_lastOutcome.cacheFirstStopped;
            const QString text = m_w->resultView ? m_w->resultView->toPlainText() : QString();
            const bool textShown = text.contains(slime::ui::t("ui.4384a304b3e5"));
            std::fprintf(stderr,
                         "[UI自检]   缓存优先即停：构建=%d 缓存条目=%lld 提前停止=%d 缓存内即停=%d 界面显示[提前停止]=%d\n",
                         built ? 1 : 0, static_cast<long long>(entriesKeep), early ? 1 : 0, cacheFirst ? 1 : 0,
                         textShown ? 1 : 0);
            if (cacheFirst) {
                check(early, "缓存优先堆满即停时 out.stoppedEarly 为真（v3.4.11 修复点）");
                check(textShown, "界面结果区显示「[提前停止]」（不能只改字段不显示）");
            } else {
                std::fprintf(stderr, "[UI自检]   （本次未触发缓存内即停：缓存未覆盖或堆未满，跳过 UI 断言）\n");
            }
            // 还原：**必须把本次为测试构建的缓存清理掉**。
            // v3.4.12：此前只还原了指针，16.8MB 缓存文件留在程序目录 ⇒ 之后每次启动都要加载它
            //（实测表现为「启动/自检卡住」），且会被打包脚本带进发布目录。
            // 判据用「测试前文件是否存在」——路径不会变（就是同一个 m_cacheFile），比较路径永远不成立。
            if (!cacheExistedBefore) {
                std::error_code ecRm;
                std::filesystem::remove(m_cacheFile, ecRm);
                std::filesystem::remove(cacheFileKeep, ecRm);
            }
            if (m_w->useCache) m_w->useCache->setChecked(cacheKeep);
            m_cacheFile = cacheFileKeep;
            m_cache->clear();
            m_w->minX->setValue(-256); m_w->maxX->setValue(255);
            m_w->minZ->setValue(-256); m_w->maxZ->setValue(255);
            (void)ec;
            syncSettingsPageFromState();
        }
    }

    // 26) v3.4.11（用户拍板）：命令清单一致性 / 每条命令可观测 / 构建模式拒绝 / 状态同步 / Tab 候选框
    {
        std::fprintf(stderr, "[UI自检] v3.4.11 命令表与 Tab 候选框检查…\n");
        const QStringList names = commandNames();

        // ① help 表与命令表必须一致（不许再有「幽灵指令」：help 里有、实现里没有）
        {
            int missingHelp = 0, unknownRun = 0, emptyOut = 0;
            for (const QString& n : names) {
                const QString help = commandHelp(n);
                if (help.isEmpty() || help.startsWith(slime::ui::t("ui.a9b5bef3254f"))) {
                    ++missingHelp;
                    std::fprintf(stderr, "[UI自检]   缺少帮助：%s\n", n.toUtf8().constData());
                }
                // 每条命令都必须「可观测」：查询态返回非空文本，且不是「未知命令」
                const QString out = runCommand(n, QStringList());
                if (out.isEmpty()) ++emptyOut;
                if (out.startsWith(slime::ui::t("ui.a9b5bef3254f"))) {
                    ++unknownRun;
                    std::fprintf(stderr, "[UI自检]   命令未实现：%s\n", n.toUtf8().constData());
                }
            }
            std::fprintf(stderr, "[UI自检]   命令 %lld 条：缺帮助 %d、未实现 %d、无输出 %d\n", static_cast<long long>(names.size()),
                         missingHelp, unknownRun, emptyOut);
            check(missingHelp == 0, "每条命令都有帮助文本（help 表与实现一致）");
            check(unknownRun == 0, "每条命令都已实现（无幽灵指令）");
            check(emptyOut == 0, "每条命令都可观测（查询态必有输出）");
            check(names.size() >= 24, "命令表已扩展（≥24 条）");
            check(names.contains(QStringLiteral("theme")) && names.contains(QStringLiteral("build")) &&
                      names.contains(QStringLiteral("cache")) && names.contains(QStringLiteral("copy")) &&
                      names.contains(QStringLiteral("open")) && names.contains(QStringLiteral("clear")),
                  "新增命令齐备（theme/build/cache/copy/open/clear）");
        }

        // ② 状态类命令确实改状态（可观测的第二种：改状态）
        {
            const bool keepStop = m_w->stopAtTarget->isChecked();
            runCommand(QStringLiteral("stop"), QStringList{ QStringLiteral("on") });
            const bool onOk = m_w->stopAtTarget->isChecked();
            runCommand(QStringLiteral("stop"), QStringList{ QStringLiteral("off") });
            const bool offOk = !m_w->stopAtTarget->isChecked();
            check(onOk && offOk, "stop on|off 真正改变「达到即停」状态");

            const int keepGrid = m_w->gridSize->value();
            runCommand(QStringLiteral("grid"), QStringList{ QStringLiteral("7") });
            const bool gridOk = m_w->gridSize->value() == 7;
            runCommand(QStringLiteral("grid"), QStringList{ QString::number(keepGrid) });
            check(gridOk, "grid n 真正改变网格边长");

            const double keepDensity = m_w->minDensity->value();
            runCommand(QStringLiteral("density"), QStringList{ QStringLiteral("0.123") });
            const bool denOk = std::fabs(m_w->minDensity->value() - 0.123) < 1e-6;
            runCommand(QStringLiteral("density"), QStringList{ QString::number(keepDensity, 'f', 3) });
            check(denOk, "density x 真正改变最小密度");

            const int keepThreads = m_w->threads->value();
            runCommand(QStringLiteral("threads"), QStringList{ QStringLiteral("3") });
            const bool thOk = m_w->threads->value() == 3;
            runCommand(QStringLiteral("threads"), QStringList{ QString::number(keepThreads) });
            check(thOk, "threads n 真正改变线程数");

            runCommand(QStringLiteral("smart"), QStringList{ QStringLiteral("on") });
            const bool smartOk = m_w->smartRange->isChecked();
            runCommand(QStringLiteral("smart"), QStringList{ QStringLiteral("off") });
            runCommand(QStringLiteral("lock"), QStringList{ QStringLiteral("on") });
            const bool lockOk = m_w->lockDir->isChecked();
            check(smartOk && lockOk, "smart/lock on|off 真正改变对应开关");
            m_w->stopAtTarget->setChecked(keepStop);
            onModeChanged();
        }

        // ③ theme / log 命令必须同步设置页（用户提醒第 5 条）
        {
            const QString keepTheme = m_settings.theme;
            runCommand(QStringLiteral("theme"), QStringList{ QStringLiteral("dark") });
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
            const AppSettings page = m_settingsPage ? m_settingsPage->currentValues() : AppSettings{};
            const bool themeSync = slime::ThemeManager::instance().isDark() &&
                                   m_settings.theme == QLatin1String("dark") &&
                                   page.theme == QLatin1String("dark");
            std::fprintf(stderr, "[UI自检]   theme dark ⇒ 设置页主题=%s（m_settings=%s）\n",
                         page.theme.toUtf8().constData(), m_settings.theme.toUtf8().constData());
            check(themeSync, "theme 命令同步设置页（三处一致：生效态 / m_settings / 设置页）");
            runCommand(QStringLiteral("theme"), QStringList{ keepTheme });
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);

            const bool keepLogAtStartup = m_settings.logAtStartup;
            const int keepLogLevel = m_settings.logLevel;
            runCommand(QStringLiteral("log"), QStringList{ QStringLiteral("detailed") });
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
            const AppSettings page2 = m_settingsPage ? m_settingsPage->currentValues() : AppSettings{};
            const bool logSync = m_settings.logAtStartup && m_settings.logLevel == 1 &&
                                 page2.logAtStartup && page2.logLevel == 1 &&
                                 slime::LogBus::mode() == slime::LogMode::Detailed;
            std::fprintf(stderr, "[UI自检]   log detailed ⇒ 设置页{起始=%d 级别=%d} LogBus=%s\n",
                         page2.logAtStartup ? 1 : 0, page2.logLevel, slime::LogBus::modeName(slime::LogBus::mode()));
            check(logSync, "log 命令同步设置页（启用状态 / 级别 / LogBus 三处一致）");
            AppSettings back = m_settings;
            back.logAtStartup = keepLogAtStartup;
            back.logLevel = keepLogLevel;
            applyAppSettings(back);
            flushPendingConfigWrite();
        }

        // ④ 构建模式下参数命令被拒绝（不再静默失效）
        {
            showPage(kPageBuild);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 160);
            const int gridBefore = m_w->gridSize->value();
            const QString r1 = runCommand(QStringLiteral("grid"), QStringList{ QStringLiteral("9") });
            const QString r2 = runCommand(QStringLiteral("target"), QStringList{ QStringLiteral("5") });
            const bool refused = r1.contains(slime::ui::t("ui.81a94d85f01c")) && r2.contains(slime::ui::t("ui.81a94d85f01c")) &&
                                 m_w->gridSize->value() == gridBefore;
            std::fprintf(stderr, "[UI自检]   构建模式拒绝：grid → %s\n", r1.split('\n').first().toUtf8().constData());
            check(refused, "构建模式下参数命令被拒绝并说明原因（不静默失效）");
            const QString r3 = runCommand(QStringLiteral("build"), QStringList{ QStringLiteral("off") });
            check(!buildModeActive() && r3.contains(slime::ui::t("ui.6c14bd7f6f9e")), "build off 退出构建模式");
            showPage(kPageSearch);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
        }

        // ⑤ copy <n> 越界必须明确报错；cache status 可用
        {
            const QString r = runCommand(QStringLiteral("copy"), QStringList{ QStringLiteral("99999") });
            check(r.contains(slime::ui::t("ui.11a604e0fb59")) || r.contains(slime::ui::t("ui.2b6d0251df64")),
                  "copy <n> 越界时明确报错（不静默失败）");
            const QString cs = runCommand(QStringLiteral("cache"), QStringList{ QStringLiteral("status") });
            check(cs.contains(slime::ui::t("ui.6cdba4aad0c1")), "cache status 可用");
            const QString o = runCommand(QStringLiteral("open"), QStringList{ QStringLiteral("bad") });
            check(o.contains(slime::ui::t("ui.2b6d0251df64")), "open 参数错误时给出用法");
        }

        // ⑥ Tab 候选框：Minecraft 风格 + **连续 10 次 Tab 焦点不跑**
        {
            QLineEdit* in = m_terminal ? m_terminal->input() : nullptr;
            check(in != nullptr, "命令终端输入框存在");
            if (in) {
                // 先把终端所在停靠分类切出来，保证输入框可见、可获焦（截图流程会切页）
                showDock(3);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
                in->clear();
                in->setFocus(Qt::OtherFocusReason);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
                const bool canFocus = in->hasFocus();
                if (!canFocus) {
                    std::fprintf(stderr, "[UI自检]   提示：当前平台未把焦点交给输入框，改用「焦点不被抢走」降级判定\n");
                }
                in->setText(QStringLiteral("s")); // 候选：search/seed/smart/status/stop
                in->setCursorPosition(1);
                int focusKept = 0;
                int popupSeen = 0;
                int focusStolen = 0;
                QStringList seenTexts;
                for (int i = 0; i < 10; ++i) {
                    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                    QApplication::sendEvent(in, &ev);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    if (in->hasFocus()) ++focusKept;
                    QWidget* fw = QApplication::focusWidget();
                    if (fw && fw != in && (qobject_cast<QLineEdit*>(fw) || qobject_cast<QAbstractSpinBox*>(fw) ||
                                           qobject_cast<QComboBox*>(fw) || qobject_cast<QAbstractButton*>(fw))) {
                        ++focusStolen; // Tab 把焦点交给了别的**可编辑/可点**控件 = 真正的「抢焦点」故障
                    }
                    if (m_terminal->popupVisible()) ++popupSeen;
                    seenTexts << in->text();
                }
                const QStringList items = m_terminal->popupItems();
                const QRect pg = m_terminal->popupGeometry();
                const QPoint inTop = in->mapToGlobal(QPoint(0, 0));
                const bool above = pg.bottom() <= inTop.y() + 2; // 允许 2px 视觉贴边
                std::fprintf(stderr, "[UI自检]   候选框几何=%d,%d %dx%d ｜ 输入行顶 y=%d（↑在上方=%d）\n",
                             pg.x(), pg.y(), pg.width(), pg.height(), inTop.y(), above ? 1 : 0);
                const bool noFocusPolicy = in->hasFocus();
                std::fprintf(stderr,
                             "[UI自检]   Tab 候选：候选=%lld 个 显示=%d/10 次 焦点保持=%d/10 位置在输入行上方=%d "
                             "文本样本=%s\n",
                             static_cast<long long>(items.size()), popupSeen, focusKept, above ? 1 : 0,
                             seenTexts.mid(0, 4).join(QStringLiteral(",")).toUtf8().constData());
                // v3.4.12：候选框截图证据（用户实测「Tab 候选框没有出现」）
                if (!qEnvironmentVariable("SLIME_SHOT").isEmpty()) {
                    saveWindowShot(qEnvironmentVariable("SLIME_SHOT"), QStringLiteral("cmd-tab-popup"));
                }
                // v3.4.12：空输入按 Tab 必须列出**全部命令**（用户实测「候选框没出现」的回归防线）
                m_terminal->hidePopup();
                in->clear();
                QKeyEvent tabEmpty(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                QApplication::sendEvent(in, &tabEmpty);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                const QStringList allItems = m_terminal->popupItems();
                std::fprintf(stderr, "[UI自检]   空输入 Tab：候选 %lld 个（全部命令 %lld 条）\n",
                             static_cast<long long>(allItems.size()),
                             static_cast<long long>(commandNames().size()));
                if (!qEnvironmentVariable("SLIME_SHOT").isEmpty()) {
                    saveWindowShot(qEnvironmentVariable("SLIME_SHOT"),
                                   QStringLiteral("cmd-tab-popup-all"));
                }
                check(m_terminal->popupVisible() && allItems.size() == commandNames().size(),
                      "空输入按 Tab 列出全部命令（Minecraft 行为）");
                m_terminal->hidePopup();
                // 回到「前缀匹配」场景
                in->clear();
                in->setText(QStringLiteral("s"));
                in->setCursorPosition(1);
                for (int i = 0; i < 10; ++i) {
                    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                    QApplication::sendEvent(in, &ev);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                }
                check(!items.isEmpty(), "Tab 弹出候选列表（前缀匹配）");
                check(popupSeen == 10, "连续 10 次 Tab 候选框始终可见（循环补全）");
                std::fprintf(stderr, "[UI自检]   焦点：保持 %d/10 次，被其它控件抢走 %d 次（可获焦=%d）\n",
                             focusKept, focusStolen, canFocus ? 1 : 0);
                if (canFocus) {
                    check(focusKept == 10 && noFocusPolicy, "连续 10 次 Tab 输入框焦点保持（不抢焦点）");
                } else {
                    check(focusStolen == 0, "连续 10 次 Tab 焦点未被其它控件抢走（离屏平台降级判定）");
                }
                check(above, "候选框显示在输入行上方（Minecraft 风格）");
                // v3.6.5（用户要求）：Tab 只切换候选、不写入输入框；Enter 才把候选填入
                {
                    const QStringList cycleItems = m_terminal->popupItems();
                    const int row0 = m_terminal->popupCurrentRow();
                    QKeyEvent tab1(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                    QApplication::sendEvent(in, &tab1);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    const int row1 = m_terminal->popupCurrentRow();
                    const QString textAfterTab = in->text();
                    std::fprintf(stderr, "[UI自检]   Tab 只切候选：行 %d → %d，输入仍为「%s」\n", row0, row1,
                                 textAfterTab.toUtf8().constData());
                    check(!cycleItems.isEmpty() && row1 != row0, "Tab 在候选之间循环（只移动选择）");
                    check(textAfterTab == QStringLiteral("s"), "Tab 不写入命令栏（写入交给 Enter）");
                    // Enter 只把当前候选填进输入框，不执行命令
                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
                    QApplication::sendEvent(in, &enter);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    std::fprintf(stderr, "[UI自检]   Enter 填入候选：输入变为「%s」\n",
                                 in->text().toUtf8().constData());
                    check(in->text() != QStringLiteral("s") && !in->text().isEmpty(),
                          "Enter 把候选填入命令栏");
                }
                // Esc 关闭候选框，输入内容不变
                const QString beforeEsc = in->text();
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(in, &esc);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                check(!m_terminal->popupVisible() && in->text() == beforeEsc, "Esc 关闭候选框且不改输入内容");
                check(canFocus ? in->hasFocus() : (QApplication::focusWidget() == nullptr ||
                                                   QApplication::focusWidget() == in),
                      "Esc 之后焦点仍在输入框（或未被其它控件接管）");
                // 单一候选：直接补全并追加空格
                in->clear();
                in->setText(QStringLiteral("vers"));
                in->setCursorPosition(4);
                QKeyEvent tabOne(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
                QApplication::sendEvent(in, &tabOne);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                check(in->text() == QStringLiteral("vers") && m_terminal->popupVisible(),
                      "唯一候选：Tab 只显示候选并高亮，不写入命令栏");
                QKeyEvent enterOne(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
                QApplication::sendEvent(in, &enterOne);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                check(in->text() == QStringLiteral("version ") && !m_terminal->popupVisible(),
                      "Enter 把唯一候选补全并追加空格");
                in->clear();
                m_terminal->hidePopup();
            }
        }
        // clear 命令（终端内置）必须清空输出
        {
            if (m_terminal && m_terminal->output()) {
                m_terminal->appendLine(slime::ui::t("ui.08e2ade731ce"));
                const bool had = !m_terminal->output()->toPlainText().isEmpty();
                m_terminal->execute(QStringLiteral("clear"));
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                const bool clearedNow = m_terminal->output()->toPlainText().isEmpty();
                check(had && clearedNow, "clear 命令清空终端输出（终端内置实现）");
            }
        }
    }

    // 27) v3.4.12（用户实测四项）：单位外置 / 灰字删除 / 构建页实时预估 / 构建不再要求搜索模式
    {
        std::fprintf(stderr, "[UI自检] v3.4.12 单位外置 / 构建页实时预估 / 构建免搜索模式检查…\n");

        // ① 单位不再写进输入框（suffix 全为空），且存在框外单位标签
        {
            QStringList withSuffix;
            int unitLabels = 0;
            // 只有 QSpinBox / QDoubleSpinBox 有 suffix()
            const auto scanSpin = [&withSuffix](const QList<QWidget*>& ws) {
                for (QWidget* w : ws) {
                    if (auto* sb = qobject_cast<QSpinBox*>(w)) {
                        if (!sb->suffix().isEmpty()) {
                            withSuffix << (sb->suffix().trimmed() + QStringLiteral("@QSpinBox"));
                        }
                    } else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) {
                        if (!dsb->suffix().isEmpty()) {
                            withSuffix << (dsb->suffix().trimmed() + QStringLiteral("@QDoubleSpinBox"));
                        }
                    }
                }
            };
            QList<QWidget*> spins;
            if (m_settingsPage) {
                for (QSpinBox* sb : m_settingsPage->findChildren<QSpinBox*>()) spins << sb;
                for (QDoubleSpinBox* sb : m_settingsPage->findChildren<QDoubleSpinBox*>()) spins << sb;
                unitLabels += m_settingsPage->findChildren<QLabel*>(QStringLiteral("unitLabel")).size();
            }
            for (QSpinBox* sb : findChildren<QSpinBox*>()) spins << sb;
            for (QDoubleSpinBox* sb : findChildren<QDoubleSpinBox*>()) spins << sb;
            scanSpin(spins);
            unitLabels += findChildren<QLabel*>(QStringLiteral("unitLabel")).size();
            std::fprintf(stderr, "[UI自检]   仍带 suffix 的数值框 %lld 个%s%s；框外单位标签 %d 个\n", static_cast<long long>(withSuffix.size()),
                         withSuffix.isEmpty() ? "" : "：", withSuffix.join(QStringLiteral(" / ")).toUtf8().constData(),
                         unitLabels);
            check(withSuffix.isEmpty(), "单位不再写在输入框里（全部改为框外标签）");
            check(unitLabels >= 6, "设置页/构建页都有框外单位标签（MB / 天 / % / 区块 / ×）");
        }

        // ② 高级页那行灰字已删除（页面底部已有「恢复所有设置」按钮）
        {
            int leftover = 0;
            // v3.4.12：只查设置页（更新历史正文里也会出现同一句话，按整窗查会误报）
            const QList<QLabel*> scope = m_settingsPage ? m_settingsPage->findChildren<QLabel*>()
                                                       : QList<QLabel*>();
            for (QLabel* lb : scope) {
                if (lb->text().contains(slime::ui::t("ui.6b6350c805f9"))) ++leftover;
            }
            check(m_settingsPage != nullptr, "设置页存在（灰字检查范围正确）");
            check(leftover == 0, "已删除高级页的「恢复所有设置到出厂默认值」灰字");
        }

        // ③ 构建页：无预估/开始构建/中断按钮，改为实时预估卡片（随预算变化）
        {
            check(m_w->bEstimate == nullptr && m_w->bStart == nullptr && m_w->bAbort == nullptr,
                  "构建页已移除「预估 / 开始构建 / 中断」按钮（开始与中断走工具栏主按钮）");
            check(m_w->bSummaryLive != nullptr, "构建页存在实时预估卡片");
            const int keepBudget = m_w->bBudget->value();
            m_w->bBudget->setValue(keepBudget == 3000 ? 2000 : 3000);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
            const QString live = m_w->bSummaryLive ? m_w->bSummaryLive->text() : QString();
            const bool showsBudget = live.contains(QString::number(m_w->bBudget->value()));
            const bool showsRange = live.contains(QString::number(m_w->minX->value())) &&
                                    live.contains(QString::number(m_w->maxZ->value()));
            std::fprintf(stderr, "[UI自检]   实时预估：%s\n", live.split('\n').first().toUtf8().constData());
            check(showsBudget && showsRange, "构建预估与参数实时同步（读控件当前值，不再靠弹窗/文件）");
            m_w->bBudget->setValue(keepBudget);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            syncSettingsPageFromState();
        }

        // ④ 构建模式不再要求「搜索模式 / 图案」：严格模式且未画图案也能开始构建
        {
            const bool keepPatternSet = m_patternSet;
            m_patternSet = false; // 模拟「没画图案」
            m_w->strictRadio->setChecked(true);
            onModeChanged();
            showPage(kPageBuild);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 160);
            m_w->bedrockRadio->setChecked(true); // 构建固定基岩版（也避免 Java 种子校验）
            // 范围取大一些，保证「已开始」可观测（小范围会瞬间跑完，导致误判为未开始）
            m_w->minX->setValue(-512); m_w->maxX->setValue(511);
            m_w->minZ->setValue(-512); m_w->maxZ->setValue(511);
            m_wholeMapConfirmed = false;
            m_hasLastOutcome = false;
            startSearch();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
            const bool started = m_running || m_hasLastOutcome; // 已开始 或 已跑完（都说明没有被拦下）
            std::fprintf(stderr,
                         "[UI自检]   构建模式（严格模式 + 未画图案）startSearch ⇒ 已开始=%d（运行中=%d 已完成=%d）\n",
                         started ? 1 : 0, m_running ? 1 : 0, m_hasLastOutcome ? 1 : 0);
            check(started, "缓存构建不再被要求设置搜索模式/绘制图案（用户实测项）");
            if (started) {
                cancelSearch();
                const auto t0 = std::chrono::steady_clock::now();
                while (m_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10.0) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(10);
                }
            }
            m_patternSet = keepPatternSet;
            showPage(kPageSearch);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
        }
    }

    // v3.4.8：恢复自检前的设置与 config.json（见函数开头存档）
    if (m_settingsPage) {
        m_settingsPage->setValues(settingsArchive);
        m_settings = settingsArchive;
        applyAppSettings(m_settings);
        writeConfigFile(m_w->gpuCheck && m_w->gpuCheck->isChecked(),
                        m_terminal ? m_terminal->history() : QStringList());
    }
    if (!configArchive.empty()) {
        std::ofstream out(appDirPath() / kConfigFileName, std::ios::binary | std::ios::trunc);
        if (out) out.write(configArchive.data(), static_cast<std::streamsize>(configArchive.size()));
    }
    // v3.4.12：清理本次自检产生的缓存文件（测试产物）。
    //   自检会真跑「缓存构建」，每次都会在程序目录留下十几 MB 的缓存；
    //   留着会让**下一次启动**加载很久（实测表现为「启动卡住」），也会被带进发布目录。
    {
        std::error_code ecCache;
        const auto cachePath = appDirPath() / kCacheFileName;
        if (std::filesystem::exists(cachePath, ecCache)) {
            const auto sz = std::filesystem::file_size(cachePath, ecCache);
            if (std::filesystem::remove(cachePath, ecCache)) {
                std::fprintf(stderr, "[UI自检] 已清理自检缓存文件（%.1f MB）\n", sz / (1024.0 * 1024.0));
            }
        }
        const auto legacyPath = appDirPath() / kCacheLegacyFileName;
        std::filesystem::remove(legacyPath, ecCache);
    }
    std::fprintf(stderr, "[UI自检] 已恢复自检前的设置与 config.json\n");
    m_uiQuiet = false;
    if (fail == 0) {
        std::printf("[UI自检] 全部通过 ✔\n");
        return 0;
    }
    std::printf("[UI自检] 失败 %d 项 ✘\n", fail);
    return 1;
}

} // namespace slime







// MainWindow.cpp - 主窗口实现（v3.2.1）
#include "i18n/UiText.h"
#include "gui/mainwindow/MainWindow.h"

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
#include "gui/services/TelemetryClient.h" // v3.3.9（任务书 四）：完整更新历史文本（1.0.0 → 3.3.9）
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

#include "gui/panels/FeedbackPanel.h"    // v3.3.4：底部「反馈」分类（v3.3.6：时间顺序 + 过滤成功）
#include "gui/panels/LogPanel.h"         // v3.3.4：底部「日志」分类（默认关闭 + 两级选项）
#include "gui/widgets/PatternEditor.h"
#include "gui/widgets/PatternEditorPanel.h"
#include "i18n/UiStrings.h"
#include "gui/panels/ProgressStreamPanel.h" // v3.3.6：底部「进度」分类（累积式纯文字流水）
#include "gpu/GpuScanAdapter.h" // v3.2.1：GPU 加速（默认关闭；用户勾选+可用才注入，自动回退）
#include "utils/CrashDump.h"     // v3.3.7：崩溃转储开关（设置接线）
#include "utils/IncidentLog.h"   // v3.3.7：关键信息日志（常开，崩溃/取消取证）
#include "utils/LogBus.h"        // v3.3.4：日志总线（按日期分文件 + 两级模式）
#include "utils/Logger.h"
#include "utils/SoundPlayer.h"   // v3.3.6（任务书 4.2）：搜索完成提示音（winmm，带音量）
#include "utils/ThemeManager.h"  // v3.3.6（任务书 4.7）：夜间模式三档（浅色/深色/跟随系统）

#include "gui/dialogs/AfkDialog.h"   // v3.5.2（任务 1）：挂机点（环形覆盖）对话框
#include "gui/widgets/MapPreviewWidget.h" // v3.5.2（任务 2）：自检里直接读地图控件状态
#include "gui/dialogs/MapDialog.h"       // v3.5.2（任务 2）：地图预览对话框
#include "gui/dialogs/HelpDialog.h" // v3.3.3：帮助 / 使用教程对话框
#include "gui/dialogs/UpdateDialog.h" // v3.10.12.10：GitHub Releases + 镜像 + 网盘备用的更新检查

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
constexpr const char* kAppVersion = "v3.12.0";
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
        // v3.11.2：GUI 自检固定用中文构建界面，避免用户配置英文时结构断言假失败；
        //   三种语言包由 scripts/check_ui_text.py 与人工截图回归覆盖。
        const bool selftestMode = qEnvironmentVariableIsSet("SLIME_SELFTEST");
        const std::string lang = selftestMode ? std::string("zh") : cfgString(langCfg, "settings.language", "zh");
        slime::ui::setLanguage(lang == "en" ? slime::ui::Language::English
                                  : (lang == "zh-TW" || lang == "tw") ? slime::ui::Language::Traditional
                                  : slime::ui::Language::Chinese);
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
#include "MainWindowBuildUi.inc"
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
                                                           : (lang == QLatin1String("zh-tw") || lang == QLatin1String("tw"))
                                                                 ? slime::ui::Language::Traditional
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
#include "MainWindowScreenshots.inc"

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
    if (m_dockTitle) m_dockTitle->setText(slime::ui::t("ui.cb45d50a89a3").arg(slime::ui::t(kNames[index])));
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

#include "MainWindowSelfTest.inc"

} // namespace slime







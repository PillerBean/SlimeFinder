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

#include "MainWindowConfig.inc"
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
#include "MainWindowInteraction.inc"
#include "MainWindowServices.inc"
#include "MainWindowSearchResults.inc"
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







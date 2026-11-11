// MainWindow.h - 主窗口（史莱姆区块查找器 GUI）
//
// 交互流程：游戏版本 → 种子 → 范围(快速预设/边界截断) → 模式（严格图案 / 密度）→
// 搜索算法（精确 / 分层 / 快速估算 / 保守）→ 查询起点 → 开始搜索；
// 搜索在后台线程运行（OpenMP 并行），进度通过队列回调更新 UI，用户可随时取消。
// v3.1.0 新增：预设范围、世界边界自动截断、逐结果“复制为 Minecraft 命令”、
// 联系我们对话框、缓存容量限制（config.json）、logs/performance.log。
// v3.3.0 新增：算法模式选择（含参数与精度提示）、结果区模式标注与警告条。
// v3.3.3 新增：帮助/使用教程、GPU 加速临界值提示、进度分阶段文字、缓存状态标签刷新。
// v3.3.4 结构性重构（任务书 1/2）：QMainWindow 原生的菜单栏/工具栏/状态栏 +
// 中部垂直 QSplitter[水平 QSplitter(左 QScrollArea+6 分组 / 右 信息栏+结果 QTextEdit)]
// + 底部 QTabWidget（进度 / 反馈 / 日志 / 命令终端）；种子默认空、目标数量默认 1；
// 新增更新提醒（build_date.txt）与命令终端宿主（CommandHost）。
// 注意：按任务书要求**不做折叠、不搬移布局条目**。
#pragma once

#include <QMainWindow>

#include "core/AfkPlanner.h"
#include "core/BiomeMap.h"   // v3.5.5：环形覆盖候选（成员用 slime::AfkCandidate）
#include <QString>
#include <QStringList>

class QSystemTrayIcon; // v3.3.9（任务书 5.1）：托盘图标
class QIcon;           // v3.4.0：左导航 / 停靠图标（QPainter 绘制）
class QStackedWidget;  // v3.4.0：页面栈
class QListWidget;     // v3.4.0：更新历史版本索引
class QTreeWidget;     // v3.4.0：更新历史可展开列表
class QPushButton;     // v3.4.0：标签页 / 导航 / 停靠按钮
class QVBoxLayout;     // v3.4.3：搜索页参数列
class QToolButton;     // v3.4.1：左导航（上图案下文字）
#include <QList>       // v3.4.0：QList<QPushButton*>

#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>

#include "core/CacheManager.h"
#include "cache/CacheStore.h"
#include "core/SearchTask.h"
#include "core/SlimeEngine.h"
#include "gui/panels/CommandTerminal.h" // v3.3.4：命令宿主接口（CommandHost）
#include "gui/dialogs/SettingsDialog.h"  // v3.3.5：程序设置（AppSettings + 五标签页对话框）

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPoint;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QResizeEvent;
class QScrollArea;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QTextEdit;
class QTimer;
class QToolBar;
class QToolButton;

class FeedbackPanel; // v3.3.4：底部「反馈」分类（v3.3.6 改版：时间顺序 + 过滤成功）
class LogPanel;      // v3.3.4：底部「日志」分类
class ProgressStreamPanel; // v3.3.6：底部「进度」分类（累积式纯文字流水）
class MapPreviewWidget;   // v3.5.4：搜索页内嵌地图（全局类，勿在 namespace slime 内重复声明）

namespace slime {

namespace gui {
class TelemetryClient;
}

class MainWindow : public QMainWindow, public CommandHost {
    Q_OBJECT
public:
    // v3.5.20：按设置显示主窗口（无边框全屏 / 窗口最大化）—— main() 在构造后调用
    QString successProbabilityText(int64_t areaX, int64_t areaZ) const;  // v3.6.6：成功率预测
    void updateSimDistanceNote();                 // v3.6.18：模拟距离提示
    void clearResultsForNewSearch();               // v3.6.19：新搜索前清场
    void updateStartEnabled();                    // v3.6.5：条件不足时灰掉开始按钮
    void restartApplication();                    // v3.6.6：自动重启
    void startLanguageWatch();                  // v3.6.4：英文模式下周期重翻译
    void checkGpuCrashLoopOnStartup();          // v3.6.2：GPU 驱动崩溃后自动降级
    void saveMapVersionToConfig();               // v3.5.26：群系版本写入 config.json
    void showWithMode();
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // 无界面自检（预设/边界/命令/结果/配置/整图估算/算法模式/界面结构），供 CI 与发布前冒烟使用
    int runSelfTest();

    // v3.3.4（任务书 2.4）：命令终端宿主 —— 命令执行与搜索线程独立，立即生效（可插队）
    QString runCommand(const QString& cmd, const QStringList& args) override;
    QStringList commandNames() const override;
    QString commandHelp(const QString& cmd) const override;

protected:
    void closeEvent(QCloseEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override; // v3.3.0：窗口尺寸变化后重算换行标签高度

private slots:
    void onVersionChanged();
    void onModeChanged();
    void editPattern();
    void importCache();
    void clearCache();
    void convertBlockCoords();
    void startSearch();
    void cancelSearch();
    void copyResults();
    void updateCacheStatusUi();
    void showContact();
    void showCredits();   // v3.7.5：鸣谢名单
    void showTelemetryDialog();   // v3.10.12.12：匿名统计说明/开关
    void updateTelemetryNotice(); // v3.10.12.12：刷新状态栏公告
    void applyPreset(int halfSpan);
    void applyFullscreenMode();                 // v3.5.20：立即套用全屏方式
    void toggleFullscreenMode();                // v3.5.20：F11 切换
    void applyLanguage();                       // v3.5.27：按翻译表递归改写界面文案
    void retranslateModeUi();                  // v3.5.19：语言切换后刷新已迁移文案
    void setSearchRadius(int radiusChunks);   // v3.5.16：半径 ⇒ 正方形范围 [-r, r-1]
    void syncRadiusFromRange();               // v3.5.16：范围被改动时反推半径显示
    void clampRangeToBound();
    // v3.3.4：菜单/工具栏动作
    void showEnableLogDialog(); // 工具 → 启用日志…（隐蔽入口，两级选项）
    void exportResultsToFile(); // 文件 → 导出结果 Ctrl+S
    void copyAllCommands();     // 编辑 → 复制全部命令
    void clearResults();        // 编辑 → 清空结果 Ctrl+L
    void resetLayout();         // 查看 → 恢复默认布局 Ctrl+0
    void showAboutDialog();     // 帮助 → 关于
    void openReleasePage();     // 帮助 → 检查更新（GitHub/镜像/网盘对话框）
    void openPanPage();         // 帮助 → 网盘下载页（国内备用，手动）
    void highlightSeedField();  // v3.3.4（任务书 4.2）：种子输入框高亮闪烁
    void captureUiScreenshots(const QString& prefix); // v3.3.4（任务书 7）：UI 截图验证
    // ---- v3.3.5：Bug 2/4/5/6 与优化 4/5 ----
    void showGpuStatus();        // 工具 → GPU 状态（设备/显存/CU/实测吞吐 + 「不一定更快」提醒）
    void showAfkDialog();        // v3.5.2（任务 1）：工具 → 挂机点（环形覆盖，1/16 区块精度）
    void showMapDialog();        // v3.5.2（任务 2）：工具 → 地图预览（史莱姆区块/结果/挂机点）
    void toggleLogging();        // 工具 → 启用日志… / 关闭日志（动态切换）
    void updateLogMenuText();    // 按当前日志模式刷新菜单项文字
    void showProgramSettings();  // 工具 → 程序设置…（五标签页模态对话框）
    void applyAppSettings(const struct AppSettings& s); // 应用设置（立即生效 + 写 config.json）
    void loadAppSettings();      // 从 config.json 读取设置到 m_settings
    void startGpuBenchmark();    // Bug 6：后台真实基准（CPU/GPU 各两档）→ 实测吞吐率
    void updateGpuBenchmarkUi(); // 基准完成后刷新实测吞吐文案
    void applyThemeFromSettings(); // v3.3.6（任务书 4.7）：按设置切换浅色/深色/跟随系统
    void installWheelFilter();   // 优化 4：给全部 QSpinBox/QDoubleSpinBox 装滚轮过滤器
    bool eventFilter(QObject* watched, QEvent* ev) override; // 优化 4：滚轮开关
    bool forwardWheelToScrollArea(QWidget* from, QWheelEvent* we); // v3.5.1：把滚轮转交外层滚动区
    void keyPressEvent(QKeyEvent* ev) override;                 // v3.5.1：Esc = 取消进行中的任务

    void updateElapsedUi();     // v3.3.4：进度行/状态栏（已用时 / 预计剩余）
    // ---- v3.3.6（任务书）----
    void appendProgressStreamLine(); // 调整 3：每 0.5 秒向「进度」分类追加一条流水
    void selectBottomCategory(int index); // 调整 2：底部左侧竖排导航切换
    void updateNavBadges();          // 调整 2：仅「反馈」显示 ⚠️/❌ 计数

private:
    struct Widgets;
    struct RunInfo {
        Version version = Version::Java;
        Mode mode = Mode::Strict;
        Algorithm algorithm = Algorithm::Exact; // v3.3.0：本次搜索算法（日志/结果标注）
        int layerBlock = 16;                     // v3.3.0：分层级别
        double layerRatio = 1.0;                 // v3.3.0：分层阈值比例
        int sampleStep = 8;                      // v3.3.0：采样间隔
        bool sampleExact = false;                // v3.3.0：精算采样
        int gridSizeUsed = 5;                    // v3.3.0：本次搜索网格（性能日志）
        double minDensityUsed = 0.3;             // v3.3.0：本次搜索阈值（性能日志）
        SearchOptions options;                   // v3.3.0：完整选项（性能日志做精确模式对照）
        Rect region;
        int64_t targetCount = 1;                 // v3.3.4：默认 1（任务书 4.3）
        int64_t startX = 0;
        int64_t startZ = 0;
        bool mergeCacheAfter = false; // Bedrock + 使用缓存 + 未全覆盖 → 完成后合并缓存
        bool cacheWasUsed = false;
    };

    // ---- 界面构建（v3.3.4 分层构建，层级 ≤ 8）----
    void buildUi();
    // v3.3.8.1（任务书 1.3）：把「选文件」与「后台线程读取 + 进度条 + 合并」拆开，
    //   便于 GUI 自检在不弹文件对话框的前提下驱动**同一条生产路径**（导入大缓存不冻结）。
    void importCacheFromPath(const QString& path, bool showProgressDialog, bool showResultDialogs);
    void buildMenus();    // QMainWindow 原生菜单栏（文件/编辑/查看/工具/帮助）
    void buildToolBar();  // QMainWindow 原生工具栏（开始/取消/导入/清除/帮助 + 更新徽章）
    void showResultContextMenu(const QPoint& pos); // 结果视图右键菜单（复制选中/该条命令/全部命令）

    void applyVersionDefaults();
    void applyVersionUi();
    void setInputsEnabled(bool on);
    void setRunningUi(bool running);
    void setStatus(const QString& text);
    void logSearchParams(const SearchOptions& opt);
    void onSearchFinished(const SearchOutcome& out, const RunInfo& info, int runId); // v3.3.7：带运行编号（陈旧事件丢弃）
    QString describeResultsText(const SearchOutcome& out, const RunInfo& info) const;
    // v3.9.10（过程可视化）：把搜索过程（阶段耗时 / GPU 使用与回退原因 / 停止语义）汇成多行文本
    QString processSummaryText(const SearchOutcome& out) const;
    // v3.9.11：结果要求方案（预设）—— 只控制会改变结果的开关；GPU 等加速开关不参与
    void applyResultConflicts();
    void updatePresetDisclaimer();
    void updateAdvancedVisibility();
    void rebuildPresets();
    void applySelectedPreset();
    void markPresetCustom(const QString& reason);
    bool m_presetApplying = false;
    int m_presetPatternMode = -1;   // -1 未初始化；0 密度；1 图案
    int m_presetLastIndex = 0;
    // v3.3.4：结果以文本形式显示在右侧 QTextEdit（可选中、可右键复制命令）
    void populateResultText(const SearchOutcome& out, const RunInfo& info);
    void runRingSearch();   // v3.5.5：环形覆盖搜索分支
    void updateSearchShapeEnabled(); // v3.5.6：环形模式下置灰无关选项
    void toggleResultPane();
    void adjustMapSquare();
    void fillResultTable();
    int64_t m_resultTextTotal = 0; // 当前结果文本中的结果条数（供复制全部命令使用）
    // v3.3.3：结果排序（只改显示顺序，不改变结果集合与复制内容）
    void applyResultSort();
    QString cacheCommandFor(const MatchResult& r) const;
    std::filesystem::path appDirPath() const;
    int versionBound() const noexcept;
    int64_t cacheMaxBytes() const;
    void writePerformanceLog(const SearchOutcome& out, const RunInfo& info) const;
    // v3.3.0：算法模式（精确/分层/快速估算/保守）
    Algorithm currentAlgorithm() const;
    void updateAlgorithmPanels();      // 依当前算法启用/禁用参数控件并刷新提示
    void loadAlgorithmFromConfig();    // 读取 config.json 的 algorithm 块
    void saveAlgorithmToConfig();      // 写回 config.json（保留其它配置块）
    void writeConfigFile(bool gpuEnabled, const QStringList& terminalHistory); // 整体写回
    QString algorithmDisplayName(Algorithm a) const;
    // v3.3.0：界面截图（SLIME_SHOT=<前缀> 时自检过程中输出，便于核对文字是否被遮挡）
    void saveWindowShot(const QString& prefix, const QString& tag);
    void reserveWrappedHeight(QLabel* label); // 依据文本与可用宽度预留换行高度（防裁剪）
    // 非精确模式的结果差异校验（小范围自动跑一次精确对照，用于性能日志“与精确模式差异”）
    bool crossCheckExact(const SearchOptions& opt, int64_t sampleFound, int64_t* exactFound) const;
    // v3.2.1：GPU 加速（默认关闭，完全由用户勾选控制；不可用自动回退 CPU）
    bool gpuEnabledFromConfig() const;          // 读取 config.json 的 gpu.enabled（缺省 false）
    void saveGpuEnabledToConfig(bool enabled);  // 写回 config.json
    void ensureGpuScanOnce();                   // 勾选时初始化并检测一次
    void onGpuCheckChanged(Qt::CheckState state);
    void updateGpuStatusUi();                   // 状态文本（未启用/检测中/可用/不可用）
    // v3.3.6（任务书 Bug 2）：彻底移除临界值；GPU 相关只剩「设备信息 + 实测吞吐 + 提醒文案」
    QString gpuReminderText() const;  // 「GPU 加速不一定比纯 CPU 强…」提醒（tooltip 与状态对话框共用）
    QString gpuStatusText() const;    // GPU 状态对话框正文（自检直接断言，不弹窗）
    bool darkTheme() const;           // v3.3.6：当前是否深色（少数内联状态色据此切换）
    void applyInlineThemeColors();    // v3.3.6：主题切换后刷新这些内联状态色
    std::shared_ptr<slime::GpuScan> m_gpuScan;  // 检测成功后的提供者（可为空）
    bool m_loadingConfig = false;               // 载入 config 时抑制复选框写回
    bool m_gpuProbeDone = false;                // 是否已检测（成功/失败均为最终状态）
    bool m_gpuAvailable = false;                // 最近一次检测结果
    std::string m_gpuProbeReason;               // 不可用原因（日志/状态展示）
    // v3.1.1：整图（世界边界）搜索确认——基于本机实测速率的动态耗时估算
    bool confirmWholeMapSearch(bool applyRange, int halfSpan);
    SearchOptions wholeMapOptions(int halfSpan) const;
    bool m_wholeMapConfirmed = false;

    // ---- v3.3.4（任务书 2.2/2.3）：反馈与日志辅助 ----
    void addFeedback(int level, const QString& message); // level: 0 错误 / 1 警告 / 2 信息 / 3 成功
    void addError(const QString& message);               // 错误 + 自动切到反馈标签页
    // ---- v3.3.4（任务书 2.4）：命令终端历史持久化 ----
    void saveTerminalHistoryToConfig(const QStringList& history);
    QStringList loadTerminalHistoryFromConfig();
    // v3.3.4（任务书 3）：更新提醒（本地、不联网）
    void initUpdateReminder();      // 读取 build_date.txt，必要时显示徽章
    QString releasePageUrl() const; // 网盘/发布页地址

    // v3.4.0（ui_prototype/SlimeFinder-UI.html）：左导航 + 标签页 + 页面栈 + 底部停靠面板
    void showPage(int page);            // 切换页面（标签页与左导航同源）
    void showDock(int index);           // 展开底部停靠面板并切到第 index 类
    void hideDock();                    // 收起停靠面板（高度归 0）
    void buildV340Pages();              // 构建范围 / 设置 / 更新历史 / 控件状态 四页
    void showBuildEstimate();           // 构建预估（构建范围页「预估」按钮）
    QColor themedIconColor() const;                  // v3.4.8：自绘图案的当前主题取色（切主题立即重绘）
    void refreshThemedIcons();                       // v3.4.8：按当前主题重建导航/停靠图案
    void resetAllSettings();                         // v3.4.8：恢复所有设置（出厂默认值）
    void setBuildModeActive(bool on);                // v3.4.9：构建模式状态（搜索页开关已移除）
    void scheduleConfigWrite();                      // v3.4.11：配置写盘合并（空闲 500ms 落盘）
    void flushPendingConfigWrite();                  // v3.4.11：立即落盘（滑块释放/退出前）
    void syncSettingsPageFromState();                // v3.4.11：真实状态 → 设置页（单向收敛）
    void pushSearchRangeToBuild();                   // v3.4.9：搜索页范围 → 构建页（同一份数据）
    void updateBuildEstimateLive();                  // v3.4.12：构建预估实时刷新（读控件）
    QIcon v340NavIcon(int kind, double scale = 1.0); // v3.4.6：scale 供选中动画        // 左导航线性图标（QPainter 绘制，20×20）
    QPushButton* m_dockCollapse = nullptr;            // v3.4.8：停靠面板收起按钮（主题切换需重绘图案）
    QIcon v340DockIcon(int kind);       // 停靠图标条（0..3 分类 + 4 收起）
    int currentPage() const { return m_currentPage; }

    QStackedWidget* m_pageStack = nullptr;
    QWidget* m_navColumn = nullptr;
    // v3.9.15：顶部全局设置栏（平台 / 游戏版本 / 世界种子）
    QWidget* m_topBar = nullptr;
    QHBoxLayout* m_topBarLay = nullptr;
    QLabel* m_tabHint = nullptr;
    QList<QPushButton*> m_tabButtons;     // v3.4.1：标签页已移除（保留空列表，showPage 兼容）
    QList<QToolButton*> m_navButtons;     // v3.4.1：左导航改 QToolButton（上图案下文字）
    QList<QPushButton*> m_dockIcons;
    int m_currentPage = 0;
    int m_dockHeight = 240;             // v3.10.1：过程面板（右列下半）收起前高度
    QLabel* m_dockTitle = nullptr;      // v3.10.1：过程面板标题（"过程 · 进度"）
    std::vector<QToolButton*> m_dockHeadTabs;   // v3.10.1：过程面板标题栏页签（进度/反馈/日志/命令）
    QToolButton* m_dockToggleBtn = nullptr;     // v3.10.2：过程面板 收起/展开 按钮
    bool m_dockCollapsed = false;               // v3.10.2：过程面板是否只保留标题行
    bool m_customDisclaimerLogged = false;      // v3.10.9：自定义方案速度声明只提示一次
    QVBoxLayout* m_paramLay = nullptr;        // v3.4.3：搜索页参数列（挂构建「额外选项」）
    QWidget* m_buildExtra = nullptr;          // v3.4.3：缓存构建额外选项（勾选构建模式才显示）
    SettingsDialog* m_settingsPage = nullptr; // 设置页（嵌入 SettingsDialog 内容）
    QTimer* m_settingsApplyTimer = nullptr;   // v3.4.2：设置立即生效（250ms 去抖）
    // v3.4.10：排序按钮已移除（任务书要求严格按版本号排序）

    // ---- v3.3.9（任务书 1.1）：缓存构建模式（唯一可写缓存的模式；分段构建 + 落盘 + 中断续传）----
    bool buildModeActive() const;
    // v3.10.8：把本次搜索的**全部有效参数**写成一行指纹（排查「预设快/自定义慢」）
    QString searchOptionsFingerprint(const SearchOptions& opt) const;
    // v3.10.8：把本次搜索的**全部有效参数**写成一行指纹（排查「预设快/自定义慢」）
    bool prepareBuildRun(SearchOptions& opt, RunInfo& info); // 预估/上限确认/分段；false = 用户放弃
    void launchBuildBand();                                   // 启动当前分段（续跑时反复调用）
    void handleBuildBandFinished(const SearchOutcome& out, int runId);
    void updateBuildProgressUi(bool scanning);                // 四项进度 + 「缓存扫描/新区域扫描」
    int64_t buildTotalChunks() const;                         // 本次构建范围的总区块数（去重口径）
    void updateBuildHintUi();                                 // 勾选状态 → 提示文字 + 控件可用性
    // v3.3.9.1（任务书 2.1）：启动时按设置自动检测 GPU 状态
    void maybeAutoCheckGpuAtStartup();

    int m_lastEffectiveThreads = 0; // v3.3.9（任务书 2.2）：最近一次生效线程数（CPU 上限接线自检用）
    bool m_buildActive = false;
    bool m_buildModeActive = false;
    // v3.10.7：进入缓存构建会临时强制「基岩版」；退出时必须恢复用户原本的平台
    bool m_preBuildWasBedrock = false;
    bool m_buildSavedVersion = false;  // v3.4.9：是否处于缓存构建模式（取代原搜索页开关）
    bool m_rangeSyncGuard = false;   // v3.4.9：搜索页/构建页范围同步的递归保护
    bool m_settingsSyncGuard = false; // v3.4.11：设置页 ←→ m_settings 程序化同步的抑制标记
    bool m_configWritePending = false; // v3.4.11：是否有待落盘的配置改动
    int m_configWriteCount = 0;        // v3.4.11：config.json 实际写入次数（自检断言合并写）
    QTimer* m_configWriteTimer = nullptr; // v3.4.11：合并写定时器（单次触发，500ms）
    bool m_buildStopRequested = false; // v3.3.9.1：越界/异常时的强制收尾标记      // 当前正在跑缓存构建（分段续跑由它驱动）
    SearchOptions m_buildBaseOpt;    // 构建基础选项（每段只改 minZ/maxZ）
    RunInfo m_buildRunInfo;
    std::vector<Rect> m_buildBands;
    size_t m_buildBandIndex = 0;
    int64_t m_buildDoneChunks = 0;   // 已完成分段累计（引擎口径，含块间重叠）
    int64_t m_buildSkippedTotal = 0; // 已完成分段里「因缓存已覆盖而跳过」的区块数
    qint64 m_buildStartMs = 0;

    Widgets* m_w = nullptr;
    std::unique_ptr<CacheManager> m_cache;
    std::unique_ptr<gui::TelemetryClient> m_telemetry; // v3.10.12.12：默认关闭，用户主动开启才发送
    QLabel* m_telemetryNotice = nullptr;               // v3.10.12.12：状态栏里的开启公告
    // v3.3.9（任务书 5.1）：托盘图标（图标与窗口/任务栏一致；系统无托盘时为 nullptr）
    QSystemTrayIcon* m_tray = nullptr;
    std::filesystem::path m_cacheFile;
    std::filesystem::path m_cacheDir; // v3.12.3：新目录缓存
    std::unique_ptr<slime::cache::CacheStore> m_cacheStore;
    Pattern m_pattern;      // 当前绘制图案（strict 模式）
    bool m_patternSet = false;
    bool m_running = false;
    bool m_uiQuiet = false; // 自检时抑制模态对话框
    int64_t m_searchStartMs = 0; // 本次搜索开始时刻（epoch ms，性能日志用）
    SearchTask m_task;
    QString m_lastResultText; // 最近一次完整结果文本（供“复制结果”）
    // v3.3.3：保留最近一次搜索结果，用于结果区排序（只改显示顺序）
    SearchOutcome m_lastOutcome;
    RunInfo m_lastInfo;
    bool m_hasLastOutcome = false;
    bool m_stageWasMatching = false; // v3.3.3：进度阶段文字（扫描→匹配 切换提示只显示一次）

    // ---- v3.3.4：中央分区 / 底部面板（v3.3.6：改为左竖排导航 + 右内容区）----
    QHBoxLayout* m_progressRow = nullptr; // v3.3.6：工具栏下方进度行（横跨整窗，约 28px）
    QWidget* m_progressRowWidget = nullptr; // 进度行容器（自检断言位置/高度用）
    QSplitter* m_vSplit = nullptr;
    int m_mapJavaVersionId = 0;        // v3.5.4：地图群系版本
    QWidget* m_mapPanel = nullptr;     // v3.5.4：搜索页右侧地图面板
    std::vector<slime::AfkCandidate> m_lastAfk;
    std::unique_ptr<slime::BiomeMap> m_ringBiome;   // v3.5.7：环形加权复用
    int m_ringBiomeVersion = -1;
    int64_t m_ringBiomeSeed = 0;   // v3.5.5：环形覆盖的候选点
    QWidget* m_mapPlaceholder = nullptr;
    MapPreviewWidget* m_map = nullptr;
    QWidget* m_resultPane = nullptr;   // v3.5.5：结果面板（可折叠）
    QToolButton* m_resultToggle = nullptr;
    QLabel* m_resultCountLabel = nullptr;
    QToolButton* m_mapToggle = nullptr;
    QSplitter* m_rightSplit = nullptr;
    QTimer* m_mapSyncTimer = nullptr;
    QPoint m_resultClickPos;
    int m_mapFollowChunkX = 0;
    int m_mapFollowChunkZ = 0;
    bool m_mapViewInitialised = false;
    bool m_pendingShowMode = false;   // v3.5.20：窗口尚未显示时的待办全屏
    QAction* m_toolBarSpacerAction = nullptr;
    QTimer* m_langTimer = nullptr;
    bool m_rightSplitUserMoved = false;
    int m_mapPaneHeight = 0;
    // v3.10.0：三纵列布局 —— 中间地图列
    QWidget* m_mapColumn = nullptr;
    QVBoxLayout* m_mapColLay = nullptr;
    int m_mapPaneWidth = 660;
    QWidget* m_mapSpacer = nullptr;      // v3.6.9.1：折叠时才显示，吸收剩余空间
    QWidget* m_resultSpacer = nullptr;   // v3.6.9.1：同上（结果面板）             // v3.6.8：折叠前的地图面板高度，展开时恢复  // v3.6.6：用户拖过分隔条后不再自动改尺寸
    bool m_restartPending = false;
    int m_lastRingOuter = 0;      // v3.6.18：本次环形搜索的有效外沿（模拟距离可能收窄）
    double m_minDensityBeforeRing = 0.3;  // v3.10.12.10：切回正方形时恢复环形前的阈值
    double m_minDensityCommitted = 0.3;   // v3.10.12.11：焦点离开后才判断“最小密度被改”
    std::atomic<bool> m_ringCancel{ false };  // v3.10.12.11：环形自动预筛的取消标志
    bool m_ringSearching = false;             // v3.10.12.11：环形搜索（含预筛）进行中
    QLabel* m_resultsNotice = nullptr;   // v3.6.5：结果区上方提醒   // v3.6.4：语言巡检定时器
    QPushButton* m_mapIconBtn = nullptr;   // v3.6.1：左栏图标条里的地图开关
    void updateMapFromForm();
    void toggleMapPanel();
    void focusMapOnChunk(int chunkX, int chunkZ);
    void focusAfkCandidate(int index);   // v3.5.10：选中环形候选（只显示该环）
    void computeAfkForMap();
    QSplitter* m_hSplit = nullptr;
    QListWidget* m_nav = nullptr;         // v3.3.6：底部左侧竖排导航（进度/反馈/日志/命令）
    QStackedWidget* m_stack = nullptr;    // v3.3.6：底部右侧内容区
    FeedbackPanel* m_feedback = nullptr;
    LogPanel* m_logPanel = nullptr;
    ProgressStreamPanel* m_progressStream = nullptr;
    CommandTerminal* m_terminal = nullptr;
    QTimer* m_elapsedTimer = nullptr; // 进度行/状态栏的已用时刷新（200ms）
    QTimer* m_streamTimer = nullptr;  // v3.3.6：进度流水追加定时器（500ms，仅搜索中）
    QLabel* m_statusSep = nullptr;    // v3.3.7.2（1.3）：状态栏临时消息与永久控件之间的分隔符
    double m_etaSec = 0.0;            // 最近一次进度回调给出的预计剩余秒数
    int64_t m_streamScanned = 0;      // v3.3.6：流水行显示的「扫描 N 区块」（v3.3.7：改用引擎真实计数）
    int m_streamTick = 0;             // v3.3.7：流水节拍计数（每 ~5 秒写一次关键信息日志）
    int m_runId = 0;                  // v3.3.7（P0-2）：本次运行编号（丢弃陈旧的结果/进度事件）
    int64_t m_runTotalChunks = 0;     // v3.3.6：本次搜索范围区块总数（流水推算用）
    // v3.3.7.2（任务书 1.2）：界面显示用的总扫描量 —— 与引擎 liveScanned 同源（引擎未公开时退回区域区块数）
    int64_t displayTotalChunks() const;
    // v3.3.7.2（任务书 1.4）：本次运行的模式/算法 —— 阶段文案按它们分派（密度=匹配密度、估算=估算）
    slime::Mode m_runMode = slime::Mode::Strict;
    slime::Algorithm m_runAlg = slime::Algorithm::Exact;
    bool m_gpuReminderShown = false;  // v3.3.6：启用 GPU 时是否已推送「不一定更快」提醒（自检用）
    bool m_largeRangeWarned = false;  // v3.3.6：大范围阈值分支是否被走到（自检用）
    bool m_settingsDialogOpened = false; // v3.3.6：工具栏 [⚙ 设置] 点击是否进入设置入口（自检用）
    // 更新提醒
    QLabel* m_updateBadge = nullptr;
    QToolBar* m_toolBar = nullptr;
    int m_daysSinceRelease = 0;
    bool m_updateBadgeVisible = false;
    // ---- v3.3.5：程序设置 / 日志开关 / GPU 基准 ----
    AppSettings m_settings;              // 来自 config.json 的程序设置（默认值来源）
    QAction* m_actLogToggle = nullptr;   // 「启用日志…/关闭日志」动态菜单项
    double m_gpuBenchCpuRate = 0.0;      // 实测 CPU 吞吐（区块/秒，当前版本）
    double m_gpuBenchGpuRate = 0.0;      // 实测 GPU 吞吐（区块/秒，含回读）
    QString m_gpuBenchText;              // 实测吞吐结论文本（状态对话框用；v3.3.6 不再给交叉点）
    bool m_updateBrowserOpened = false;  // v3.3.5 Bug 4：仅当用户点「打开浏览器」时置位

    // 菜单动作（自检断言用）
    QMenu* m_menuFile = nullptr;
    QMenu* m_menuEdit = nullptr;
    QMenu* m_menuView = nullptr;
    QMenu* m_menuTools = nullptr;
    QMenu* m_menuHelp = nullptr;
};

} // namespace slime

// SettingsDialog.h —— 程序设置对话框（v3.3.5 任务书 优化 5）
//
// 设计约定（与主窗口的分工）：
//   1. 本对话框只负责「显示 / 收集」配置项，不读写 config.json、不产生任何副作用；
//      磁盘上的清理动作（清空缓存、打开日志目录）也只记录「用户点过按钮」，
//      由调用方在用户点「应用」后真正执行 —— 这样「取消」保证不产生任何影响。
//   2. 三个按钮的语义：
//        [恢复默认] 把控件填成 factoryDefaults()，不自动应用、不关闭窗口；
//        [取消]     直接 reject()，不保存；
//        [应用]     记录当前控件值到内部成员并置 applied()=true，但「不关闭」对话框，
//                   用户可以接着改、再点一次应用。
//   3. 调用方用法（示例）：
//        SettingsDialog dlg(AppSettings{}, this);
//        if (dlg.exec() == QDialog::Accepted || dlg.applied()) { ... }
//      实际上以 applied() 为准：只有点过「应用」才写 config.json 并让设置生效。
//   4. 供 GUI 自检使用的接口：factoryDefaults() / currentValues() / setValues() / tabs()。
//      其中 tabs() 应恰好返回 5 个标签页（通用 / 搜索 / 缓存 / 日志 / 高级）。
//
// 注意：本头文件声明了 Q_OBJECT，故 .cpp 中「不需要」也不应 include "SettingsDialog.moc"
//       （工程已开启 CMake AUTOMOC，moc 由构建系统自动处理）。
#pragma once

#include <QDialog>
#include <QKeyEvent>
#include <QString>
#include <QVBoxLayout>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTabWidget;

// 全部可配置项（纯数据；由主窗口负责读写 config.json 与生效）
struct AppSettings {
    // ---- 通用 ----
    bool wheelAdjust = false;         // 允许鼠标滚轮调整输入框数值（默认关闭）
    bool rememberWindow = true;       // 窗口大小记忆（默认开启）
    bool autoRestart = false;         // v3.6.6：需要重启才生效的设置，改完自动重启程序
    QString theme = "system";         // v3.3.6：light / dark / system（夜间模式三档）
    QString language = "zh";          // v3.5.19：zh / en（界面语言，文案走 UiStrings.h）
    QString fullscreenMode = "windowed";   // v3.5.22（用户要求）：默认窗口最大化（有边框），可选无边框全屏
    bool soundOnComplete = false;     // v3.3.6：搜索完成时播放提示音（默认关闭）
    int soundVolume = 70;             // v3.3.6：提示音音量 0~100（%）
    // ---- 搜索（新搜索的初始默认值）----
    QString defaultAlgorithm = "exact"; // exact / layered / sample / brute
    int defaultThreads = 0;             // 0 = 自动（全部逻辑核心）
    int defaultTargetCount = 1;
    QString defaultPreset = "none";     // none / near / local / global / world / bedrock
    bool defaultGpu = false;            // 默认启用 GPU 加速
    bool defaultStopAtTarget = false;   // 默认开启及时反馈
    bool showGpuReminder = true;        // v3.3.6：启用 GPU 时显示「不一定更快」提醒（默认开启）
    int largeRangeWarnChunks = 100000000; // v3.3.6：超过此区块数时弹窗确认（默认 1 亿）
    // ---- 缓存 ----
    bool autoLoadCache = true;          // 启动时自动加载缓存
    QString cacheDir;                   // 空 = 程序目录（默认）
    int cacheMaxMB = 10240;
    // ---- 日志 ----
    bool logAtStartup = false;          // 启动时启用日志（默认关闭）
    int logLevel = 0;                   // 0 = 简要，1 = 详细
    QString logSubDir = "logs";         // 日志子目录（相对程序目录）
    int logKeepDays = 7;
    bool logAutoScroll = true;          // v3.3.6：日志面板自动滚动（默认开启）
    int logMaxLines = 10000;            // v3.3.6：日志面板最大显示行数（超过丢弃最旧）
    // ---- v3.3.9（任务书 二）：资源控制（放设置栏）----
    int cpuLimitPercent = 100;          // CPU 占用率上限（10~100；100 = 不限）
    int gpuLimitPercent = 100;          // v3.7.1：GPU 占用率上限（10~100；100 = 不限）
    // v3.8.11：图案 GPU 流式匹配（实验转正；默认关闭。目前只在 AMD 显卡上验证过，
    //   NVIDIA / Intel 未验证）
    bool gpuPatternStream = true;
    // v3.10.11.1：结果自优化（GPU 密度匹配提前停止；默认关，行为与旧版一致）
    bool gpuResultSelfOptimize = false;
    int memLimitMB = 0;                 // 内存上限（MB；0 = 自动 = 1.5GB 分块位图预算）
    // v3.3.9.1（任务书 2.1）：启动时自动检测 GPU 状态（默认关闭；无/异常 OpenCL 驱动下探测有原生崩溃风险）
    bool autoCheckGpuAtStartup = false;
    // ---- 高级 ----
    int updateReminderDays = 30;        // 发布日期超过此天数后显示更新提醒
    bool crashDump = true;              // v3.3.6：崩溃时生成转储文件（默认开启）
    bool debugMode = false;             // v3.3.6：调试模式（显示额外调试信息，默认关闭）
    // v3.5.18（用户要求）：高级调试参数 —— 精细化调整环形搜索的关键数字（后果自负）
    int afkRingInner = 24;      // 环形内半径（方块，过近不刷）
    int afkRingOuter = 128;     // 环形外半径（方块，过远消失）
    int ringCoarseStep = 4;     // 环形粗扫步长（方块）
    int ringStatBands = 4;      // 严格统计的行带数（并行度/开销权衡）
    // v3.10.2（用户要求）：进度速度显示单位 —— 0 自动（≥1 亿用亿）/ 1 万/秒 / 2 亿/秒
    int rateUnit = 0;
};

class SettingsDialog : public QDialog {
    Q_OBJECT
signals:
    void contactRequested();
public:
    explicit SettingsDialog(const AppSettings& current, QWidget* parent = nullptr);

    // 用户点「应用」后可用（点「取消」/关闭窗口时不应读取）
    AppSettings settings() const;
    // 是否点了「应用」（调用方据此决定是否写 config.json 并生效）
    bool applied() const;

    // 供 GUI 自检使用
    static AppSettings factoryDefaults();   // 「恢复默认」用的出厂值
    AppSettings currentValues() const;      // 当前控件值（不依赖是否点过应用）
    void setValues(const AppSettings& s);   // 以编程方式填值（供「恢复默认」与自检使用）
    QTabWidget* tabs() const;               // 自检：应恰好 5 个标签页

    // 缓存 / 日志两个「动作型」请求：点击按钮后置位，由调用方在「应用」时执行
    bool clearCacheRequested() const;       // 用户点了「清空所有缓存」
    bool openLogDirRequested() const;       // 用户点了「打开日志目录」

private:
    // ---- 界面构建（分层构建，避免单个函数过长）----
    void buildUi();
    QWidget* buildGeneralPage();
    QWidget* buildSearchPage();
    QWidget* buildCachePage();
    QWidget* buildLogPage();
    QWidget* buildAdvancedPage();
    void buildButtonRow(QVBoxLayout* root);
    void connectSignals();

    // ---- 交互 ----
    void onApplied();      // 记录当前值并置 applied 标志（不关闭窗口）
    void onRestoreDefaults();
    void onResetAll();     // v3.3.6（高级页「重置所有设置」）：全部页面恢复出厂默认值
    void onBrowseCacheDir();
    void onBrowseLogDir();
    void onVolumeChanged(int value); // v3.3.6：音量滑块 → 百分比文字

    // v3.6.8（用户反馈：设置页按 Esc 只剩「恢复默认」按钮）：
    //   本类被嵌入主窗口当作普通页面使用，QDialog 默认的 Esc→reject() 会 hide() 整个页面。
    void keyPressEvent(QKeyEvent* ev) override;

    // ---- 数据 ----
    AppSettings m_settings;    // 点过「应用」后的值（未点过时为传入的初值）
    bool m_applied = false;    // 是否点过「应用」
    bool m_clearCache = false; // 缓存页「清空所有缓存」是否被点过
    bool m_openLogDir = false; // 日志页「打开日志目录」是否被点过

    // ---- 控件（通用）----
    QCheckBox* m_wheelAdjust = nullptr;
    QCheckBox* m_rememberWindow = nullptr;
    QCheckBox* m_autoRestart = nullptr;   // v3.6.6：需要重启的设置自动重启程序
    // v3.3.6：主题三档 / 提示音 / 音量 / 预留语言
    QComboBox* m_theme = nullptr;
    QCheckBox* m_soundOnComplete = nullptr;
    QSlider* m_soundVolume = nullptr;
    QLabel* m_soundVolumeText = nullptr;
    QComboBox* m_language = nullptr;      // 预留（禁用状态）

    // ---- 控件（搜索）----
    QComboBox* m_defaultAlgorithm = nullptr;
    QComboBox* m_defaultThreads = nullptr; // 第 0 项 = 自动（值 0），其后 1..64
    QSpinBox* m_defaultTargetCount = nullptr;
    QComboBox* m_defaultPreset = nullptr;
    QCheckBox* m_defaultGpu = nullptr;
    QCheckBox* m_gpuPatternStream = nullptr;   // v3.8.11：图案 GPU 流式匹配
    QCheckBox* m_gpuResultSelfOptimize = nullptr;   // v3.10.11.1：结果自优化
    QCheckBox* m_defaultStopAtTarget = nullptr;
    // v3.3.6：GPU 提醒开关 + 大范围警告阈值
    QCheckBox* m_showGpuReminder = nullptr;
    QSpinBox* m_largeRangeWarn = nullptr;

    // ---- 控件（缓存）----
    QCheckBox* m_autoLoadCache = nullptr;
    QLineEdit* m_cacheDir = nullptr;
    QPushButton* m_browseCacheDir = nullptr;
    QSpinBox* m_cacheMaxMB = nullptr;
    QPushButton* m_clearCacheBtn = nullptr;

    // ---- 控件（日志）----
    QCheckBox* m_logAtStartup = nullptr;
    QRadioButton* m_logLevelBrief = nullptr;
    QRadioButton* m_logLevelDetail = nullptr;
    QLineEdit* m_logSubDir = nullptr;
    QPushButton* m_browseLogDir = nullptr;
    QSpinBox* m_logKeepDays = nullptr;
    QPushButton* m_openLogDirBtn = nullptr;
    // v3.3.6：日志面板自动滚动 + 界面最大行数
    QCheckBox* m_logAutoScroll = nullptr;
    QSpinBox* m_logMaxLines = nullptr;

    // ---- 控件（高级）----
    QSpinBox* m_updateReminderDays = nullptr;
    // v3.3.9（任务书 二）：资源控制
    QSpinBox* m_cpuLimit = nullptr;
    QSpinBox* m_gpuLimit = nullptr;   // v3.7.1：GPU 占用率上限
    QSpinBox* m_memLimit = nullptr;
    QCheckBox* m_autoCheckGpu = nullptr; // v3.3.9.1（任务书 2.1）：启动时自动检查 GPU 状态
    // v3.3.6：崩溃转储 / 调试模式 / 重置所有设置
    QCheckBox* m_crashDump = nullptr;
    QComboBox* mLanguage = nullptr;   // v3.5.19：界面语言
    QComboBox* mFullscreenMode = nullptr;   // v3.5.20：全屏方式
    QCheckBox* m_debugMode = nullptr;
    // v3.5.18：高级调试参数
    QSpinBox* m_ringInner = nullptr;
    QSpinBox* m_ringOuter = nullptr;
    QSpinBox* m_ringCoarseStep = nullptr;
    QSpinBox* m_ringStatBands = nullptr;
    QPushButton* m_resetAllBtn = nullptr;

    // ---- 底部按钮 ----
    QPushButton* m_restoreBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_applyBtn = nullptr;

    QTabWidget* m_tabs = nullptr;
};

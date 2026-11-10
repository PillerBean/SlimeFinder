// SettingsDialog.cpp —— 程序设置对话框的实现（v3.3.6 任务书 §4.2 ~ §4.6）
//
// 实现要点：
//   1. 5 个标签页（通用 / 搜索 / 缓存 / 日志 / 高级），每页由 buildXxxPage() 独立构建，
//      单个函数都保持在较短长度，便于对照任务书逐条检查。
//   2. 每个选项都是「控件 + 下方一行灰色小字说明」的两段式结构，统一走 addOptionRow()：
//      控件标题列用 addRow()、说明用 grayNote()（字号 8pt、颜色 #666、自动换行）。
//      任务书里的分组（界面行为 / 外观 / 声音 / 预留位置 / 默认值 / 提醒）用 addGroup()
//      生成分组框，这样「每个选项都有说明行」由结构保证、不会漏写。
//   3. 本文件不产生任何副作用：清空缓存、打开日志目录只置位标记，
//      真正的磁盘操作由调用方在 applied() 为真时执行 —— 因此「取消」绝对安全。
//      「恢复默认」与高级页「重置所有设置」也只把控件填成 factoryDefaults()，
//      不写 m_settings、不置 applied()、不关闭窗口，用户仍需点「应用」。
//   4. 带 Q_OBJECT 的类由 CMake AUTOMOC 处理，本文件不 include "SettingsDialog.moc"。
#include "gui/SettingsDialog.h"
#include "gui/UiStrings.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStringLiteral>
#include <QTabWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// 灰色小字说明：字号约 8pt、颜色 #666、自动换行（全对话框的说明行统一走这里）
QLabel* grayNote(const QString& text, QWidget* parent)
{
    auto* lab = new QLabel(text, parent);
    lab->setWordWrap(true);
    QFont f = lab->font();
    f.setPointSize(8);
    lab->setFont(f);
    lab->setStyleSheet(QStringLiteral("color:#666;"));
    return lab;
}

// 选项标题（左侧标签列）
QLabel* optionLabel(const QString& text, QWidget* parent)
{
    auto* lab = new QLabel(text, parent);
    lab->setWordWrap(false);
    return lab;
}

// 把一个「选项标题 + 控件」放进两列布局：第 0 列标题（顶对齐），第 1 列控件（可伸展）
// v3.4.12（用户要求：把单位从输入框里拿出来）：控件 + 单位小标签并排，单位显示在**框外**
QWidget* withUnit(QWidget* control, const QString& unit);

// v3.4.12（用户要求：把单位从输入框里拿出来）：控件 + 单位小标签并排，单位在**框外**
QWidget* withUnit(QWidget* control, const QString& unit)
{
    auto* box = new QWidget(control->parentWidget());
    auto* lay = new QHBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(control);
    auto* u = new QLabel(unit, box);
    u->setObjectName(QStringLiteral("unitLabel"));
    lay->addWidget(u);
    lay->addStretch(1);
    return box;
}

void addRow(QGridLayout* grid, int row, const QString& title, QWidget* control)
{
    const Qt::Alignment labelAlign = Qt::AlignRight | Qt::AlignVCenter;
    if (title.isEmpty()) {
        grid->addWidget(control, row, 1, Qt::AlignLeft | Qt::AlignVCenter);
    } else {
        grid->addWidget(optionLabel(title, control->parentWidget()), row, 0, labelAlign);
        grid->addWidget(control, row, 1, Qt::AlignLeft | Qt::AlignVCenter);
    }
}

// 一个选项的完整两段式结构：标题 + 控件一行，下面紧跟一行灰色小字说明。
// 返回「下一行的行号」，所以调用方写成 row = addOptionRow(form, row, ...) 即可。
// 说明文字一律非空（任务书要求每个新增选项都带说明），由本函数统一收口。
int addOptionRow(QGridLayout* grid, int row, const QString& title, QWidget* control, const QString& desc)
{
    addRow(grid, row++, title, control);
    grid->addWidget(grayNote(desc, control->parentWidget()), row++, 1);
    return row;
}

// 一行表单：选项标题 + 控件（第 0 列为标题，第 1 列可伸展）
QGridLayout* makeForm(int labelWidth)
{
    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    grid->setColumnMinimumWidth(0, labelWidth);
    grid->setColumnStretch(1, 1);
    return grid;
}

// 分组框 + 内部表单：把表单放进带标题的分组框并挂到 parentLay 上，返回表单布局，
// 调用方随后用 addOptionRow() 往里逐项添加（任务书 §4.2/§4.3 的分组即由此而来）
QGridLayout* addGroup(QVBoxLayout* parentLay, QWidget* parent, const QString& title, int labelWidth)
{
    auto* box = new QGroupBox(title, parent);
    auto* boxLay = new QVBoxLayout(box);
    boxLay->setContentsMargins(10, 10, 10, 10);
    boxLay->setSpacing(6);

    QGridLayout* form = makeForm(labelWidth);
    boxLay->addLayout(form);
    parentLay->addWidget(box);
    return form;
}

// 组合两个控件为一行（例如输入框 + 「浏览…」按钮）
QWidget* pairWidget(QWidget* first, QWidget* second, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(first, 1);
    lay->addWidget(second, 0);
    return row;
}

// 组合「按钮 + 右侧弹簧」为一行（按钮靠左）
QWidget* buttonRow(QWidget* button, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(button, 0);
    lay->addStretch(1);
    return row;
}

// 在指定下拉框里按 itemData 找值对应的下标；找不到返回 -1
int comboIndexForData(const QComboBox* box, const QString& value)
{
    for (int i = 0; i < box->count(); ++i) {
        if (box->itemData(i).toString() == value) {
            return i;
        }
    }
    return -1;
}

// 下拉框填值：先按 itemData 精确匹配，未知值用 fallbackData（出厂默认值）对应的项，
// 都找不到才回落到第 0 项 —— 保证界面永远有合法选中项，且不会被改成「另一个合法值」
void selectComboData(QComboBox* box, const QString& value, const QString& fallbackData)
{
    int idx = comboIndexForData(box, value);
    if (idx < 0) {
        idx = comboIndexForData(box, fallbackData);
    }
    box->setCurrentIndex(idx >= 0 ? idx : 0);
}

} // namespace

// ============================================================================
// 构造 / 界面构建
// ============================================================================

SettingsDialog::SettingsDialog(const AppSettings& current, QWidget* parent)
    : QDialog(parent)
    , m_settings(current)
{
    setWindowTitle(QStringLiteral("程序设置"));
    setMinimumSize(640, 560); // 约 620×520：5 个标签页都能完整显示且不拥挤
    buildUi();
    connectSignals();
    // 用调用方传入的现值填充控件；m_applied 保持 false（未点「应用」前不可读取）
    setValues(current);
}

void SettingsDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    m_tabs = new QTabWidget(this);
    // v3.5.19：每页套一层滚动区 —— 页内容变多时不会再垂直挤压（此前出现文字/输入框互相遮挡）
    const auto wrapScroll = [this](QWidget* page) -> QWidget* {
        auto* sa = new QScrollArea(this);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        sa->setWidget(page);
        return sa;
    };
    // 标签页顺序固定：通用 / 搜索 / 缓存 / 日志 / 高级（自检会断言数量与顺序）
    m_tabs->addTab(wrapScroll(buildGeneralPage()), QStringLiteral("通用"));
    m_tabs->addTab(wrapScroll(buildSearchPage()), QStringLiteral("搜索"));
    m_tabs->addTab(wrapScroll(buildCachePage()), QStringLiteral("缓存"));
    m_tabs->addTab(wrapScroll(buildLogPage()), QStringLiteral("日志"));
    m_tabs->addTab(wrapScroll(buildAdvancedPage()), QStringLiteral("高级"));
    m_tabs->setCurrentIndex(0); // 默认停在「通用」
    root->addWidget(m_tabs, 1);

    buildButtonRow(root);
}

// 通用页（任务书 §4.2）：界面行为 / 外观 / 声音 / 预留位置（暂不可用）
QWidget* SettingsDialog::buildGeneralPage()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(10);

    // ---- 界面行为 ----
    {
        QGridLayout* form = addGroup(lay, page, QStringLiteral("界面行为"), 0);
        int row = 0;

        // 允许鼠标滚轮调整输入框数值
        m_wheelAdjust = new QCheckBox(QStringLiteral("允许鼠标滚轮调整输入框数值"), page);
        // v3.5.19：界面语言（文案走 UiStrings.h，可扩展更多语言）
    mLanguage = new QComboBox(page);
    mLanguage->addItem(QStringLiteral("简体中文"), QStringLiteral("zh"));
    mLanguage->addItem(QStringLiteral("English"), QStringLiteral("en"));
    // v3.5.20：全屏方式（无边框全屏 / 窗口最大化）
    mFullscreenMode = new QComboBox(page);
    mFullscreenMode->addItem(QStringLiteral("无边框全屏"), QStringLiteral("borderless"));
    mFullscreenMode->addItem(QStringLiteral("窗口最大化（有边框）"), QStringLiteral("windowed"));
    row = addOptionRow(form, row, QStringLiteral("全屏方式:"), mFullscreenMode,
                       QStringLiteral("无边框全屏 = 铺满屏幕无标题栏；窗口最大化 = 保留标题栏与边框。F11 随时切换"));

    row = addOptionRow(form, row, QStringLiteral("界面语言（重启后生效）:"), mLanguage,
                       QStringLiteral("切换后立即套用（已迁移的界面文案生效；其余文案逐步迁移）"));

    row = addOptionRow(form, row, QString(), m_wheelAdjust,
                           QStringLiteral("关闭后，滚轮划过输入框不会改变数值"));

        // 窗口大小记忆
        m_rememberWindow = new QCheckBox(QStringLiteral("窗口大小记忆（下次启动）"), page);
        row = addOptionRow(form, row, QString(), m_rememberWindow,
                           QStringLiteral("下次启动时恢复上次的窗口大小和位置"));

        // v3.6.6（用户要求）：有些设置必须重启才生效 —— 可以设置成改完自动重启
        m_autoRestart = new QCheckBox(QStringLiteral("修改需要重启的设置后自动重启"), page);
        row = addOptionRow(form, row, QString(), m_autoRestart,
                           QStringLiteral("日志目录、日志保留天数、窗口记忆等改动需要重开程序；"
                                          "开启后保存设置时若检测到这类改动，会自动重启一次"));
    }

    // ---- 外观：主题三档（itemData 即 AppSettings::theme 的取值）----
    {
        QGridLayout* form = addGroup(lay, page, QStringLiteral("外观"), 60);
        int row = 0;

        m_theme = new QComboBox(page);
        m_theme->addItem(QStringLiteral("浅色"), QStringLiteral("light"));
        m_theme->addItem(QStringLiteral("深色"), QStringLiteral("dark"));
        m_theme->addItem(QStringLiteral("跟随系统"), QStringLiteral("system"));
        row = addOptionRow(form, row, QStringLiteral("主题:"), m_theme,
                           QStringLiteral("跟随系统时，会自动同步系统主题"));
    }

    // ---- 声音：完成提示音开关 + 音量滑块（百分比实时显示）----
    {
        QGridLayout* form = addGroup(lay, page, QStringLiteral("声音"), 60);
        int row = 0;

        m_soundOnComplete = new QCheckBox(QStringLiteral("搜索完成时播放提示音"), page);
        row = addOptionRow(form, row, QString(), m_soundOnComplete,
                           QStringLiteral("搜索结束时发出声音提醒"));

        m_soundVolume = new QSlider(Qt::Horizontal, page);
        m_soundVolume->setRange(0, 100);
        m_soundVolume->setSingleStep(5);
        m_soundVolume->setPageStep(10);
        m_soundVolume->setMinimumWidth(200);
        m_soundVolumeText = new QLabel(page);
        m_soundVolumeText->setMinimumWidth(40); // 固定最小宽度：百分比变化时布局不跳动
        onVolumeChanged(m_soundVolume->value()); // 先填一次文字；之后由 valueChanged 更新
        row = addOptionRow(form, row, QStringLiteral("音量:"),
                           pairWidget(m_soundVolume, m_soundVolumeText, page),
                           QStringLiteral("提示音的音量大小（0 ~ 100%）"));
    }

return page;
}

// 搜索页（任务书 §4.3）：默认值 / 提醒
QWidget* SettingsDialog::buildSearchPage()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(10);

    // ---- 默认值（新搜索的初始默认值）----
    {
        QGridLayout* form = addGroup(lay, page, QStringLiteral("默认值"), 96);
        int row = 0;

        // 默认搜索模式
        m_defaultAlgorithm = new QComboBox(page);
        m_defaultAlgorithm->addItem(QStringLiteral("精确模式"), QStringLiteral("exact"));
        m_defaultAlgorithm->addItem(QStringLiteral("分层模式"), QStringLiteral("layered"));
        m_defaultAlgorithm->addItem(QStringLiteral("快速估算模式"), QStringLiteral("sample"));
        m_defaultAlgorithm->addItem(QStringLiteral("保守模式"), QStringLiteral("brute"));
        row = addOptionRow(form, row, QStringLiteral("默认搜索模式:"), m_defaultAlgorithm,
                           QStringLiteral("启动时的默认搜索算法"));

        // 默认线程数（值 0 = 自动：全部逻辑核心；其后为 1..64 的具体数值）
        m_defaultThreads = new QComboBox(page);
        m_defaultThreads->addItem(QStringLiteral("自动（全部核心）"), 0);
        for (int n = 1; n <= 64; ++n) {
            m_defaultThreads->addItem(QString::number(n), n);
        }
        row = addOptionRow(form, row, QStringLiteral("默认线程数:"), m_defaultThreads,
                           QStringLiteral("搜索时使用的并行线程数"));

        // 默认目标数量
        m_defaultTargetCount = new QSpinBox(page);
        m_defaultTargetCount->setRange(1, 1000000);
        row = addOptionRow(form, row, QStringLiteral("默认目标数量:"), m_defaultTargetCount,
                           QStringLiteral("只显示最近的 N 个结果"));

        // 默认范围预设
        m_defaultPreset = new QComboBox(page);
        m_defaultPreset->addItem(QStringLiteral("不预设"), QStringLiteral("none"));
        m_defaultPreset->addItem(QStringLiteral("附近"), QStringLiteral("near"));
        m_defaultPreset->addItem(QStringLiteral("局部"), QStringLiteral("local"));
        m_defaultPreset->addItem(QStringLiteral("全局"), QStringLiteral("global"));
        m_defaultPreset->addItem(QStringLiteral("Java 边界"), QStringLiteral("world"));
        m_defaultPreset->addItem(QStringLiteral("基岩边界"), QStringLiteral("bedrock"));
        row = addOptionRow(form, row, QStringLiteral("默认范围预设:"), m_defaultPreset,
                           QStringLiteral("启动时自动填入的搜索范围"));

        // 默认启用 GPU 加速
        m_defaultGpu = new QCheckBox(QStringLiteral("默认启用 GPU 加速 ⭐（现代 GPU 通常显著更快；驱动异常时可关闭）"), page);
    // v3.8.11：图案流式开关（实验转正；仅 AMD 显卡验证过）
    m_gpuPatternStream = new QCheckBox(
        QStringLiteral("启用 GPU 图案流式匹配 ⭐（AMD 实测约 2~18 倍提速，NVIDIA / Intel 未验证）"),
        page);
    m_gpuPatternStream->setToolTip(QStringLiteral(
        "图案判定与匹配全部在显卡内完成，只回传命中。区域越大收益越明显；\n"
        "如遇异常或结果不符请关闭此项（会自动回到 CPU 路径）。"));
    // v3.10.11.1：结果自优化（默认关；只影响速度与"已保留数量"统计，结果内容不变）
    m_gpuResultSelfOptimize = new QCheckBox(
        QStringLiteral("结果自优化 ⭐（命中过多时提前停止；不改变结果内容）"), page);
    m_gpuResultSelfOptimize->setToolTip(QStringLiteral(
        "只保留离起点最近的 N 个结果；命中很多时自动跳过远处区域，大范围搜索更快。\n"
        "只影响「已保留数量」统计，不改变结果内容。适合：找最近 N 个、阈值偏低、大范围、命中密集、想尽快出结果。"));
        row = addOptionRow(form, row, QString(), m_gpuResultSelfOptimize,
                           QStringLiteral("命中过多时提前停止；只保留最近的 N 个结果。"));
        row = addOptionRow(form, row, QString(), m_gpuPatternStream,
                           QStringLiteral("图案判定与匹配全部在显卡内完成，只回传命中。"));
        row = addOptionRow(form, row, QString(), m_defaultGpu,
                           QStringLiteral("启动时自动开启 GPU（需显卡支持 OpenCL）"));
        {
            auto* starNote = new QLabel(QStringLiteral("⭐ = 推荐开启（不改变结果，通常更快）"), page);
            starNote->setStyleSheet(QStringLiteral("color:#666;font-size:8pt;"));
            form->addWidget(starNote, row, 0, 1, 3);
            ++row;
        }

        // 默认开启及时反馈
        m_defaultStopAtTarget = new QCheckBox(QStringLiteral("默认开启及时反馈"), page);
        row = addOptionRow(form, row, QString(), m_defaultStopAtTarget,
                           QStringLiteral("找到 N 个目标后立即停止搜索"));
    }

    // ---- 提醒：GPU 提醒开关 + 大范围搜索警告阈值 ----
    {
        QGridLayout* form = addGroup(lay, page, QStringLiteral("提醒"), 96);
        int row = 0;

        // 显示 GPU 加速提醒
        m_showGpuReminder = new QCheckBox(QStringLiteral("显示 GPU 加速提醒"), page);
        row = addOptionRow(form, row, QString(), m_showGpuReminder,
                           QStringLiteral("开启后，启用 GPU 时显示“不一定更快”提醒"));

        // 大范围搜索警告阈值（默认 1 亿区块）
        m_largeRangeWarn = new QSpinBox(page);
        m_largeRangeWarn->setRange(1000000, 2000000000);
        m_largeRangeWarn->setSingleStep(10000000);
        row = addOptionRow(form, row, QStringLiteral("大范围搜索警告阈值:"), withUnit(m_largeRangeWarn, QStringLiteral("区块")),
                           QStringLiteral("范围超过此值时弹窗确认"));
    }

    lay->addStretch(1);
    return page;
}

// 缓存页（任务书 §4.4）：保持原有内容
QWidget* SettingsDialog::buildCachePage()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(6);

    auto* form = makeForm(96);
    int row = 0;

    // 启动时自动加载缓存
    m_autoLoadCache = new QCheckBox(QStringLiteral("启动时自动加载缓存"), page);
    row = addOptionRow(form, row, QString(), m_autoLoadCache,
                       QStringLiteral("启动后自动读取磁盘上的缓存文件，加快首次搜索"));

    // 缓存目录（留空 = 程序目录）
    m_cacheDir = new QLineEdit(page);
    m_cacheDir->setPlaceholderText(QStringLiteral("留空 = 程序目录"));
    m_cacheDir->setClearButtonEnabled(true);
    m_browseCacheDir = new QPushButton(QStringLiteral("浏览…"), page);
    row = addOptionRow(form, row, QStringLiteral("缓存目录:"), pairWidget(m_cacheDir, m_browseCacheDir, page),
                       QStringLiteral("缓存文件的存放位置，留空表示程序目录"));

    // 缓存容量上限
    m_cacheMaxMB = new QSpinBox(page);
    m_cacheMaxMB->setRange(1, 1048576);
    m_cacheMaxMB->setSingleStep(64);
    row = addOptionRow(form, row, QStringLiteral("缓存容量上限:"), withUnit(m_cacheMaxMB, QStringLiteral("MB")),
                       QStringLiteral("超过此大小后自动清理最旧的数据"));

    lay->addLayout(form);

    // 清空所有缓存：只置位标记，真正的删除由调用方在「应用」时执行
    m_clearCacheBtn = new QPushButton(QStringLiteral("清空所有缓存"), page);
    lay->addWidget(buttonRow(m_clearCacheBtn, page));
    lay->addWidget(grayNote(QStringLiteral("点击应用后删除磁盘上的缓存文件"), page));

    lay->addStretch(1);
    return page;
}

// 日志页（任务书 §4.5）：原有项 + 日志自动滚动 / 界面最大行数
QWidget* SettingsDialog::buildLogPage()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(6);

    auto* form = makeForm(96);
    int row = 0;

    // 启动时启用日志
    m_logAtStartup = new QCheckBox(QStringLiteral("启动时启用日志"), page);
    row = addOptionRow(form, row, QString(), m_logAtStartup,
                       QStringLiteral("程序启动后自动开始记录日志"));

    // 日志级别：简要 / 详细（互斥）
    m_logLevelBrief = new QRadioButton(QStringLiteral("简要"), page);
    m_logLevelDetail = new QRadioButton(QStringLiteral("详细"), page);
    auto* levelRow = new QWidget(page);
    auto* levelLay = new QHBoxLayout(levelRow);
    levelLay->setContentsMargins(0, 0, 0, 0);
    levelLay->setSpacing(12);
    levelLay->addWidget(m_logLevelBrief, 0);
    levelLay->addWidget(m_logLevelDetail, 0);
    levelLay->addStretch(1);
    auto* levelGroup = new QButtonGroup(page);
    levelGroup->addButton(m_logLevelBrief, 0);
    levelGroup->addButton(m_logLevelDetail, 1);
    levelGroup->setExclusive(true);
    addRow(form, row++, QStringLiteral("日志级别:"), levelRow);
    form->addWidget(grayNote(QStringLiteral("简要：只记录关键事件；详细：记录所有事件"), page), row++, 1);
    form->addWidget(grayNote(QStringLiteral("详细会降低 10~30% 速度"), page), row++, 1);

    // 日志路径（相对程序目录的子目录名）
    m_logSubDir = new QLineEdit(page);
    m_logSubDir->setPlaceholderText(QStringLiteral("logs"));
    m_logSubDir->setClearButtonEnabled(true);
    m_browseLogDir = new QPushButton(QStringLiteral("浏览…"), page);
    row = addOptionRow(form, row, QStringLiteral("日志路径:"), pairWidget(m_logSubDir, m_browseLogDir, page),
                       QStringLiteral("日志文件的存放位置（相对程序目录的子目录名）"));

    // 保留天数
    m_logKeepDays = new QSpinBox(page);
    m_logKeepDays->setRange(1, 3650);
    row = addOptionRow(form, row, QStringLiteral("保留天数:"), withUnit(m_logKeepDays, QStringLiteral("天")),
                       QStringLiteral("超过天数后自动删除旧日志"));

    // 日志自动滚动（v3.3.6 新增）
    m_logAutoScroll = new QCheckBox(QStringLiteral("日志自动滚动"), page);
    row = addOptionRow(form, row, QString(), m_logAutoScroll,
                       QStringLiteral("新日志出现时自动滚动到底部"));

    // 界面最大行数（v3.3.6 新增）
    m_logMaxLines = new QSpinBox(page);
    m_logMaxLines->setRange(1000, 200000);
    m_logMaxLines->setSingleStep(1000);
    row = addOptionRow(form, row, QStringLiteral("界面最大行数:"), m_logMaxLines,
                       QStringLiteral("超过此数量后丢弃最旧的日志"));

    lay->addLayout(form);

    // 打开日志目录：同样只置位标记，交由调用方在「应用」时执行
    m_openLogDirBtn = new QPushButton(QStringLiteral("打开日志目录"), page);
    lay->addWidget(buttonRow(m_openLogDirBtn, page));
    lay->addWidget(grayNote(QStringLiteral("点击应用后打开上面设置目录中的日志文件夹"), page));

    lay->addStretch(1);
    return page;
}

// 高级页（任务书 §4.6）：更新提醒天数 + 崩溃转储 / 调试模式 / 重置所有设置
QWidget* SettingsDialog::buildAdvancedPage()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(6);

    auto* form = makeForm(96);
    int row = 0;

    // v3.3.9（任务书 2.2）：CPU 占用率上限
    m_cpuLimit = new QSpinBox(page);
    m_cpuLimit->setRange(10, 100);
    row = addOptionRow(form, row, QStringLiteral("CPU 占用率上限（下次搜索）:"), withUnit(m_cpuLimit, QStringLiteral("%")),
                       QStringLiteral("按逻辑核心数折算生效线程数（设 50% 时实测占用约 50%）"));

    // v3.7.1：GPU 占用率上限（瓦片之间按占空比休眠）
    m_gpuLimit = new QSpinBox(page);
    m_gpuLimit->setRange(10, 100);
    row = addOptionRow(form, row, QStringLiteral("GPU 占用率上限（下次搜索）:"),
                       withUnit(m_gpuLimit, QStringLiteral("%")),
                       QStringLiteral("GPU 启用时按占空比限速：设 50% 时 GPU 忙约一半时间，留给其他程序"));
    m_gpuLimit->setEnabled(m_defaultGpu->isChecked());
    QObject::connect(m_defaultGpu, &QCheckBox::toggled, m_gpuLimit,
                     [this](bool on) { if (m_gpuLimit) m_gpuLimit->setEnabled(on); });

    // v3.3.9（任务书 2.1）：内存上限
    m_memLimit = new QSpinBox(page);
    m_memLimit->setRange(0, 32768);
    m_memLimit->setSpecialValueText(QStringLiteral("自动"));
    row = addOptionRow(form, row, QStringLiteral("内存上限（下次搜索）:"), withUnit(m_memLimit, QStringLiteral("MB")),
                       QStringLiteral("分块位图预算：超出时自动降低并发（0 = 自动）"));

    // v3.3.9.1（任务书 2.1）：启动时自动检查 GPU 状态
    m_autoCheckGpu = new QCheckBox(QStringLiteral("启动时自动检查 GPU 状态"), page);
    row = addOptionRow(form, row, QStringLiteral("启动时检测（下次启动）:"), m_autoCheckGpu,
                       slime::ui::tr(QStringLiteral("开启后：启动时探测一次 OpenCL 并在「性能设置」里显示状态；"
                                      "关闭则不检测（勾选「启用 GPU 加速」时仍会即时检测）")));
    m_autoCheckGpu->setChecked(false); // 默认关闭：无/异常 OpenCL 驱动下探测有原生崩溃风险

    // 更新提醒天数
    m_updateReminderDays = new QSpinBox(page);
    m_updateReminderDays->setRange(1, 3650);
    row = addOptionRow(form, row, QStringLiteral("更新提醒天数:"), withUnit(m_updateReminderDays, QStringLiteral("天")),
                       QStringLiteral("程序发布日期超过此天数后显示更新提醒"));

    // 崩溃时生成转储文件（v3.3.6 新增）
    m_crashDump = new QCheckBox(QStringLiteral("崩溃时生成转储文件"), page);
    row = addOptionRow(form, row, QString(), m_crashDump,
                       QStringLiteral("程序崩溃时生成 .dmp 文件，便于排查"));

    // 调试模式（v3.3.6 新增）
    m_debugMode = new QCheckBox(QStringLiteral("调试模式"), page);
    row = addOptionRow(form, row, QString(), m_debugMode,
                       QStringLiteral("开启后显示额外调试信息"));

    // v3.5.18（用户要求）：高级调试参数 —— 精细化调整各个数据的关键数字
    lay->addSpacing(6);
    auto* advTitle = new QLabel(slime::ui::text(slime::ui::Key::AdvancedParamsTitle), page);
    advTitle->setObjectName(QStringLiteral("topSummary"));
    lay->addWidget(advTitle);
    auto* advForm = makeForm(120);
    int arow = 0;
    m_ringInner = new QSpinBox(page);
    m_ringInner->setRange(0, 64);
    arow = addOptionRow(advForm, arow, slime::ui::text(slime::ui::Key::RingInner),
                        withUnit(m_ringInner, QStringLiteral("格")),
                        slime::ui::text(slime::ui::Key::RingInnerTip));
    m_ringOuter = new QSpinBox(page);
    m_ringOuter->setRange(16, 1024);
    arow = addOptionRow(advForm, arow, slime::ui::text(slime::ui::Key::RingOuter),
                        withUnit(m_ringOuter, QStringLiteral("格")),
                        slime::ui::text(slime::ui::Key::RingOuterTip));
    m_ringCoarseStep = new QSpinBox(page);
    m_ringCoarseStep->setRange(1, 64);
    arow = addOptionRow(advForm, arow, slime::ui::text(slime::ui::Key::RingCoarseStep),
                        withUnit(m_ringCoarseStep, QStringLiteral("格")),
                        slime::ui::text(slime::ui::Key::RingCoarseStepTip));
    m_ringStatBands = new QSpinBox(page);
    m_ringStatBands->setRange(1, 16);
    arow = addOptionRow(advForm, arow, slime::ui::text(slime::ui::Key::StatBands), m_ringStatBands,
                        slime::ui::text(slime::ui::Key::StatBandsTip));
    lay->addLayout(advForm);

    lay->addLayout(form);

    // 重置所有设置：把五个页面全部填回出厂默认值，但不应用、不关闭（用户仍需点「应用」）
    m_resetAllBtn = new QPushButton(QStringLiteral("重置所有设置"), page);
    lay->addWidget(buttonRow(m_resetAllBtn, page));
    // v3.4.12（用户要求）：删掉这行灰字「恢复所有设置到出厂默认值」——
    // 设置页底部已经有「恢复所有设置」按钮（本页的「重置所有设置」按钮在设置页里被隐藏）。

    lay->addStretch(1);
    return page;
}

// v3.5.27（用户反馈：有些设置要重启才生效却没提醒）：
//   对话框底部统一说明一次，避免用户在「按了应用却没变化」里困惑。
static QLabel* makeRestartHint(QWidget* parent)
{
    auto* lb = new QLabel(QStringLiteral("提示：标注「下次启动 / 重启后生效」的设置不会立即生效；"
                                         "其余设置点「应用」后马上生效。"),
                          parent);
    lb->setWordWrap(true);
    lb->setObjectName(QStringLiteral("restartHint"));
    return lb;
}

void SettingsDialog::buildButtonRow(QVBoxLayout* root)
{
    root->addWidget(makeRestartHint(this));
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    m_restoreBtn = new QPushButton(QStringLiteral("恢复默认"), this);
    m_restoreBtn->setToolTip(QStringLiteral("把下面所有选项恢复为出厂默认值（还需点「应用」才会生效）"));
    row->addWidget(m_restoreBtn);

    row->addStretch(1);

    m_cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    m_cancelBtn->setToolTip(QStringLiteral("放弃本次修改，不保存、不生效"));
    row->addWidget(m_cancelBtn);

    m_applyBtn = new QPushButton(QStringLiteral("应用"), this);
    m_applyBtn->setDefault(true); // 「应用」为默认按钮（回车即应用）
    m_applyBtn->setAutoDefault(true);
    m_applyBtn->setToolTip(QStringLiteral("保存当前设置并立即生效（对话框保持打开，可继续修改）"));
    row->addWidget(m_applyBtn);

    root->addLayout(row);
}

void SettingsDialog::connectSignals()
{
    // 「应用」：记录当前值并置 applied 标志，但不关闭对话框
    connect(m_applyBtn, &QPushButton::clicked, this, &SettingsDialog::onApplied);
    // 「恢复默认」：只把控件填回出厂值，不自动应用
    connect(m_restoreBtn, &QPushButton::clicked, this, &SettingsDialog::onRestoreDefaults);
    // 「取消」：直接 reject()，绝不记录当前值，因此 applied() 保持 false
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    // 高级页「重置所有设置」：同样只填值，不应用、不关闭
    connect(m_resetAllBtn, &QPushButton::clicked, this, &SettingsDialog::onResetAll);

    // 音量滑块 → 百分比文字（实时联动）
    connect(m_soundVolume, &QSlider::valueChanged, this, &SettingsDialog::onVolumeChanged);

    // 两个「浏览…」按钮
    connect(m_browseCacheDir, &QPushButton::clicked, this, &SettingsDialog::onBrowseCacheDir);
    connect(m_browseLogDir, &QPushButton::clicked, this, &SettingsDialog::onBrowseLogDir);

    // 两个动作按钮：只置位标记，真正的磁盘操作由调用方在「应用」时执行
    connect(m_clearCacheBtn, &QPushButton::clicked, this, [this]() { m_clearCache = true; });
    connect(m_openLogDirBtn, &QPushButton::clicked, this, [this]() { m_openLogDir = true; });
}

// ============================================================================
// 交互
// ============================================================================

void SettingsDialog::onApplied()
{
    // 记录当前控件值；不关闭对话框（用户可以继续改、再点一次「应用」）
    m_settings = currentValues();
    m_applied = true;
}

// v3.6.8（用户反馈）：设置页是**嵌入**在主窗口里的普通页面，不是模态对话框。
//   QDialog 默认把 Esc 交给 reject() → done() → hide()，于是整页控件全部消失，
//   只剩页面外层的「恢复所有设置」按钮。这里直接吞掉 Esc（只允许子控件自己处理，
//   例如下拉框先关掉自己的弹出列表）。
void SettingsDialog::keyPressEvent(QKeyEvent* ev)
{
    if (ev->key() == Qt::Key_Escape) {
        ev->accept();
        return;
    }
    QDialog::keyPressEvent(ev);
}

void SettingsDialog::onRestoreDefaults()
{
    // 「恢复默认」只填值，不自动应用、不改变 applied() 状态
    setValues(factoryDefaults());
}

void SettingsDialog::onResetAll()
{
    // 「重置所有设置」= 把五个标签页全部填回 factoryDefaults()；
    // 与「恢复默认」一样只改控件：不写 m_settings、不置 m_applied、不关闭窗口
    setValues(factoryDefaults());
}

void SettingsDialog::onVolumeChanged(int value)
{
    // 音量百分比文字（例如 70%），随滑块实时更新
    m_soundVolumeText->setText(QStringLiteral("%1%").arg(value));
}

void SettingsDialog::onBrowseCacheDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择缓存目录"),
        m_cacheDir->text().isEmpty() ? QDir::currentPath() : m_cacheDir->text());
    if (!dir.isEmpty()) {
        m_cacheDir->setText(QDir::toNativeSeparators(dir));
    }
}

void SettingsDialog::onBrowseLogDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择日志目录"),
        m_logSubDir->text().isEmpty() ? QDir::currentPath() : m_logSubDir->text());
    if (!dir.isEmpty()) {
        m_logSubDir->setText(QDir::toNativeSeparators(dir));
    }
}

// ============================================================================
// 数据接口
// ============================================================================

AppSettings SettingsDialog::settings() const
{
    return m_settings;
}

bool SettingsDialog::applied() const
{
    return m_applied;
}

bool SettingsDialog::clearCacheRequested() const
{
    return m_clearCache;
}

bool SettingsDialog::openLogDirRequested() const
{
    return m_openLogDir;
}

QTabWidget* SettingsDialog::tabs() const
{
    return m_tabs;
}

AppSettings SettingsDialog::factoryDefaults()
{
    // 与任务书 §4.2 ~ §4.6 的默认值、以及 AppSettings 的结构体默认值逐项一致
    AppSettings s;
    // ---- 通用 ----
    s.wheelAdjust = false;
    s.language = "zh";
    s.fullscreenMode = "borderless";
    s.rememberWindow = true;
    s.autoRestart = false;
    s.theme = QStringLiteral("system"); // 跟随系统
    s.soundOnComplete = false;
    s.soundVolume = 70;
    // ---- 搜索 ----
    s.defaultAlgorithm = QStringLiteral("exact");
    s.defaultThreads = 0;
    s.defaultTargetCount = 1;
    s.defaultPreset = QStringLiteral("none");
    s.defaultGpu = false;
    s.defaultStopAtTarget = false;
    s.showGpuReminder = true;
    s.largeRangeWarnChunks = 100000000; // 1 亿区块
    // ---- 缓存 ----
    s.autoLoadCache = true;
    s.cacheDir.clear(); // 空 = 程序目录
    s.cacheMaxMB = 1024;
    // ---- 日志 ----
    s.logAtStartup = false;
    s.logLevel = 0; // 简要
    s.logSubDir = QStringLiteral("logs");
    s.logKeepDays = 7;
    s.logAutoScroll = true;
    s.logMaxLines = 10000;
    // ---- 高级 ----
    // v3.3.9（任务书 二）：资源控制默认值（CPU 不限 / 内存自动）
    s.cpuLimitPercent = 100;
    s.memLimitMB = 0;
    s.autoCheckGpuAtStartup = false; // v3.3.9.1（任务书 2.1）：默认关闭
    s.updateReminderDays = 30;
    s.crashDump = true;
    s.debugMode = false;
    s.afkRingInner = 24;
    s.afkRingOuter = 128;
    s.ringCoarseStep = 4;
    s.ringStatBands = 4;
    return s;
}

AppSettings SettingsDialog::currentValues() const
{
    // 覆盖 AppSettings 的全部字段（通用 5 / 搜索 8 / 缓存 3 / 日志 6 / 高级 3 = 25 项）
    AppSettings s;
    // ---- 通用 ----
    s.wheelAdjust = m_wheelAdjust->isChecked();
    s.rememberWindow = m_rememberWindow->isChecked();
    s.autoRestart = m_autoRestart && m_autoRestart->isChecked();
    s.theme = m_theme->currentData().toString();
    if (s.theme.isEmpty()) {
        s.theme = QStringLiteral("system");
    }
    s.soundOnComplete = m_soundOnComplete->isChecked();
    s.soundVolume = m_soundVolume->value();
    // ---- 搜索（下拉框取 itemData 里的字符串值，找不到时回落到出厂值）----
    s.defaultAlgorithm = m_defaultAlgorithm->currentData().toString();
    if (s.defaultAlgorithm.isEmpty()) {
        s.defaultAlgorithm = QStringLiteral("exact");
    }
    s.defaultThreads = m_defaultThreads->currentData().toInt(); // 0 = 自动
    s.defaultTargetCount = m_defaultTargetCount->value();
    s.defaultPreset = m_defaultPreset->currentData().toString();
    if (s.defaultPreset.isEmpty()) {
        s.defaultPreset = QStringLiteral("none");
    }
    s.defaultGpu = m_defaultGpu->isChecked();
    s.gpuPatternStream = m_gpuPatternStream && m_gpuPatternStream->isChecked();
    s.gpuResultSelfOptimize = m_gpuResultSelfOptimize && m_gpuResultSelfOptimize->isChecked();
    s.defaultStopAtTarget = m_defaultStopAtTarget->isChecked();
    s.showGpuReminder = m_showGpuReminder->isChecked();
    s.largeRangeWarnChunks = m_largeRangeWarn->value();
    // ---- 缓存 ----
    s.autoLoadCache = m_autoLoadCache->isChecked();
    s.cacheDir = m_cacheDir->text().trimmed(); // 空 = 程序目录
    s.cacheMaxMB = m_cacheMaxMB->value();
    // ---- 日志 ----
    s.logAtStartup = m_logAtStartup->isChecked();
    s.logLevel = m_logLevelDetail->isChecked() ? 1 : 0;
    s.logSubDir = m_logSubDir->text().trimmed();
    if (s.logSubDir.isEmpty()) {
        s.logSubDir = QStringLiteral("logs");
    }
    s.logKeepDays = m_logKeepDays->value();
    s.logAutoScroll = m_logAutoScroll->isChecked();
    s.logMaxLines = m_logMaxLines->value();
    // ---- 高级 ----
    s.cpuLimitPercent = m_cpuLimit->value();
    s.gpuLimitPercent = m_gpuLimit->value();
    s.memLimitMB = m_memLimit->value();
    s.autoCheckGpuAtStartup = m_autoCheckGpu->isChecked();
    s.updateReminderDays = m_updateReminderDays->value();
    s.crashDump = m_crashDump->isChecked();
    s.debugMode = m_debugMode->isChecked();
    if (mLanguage) s.language = mLanguage->currentData().toString();
    if (mFullscreenMode) s.fullscreenMode = mFullscreenMode->currentData().toString();
    if (m_ringInner) s.afkRingInner = m_ringInner->value();
    if (m_ringOuter) s.afkRingOuter = m_ringOuter->value();
    if (m_ringCoarseStep) s.ringCoarseStep = m_ringCoarseStep->value();
    if (m_ringStatBands) s.ringStatBands = m_ringStatBands->value();
    return s;
}

void SettingsDialog::setValues(const AppSettings& s)
{
    // ---- 通用 ----
    m_wheelAdjust->setChecked(s.wheelAdjust);
    m_rememberWindow->setChecked(s.rememberWindow);
    if (m_autoRestart) m_autoRestart->setChecked(s.autoRestart);
    // 主题：未知值回落到出厂值 "system"（跟随系统），不会被误解成「浅色」
    selectComboData(m_theme, s.theme, factoryDefaults().theme);
    m_soundOnComplete->setChecked(s.soundOnComplete);
    m_soundVolume->setValue(s.soundVolume); // 值变化时 valueChanged 已刷新文字
    onVolumeChanged(m_soundVolume->value()); // 值未变化（如上一次相同）时也保证文字一致

    // ---- 搜索：按值查下标，未知值一律回落到出厂默认项，保证界面始终有合法选中项 ----
    const AppSettings def = factoryDefaults();
    selectComboData(m_defaultAlgorithm, s.defaultAlgorithm, def.defaultAlgorithm);
    selectComboData(m_defaultThreads, QString::number(s.defaultThreads), QString::number(def.defaultThreads));
    m_defaultTargetCount->setValue(s.defaultTargetCount);
    selectComboData(m_defaultPreset, s.defaultPreset, def.defaultPreset);
    m_defaultGpu->setChecked(s.defaultGpu);
    if (m_gpuPatternStream) m_gpuPatternStream->setChecked(s.gpuPatternStream);
    if (m_gpuResultSelfOptimize) m_gpuResultSelfOptimize->setChecked(s.gpuResultSelfOptimize);
    m_defaultStopAtTarget->setChecked(s.defaultStopAtTarget);
    m_showGpuReminder->setChecked(s.showGpuReminder);
    m_largeRangeWarn->setValue(s.largeRangeWarnChunks);

    // ---- 缓存 ----
    m_autoLoadCache->setChecked(s.autoLoadCache);
    m_cacheDir->setText(s.cacheDir);
    m_cacheMaxMB->setValue(s.cacheMaxMB);

    // ---- 日志 ----
    m_logAtStartup->setChecked(s.logAtStartup);
    m_logLevelBrief->setChecked(s.logLevel != 1);
    m_logLevelDetail->setChecked(s.logLevel == 1);
    m_logSubDir->setText(s.logSubDir);
    m_logKeepDays->setValue(s.logKeepDays);
    m_logAutoScroll->setChecked(s.logAutoScroll);
    m_logMaxLines->setValue(s.logMaxLines);

    // ---- 高级 ----
    m_cpuLimit->setValue(s.cpuLimitPercent);
    m_gpuLimit->setValue(s.gpuLimitPercent);
    m_memLimit->setValue(s.memLimitMB);
    m_autoCheckGpu->setChecked(s.autoCheckGpuAtStartup);
    m_updateReminderDays->setValue(s.updateReminderDays);
    m_crashDump->setChecked(s.crashDump);
    if (mLanguage) {
        const int li = mLanguage->findData(s.language);
        mLanguage->setCurrentIndex(li >= 0 ? li : 0);
    }
    if (mFullscreenMode) {
        const int fi = mFullscreenMode->findData(s.fullscreenMode);
        mFullscreenMode->setCurrentIndex(fi >= 0 ? fi : 0);
    }
    m_debugMode->setChecked(s.debugMode);
    if (m_ringInner) m_ringInner->setValue(s.afkRingInner);
    if (m_ringOuter) m_ringOuter->setValue(s.afkRingOuter);
    if (m_ringCoarseStep) m_ringCoarseStep->setValue(s.ringCoarseStep);
    if (m_ringStatBands) m_ringStatBands->setValue(s.ringStatBands);

    // 注：语言下拉框是禁用状态的预留控件，对应 AppSettings 里没有字段，故此处不涉及；
    //     AppSettings 的 25 个字段在上面的两个函数里全部有对应读/写。
}

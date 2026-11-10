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
#include "gui/UiText.h"
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
    setWindowTitle(slime::ui::t("ui.4424d94f070e"));
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
    m_tabs->addTab(wrapScroll(buildGeneralPage()), slime::ui::t("ui.1a0fdce8f88f"));
    m_tabs->addTab(wrapScroll(buildSearchPage()), slime::ui::t("ui.f04090805c6e"));
    m_tabs->addTab(wrapScroll(buildCachePage()), slime::ui::t("ui.6cdba4aad0c1"));
    m_tabs->addTab(wrapScroll(buildLogPage()), slime::ui::t("ui.4de50894b8c1"));
    m_tabs->addTab(wrapScroll(buildAdvancedPage()), slime::ui::t("ui.c009d0ab82f9"));
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
        QGridLayout* form = addGroup(lay, page, slime::ui::t("ui.11dc46ad2968"), 0);
        int row = 0;

        // 允许鼠标滚轮调整输入框数值
        m_wheelAdjust = new QCheckBox(slime::ui::t("ui.09e31f95953e"), page);
        // v3.5.19：界面语言（文案走 UiStrings.h，可扩展更多语言）
    mLanguage = new QComboBox(page);
    mLanguage->addItem(slime::ui::t("ui.93659150d0e5"), QStringLiteral("zh"));
    mLanguage->addItem(QStringLiteral("English"), QStringLiteral("en"));
    // v3.5.20：全屏方式（无边框全屏 / 窗口最大化）
    mFullscreenMode = new QComboBox(page);
    mFullscreenMode->addItem(slime::ui::t("ui.2a0936486d4a"), QStringLiteral("borderless"));
    mFullscreenMode->addItem(slime::ui::t("ui.cd795bf4ebc9"), QStringLiteral("windowed"));
    row = addOptionRow(form, row, slime::ui::t("ui.986948088c99"), mFullscreenMode,
                       slime::ui::t("ui.19aae383f665"));

    row = addOptionRow(form, row, slime::ui::t("ui.5f44341e655a"), mLanguage,
                       slime::ui::t("ui.3ce5159878c4"));

    row = addOptionRow(form, row, QString(), m_wheelAdjust,
                           slime::ui::t("ui.2206d559f0ed"));

        // 窗口大小记忆
        m_rememberWindow = new QCheckBox(slime::ui::t("ui.0ae76927ddb2"), page);
        row = addOptionRow(form, row, QString(), m_rememberWindow,
                           slime::ui::t("ui.de897868b958"));

        // v3.6.6（用户要求）：有些设置必须重启才生效 —— 可以设置成改完自动重启
        m_autoRestart = new QCheckBox(slime::ui::t("ui.0c8480b64be2"), page);
        row = addOptionRow(form, row, QString(), m_autoRestart,
                           slime::ui::t("ui.80ec3930cc1f"));
    }

    // ---- 外观：主题三档（itemData 即 AppSettings::theme 的取值）----
    {
        QGridLayout* form = addGroup(lay, page, slime::ui::t("ui.09b58aa3422c"), 60);
        int row = 0;

        m_theme = new QComboBox(page);
        m_theme->addItem(slime::ui::t("ui.80ec9e2b1bc4"), QStringLiteral("light"));
        m_theme->addItem(slime::ui::t("ui.30b2c979ac87"), QStringLiteral("dark"));
        m_theme->addItem(slime::ui::t("ui.f4bbd91f79ba"), QStringLiteral("system"));
        row = addOptionRow(form, row, slime::ui::t("ui.2f68fa962c0c"), m_theme,
                           slime::ui::t("ui.88eaa31025dc"));
    }

    // ---- 声音：完成提示音开关 + 音量滑块（百分比实时显示）----
    {
        QGridLayout* form = addGroup(lay, page, slime::ui::t("ui.ab9ff81dac9b"), 60);
        int row = 0;

        m_soundOnComplete = new QCheckBox(slime::ui::t("ui.d25b02c5d1ee"), page);
        row = addOptionRow(form, row, QString(), m_soundOnComplete,
                           slime::ui::t("ui.4eff1d92a979"));

        m_soundVolume = new QSlider(Qt::Horizontal, page);
        m_soundVolume->setRange(0, 100);
        m_soundVolume->setSingleStep(5);
        m_soundVolume->setPageStep(10);
        m_soundVolume->setMinimumWidth(200);
        m_soundVolumeText = new QLabel(page);
        m_soundVolumeText->setMinimumWidth(40); // 固定最小宽度：百分比变化时布局不跳动
        onVolumeChanged(m_soundVolume->value()); // 先填一次文字；之后由 valueChanged 更新
        row = addOptionRow(form, row, slime::ui::t("ui.856527075c85"),
                           pairWidget(m_soundVolume, m_soundVolumeText, page),
                           slime::ui::t("ui.448b72e4377e"));
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
        QGridLayout* form = addGroup(lay, page, slime::ui::t("ui.d93919c37b61"), 96);
        int row = 0;

        // 默认搜索模式
        m_defaultAlgorithm = new QComboBox(page);
        m_defaultAlgorithm->addItem(slime::ui::t("ui.73b75d28b4b7"), QStringLiteral("exact"));
        m_defaultAlgorithm->addItem(slime::ui::t("ui.f18c79796e88"), QStringLiteral("layered"));
        m_defaultAlgorithm->addItem(slime::ui::t("ui.dcee497c44dd"), QStringLiteral("sample"));
        m_defaultAlgorithm->addItem(slime::ui::t("ui.9e6092059b8f"), QStringLiteral("brute"));
        row = addOptionRow(form, row, slime::ui::t("ui.f554286784b2"), m_defaultAlgorithm,
                           slime::ui::t("ui.f78a372beebf"));

        // 默认线程数（值 0 = 自动：全部逻辑核心；其后为 1..64 的具体数值）
        m_defaultThreads = new QComboBox(page);
        m_defaultThreads->addItem(slime::ui::t("ui.429bd0d9ada1"), 0);
        for (int n = 1; n <= 64; ++n) {
            m_defaultThreads->addItem(QString::number(n), n);
        }
        row = addOptionRow(form, row, slime::ui::t("ui.459b3f1d55cd"), m_defaultThreads,
                           slime::ui::t("ui.efad005368bf"));

        // 默认目标数量
        m_defaultTargetCount = new QSpinBox(page);
        m_defaultTargetCount->setRange(1, 1000000);
        row = addOptionRow(form, row, slime::ui::t("ui.c2488e6e8425"), m_defaultTargetCount,
                           slime::ui::t("ui.84266b3227dc"));

        // 默认范围预设
        m_defaultPreset = new QComboBox(page);
        m_defaultPreset->addItem(slime::ui::t("ui.0c4c3bfaba10"), QStringLiteral("none"));
        m_defaultPreset->addItem(slime::ui::t("ui.08c2940d3842"), QStringLiteral("near"));
        m_defaultPreset->addItem(slime::ui::t("ui.d4548049015d"), QStringLiteral("local"));
        m_defaultPreset->addItem(slime::ui::t("ui.a5644f4bbfef"), QStringLiteral("global"));
        m_defaultPreset->addItem(slime::ui::t("ui.3a7839501e82"), QStringLiteral("world"));
        m_defaultPreset->addItem(slime::ui::t("ui.c2fa11e4787c"), QStringLiteral("bedrock"));
        row = addOptionRow(form, row, slime::ui::t("ui.865bb17ab029"), m_defaultPreset,
                           slime::ui::t("ui.4dcedae6e410"));

        // 默认启用 GPU 加速
        m_defaultGpu = new QCheckBox(slime::ui::t("ui.a2c3a0944d01"), page);
    // v3.8.11：图案流式开关（实验转正；仅 AMD 显卡验证过）
    m_gpuPatternStream = new QCheckBox(
        slime::ui::t("ui.d1712c9a5036"),
        page);
    m_gpuPatternStream->setToolTip(slime::ui::t("ui.dfb54fcfc94b"));
    // v3.10.11.1：结果自优化（默认关；只影响速度与"已保留数量"统计，结果内容不变）
    m_gpuResultSelfOptimize = new QCheckBox(
        slime::ui::t("ui.a62e0463529b"), page);
    m_gpuResultSelfOptimize->setToolTip(slime::ui::t("ui.17d74c302717"));
        row = addOptionRow(form, row, QString(), m_gpuResultSelfOptimize,
                           slime::ui::t("ui.d46582642f1b"));
        row = addOptionRow(form, row, QString(), m_gpuPatternStream,
                           slime::ui::t("ui.bf4c59f8fc39"));
        row = addOptionRow(form, row, QString(), m_defaultGpu,
                           slime::ui::t("ui.764cc3ac458f"));
        {
            auto* starNote = new QLabel(slime::ui::t("ui.e31ae720ef3f"), page);
            starNote->setStyleSheet(QStringLiteral("color:#666;font-size:8pt;"));
            form->addWidget(starNote, row, 0, 1, 3);
            ++row;
        }

        // 默认开启及时反馈
        m_defaultStopAtTarget = new QCheckBox(slime::ui::t("ui.b6500cee58cb"), page);
        row = addOptionRow(form, row, QString(), m_defaultStopAtTarget,
                           slime::ui::t("ui.324dce9760ed"));
    }

    // ---- 提醒：GPU 提醒开关 + 大范围搜索警告阈值 ----
    {
        QGridLayout* form = addGroup(lay, page, slime::ui::t("ui.81944e48a3b4"), 96);
        int row = 0;

        // 显示 GPU 加速提醒
        m_showGpuReminder = new QCheckBox(slime::ui::t("ui.1d9409096661"), page);
        row = addOptionRow(form, row, QString(), m_showGpuReminder,
                           slime::ui::t("ui.9df2bbd65acb"));

        // 大范围搜索警告阈值（默认 1 亿区块）
        m_largeRangeWarn = new QSpinBox(page);
        m_largeRangeWarn->setRange(1000000, 2000000000);
        m_largeRangeWarn->setSingleStep(10000000);
        row = addOptionRow(form, row, slime::ui::t("ui.3b049db3f45a"), withUnit(m_largeRangeWarn, slime::ui::t("ui.3da82d85c5de")),
                           slime::ui::t("ui.f475d975dce0"));
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
    m_autoLoadCache = new QCheckBox(slime::ui::t("ui.479f490966ed"), page);
    row = addOptionRow(form, row, QString(), m_autoLoadCache,
                       slime::ui::t("ui.dbc7674fcc55"));

    // 缓存目录（留空 = 程序目录）
    m_cacheDir = new QLineEdit(page);
    m_cacheDir->setPlaceholderText(slime::ui::t("ui.bd1e0668f5f7"));
    m_cacheDir->setClearButtonEnabled(true);
    m_browseCacheDir = new QPushButton(slime::ui::t("ui.01463dd05cfd"), page);
    row = addOptionRow(form, row, slime::ui::t("ui.46bc75565583"), pairWidget(m_cacheDir, m_browseCacheDir, page),
                       slime::ui::t("ui.da60e8f2becf"));

    // 缓存容量上限
    m_cacheMaxMB = new QSpinBox(page);
    m_cacheMaxMB->setRange(1, 1048576);
    m_cacheMaxMB->setSingleStep(64);
    row = addOptionRow(form, row, slime::ui::t("ui.404d4b7b93cd"), withUnit(m_cacheMaxMB, QStringLiteral("MB")),
                       slime::ui::t("ui.7ef39cf15415"));

    lay->addLayout(form);

    // 清空所有缓存：只置位标记，真正的删除由调用方在「应用」时执行
    m_clearCacheBtn = new QPushButton(slime::ui::t("ui.eb678d4ebb45"), page);
    lay->addWidget(buttonRow(m_clearCacheBtn, page));
    lay->addWidget(grayNote(slime::ui::t("ui.a1ddb350e1c0"), page));

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
    m_logAtStartup = new QCheckBox(slime::ui::t("ui.2e2fcc576b60"), page);
    row = addOptionRow(form, row, QString(), m_logAtStartup,
                       slime::ui::t("ui.ee65030387a8"));

    // 日志级别：简要 / 详细（互斥）
    m_logLevelBrief = new QRadioButton(slime::ui::t("ui.f6bd95650e81"), page);
    m_logLevelDetail = new QRadioButton(slime::ui::t("ui.613f7a44e2d7"), page);
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
    addRow(form, row++, slime::ui::t("ui.c792234b89ab"), levelRow);
    form->addWidget(grayNote(slime::ui::t("ui.9bce7908813e"), page), row++, 1);
    form->addWidget(grayNote(slime::ui::t("ui.45564edd2d0f"), page), row++, 1);

    // 日志路径（相对程序目录的子目录名）
    m_logSubDir = new QLineEdit(page);
    m_logSubDir->setPlaceholderText(QStringLiteral("logs"));
    m_logSubDir->setClearButtonEnabled(true);
    m_browseLogDir = new QPushButton(slime::ui::t("ui.01463dd05cfd"), page);
    row = addOptionRow(form, row, slime::ui::t("ui.992ebf7cb1eb"), pairWidget(m_logSubDir, m_browseLogDir, page),
                       slime::ui::t("ui.835d79e59522"));

    // 保留天数
    m_logKeepDays = new QSpinBox(page);
    m_logKeepDays->setRange(1, 3650);
    row = addOptionRow(form, row, slime::ui::t("ui.ee97f1297396"), withUnit(m_logKeepDays, slime::ui::t("ui.c3304d1e49e3")),
                       slime::ui::t("ui.62f20091f5a7"));

    // 日志自动滚动（v3.3.6 新增）
    m_logAutoScroll = new QCheckBox(slime::ui::t("ui.1519e985fa7e"), page);
    row = addOptionRow(form, row, QString(), m_logAutoScroll,
                       slime::ui::t("ui.13e3fceedcf3"));

    // 界面最大行数（v3.3.6 新增）
    m_logMaxLines = new QSpinBox(page);
    m_logMaxLines->setRange(1000, 200000);
    m_logMaxLines->setSingleStep(1000);
    row = addOptionRow(form, row, slime::ui::t("ui.ca3b6ed0e43c"), m_logMaxLines,
                       slime::ui::t("ui.8444c537834e"));

    lay->addLayout(form);

    // 打开日志目录：同样只置位标记，交由调用方在「应用」时执行
    m_openLogDirBtn = new QPushButton(slime::ui::t("ui.e153b1be0e3e"), page);
    lay->addWidget(buttonRow(m_openLogDirBtn, page));
    lay->addWidget(grayNote(slime::ui::t("ui.02266b68bf12"), page));

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
    row = addOptionRow(form, row, slime::ui::t("ui.9be6e602e6c1"), withUnit(m_cpuLimit, QStringLiteral("%")),
                       slime::ui::t("ui.24db52705831"));

    // v3.7.1：GPU 占用率上限（瓦片之间按占空比休眠）
    m_gpuLimit = new QSpinBox(page);
    m_gpuLimit->setRange(10, 100);
    row = addOptionRow(form, row, slime::ui::t("ui.c4351eb2a3bc"),
                       withUnit(m_gpuLimit, QStringLiteral("%")),
                       slime::ui::t("ui.14cf5323f865"));
    m_gpuLimit->setEnabled(m_defaultGpu->isChecked());
    QObject::connect(m_defaultGpu, &QCheckBox::toggled, m_gpuLimit,
                     [this](bool on) { if (m_gpuLimit) m_gpuLimit->setEnabled(on); });

    // v3.3.9（任务书 2.1）：内存上限
    m_memLimit = new QSpinBox(page);
    m_memLimit->setRange(0, 32768);
    m_memLimit->setSpecialValueText(slime::ui::t("ui.4afad877551a"));
    row = addOptionRow(form, row, slime::ui::t("ui.17f9c0ba9737"), withUnit(m_memLimit, QStringLiteral("MB")),
                       slime::ui::t("ui.558c9c678bd5"));

    // v3.3.9.1（任务书 2.1）：启动时自动检查 GPU 状态
    m_autoCheckGpu = new QCheckBox(slime::ui::t("ui.52da0772de3c"), page);
    row = addOptionRow(form, row, slime::ui::t("ui.be04dbe3dfd8"), m_autoCheckGpu,
                       slime::ui::tr(slime::ui::t("ui.32eda8324001")));
    m_autoCheckGpu->setChecked(false); // 默认关闭：无/异常 OpenCL 驱动下探测有原生崩溃风险

    // 更新提醒天数
    m_updateReminderDays = new QSpinBox(page);
    m_updateReminderDays->setRange(1, 3650);
    row = addOptionRow(form, row, slime::ui::t("ui.77f9ed9298f7"), withUnit(m_updateReminderDays, slime::ui::t("ui.c3304d1e49e3")),
                       slime::ui::t("ui.a118d9b38ff3"));

    // 崩溃时生成转储文件（v3.3.6 新增）
    m_crashDump = new QCheckBox(slime::ui::t("ui.1153d028fe6d"), page);
    row = addOptionRow(form, row, QString(), m_crashDump,
                       slime::ui::t("ui.aee38865f067"));

    // 调试模式（v3.3.6 新增）
    m_debugMode = new QCheckBox(slime::ui::t("ui.0ee208982779"), page);
    row = addOptionRow(form, row, QString(), m_debugMode,
                       slime::ui::t("ui.8af4829cde61"));

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
                        withUnit(m_ringInner, slime::ui::t("ui.452463f58021")),
                        slime::ui::text(slime::ui::Key::RingInnerTip));
    m_ringOuter = new QSpinBox(page);
    m_ringOuter->setRange(16, 1024);
    arow = addOptionRow(advForm, arow, slime::ui::text(slime::ui::Key::RingOuter),
                        withUnit(m_ringOuter, slime::ui::t("ui.452463f58021")),
                        slime::ui::text(slime::ui::Key::RingOuterTip));
    m_ringCoarseStep = new QSpinBox(page);
    m_ringCoarseStep->setRange(1, 64);
    arow = addOptionRow(advForm, arow, slime::ui::text(slime::ui::Key::RingCoarseStep),
                        withUnit(m_ringCoarseStep, slime::ui::t("ui.452463f58021")),
                        slime::ui::text(slime::ui::Key::RingCoarseStepTip));
    m_ringStatBands = new QSpinBox(page);
    m_ringStatBands->setRange(1, 16);
    arow = addOptionRow(advForm, arow, slime::ui::text(slime::ui::Key::StatBands), m_ringStatBands,
                        slime::ui::text(slime::ui::Key::StatBandsTip));
    lay->addLayout(advForm);

    lay->addLayout(form);

    // 重置所有设置：把五个页面全部填回出厂默认值，但不应用、不关闭（用户仍需点「应用」）
    m_resetAllBtn = new QPushButton(slime::ui::t("ui.ad5d566fb4a3"), page);
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
    auto* lb = new QLabel(slime::ui::t("ui.03515a932d6b"),
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

    m_restoreBtn = new QPushButton(slime::ui::t("ui.a191935bc678"), this);
    m_restoreBtn->setToolTip(slime::ui::t("ui.1edc26c00ab0"));
    row->addWidget(m_restoreBtn);

    row->addStretch(1);

    m_cancelBtn = new QPushButton(slime::ui::t("ui.4d0b4688c787"), this);
    m_cancelBtn->setToolTip(slime::ui::t("ui.f1bf6149fc7c"));
    row->addWidget(m_cancelBtn);

    m_applyBtn = new QPushButton(slime::ui::t("ui.4562024ddec7"), this);
    m_applyBtn->setDefault(true); // 「应用」为默认按钮（回车即应用）
    m_applyBtn->setAutoDefault(true);
    m_applyBtn->setToolTip(slime::ui::t("ui.4d6f927eb818"));
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
        this, slime::ui::t("ui.986887674e43"),
        m_cacheDir->text().isEmpty() ? QDir::currentPath() : m_cacheDir->text());
    if (!dir.isEmpty()) {
        m_cacheDir->setText(QDir::toNativeSeparators(dir));
    }
}

void SettingsDialog::onBrowseLogDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, slime::ui::t("ui.bbb695ee7b6d"),
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

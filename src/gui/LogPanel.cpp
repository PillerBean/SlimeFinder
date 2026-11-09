// LogPanel.cpp —— 底部「日志」标签页实现（v3.3.4 任务书 2.3）
//
// 设计要点：
//   1. 默认关闭（LogBus::Off）：只显示一行提示文字，工具行与文本视图都不显示
//      （任务书要求「日志栏默认只有这一行文字」）；
//   2. 500ms 节流刷新：QTimer 每 500ms 调一次 refreshFromBus()，空闲时不做任何重建；
//      另外注册 LogBus 监听器做「即时但限频」的补充刷新（跨线程只投递事件，界面操作仍在 GUI 线程）；
//   3. 刷新策略：环形缓冲没有丢条目时只 append 新增行（避免每 500ms 重建上万行导致卡顿），
//      一旦发生丢弃 / 过滤级别变化 / 清空，就整体重建；
//   4. 提示文字与启用对话框的选项文案均与任务书逐字一致（UTF-8 源码 + QString::fromUtf8）。
#include "gui/LogPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>
#include <string>
#include <vector>

namespace {

// 一条日志在界面上的固定前缀（时间 + 级别，纯 ASCII，避免任何编码问题）
QString entryPrefix(const slime::LogEntry& e)
{
    char buf[64] = {0};
    std::snprintf(buf, sizeof(buf), "[%9.3fs][%-5s] ", e.t, slime::LogBus::levelName(e.level));
    return QString::fromLatin1(buf);
}

// 级别过滤：下拉框选「全部」时 minLevel < 0；否则只显示不低于所选级别的条目
bool passesFilter(const slime::LogEntry& e, int minLevel)
{
    return minLevel < 0 || static_cast<int>(e.level) >= minLevel;
}

} // namespace

LogPanel::LogPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 6);
    root->setSpacing(4);

    // ---- 关闭模式唯一可见的一行提示（文案与任务书逐字一致）----
    m_hint = new QLabel(QString::fromUtf8("日志未启用。如需启用，请到 [工具] 菜单 → [启用日志]"), this);
    m_hint->setWordWrap(true);
    m_hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_hint);

    // ---- 状态行（启用后显示：当前模式 / 内存条数 / 文件路径）----
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_status);

    // ---- 工具行（启用后才显示）----
    m_toolbar = new QWidget(this);
    auto* bar = new QHBoxLayout(m_toolbar);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(8);

    bar->addWidget(new QLabel(QString::fromUtf8("级别："), m_toolbar));
    m_level = new QComboBox(m_toolbar);
    m_level->addItem(QString::fromUtf8("全部"), -1);
    m_level->addItem(QString::fromUtf8("TRACE"), static_cast<int>(slime::LogLevel::Trace));
    m_level->addItem(QString::fromUtf8("DEBUG"), static_cast<int>(slime::LogLevel::Debug));
    m_level->addItem(QString::fromUtf8("INFO"), static_cast<int>(slime::LogLevel::Info));
    m_level->addItem(QString::fromUtf8("WARN"), static_cast<int>(slime::LogLevel::Warning));
    m_level->addItem(QString::fromUtf8("ERROR"), static_cast<int>(slime::LogLevel::Error));
    m_level->setToolTip(QString::fromUtf8("只显示不低于所选级别的日志（选「全部」则不过滤）"));
    bar->addWidget(m_level);

    m_autoScroll = new QCheckBox(QString::fromUtf8("自动滚动"), m_toolbar);
    m_autoScroll->setChecked(true); // 默认勾选
    m_autoScroll->setToolTip(QString::fromUtf8("勾选后每次刷新都滚到最新一行"));
    bar->addWidget(m_autoScroll);

    m_clear = new QPushButton(QString::fromUtf8("清空"), m_toolbar);
    m_clear->setToolTip(QString::fromUtf8("清空内存中的日志（不影响已写入的日志文件）"));
    bar->addWidget(m_clear);

    m_copy = new QPushButton(QString::fromUtf8("复制全部"), m_toolbar);
    m_copy->setToolTip(QString::fromUtf8("把当前显示的日志复制到剪贴板"));
    bar->addWidget(m_copy);

    m_enable = new QPushButton(QString::fromUtf8("启用日志…"), m_toolbar);
    m_enable->setToolTip(QString::fromUtf8("重新选择日志模式（简要 / 详细）"));
    bar->addWidget(m_enable);

    bar->addStretch(1);
    root->addWidget(m_toolbar);

    // ---- 只读文本视图：等宽字体；条数上限由 LogBus 环形缓冲保证，
    //      因此这里刻意不调用 setMaximumBlockCount（避免与 LogBus 的上限重复/冲突）----
    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setUndoRedoEnabled(false);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    root->addWidget(m_view, 1);

    // ---- 交互 ----
    connect(m_level, &QComboBox::currentIndexChanged, this, [this](int) {
        resetRenderedState();
        refreshFromBus();
    });
    connect(m_clear, &QPushButton::clicked, this, [this] {
        slime::LogBus::clear();
        resetRenderedState();
        refreshFromBus();
    });
    connect(m_copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(filteredText());
    });
    connect(m_enable, &QPushButton::clicked, this, &LogPanel::enableRequested);

    // ---- 500ms 节流刷新（任务书：界面节流刷新 500ms）----
    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &LogPanel::refreshFromBus);
    m_timer->start();

    // ---- LogBus 监听器：可能来自工作线程（含搜索线程），
    //      这里只「合并 + 投递」一个排队事件，真正的界面操作仍在 GUI 线程执行 ----
    slime::LogBus::setListener([this] {
        if (m_refreshPosted.exchange(true)) return; // 已有一个待处理刷新：合并
        QMetaObject::invokeMethod(this, [this] {
            m_refreshPosted.store(false);
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_lastRefreshMs != 0 && now - m_lastRefreshMs < 200) return; // 200ms 内不重复重建
            refreshFromBus();
        }, Qt::QueuedConnection);
    });

    // 首次按当前模式摆放界面（默认 Off → 只显示提示行）
    applyModeVisibility();
    refreshFromBus();
}

LogPanel::~LogPanel()
{
    // 面板销毁后不再回调（监听器捕获了 this，必须清掉，避免工作线程回调悬空指针）
    slime::LogBus::setListener(nullptr);
    if (m_timer) m_timer->stop();
}

bool LogPanel::isLoggingEnabled() const
{
    return slime::LogBus::enabled();
}

QPlainTextEdit* LogPanel::view() const
{
    return m_view;
}

QLabel* LogPanel::hintLabel() const
{
    return m_hint;
}

// ---- v3.3.6（任务书 4.5）：设置界面驱动的显示参数 ----
void LogPanel::setAutoScroll(bool on)
{
    if (m_autoScroll) m_autoScroll->setChecked(on);
}

bool LogPanel::autoScroll() const
{
    return m_autoScroll && m_autoScroll->isChecked();
}

void LogPanel::setMaxLines(int lines)
{
    m_maxLines = lines > 0 ? lines : 0;
    if (m_view) enforceMaxLines();
}

int LogPanel::maxLines() const
{
    return m_maxLines;
}

// 超过上限时从**最旧**（文档开头）丢弃，保留最新 m_maxLines 行。
// 只动文档前部，因此增量追加逻辑（依赖末尾条目不变）仍然成立。
void LogPanel::enforceMaxLines()
{
    if (m_maxLines <= 0 || !m_view) return;
    const int blocks = m_view->document()->blockCount();
    if (blocks <= m_maxLines) return;
    const int drop = blocks - m_maxLines;
    QTextCursor cur(m_view->document());
    cur.movePosition(QTextCursor::Start);
    cur.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, drop);
    cur.removeSelectedText();
}

void LogPanel::applyModeVisibility()
{
    const bool on = slime::LogBus::enabled();
    m_hint->setVisible(!on);   // 关闭：只剩这一行提示
    m_status->setVisible(on);
    m_toolbar->setVisible(on);
    m_view->setVisible(on);
}

void LogPanel::refreshFromBus()
{
    applyModeVisibility();
    if (!slime::LogBus::enabled()) return; // 关闭模式：不拉取、不重建
    rebuildView();
    m_lastRefreshMs = QDateTime::currentMSecsSinceEpoch();
}

void LogPanel::resetRenderedState()
{
    m_rendered = false;
    m_renderedCount = 0;
    m_renderedDropped = 0;
    m_renderedFilter = 0;
    m_renderedTailT = 0.0;
    m_renderedTailLevel = -1;
    m_renderedTailText.clear();
}

void LogPanel::rebuildView()
{
    const std::vector<slime::LogEntry> entries = slime::LogBus::snapshot();
    const int64_t dropped = slime::LogBus::droppedCount();
    const int minLevel = m_level->currentData().toInt();

    // 可以「只追加」的条件：过滤级别没变、没有因上限丢条目、且上次消费到的末尾条目仍然是同一条
    // （t + 级别 + 文本三重比对，确保环形缓冲前端没有滑动；任一不满足就整体重建）
    const bool canAppend = m_rendered
        && m_renderedFilter == minLevel
        && m_renderedDropped == dropped
        && m_renderedCount > 0
        && m_renderedCount <= entries.size()
        && entries[m_renderedCount - 1].level == static_cast<slime::LogLevel>(m_renderedTailLevel)
        && entries[m_renderedCount - 1].t == m_renderedTailT
        && entries[m_renderedCount - 1].text == m_renderedTailText;

    const size_t first = canAppend ? m_renderedCount : 0;
    QString add;
    add.reserve(static_cast<int>(entries.size() - first) * 96);
    for (size_t i = first; i < entries.size(); ++i) {
        const slime::LogEntry& e = entries[i];
        if (!passesFilter(e, minLevel)) continue;
        add += entryPrefix(e);
        add += QString::fromStdString(e.text);
        add += QLatin1Char('\n');
    }

    const bool hasNew = !add.isEmpty();
    const bool scroll = m_autoScroll->isChecked();
    const int keepScroll = m_view->verticalScrollBar()->value();

    if (canAppend) {
        if (hasNew) {
            add.chop(1); // 去掉最后多出来的换行，避免尾部出现空行
            m_view->appendPlainText(add); // 只追加新增行：界面开销与新增条数成正比
            if (scroll) {
                m_view->moveCursor(QTextCursor::End); // 自动滚动到最新一行
            } else {
                m_view->verticalScrollBar()->setValue(keepScroll); // 保持用户当前浏览位置
            }
        }
    } else {
        // 整体重建：即使过滤后一条不剩也要写入文本（「清空」或切换过滤级别后视图必须立刻同步）
        if (hasNew) add.chop(1);
        m_view->setUpdatesEnabled(false);
        m_view->setPlainText(add);
        m_view->setUpdatesEnabled(true);
        if (scroll) {
            m_view->moveCursor(QTextCursor::End);
        } else {
            m_view->verticalScrollBar()->setValue(keepScroll);
        }
    }

    // 记录本次消费进度（始终消费到 snapshot 末尾）
    m_renderedFilter = minLevel;
    m_renderedDropped = dropped;
    if (entries.empty()) {
        m_rendered = false;
        m_renderedCount = 0;
        m_renderedTailT = 0.0;
        m_renderedTailLevel = -1;
        m_renderedTailText.clear();
    } else {
        const slime::LogEntry& tail = entries.back();
        m_rendered = true;
        m_renderedCount = entries.size();
        m_renderedTailT = tail.t;
        m_renderedTailLevel = static_cast<int>(tail.level);
        m_renderedTailText = tail.text;
    }

    updateStatusLabel(entries.size(), dropped);
    enforceMaxLines(); // v3.3.6：界面最大行数上限（超过丢弃最旧）
}

void LogPanel::updateStatusLabel(size_t total, int64_t dropped)
{
    QString modeText;
    switch (slime::LogBus::mode()) {
    case slime::LogMode::Brief:    modeText = QString::fromUtf8("简要（仅 INFO 及以上）"); break;
    case slime::LogMode::Detailed: modeText = QString::fromUtf8("详细（含 DEBUG / TRACE）"); break;
    default:                       modeText = QString::fromUtf8("关闭"); break;
    }

    QString file = QString::fromStdString(slime::LogBus::currentFile());
    if (file.isEmpty()) file = QString::fromUtf8("（未设置日志目录）");

    QString text = QString::fromUtf8("当前模式：") + modeText
                 + QString::fromUtf8("｜ 内存 ") + QString::number(static_cast<qulonglong>(total))
                 + QString::fromUtf8(" / ") + QString::number(static_cast<qulonglong>(slime::LogBus::kMaxEntries))
                 + QString::fromUtf8(" 条 ｜ 文件：") + file;
    if (dropped > 0) {
        text += QString::fromUtf8(" ｜ 已丢弃 ") + QString::number(static_cast<qlonglong>(dropped))
              + QString::fromUtf8(" 条（仅保留最新 10000 条）");
    }
    m_status->setText(text);
}

QString LogPanel::filteredText() const
{
    const std::vector<slime::LogEntry> entries = slime::LogBus::snapshot();
    const int minLevel = m_level->currentData().toInt();

    QString text;
    text.reserve(static_cast<int>(entries.size()) * 96);
    for (const slime::LogEntry& e : entries) {
        if (!passesFilter(e, minLevel)) continue;
        text += entryPrefix(e);
        text += QString::fromStdString(e.text);
        text += QLatin1Char('\n');
    }
    return text;
}

// ---------------------------------------------------------------- 启用日志对话框

bool showEnableLogDialog(QWidget* parent, slime::LogMode* outMode)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QString::fromUtf8("启用日志"));
    dlg.setModal(true);

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(16, 14, 16, 12);
    root->setSpacing(6);

    // 选项一：简要日志（默认选中）
    auto* brief = new QRadioButton(QString::fromUtf8("简要日志（推荐）"), &dlg);
    brief->setChecked(true);
    auto* briefLine1 = new QLabel(QString::fromUtf8("记录关键事件（启动、搜索、错误）"), &dlg);
    auto* briefLine2 = new QLabel(QString::fromUtf8("性能影响：可忽略"), &dlg);

    // 选项二：详细日志
    auto* detailed = new QRadioButton(QString::fromUtf8("详细日志"), &dlg);
    auto* detailLine1 = new QLabel(QString::fromUtf8("记录所有事件（含 DEBUG / TRACE）"), &dlg);
    auto* detailLine2 = new QLabel(QString::fromUtf8("⚠️ 可能降低搜索速度 10~30%"), &dlg);

    // 说明文字缩进，视觉上从属于上面的单选项（同一个父窗口 → 两个单选项自动互斥）
    for (QLabel* line : { briefLine1, briefLine2, detailLine1, detailLine2 }) {
        line->setIndent(22);
        line->setWordWrap(true);
    }

    root->addWidget(brief);
    root->addWidget(briefLine1);
    root->addWidget(briefLine2);
    root->addSpacing(10);
    root->addWidget(detailed);
    root->addWidget(detailLine1);
    root->addWidget(detailLine2);
    root->addSpacing(10);

    auto* box = new QDialogButtonBox(&dlg);
    QPushButton* cancelBtn = box->addButton(QString::fromUtf8("取消"), QDialogButtonBox::RejectRole);
    QPushButton* okBtn = box->addButton(QString::fromUtf8("启用"), QDialogButtonBox::AcceptRole);
    okBtn->setDefault(true);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    root->addWidget(box);

    if (dlg.exec() != QDialog::Accepted) return false; // 取消：不改动任何状态

    const slime::LogMode choice = detailed->isChecked() ? slime::LogMode::Detailed
                                                       : slime::LogMode::Brief;
    slime::LogBus::setMode(choice);
    if (outMode) *outMode = choice;
    return true;
}

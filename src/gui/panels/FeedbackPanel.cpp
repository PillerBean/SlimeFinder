// FeedbackPanel.cpp - 底部「反馈」标签页实现
//
// 参考：v3.3.6 任务书 §三 调整 4（v3.3.4 任务书 2.2 的四级顺序仍决定级别优先级）。
//
// 行为要点：
//   * 不再有「错误 / 警告 / 信息 / 成功」分组：单一扁平列表，全部条目按时间顺序，
//     最新追加的在最下方（QTreeWidget::addTopLevelItem + scrollToItem）；
//   * 「成功」级被完全过滤：不显示、不计数、add() 返回 nullptr；
//   * 行格式：<图标> <HH:mm:ss> <消息>，图标为 ❌ / ⚠️ / ℹ️；
//     时间戳在插入时取一次并写入条目文本，allText() 直接读取该文本，
//     因此复制内容与显示行逐字一致；
//   * 级别只用前景色区分（错误偏红、警告橙棕、信息默认），不设置背景色，
//     以免覆盖应用的深色主题；
//   * 无过滤按钮、无级别显隐 UI：工具行仅有「清空」「复制全部」；
//   * 内存保护：最多保留 2000 条，超出时丢弃最旧（第一条）并同步修正计数。

#include "i18n/UiText.h"
#include "gui/panels/FeedbackPanel.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStringList>
#include <QTime>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

// 实际显示的级别下标：0 = 错误，1 = 警告，2 = 信息
constexpr int kErrorIndex = 0;
constexpr int kWarningIndex = 1;
constexpr int kInfoIndex = 2;
// 显示的级别数量（「成功」不占槽位：它永不显示、永不计数）
constexpr int kLevelSlots = 3;
// 反馈条目上限（内存保护）：超出时丢弃最旧的一条
constexpr int kMaxEntries = 2000;

// 条目私有数据角色：保存级别下标，供容量淘汰时修正计数
constexpr int kLevelRole = Qt::UserRole;

// 级别 -> 显示下标；「成功」与越界值返回 -1（表示该级别被过滤，永不显示）
int displayedLevelIndex(FeedbackLevel level)
{
    switch (level) {
    case FeedbackLevel::Error:
        return kErrorIndex;
    case FeedbackLevel::Warning:
        return kWarningIndex;
    case FeedbackLevel::Info:
        return kInfoIndex;
    case FeedbackLevel::Success:
    default:
        return -1;
    }
}

// 级别图标（后跟一个空格，再跟时间戳）
QString levelIcon(int index)
{
    switch (index) {
    case kErrorIndex:
        return QStringLiteral("❌");     // U+274C
    case kWarningIndex:
        return QStringLiteral("⚠️");    // U+26A0 U+FE0F
    case kInfoIndex:
        return QStringLiteral("ℹ️");    // U+2139 U+FE0F
    default:
        return QString();
    }
}

// 级别前景色：错误偏红、警告橙棕、信息用默认色（不设置前景色）；只改前景，不动背景
QColor levelColor(int index)
{
    switch (index) {
    case kErrorIndex:
        return QColor(0xC6, 0x28, 0x28);   // #c62828
    case kWarningIndex:
        return QColor(0xB2, 0x6A, 0x00);   // #b26a00
    default:
        return QColor();                    // 信息：无效颜色 -> 使用主题默认前景色
    }
}

}   // namespace

// ---------------------------------------------------------------------------
// 不透明实现：仅在本文件中可见
// ---------------------------------------------------------------------------
struct FeedbackPanel::Impl {
    QTreeWidget* tree = nullptr;         // 扁平反馈列表（单列、无表头）
    QPushButton* clearButton = nullptr;  // 「清空」
    QPushButton* copyButton = nullptr;   // 「复制全部」
    int counts[kLevelSlots] = {0, 0, 0}; // 错误 / 警告 / 信息 的条目数（成功级不计数）

    // 指定显示下标的条目数（越界返回 0）
    int countAt(int index) const
    {
        return (index >= 0 && index < kLevelSlots) ? counts[index] : 0;
    }

    // 实际显示的条目总数
    int total() const
    {
        int sum = 0;
        for (const int value : counts) {
            sum += value;
        }
        return sum;
    }
};

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------
FeedbackPanel::FeedbackPanel(QWidget* parent) : QWidget(parent), m_impl(new Impl)
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    // ---------------- 工具行：弹簧 + 清空 + 复制全部 ----------------
    auto* toolLayout = new QHBoxLayout();
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->setSpacing(6);
    toolLayout->addStretch(1);

    auto* clearBtn = new QPushButton(slime::ui::t("ui.84fcd70d4280"), this);
    clearBtn->setAutoDefault(false);
    clearBtn->setToolTip(slime::ui::t("ui.30e6eba7ba0d"));
    connect(clearBtn, &QPushButton::clicked, this, &FeedbackPanel::clearAll);
    toolLayout->addWidget(clearBtn);
    m_impl->clearButton = clearBtn;

    auto* copyBtn = new QPushButton(slime::ui::t("ui.55f59bc2f0c3"), this);
    copyBtn->setAutoDefault(false);
    copyBtn->setToolTip(slime::ui::t("ui.9b1fcc8158b8"));
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        if (QClipboard* clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(allText());
        }
    });
    toolLayout->addWidget(copyBtn);
    m_impl->copyButton = copyBtn;

    rootLayout->addLayout(toolLayout);

    // ---------------- 扁平反馈列表 ----------------
    auto* tree = new QTreeWidget(this);
    tree->setObjectName(QStringLiteral("feedbackList"));
    tree->setColumnCount(1);
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(false);   // 扁平列表：没有分组行、没有展开箭头
    tree->setItemsExpandable(false);
    tree->setIndentation(0);           // 顶层条目不需要层级缩进
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);   // 只读
    tree->setMinimumHeight(90);
    rootLayout->addWidget(tree, 1);
    m_impl->tree = tree;
}

FeedbackPanel::~FeedbackPanel()
{
    delete m_impl;
    m_impl = nullptr;
}

// ---------------------------------------------------------------------------
// 追加反馈
// ---------------------------------------------------------------------------
QTreeWidgetItem* FeedbackPanel::add(FeedbackLevel level, const QString& message)
{
    const int index = displayedLevelIndex(level);
    if (index < 0) {
        return nullptr;   // 成功级（以及越界级别值）：永不显示、永不计数
    }

    // 容量保护：先腾出位置（丢弃最旧的第一条并修正计数），保证新条目一定被保留
    while (m_impl->tree->topLevelItemCount() >= kMaxEntries) {
        QTreeWidgetItem* oldest = m_impl->tree->takeTopLevelItem(0);
        if (oldest == nullptr) {
            break;
        }
        const int oldestIndex = oldest->data(0, kLevelRole).toInt();
        if (oldestIndex >= 0 && oldestIndex < kLevelSlots && m_impl->counts[oldestIndex] > 0) {
            --m_impl->counts[oldestIndex];
        }
        delete oldest;
    }

    auto* item = new QTreeWidgetItem();
    // 时间戳在插入时取一次，并写入条目文本；allText() 直接读取该文本，逐字一致
    const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    item->setText(0, QStringLiteral("%1 %2 %3").arg(levelIcon(index), stamp, message));
    item->setToolTip(0, message);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);   // 只读、可选中
    if (const QColor color = levelColor(index); color.isValid()) {
        item->setForeground(0, QBrush(color));   // 仅前景色：背景交给主题
    }
    item->setData(0, kLevelRole, index);

    m_impl->tree->addTopLevelItem(item);   // 追加到末尾：最新在最下方
    ++m_impl->counts[index];
    m_impl->tree->scrollToItem(item);      // 自动滚动到最新条目

    return item;
}

void FeedbackPanel::addSevere(const QString& message)
{
    add(FeedbackLevel::Error, message);
    emit severeAdded(message);   // 调用方据此弹窗并切换到本标签页
}

// ---------------------------------------------------------------------------
// 计数 / 清空 / 导出
// ---------------------------------------------------------------------------
int FeedbackPanel::count(FeedbackLevel level) const
{
    // 成功级（以及越界级别值）恒为 0：它们从不进入列表
    return m_impl->countAt(displayedLevelIndex(level));
}

int FeedbackPanel::warningCount() const
{
    return m_impl->counts[kWarningIndex];
}

int FeedbackPanel::errorCount() const
{
    return m_impl->counts[kErrorIndex];
}

int FeedbackPanel::totalCount() const
{
    return m_impl->total();
}

void FeedbackPanel::clearAll()
{
    m_impl->tree->clear();   // 删除全部条目
    for (int i = 0; i < kLevelSlots; ++i) {
        m_impl->counts[i] = 0;
    }
}

QString FeedbackPanel::allText() const
{
    // 显示文本即条目文本（含图标与插入时的时间戳），故复制内容与显示逐行一致
    QStringList lines;
    const int rows = m_impl->tree->topLevelItemCount();
    lines.reserve(rows);
    for (int row = 0; row < rows; ++row) {
        if (QTreeWidgetItem* item = m_impl->tree->topLevelItem(row)) {
            lines << item->text(0);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QTreeWidget* FeedbackPanel::tree() const
{
    return m_impl->tree;
}

QPushButton* FeedbackPanel::clearButton() const
{
    return m_impl->clearButton;
}

QPushButton* FeedbackPanel::copyButton() const
{
    return m_impl->copyButton;
}

// ---------------------------------------------------------------------------
// 左侧导航徽章文案（v3.3.6：只统计警告与错误）
// ---------------------------------------------------------------------------
QString FeedbackPanel::badgeText(int warnings, int errors)
{
    const int warn = warnings > 0 ? warnings : 0;   // 负数视为 0
    const int err = errors > 0 ? errors : 0;

    QString text;
    if (warn > 0) {
        text += QStringLiteral("⚠️%1").arg(warn);
    }
    if (err > 0) {
        text += QStringLiteral("❌%1").arg(err);
    }
    return text;   // 两者都为 0（或负数）时返回空字符串
}

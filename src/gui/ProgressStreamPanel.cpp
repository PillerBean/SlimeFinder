// ProgressStreamPanel.cpp - 底部「进度」标签页（累积式纯文字流水）实现
//
// 参考：v3.3.6 任务书 三 调整 3（进度分类改为累积式纯文字记录，每 0.5 秒追加一行 + 结束汇总行）。
//
// 行为要点：
//   * 追加节奏由调用方（MainWindow 的 0.5 秒刷新）驱动，本文件内不创建任何 QTimer；
//   * 一行文本的格式完全由两个纯静态函数决定（自检可脱离界面直接断言）：
//       [HH:MM:SS] 已用时 <耗时 1 位小数> 秒，扫描 <已扫描区块> 区块，找到 <命中> 个
//       [HH:MM:SS] 已完成：共扫描 <总数> 区块，找到 <命中> 个结果，耗时 <耗时 1 位小数> 秒
//     「：」「，」均为全角字符，数字用 QString::arg 原样输出（fieldWidth = 0 → 无千位分隔符）；
//   * 负数一律钳制为 0（NaN 同样归零），保证界面不会出现「-1 区块」这类脏数据；
//   * 视图为只读 QPlainTextEdit：等宽字体、不换行、最多 20 万块（超出自动丢弃最旧的行），
//     每次追加后把垂直滚动条固定在最大值（保持最底部）；
//   * [清空] → clearAll()（清空 + 复位行计数）；[复制全部] → 把 allText() 写入剪贴板。
#include "gui/ProgressStreamPanel.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

// 视图最多保留的文本块数（内存保护；与 LogBus 的环形缓冲无关，这里只保护界面内存）
constexpr int kMaxBlocks = 200000;

// 秒数：负值 / NaN 统一按 0 处理
double clampSeconds(double seconds)
{
    return (seconds > 0.0) ? seconds : 0.0;
}

// 计数：负值统一按 0 处理
qint64 clampCount(qint64 value)
{
    return (value > 0) ? value : 0;
}

}   // namespace

// ---------------------------------------------------------------------------
// 纯函数：一行进度 / 一行汇总的固定格式
// ---------------------------------------------------------------------------
QString ProgressStreamPanel::formatProgressLine(const QTime& t, double elapsedSec, qint64 scannedChunks, qint64 found)
{
    // 时间戳固定 [HH:MM:SS]（24 小时制，本地时间由调用方给出）；
    // 耗时保留 1 位小数（.arg(double, 0, 'f', 1)：fieldWidth=0 → 无千位分隔符）
    return QStringLiteral("[%1] 已用时 %2 秒，扫描 %3 区块，找到 %4 个")
        .arg(t.toString(QStringLiteral("HH:mm:ss")))
        .arg(clampSeconds(elapsedSec), 0, 'f', 1)
        .arg(clampCount(scannedChunks))
        .arg(clampCount(found));
}

QString ProgressStreamPanel::formatProgressLineEx(const QTime& t, double elapsedSec, qint64 scannedChunks,
                                                 qint64 totalChunks, qint64 found)
{
    // v3.3.7（任务书 P0-3）：扫描计数与进度百分比同源 —— 显示「扫描 N/M 区块（xx%）」
    const qint64 scanned = clampCount(scannedChunks);
    const qint64 total = clampCount(totalChunks);
    if (total <= 0) {
        return formatProgressLine(t, elapsedSec, scanned, found);
    }
    const double pct = 100.0 * static_cast<double>(scanned) / static_cast<double>(total);
    return QStringLiteral("[%1] 已用时 %2 秒，扫描 %3/%4 区块（%5%），找到 %6 个")
        .arg(t.toString(QStringLiteral("HH:mm:ss")))
        .arg(clampSeconds(elapsedSec), 0, 'f', 1)
        .arg(scanned)
        .arg(total)
        .arg(pct, 0, 'f', 1)
        .arg(clampCount(found));
}

QString ProgressStreamPanel::formatFinalLine(const QTime& t, qint64 totalChunks, qint64 found, double elapsedSec)
{
    return QStringLiteral("[%1] 已完成：共扫描 %2 区块，找到 %3 个结果，耗时 %4 秒")
        .arg(t.toString(QStringLiteral("HH:mm:ss")))
        .arg(clampCount(totalChunks))
        .arg(clampCount(found))
        .arg(clampSeconds(elapsedSec), 0, 'f', 1);
}

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------
ProgressStreamPanel::ProgressStreamPanel(QWidget* parent) : QWidget(parent)
{
    // 供主窗口 / 自检用 findChild 定位
    setObjectName(QStringLiteral("progressStream"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ---------------- 工具行：靠右的 [清空] [复制全部] ----------------
    auto* toolRow = new QHBoxLayout();
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(6);
    toolRow->addStretch(1);

    m_clear = new QPushButton(QStringLiteral("清空"), this);
    m_clear->setAutoDefault(false);
    m_clear->setToolTip(QStringLiteral("清空全部进度记录（不再询问）"));
    connect(m_clear, &QPushButton::clicked, this, &ProgressStreamPanel::clearAll);
    toolRow->addWidget(m_clear);

    m_copy = new QPushButton(QStringLiteral("复制全部"), this);
    m_copy->setAutoDefault(false);
    m_copy->setToolTip(QStringLiteral("把全部进度记录复制到剪贴板"));
    connect(m_copy, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(allText());
    });
    toolRow->addWidget(m_copy);

    root->addLayout(toolRow);

    // ---------------- 只读文本视图（等宽字体 / 不换行 / 20 万块上限）----------------
    m_view = new QPlainTextEdit(this);
    m_view->setObjectName(QStringLiteral("progressStreamView"));
    m_view->setReadOnly(true);
    m_view->setUndoRedoEnabled(false);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_view->setMaximumBlockCount(kMaxBlocks);
    root->addWidget(m_view, 1);
}

// ---------------------------------------------------------------------------
// 追加
// ---------------------------------------------------------------------------
void ProgressStreamPanel::appendProgress(double elapsedSec, qint64 scannedChunks, qint64 found)
{
    // 时间戳取当前本地时间（节奏由调用方掌握，这里只格式化）
    appendLine(formatProgressLine(QTime::currentTime(), elapsedSec, scannedChunks, found));
}

// v3.3.7（P0-3）：带总量的重载（扫描计数与百分比同源）
void ProgressStreamPanel::appendProgress(double elapsedSec, qint64 scannedChunks, qint64 totalChunks, qint64 found)
{
    appendLine(formatProgressLineEx(QTime::currentTime(), elapsedSec, scannedChunks, totalChunks, found));
}

void ProgressStreamPanel::appendFinal(qint64 totalChunks, qint64 found, double elapsedSec)
{
    appendLine(formatFinalLine(QTime::currentTime(), totalChunks, found, elapsedSec));
}

void ProgressStreamPanel::appendRawLine(const QString& line)
{
    // 原样追加：appendPlainText 自带「另起一行」语义（空文档不会产生前导空行）
    appendLine(line);
}

void ProgressStreamPanel::appendLine(const QString& line)
{
    if (m_view == nullptr) {
        return;
    }

    m_view->appendPlainText(line);
    ++m_lines;

    // 追加后保持在最底部：光标移到文末，并把垂直滚动条固定在最大值
    m_view->moveCursor(QTextCursor::End);
    if (QScrollBar* bar = m_view->verticalScrollBar(); bar != nullptr) {
        bar->setValue(bar->maximum());
    }
}

// ---------------------------------------------------------------------------
// 行数 / 清空 / 取全部文本
// ---------------------------------------------------------------------------
int ProgressStreamPanel::lineCount() const
{
    return m_lines;
}

void ProgressStreamPanel::clearAll()
{
    if (m_view != nullptr) {
        m_view->clear();
    }
    m_lines = 0;   // 复位行计数（下一次追加后从 1 重新计数）
}

QString ProgressStreamPanel::allText() const
{
    return (m_view != nullptr) ? m_view->toPlainText() : QString();
}

// ---------------------------------------------------------------------------
// 自检用访问器
// ---------------------------------------------------------------------------
QPlainTextEdit* ProgressStreamPanel::view() const
{
    return m_view;
}

QPushButton* ProgressStreamPanel::clearButton() const
{
    return m_clear;
}

QPushButton* ProgressStreamPanel::copyButton() const
{
    return m_copy;
}

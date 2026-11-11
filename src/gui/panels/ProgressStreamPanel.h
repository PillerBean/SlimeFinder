// ProgressStreamPanel.h - 底部「进度」标签页：累积式纯文字流水
//
// 目的（v3.3.6 任务书 三 调整 3）：
//   把底部「进度」分类从「阶段文字 + 百分比」改成一段不断累积的纯文字记录：
//   调用方每 0.5 秒调一次 appendProgress()，本控件只负责「取时间戳 → 格式化 → 追加一行」，
//   搜索结束时再调 appendFinal() 追加一行汇总；另提供 [清空] [复制全部] 两个按钮。
// 时序约定：
//   * 追加节奏（0.5 秒）由调用方驱动，本控件内部**不使用任何 QTimer**；
//   * 时间戳取当前本地时间，格式固定为 [HH:MM:SS]（24 小时制）；
//   * 数字一律十进制原样输出（无千位分隔符），负数钳制为 0。
// 格式样例（与任务书逐字一致）：
//   [23:04:45] 已用时 0.2 秒，扫描 45397675 区块，找到 1 个
//   [23:04:46] 已完成：共扫描 401234567 区块，找到 10 个结果，耗时 2.1 秒
#pragma once

#include <QString>
#include <QTime>
#include <QWidget>

class QPlainTextEdit;
class QPushButton;

// v3.3.6（任务书 调整 3）：底部「进度」分类 —— 累积式纯文字流水
class ProgressStreamPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProgressStreamPanel(QWidget* parent = nullptr);

    // 纯函数（自检直接断言格式，不依赖界面状态）
    static QString formatProgressLine(const QTime& t, double elapsedSec, qint64 scannedChunks, qint64 found);
    static QString formatFinalLine(const QTime& t, qint64 totalChunks, qint64 found, double elapsedSec);
    // v3.3.7（任务书 P0-3）：带总量的形式 —— 扫描计数与进度百分比同源，界面显示 N/M（xx%）
    static QString formatProgressLineEx(const QTime& t, double elapsedSec, qint64 scannedChunks,
                                        qint64 totalChunks, qint64 found);

    // 追加（时间戳取当前本地时间）
    void appendProgress(double elapsedSec, qint64 scannedChunks, qint64 found);
    // v3.3.7（P0-3）：带总量重载（totalChunks > 0 时输出「扫描 N/M 区块（xx%）」）
    void appendProgress(double elapsedSec, qint64 scannedChunks, qint64 totalChunks, qint64 found);
    void appendFinal(qint64 totalChunks, qint64 found, double elapsedSec);
    void appendRawLine(const QString& line);   // 需要直接写一行时使用（原样追加 + 换行）

    int lineCount() const;        // 已追加的行数
    void clearAll();              // 清空 + 复位行计数
    QString allText() const;      // 「复制全部」用：当前全部文本

    QPlainTextEdit* view() const;      // 自检用
    QPushButton* clearButton() const;  // 自检用
    QPushButton* copyButton() const;   // 自检用

private:
    void appendLine(const QString& line);
    QPlainTextEdit* m_view = nullptr;
    QPushButton* m_clear = nullptr;
    QPushButton* m_copy = nullptr;
    int m_lines = 0;
};

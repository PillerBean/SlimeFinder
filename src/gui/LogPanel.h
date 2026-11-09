// LogPanel.h —— 底部「日志」标签页（v3.3.4 任务书 2.3）
//
// 与 utils/LogBus.h 的分工：LogBus 决定「记录什么、写到哪个文件」，
// 本面板只决定「怎么给用户看」：
//   · 默认关闭（LogBus::Off）时整个面板只有一行提示文字，不显示文本视图与工具行
//     （任务书：「日志栏默认只有这一行文字」）；
//   · 启用后显示「级别过滤下拉框 + 自动滚动 + 清空 + 复制全部 + 只读文本视图 + 状态行」；
//   · 500ms 节流刷新（QTimer），空闲时不重建文本，尽量少动界面；
//   · 「启用日志」对话框（showEnableLogDialog）供「工具」菜单与本面板按钮共用。
#pragma once

#include <QWidget>
#include <QString>

#include <atomic>

#include "utils/LogBus.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;

// 底部「日志」标签页（v3.3.4 任务书 2.3）
class LogPanel : public QWidget {
    Q_OBJECT
public:
    explicit LogPanel(QWidget* parent = nullptr);
    ~LogPanel() override;

    // 日志关闭时界面只显示提示行（不使用 QPlainTextEdit，而是用一个 QLabel 显示提示）
    void refreshFromBus();      // 立即从 LogBus 拉取并刷新（节流定时器之外的手动刷新，自检用）
    QPlainTextEdit* view() const;
    QLabel* hintLabel() const;  // 关闭模式下显示的提示标签
    bool isLoggingEnabled() const;

    // ---- v3.3.6（任务书 4.5）：由「程序设置 → 日志」驱动的两项显示参数 ----
    void setAutoScroll(bool on);   // 自动滚动到底部（默认开）
    bool autoScroll() const;
    void setMaxLines(int lines);   // 界面最大显示行数：超过后丢弃最旧（默认 10000）
    int maxLines() const;

signals:
    void enableRequested();     // 「启用日志」按钮（若有）被点击：由 MainWindow 弹出启用对话框

private:
    void applyModeVisibility(); // 依 LogBus 模式在「仅提示行」/「完整面板」之间切换
    void rebuildView();         // 依过滤级别重建（或追加）文本视图并刷新状态行
    void enforceMaxLines();     // v3.3.6：界面最大行数上限（超过丢弃最旧）
    void updateStatusLabel(size_t total, int64_t dropped);
    QString filteredText() const; // 当前过滤条件下的完整文本（「复制全部」用）
    void resetRenderedState();    // 让下一次刷新走整体重建

    QLabel* m_hint = nullptr;          // 关闭模式：唯一可见的一行提示
    QLabel* m_status = nullptr;        // 启用模式：一行状态前缀（模式 / 条数 / 文件）
    QWidget* m_toolbar = nullptr;      // 启用模式：工具行容器（关闭时整行隐藏）
    QComboBox* m_level = nullptr;      // 级别过滤：全部 / TRACE / DEBUG / INFO / WARN / ERROR
    QCheckBox* m_autoScroll = nullptr; // 自动滚动（默认勾选）
    QPushButton* m_clear = nullptr;    // 清空（LogBus::clear + 立即刷新）
    QPushButton* m_copy = nullptr;     // 复制全部（当前过滤结果 → 剪贴板）
    QPushButton* m_enable = nullptr;   // 重新选择日志模式（emit enableRequested）
    QPlainTextEdit* m_view = nullptr;  // 只读文本视图（关闭模式下隐藏）
    QTimer* m_timer = nullptr;         // 500ms 节流刷新定时器

    // 增量渲染状态：环形缓冲没有丢条目时只追加新增行，避免每 500ms 重建上万行
    bool m_rendered = false;           // 是否已渲染过
    size_t m_renderedCount = 0;        // 已消费的 snapshot 条目数
    int64_t m_renderedDropped = 0;     // 渲染时的丢弃计数
    int m_renderedFilter = 0;          // 渲染时的过滤级别（-1 = 全部）
    double m_renderedTailT = 0.0;      // 已消费末尾条目的 t / 级别 / 文本（用于校验环未滑动）
    int m_renderedTailLevel = -1;
    std::string m_renderedTailText;

    std::atomic<bool> m_refreshPosted{false}; // 监听器合并标记（可能来自工作线程）
    qint64 m_lastRefreshMs = 0;               // 最近一次实际刷新时刻（GUI 线程，用于限频）
    int m_maxLines = 10000;                   // v3.3.6：界面最大显示行数（超过丢弃最旧）
};

// 弹出「启用日志」对话框；返回 false 表示用户取消。成功时把选择写入 *outMode。
bool showEnableLogDialog(QWidget* parent, slime::LogMode* outMode);

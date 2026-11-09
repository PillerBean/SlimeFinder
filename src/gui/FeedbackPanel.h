// FeedbackPanel.h - 底部「反馈」标签页
//
// 目的：
//   以单一扁平列表展示程序运行过程中的反馈信息：按时间顺序追加，最新在最下方；
//   「成功」级自 v3.3.6 起被完全过滤（不显示、不计数）。
//   级别只通过图标与前景色区分，不再提供分组与过滤 UI；工具行仅有「清空」「复制全部」。
// 参考：
//   * v3.3.4 任务书 2.2（反馈级别顺序：错误 > 警告 > 信息 > 成功）；
//   * v3.3.6 任务书 §三 调整 4（取消四级分组、时间顺序、成功级过滤、徽章仅统计警告与错误）。
#pragma once

#include <QString>
#include <QWidget>

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// 反馈级别（任务书 v3.3.4 顺序：错误 > 警告 > 信息 > 成功；v3.3.6 起「成功」不再显示）
enum class FeedbackLevel { Error = 0, Warning = 1, Info = 2, Success = 3 };

// v3.3.6（任务书 调整 4）：时间顺序（最新在下方）的扁平反馈列表；成功级被过滤
class FeedbackPanel : public QWidget {
    Q_OBJECT
public:
    explicit FeedbackPanel(QWidget* parent = nullptr);
    ~FeedbackPanel() override;

    // 追加一条反馈：成功级直接忽略并返回 nullptr；其余按时间顺序追加到末尾（最新在下）
    // 条目文本形如「❌ 14:23:15 消息」；超出上限时丢弃最旧的一条
    QTreeWidgetItem* add(FeedbackLevel level, const QString& message);
    void addSevere(const QString& message);      // 错误级 + 发 severeAdded 信号

    int count(FeedbackLevel level) const;        // 该级别当前条目数（成功级恒为 0）
    int warningCount() const;                    // v3.3.6：左侧导航徽章用
    int errorCount() const;                      // v3.3.6：左侧导航徽章用
    int totalCount() const;                      // 实际显示的条目数（不含被过滤的成功级）
    void clearAll();                             // 清空列表与计数
    QString allText() const;                     // 「复制全部」：每行一条，含图标与时间戳

    QTreeWidget* tree() const;                   // 自检用（单列、无表头）
    QPushButton* clearButton() const;            // 自检用
    QPushButton* copyButton() const;             // 自检用

    // v3.3.6（任务书 §三 调整 4 计数规则）：仅统计警告与错误
    // 两者都有 -> "⚠️2❌1"；仅警告 -> "⚠️2"；仅错误 -> "❌1"；都没有 -> 空字符串
    static QString badgeText(int warnings, int errors);   // "⚠️2❌1" / "⚠️2" / "❌1" / ""

signals:
    void severeAdded(const QString& message);

private:
    struct Impl;
    Impl* m_impl = nullptr;   // 不透明实现（定义在 .cpp，避免暴露私有细节）
};

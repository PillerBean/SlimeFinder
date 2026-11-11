// CommandTerminal.h - 底部「命令终端」标签页（v3.3.4 任务书 2.4）
//
// 设计要点：
//   * 终端自己只做三件事：解析命令行、显示文本、维护历史与补全；
//     真正的活在 CommandHost::runCommand() 里完成。
//   * runCommand() 在 GUI 线程被 **同步** 调用（不排队、不等待搜索线程），
//     所以命令可以插队到正在运行的搜索任务前面立即生效；宿主内部若需要
//     跨线程取结果，由宿主自行决定策略，本控件不做任何等待。
//   * 历史最多 100 条（最新在最后），由主窗口读写 config.json：
//     setHistory()/history() 读入与取出，historyChanged() 通知保存。
//   * 交互：回车执行（执行后清空输入行，空行忽略）、↑/↓ 翻历史（↑ 更旧，
//     ↓ 更新，翻到最后一条之外恢复未提交的草稿）、Tab 弹出命令补全列表。
//   * 终端内置命令：clear —— 清空输出区，不转发给宿主、也不打印任何内容。
#pragma once
#include <QString>
#include <QStringList>
#include <QRect> // v3.4.11：候选框几何（自检用）
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;

// 命令宿主：由主窗口实现（命令解析/执行与搜索线程独立，立即生效）
class CommandHost {
public:
    virtual ~CommandHost() = default;
    // 执行一条命令（cmd 已转小写、args 为按空白切分后的参数）。
    // 返回要显示在终端里的文本（可以多行、以 '\n' 分隔）；不做任何界面操作。
    virtual QString runCommand(const QString& cmd, const QStringList& args) = 0;
    // 命令名列表（供补全与 help 使用），按字母序
    virtual QStringList commandNames() const = 0;
    // 每个命令的一行说明（供 help 与补全提示），键为命令名
    virtual QString commandHelp(const QString& cmd) const = 0;
};

class CommandTerminal : public QWidget {
    Q_OBJECT
public:
    explicit CommandTerminal(CommandHost* host, QWidget* parent = nullptr);

    // 历史（最多 100 条，去重相邻重复，最新在最后）；由主窗口读写 config.json
    void setHistory(const QStringList& history);
    QStringList history() const;

    QStringList commandNames() const;      // 转发给 host（host 为空则返回空表）
    QLineEdit* input() const;
    QPlainTextEdit* output() const;

    // 以编程方式执行一行命令（等价于用户输入并回车）；自检用
    void execute(const QString& line);

    // ---- v3.4.11：Tab 候选框（Minecraft Java 风格）自检 API ----
    bool popupVisible() const;            // 候选框是否可见
    QStringList popupItems() const;
    int popupCurrentRow() const;          // v3.6.5：候选框当前高亮行
    int popupCurrentIndex() const;        // 当前高亮项（-1 = 无）
    QRect popupGeometry() const;          // 候选框几何（自检断言「在输入行上方」）
    void hidePopup();                     // 外部请求关闭（执行命令后调用）
    void appendLine(const QString& text);  // 追加输出（不含提示符）

signals:
    void historyChanged(const QStringList& history);  // 供主窗口持久化

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

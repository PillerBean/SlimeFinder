// CommandTerminal.cpp - 底部「命令终端」标签页实现（v3.3.4 任务书 2.4）
//
// 关键约束（任务书 2.4）：命令必须「立即执行、可插队」。
// 本控件不创建线程、不加锁、不做任何等待：它在 GUI 线程里同步调用
// CommandHost::runCommand()，把「怎么插队到正在运行的搜索任务」完全交给宿主。
#include "i18n/UiText.h"
#include "gui/panels/CommandTerminal.h"

#include <QAbstractItemView>
#include <QFont>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QListWidget> // v3.4.11：Tab 候选框
#include <QStringListModel>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

// 历史条数上限（超出丢最旧的一条）
constexpr int kMaxHistory = 100;

// 启动横幅
QString bannerText()
{
    return slime::ui::t("ui.373ab2270666");
}

// 输入行提示语
QString inputPlaceholder()
{
    return slime::ui::t("ui.ff816158c7e0");
}

// 取第一个空白之前的部分（Tab 补全只针对命令名）
QString firstToken(const QString& text)
{
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i).isSpace())
            return text.left(i);
    }
    return text;
}

} // namespace

// 终端内部状态：头文件只留一个指针，实现细节全部藏在这里。
// 继承 QObject 只是为了让控件在析构时顺带删掉它（构造时以控件为父对象），
// 因此这里不需要（也不写）Q_OBJECT，不会产生额外的 moc 文件。
struct CommandTerminal::Impl : public QObject {
    explicit Impl(CommandTerminal* owner) : QObject(owner), self(owner) {}

    CommandTerminal* self = nullptr;    // 宿主控件
    CommandHost* host = nullptr;        // 命令宿主（可为空）
    QPlainTextEdit* output = nullptr;   // 只读输出区
    QLineEdit* input = nullptr;         // 单行输入
    QStringListModel* model = nullptr;  // 补全数据源
    // ---- v3.4.11：Tab 候选框（Minecraft Java 风格，不抢焦点）----
    QListWidget* popup = nullptr;       // 候选列表（Qt::ToolTip + NoFocus）
    QString popupToken;                 // 触发本轮补全的词（用来判断是否同一轮）
    bool popupOpen = false;             // 候选框逻辑开关
    int popupIndex = -1;                // 当前高亮项
    QStringList popupList;              // 当前候选
    bool selfWrite = false;             // v3.4.12：区分「自己写入」与「外部改文本」

    QStringList history;         // 历史：最旧在前、最新在最后
    QString draft;               // 翻历史前暂存的未提交文本（防止丢字）
    int histPos = -1;            // -1 = 不在翻历史；否则为 history 下标
    bool outputStarted = false;  // 输出区是否已写过内容（避免第一行前多一个空行）

    // 追加输出：按 '\n' 分行写入，首行不留空行
    void appendRaw(const QString& text)
    {
        if (!output)
            return;
        QTextCursor cur(output->document());
        cur.movePosition(QTextCursor::End);
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            if (outputStarted)
                cur.insertBlock();
            cur.insertText(line);
            outputStarted = true;
        }
        scrollToEnd();
    }

    void scrollToEnd()
    {
        if (!output)
            return;
        if (QScrollBar* bar = output->verticalScrollBar())
            bar->setValue(bar->maximum());
    }

    void clearOutput()
    {
        if (!output)
            return;
        output->clear();
        outputStarted = false;
    }

    void setInputText(const QString& text)
    {
        if (!input)
            return;
        input->setText(text);
        input->setCursorPosition(text.size());  // 光标移到末尾，便于继续编辑
    }

    void resetNav()
    {
        histPos = -1;
        draft.clear();
    }

    // ↑：更旧的一条；第一次按 ↑ 时把当前未提交文本存为草稿
    void historyPrev()
    {
        if (!input || history.isEmpty())
            return;
        if (histPos < 0) {
            draft = input->text();
            histPos = history.size();
        }
        if (histPos > 0)
            --histPos;
        setInputText(history.at(histPos));
    }

    // ↓：更新的一条；翻过最新一条后恢复草稿（草稿为空即空行）
    void historyNext()
    {
        if (!input || histPos < 0)
            return;
        if (histPos + 1 < history.size()) {
            ++histPos;
            setInputText(history.at(histPos));
        } else {
            histPos = -1;
            setInputText(draft);
            draft.clear();
        }
    }

    // 记录历史：与上一条完全相同则跳过，最多 kMaxHistory 条，最新在最后
    void recordHistory(const QString& rawLine)
    {
        if (!history.isEmpty() && history.last() == rawLine)
            return;
        history.append(rawLine);
        while (history.size() > kMaxHistory)
            history.removeFirst();
        emit self->historyChanged(history);
    }

    // v3.5.26：把「当前词 → 候选命令」抽成公用逻辑，供「边输入边提示」和 Tab 共用
    QStringList currentTokenMatches() const
    {
        QStringList out;
        if (!input || !model || model->rowCount() == 0) return out;
        const QString left = input->text().left(input->cursorPosition());
        if (left.trimmed().contains(QLatin1Char(' '))) return out;   // 只在第一个词内提示
        const QString token = firstToken(left);
        if (token.isEmpty()) return out;
        for (int i = 0; i < model->rowCount(); ++i) {
            const QString name = model->index(i, 0).data(Qt::DisplayRole).toString();
            if (name.startsWith(token, Qt::CaseInsensitive)) out << name;
        }
        return out;
    }

    // 输入变化时自动展示候选（Minecraft 命令栏风格），无需按 Tab
    void refreshPopupFromInput()
    {
        if (!popup || !input || selfWrite) return;
        QStringList matches = currentTokenMatches();
        // v3.6.5（用户要求）：候选词列表**常显**——输入为空时列出全部命令，输入时列出匹配项
        if (matches.isEmpty()) {
            if (input->text().trimmed().isEmpty()) {
                for (int i = 0; model && i < model->rowCount(); ++i)
                    matches << model->index(i, 0).data(Qt::DisplayRole).toString();
            }
        }
        if (matches.isEmpty()) {
            if (self) self->hidePopup();
            return;
        }
        showPopup(matches);
    }

    // Enter：把当前候选写入命令栏（不执行）
    bool acceptPopupSelection()
    {
        if (!popupOpen || popupList.isEmpty() || !input) return false;
        const QString pick = popupList[static_cast<size_t>((std::max)(0, popupIndex))];
        writeFirstToken(pick, true);
        if (self) self->hidePopup();
        return true;
    }

    // Tab：**内联补全**（v3.3.5 Bug 1）——不使用弹出候选窗，避免候选窗抢走键盘焦点。
    //   * 0 个候选：不改动输入内容（返回 false）
    //   * 1 个候选：直接补全并追加一个空格
    //   * 多个候选：补到最长公共前缀；若公共前缀没有增长，则把候选列表打印到输出区
    // 全过程不改变焦点（调用方在 keyPress 分支里还会显式把焦点交回输入框）。
    bool applyInlineCompletion()
    {
        if (!input)
            return false;
        const QString text = input->text();
        const int cursor = input->cursorPosition();
        const QString left = text.left(cursor);
        const QString token = firstToken(left);
        // 只在光标位于第一个词内时补全命令名；否则不处理（避免破坏参数输入）
        if (token.isEmpty() || left.trimmed().contains(QLatin1Char(' ')))
            return false;
        if (model->rowCount() == 0)
            return false;

        QStringList matches;
        for (int i = 0; i < model->rowCount(); ++i) {
            const QString name = model->index(i, 0).data(Qt::DisplayRole).toString();
            if (name.startsWith(token, Qt::CaseInsensitive))
                matches << name;
        }
        if (matches.isEmpty())
            return false; // 补全失败：不改变输入内容

        QString replacement;
        if (matches.size() == 1) {
            replacement = matches.first();
        } else {
            // 最长公共前缀（忽略大小写比较，输出用第一个候选的大小写）
            QString prefix = matches.first();
            for (const QString& m : matches) {
                int n = 0;
                while (n < prefix.size() && n < m.size() &&
                       prefix.at(n).toLower() == m.at(n).toLower())
                    ++n;
                prefix.truncate(n);
            }
            if (prefix.size() <= token.size()) {
                // 公共前缀没有增长：列出候选（不改变输入、不改变焦点）
                if (self)
                    self->appendLine(slime::ui::t("ui.7f489316c3a4") + matches.join(QStringLiteral("  ")));
                return false;
            }
            replacement = prefix;
        }

        const QString rest = text.mid(cursor);
        setInputText(replacement + (matches.size() == 1 ? QStringLiteral(" ") : QString()) + rest);
        if (input) {
            const int pos = replacement.size() + (matches.size() == 1 ? 1 : 0);
            input->setCursorPosition(pos);
            input->setFocus(Qt::OtherFocusReason); // 补全成功后保持输入框焦点
        }
        return true;
    }

    // ------------------------------------------------------------------
    // v3.4.11：Minecraft Java 风格的 Tab 候选框
    //   Tab = 补全并在候选间循环；候选框显示在输入行上方；全过程不改变焦点。
    // ------------------------------------------------------------------
    QString firstTokenOf(const QString& left) const
    {
        const QString t = left.trimmed();
        const int sp = t.indexOf(QLatin1Char(' '));
        return sp < 0 ? t : t.left(sp);
    }

    void hidePopupNow()
    {
        if (popup) popup->hide();
        popupOpen = false;
        popupIndex = -1;
        popupList.clear();
        popupToken.clear();
    }

    void writeFirstToken(const QString& replacement, bool appendSpace)
    {
        if (!input) return;
        selfWrite = true; // 自己写入不算「用户改了文本」，不触发收起候选框
        const QString text = input->text();
        const int cursor = input->cursorPosition();
        const int sp = text.indexOf(QLatin1Char(' '));
        const QString rest = (sp >= 0) ? text.mid(sp) : QString();
        const QString out = replacement + (appendSpace ? QStringLiteral(" ") : QString()) + rest.trimmed();
        setInputText(out);
        input->setCursorPosition(replacement.size() + (appendSpace ? 1 : 0));
        input->setFocus(Qt::OtherFocusReason);
        selfWrite = false;
    }

    void showPopup(const QStringList& items)
    {
        if (!popup || !input || !self) return;
        popup->clear();
        for (const QString& s : items) popup->addItem(s);
        const int rowH = popup->sizeHintForRow(0) > 0 ? popup->sizeHintForRow(0) : 18;
        // 可用高度 = 输入行顶部到终端顶部的距离（留 4px 边距），最多显示 8 行
        const int inputTop = input->mapTo(self, QPoint(0, 0)).y();
        const int avail = (std::max)(rowH + 4, inputTop - 4);
        const int maxRows = (std::max)(1, (std::min)(8, (avail - 4) / rowH));
        const int rows = (std::min)(static_cast<int>(items.size()), maxRows);
        const int h = rows * rowH + 4;
        // v3.5.26（用户反馈：TAB 补全栏占一整行）：宽度按最长候选收窄，最多不超过输入行宽
        int textW = 0;
        for (const QString& s2 : items) textW = (std::max)(textW, popup->fontMetrics().horizontalAdvance(s2));
        const int w = (std::min)(input->width(), (std::max)(140, textW + 28));
        const QPoint inTopLeft = input->mapTo(self, QPoint(0, 0));
        popup->setGeometry(inTopLeft.x(), inTopLeft.y() - h - 2, w, h); // 输入行**上方**
        popup->setCurrentRow(0);
        popup->raise();
        popup->show();
        popupOpen = true;
        popupIndex = 0;
        popupList = items;
    }

    // 返回是否消费了本次 Tab
    bool tabCycle(int dir)
    {
        if (!input || !model) return false;
        const QString left = input->text().left(input->cursorPosition());
        const QString token = firstTokenOf(left);
        // v3.4.11 修：候选框一旦打开，就继续在**同一候选集**里循环（顺序很重要：先循环、后重新计算）。
        //   旧写法用「当前词」重新匹配：输入行被补成某个完整命令名后再按 Tab 只会匹配到 1 个候选，
        //   于是退化成「唯一候选」并关掉候选框（自检实测：候选数变 0、连续 Tab 无法循环）。
        //   用户一旦输入其它字符，eventFilter 会先把候选框关掉，因此这里不会误循环。
        // v3.6.5（用户要求）：Tab 只在候选之间切换，**不**写入命令栏；写入由 Enter 负责
        if (popupOpen && !popupList.isEmpty()) {
            const int n = popupList.size();
            popupIndex = ((popupIndex + dir) % n + n) % n;
            if (popup) popup->setCurrentRow(popupIndex);
            return true;
        }

        // v3.4.12（用户实测：Tab 候选框没有出现）：空输入时列出**全部命令**（与 Minecraft 一致）。
        //   旧实现 token 为空直接返回 ⇒ 用户按 Tab 什么也看不到（尤其是刚打开终端时）。
        if (token.isEmpty()) {
            if (left.trimmed().contains(QLatin1Char(' '))) return false;
            QStringList all;
            for (int i = 0; i < model->rowCount(); ++i) {
                all << model->index(i, 0).data(Qt::DisplayRole).toString();
            }
            if (all.isEmpty()) { hidePopupNow(); return false; }
            popupToken = QString();
            showPopup(all);
            return true;
        }
        // 只有光标还在第一个词里才补全命令名（避免破坏参数输入）
        if (left.trimmed().contains(QLatin1Char(' '))) return false;

        QStringList matches;
        for (int i = 0; i < model->rowCount(); ++i) {
            const QString name = model->index(i, 0).data(Qt::DisplayRole).toString();
            if (name.startsWith(token, Qt::CaseInsensitive)) matches << name;
        }
        if (matches.isEmpty()) { hidePopupNow(); return false; }

        if (matches.size() == 1) {
            // v3.6.5：Tab 不写入，只把唯一候选显示出来（Enter 才填）
            showPopup(matches);
            return true;
        }

        // 多个候选：第一次 Tab 先补到最长公共前缀，然后打开候选框
        if (!popupOpen) {
            QString prefix = matches.first();
            for (const QString& m : matches) {
                int n = 0;
                while (n < prefix.size() && n < m.size() &&
                       prefix.at(n).toLower() == m.at(n).toLower()) ++n;
                prefix.truncate(n);
            }
            popupToken = token;
            // v3.6.5：只显示候选并高亮第 1 个，输入行保持用户输入不动
            showPopup(matches);
            return true;
        }

        // 已在候选框里：循环
        const int n = matches.size();
        popupIndex = ((popupIndex + dir) % n + n) % n;
        if (popup) popup->setCurrentRow(popupIndex);
        writeFirstToken(matches[static_cast<size_t>(popupIndex)], false);
        return true;
    }

    bool eventFilter(QObject* watched, QEvent* ev) override
    {
        if (ev->type() == QEvent::KeyPress) {
            QKeyEvent* ke = static_cast<QKeyEvent*>(ev);
            const int key = ke->key();
            if (watched == input) {
                if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
                    // v3.4.11：Minecraft 风格 —— 补全 + 候选框循环；绝不触发焦点切换
                    const bool back = (key == Qt::Key_Backtab) || (ke->modifiers() & Qt::ShiftModifier);
                    tabCycle(back ? -1 : 1);
                    if (input) {
                        input->setFocus(Qt::OtherFocusReason);
                        if (!popupOpen) input->setCursorPosition(input->text().size());
                    }
                    ke->accept();
                    return true;
                }
                if (key == Qt::Key_Escape) {
                    if (popupOpen) {
                        hidePopupNow();
                        ke->accept();
                        return true; // 只关候选框，不改输入内容
                    }
                }
                // v3.6.5（用户要求）：候选框**常显**，不再因为其它按键收起
                //   上下键在候选框打开时移动候选选择；否则走命令历史
                if (popupOpen && !popupList.isEmpty() && (key == Qt::Key_Up || key == Qt::Key_Down)) {
                    const int n = static_cast<int>(popupList.size());
                    popupIndex = ((popupIndex + (key == Qt::Key_Up ? -1 : 1)) % n + n) % n;
                    if (popup) popup->setCurrentRow(popupIndex);
                    return true;
                }
                if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                    // v3.6.5：候选框打开时 Enter 只把候选填进命令栏，不执行
                    if (acceptPopupSelection()) return true;
                }
                if (key == Qt::Key_Up) {
                    historyPrev();
                    return true;
                }
                if (key == Qt::Key_Down) {
                    historyNext();
                    return true;
                }
                return QObject::eventFilter(watched, ev);
            }
        }
        return QObject::eventFilter(watched, ev);
    }
};

CommandTerminal::CommandTerminal(CommandHost* host, QWidget* parent)
    : QWidget(parent), m_impl(new Impl(this))
{
    Impl* d = m_impl;
    d->host = host;

    // ---------- 输出区：只读、可选中复制、等宽字体 ----------
    d->output = new QPlainTextEdit(this);
    d->output->setReadOnly(true);
    d->output->setUndoRedoEnabled(false);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setStyleHint(QFont::Monospace);
    d->output->setFont(mono);

    // ---------- 输入行 ----------
    d->input = new QLineEdit(this);
    d->input->setPlaceholderText(inputPlaceholder());
    d->input->setClearButtonEnabled(false);

    // ---------- 布局：输出占满，输入在底部 ----------
    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    lay->addWidget(d->output, 1);
    lay->addWidget(d->input, 0);

    // ---------- 补全：命令名列表（前缀匹配、忽略大小写、**内联**补全）----------
    // v3.3.5 Bug 1：不再使用 QCompleter 的弹出候选窗（弹窗会抢焦点，导致 Tab 后焦点跳到别的控件）。
    // 现在只用一份命令名列表做内联补全，输入框始终保留焦点。
    d->model = new QStringListModel(host ? host->commandNames() : QStringList(), this);
    d->input->installEventFilter(d);               // Tab 补全、↑/↓ 翻历史
    // v3.4.12：文本被外部改动（程序化 setText / 历史翻页 / 清空）后，候选框状态必须重置，
    // 否则下一次 Tab 会拿上一轮的候选去循环（自检实测：唯一匹配补全等用例被污染）。
    connect(d->input, &QLineEdit::textChanged, this, [this](const QString&) {
        if (m_impl && !m_impl->selfWrite) m_impl->hidePopupNow();
    });

    // ---------- v3.4.11：Tab 候选框（Minecraft Java 风格；**不抢焦点**）----------
    // v3.3.5 曾因「候选窗抢焦点」把弹窗砍掉；这里用 ToolTip + WA_ShowWithoutActivating +
    // NoFocus 三件套保证焦点始终留在输入行，并用自检「连续 Tab 10 次焦点不动」兜住。
    // v3.5.26：边输入边提示（不等 Tab）
    if (d->input) {
        CommandTerminal::Impl* dd = d;
        QObject::connect(d->input, &QLineEdit::textEdited, this, [dd](const QString&) {
            if (dd->input && dd->popup) dd->refreshPopupFromInput();   // 边输入边提示
        });
    }
    d->popup = new QListWidget(this);
    d->popup->setObjectName(QStringLiteral("cmdPopup"));
    // v3.4.12（用户实测：候选框没有出现）：不再用 Qt::ToolTip 顶层窗（真实 Windows 会话里可能被吞掉），
    // 改成**终端内部的子控件浮层** —— 仍然 NoFocus / 不抢焦点，但显示稳定、随面板一起被裁剪。
    d->popup->setParent(this);
    d->popup->setFocusPolicy(Qt::NoFocus);
    d->popup->setAttribute(Qt::WA_ShowWithoutActivating, true);
    d->popup->setSelectionMode(QAbstractItemView::SingleSelection);
    d->popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d->popup->setUniformItemSizes(true);
    d->popup->hide();
    connect(d->popup, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!m_impl || !item) return;
        Impl* dd = m_impl;
        dd->writeFirstToken(item->text(), false);
        dd->hidePopupNow();
        if (dd->input) dd->input->setFocus(Qt::OtherFocusReason);
    });

    connect(d->input, &QLineEdit::returnPressed, this, [this]() { execute(input()->text()); });

    // ---------- 启动横幅 ----------
    d->appendRaw(bannerText());
}

void CommandTerminal::setHistory(const QStringList& history)
{
    if (!m_impl)
        return;
    m_impl->history = history;
    while (m_impl->history.size() > kMaxHistory)
        m_impl->history.removeFirst();
    m_impl->resetNav();
    // 这里不发 historyChanged：历史来自 config.json，避免「读进来又存回去」的循环
}

QStringList CommandTerminal::history() const
{
    return m_impl ? m_impl->history : QStringList();
}

QStringList CommandTerminal::commandNames() const
{
    if (!m_impl || !m_impl->host)
        return QStringList();
    return m_impl->host->commandNames();
}

QLineEdit* CommandTerminal::input() const
{
    return m_impl ? m_impl->input : nullptr;
}

QPlainTextEdit* CommandTerminal::output() const
{
    return m_impl ? m_impl->output : nullptr;
}

void CommandTerminal::execute(const QString& line)
{
    if (!m_impl)
        return;
    Impl* d = m_impl;

    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return;  // 空行忽略

    d->appendRaw(QStringLiteral("> ") + trimmed);  // 回显整行

    // 第一个词是命令（转小写），其余按空白切分作为参数
    const QStringList parts = trimmed.simplified().split(QLatin1Char(' '));
    const QString cmd = parts.value(0).toLower();
    const QStringList args = parts.mid(1);

    if (cmd == QLatin1String("clear")) {
        // 终端内置命令：清空输出区，不转发给宿主，也不打印任何内容
        d->clearOutput();
    } else if (!d->host) {
        d->appendRaw(slime::ui::t("ui.10fd6cd9a986"));
    } else {
        // 同步执行：宿主内部自行决定如何插队，本控件不等待搜索线程
        QString text = d->host->runCommand(cmd, args);
        if (text.isEmpty())
            text = slime::ui::t("ui.c680da690a1f");
        d->appendRaw(text);
    }

    // 记历史（整行，含参数）并清空输入行
    d->recordHistory(trimmed);
    d->resetNav();
    d->hidePopupNow(); // v3.4.11：执行后收起候选框
    if (d->input)
        d->input->clear();
}

bool CommandTerminal::popupVisible() const
{
    return m_impl && m_impl->popupOpen && m_impl->popup && m_impl->popup->isVisible();
}

QStringList CommandTerminal::popupItems() const
{
    return m_impl ? m_impl->popupList : QStringList();
}

int CommandTerminal::popupCurrentRow() const
{
    if (!m_impl || !m_impl->popup) return -1;
    return m_impl->popup->currentRow();
}

int CommandTerminal::popupCurrentIndex() const
{
    return m_impl ? m_impl->popupIndex : -1;
}

QRect CommandTerminal::popupGeometry() const
{
    // v3.4.12：候选框现在是终端内部子控件 ⇒ 用**全局坐标**返回，便于自检与输入行位置比较
    if (!m_impl || !m_impl->popup) return QRect();
    return QRect(m_impl->popup->mapToGlobal(QPoint(0, 0)), m_impl->popup->size());
}

void CommandTerminal::hidePopup()
{
    if (m_impl) m_impl->hidePopupNow();
}

void CommandTerminal::appendLine(const QString& text)
{
    if (!m_impl)
        return;
    m_impl->appendRaw(text);
}

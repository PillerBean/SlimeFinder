// HelpDialog.cpp —— 帮助 / 使用教程的实现（v3.3.3 任务书：面向小白的帮助教程）
//
// 设计要点：
//   1. 教程正文只写一份（helpTutorialHtml），弹窗与 GUI 自检共用，避免文案漂移；
//   2. 全文用大白话说明，不出现“位掩码 / SIMD / 线程”等术语，从使用者视角解释；
//   3. 「复制全部文字」只改写对话框内一个小标签做反馈，不弹任何模态框，
//      以免阻塞自动化自检（自检会调用 showHelpDialog 并直接关掉对话框）；
//   4. 本文件不定义 Q_OBJECT 类，故无需 include "HelpDialog.moc"。
#include "i18n/UiText.h"
#include "gui/dialogs/HelpDialog.h"
#include "i18n/UiStrings.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

// v3.11.2：教程正文外置到 assets/help/tutorial-<lang>.html，代码不再内嵌大段 HTML。
namespace {
QString loadTutorialFile(const QString& lang)
{
    const QString path = QCoreApplication::applicationDirPath() +
                         QStringLiteral("/assets/help/tutorial-") + lang + QStringLiteral(".html");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}
} // namespace

QString helpTutorialHtml()
{
    const QString lang = slime::ui::isEnglish() ? QStringLiteral("en-US")
                        : slime::ui::isTraditional() ? QStringLiteral("zh-TW")
                                                     : QStringLiteral("zh-CN");
    static QHash<QString, QString> cache;
    const auto it = cache.constFind(lang);
    if (it != cache.constEnd()) return it.value();
    QString html = loadTutorialFile(lang);
    if (html.isEmpty() && lang != QLatin1String("zh-CN")) html = loadTutorialFile(QStringLiteral("zh-CN"));
    cache.insert(lang, html);
    return html;
}

void showHelpDialog(QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(slime::ui::t("ui.e63e0a783b7d"));
    dlg.setMinimumSize(720, 560); // 约 720×560：一屏能读完整段，又不会大到超出小屏

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* tip = new QLabel(
        slime::ui::t("ui.7f6dfd1b22ad"),
        &dlg);
    tip->setWordWrap(true);
    root->addWidget(tip);

    // v3.7.5（用户要求）：左侧目录 + 右侧正文，点目录直接跳到对应章节
    struct TocEntry {
        const char* title;
        const char* anchor;
    };
    static const TocEntry kToc[] = {
        { "ui.help.toc.what", "sec1" },       { "ui.help.toc.how", "sec2" },
        { "ui.help.toc.pattern", "sec3" },     { "ui.help.toc.modes", "sec4" },
        { "ui.help.toc.biome", "sec5" }, { "ui.help.toc.gpu", "sec6" },
        { "ui.help.toc.results", "sec7" },     { "ui.help.toc.bedrock", "sec8" },
        { "ui.help.toc.faq", "sec9" },
    };

    auto* body = new QHBoxLayout();
    auto* toc = new QListWidget(&dlg);
    toc->setFixedWidth(148);
    for (const TocEntry& e : kToc) toc->addItem(slime::ui::t(e.title));
    toc->setCurrentRow(0);
    body->addWidget(toc);

    auto* browser = new QTextBrowser(&dlg);
    browser->setHtml(helpTutorialHtml());
    browser->setReadOnly(true);              // 只读，但可滚动、可选中、可复制
    browser->setOpenExternalLinks(false);    // 不自动打开外部浏览器/链接
    browser->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    browser->setMinimumHeight(320);
    body->addWidget(browser, 1);
    root->addLayout(body, 1);
    QObject::connect(toc, &QListWidget::currentRowChanged, &dlg, [browser](int row) {
        if (row < 0) return;
        const QString anchor = QStringLiteral("sec%1").arg(row + 1);
        browser->scrollToAnchor(anchor);
    });

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);

    auto* copyBtn = new QPushButton(slime::ui::t("ui.152731915eec"), &dlg);
    copyBtn->setToolTip(slime::ui::t("ui.60a0960c6a64"));
    btnRow->addWidget(copyBtn);

    // 复制结果只在这里显示，不弹任何对话框（弹窗会打断自动化自检）
    auto* copyState = new QLabel(QStringLiteral(" "), &dlg);
    copyState->setMinimumWidth(copyState->fontMetrics().horizontalAdvance(QStringLiteral(" ")) * 8);
    btnRow->addWidget(copyState);

    btnRow->addStretch(1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    if (QPushButton* closeBtn = box->button(QDialogButtonBox::Close)) {
        closeBtn->setText(slime::ui::t("ui.6c14bd7f6f9e"));
        closeBtn->setDefault(true);   // 回车即可关闭
        closeBtn->setAutoDefault(true);
    }
    btnRow->addWidget(box);
    root->addLayout(btnRow);

    QObject::connect(copyBtn, &QPushButton::clicked, &dlg, [browser, copyState]() {
        if (QClipboard* clip = QApplication::clipboard()) {
            clip->setText(browser->toPlainText());
        }
        copyState->setText(slime::ui::t("ui.c2bb6d50aebe"));
    });
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

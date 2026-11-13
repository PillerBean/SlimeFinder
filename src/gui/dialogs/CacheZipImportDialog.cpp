#include "gui/dialogs/CacheZipImportDialog.h"

#include "cache/CacheStore.h"
#include "i18n/UiText.h"

#include <QApplication>
#include <QCloseEvent>
#include <QProgressBar>
#include <QColor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QMetaObject>

#include <algorithm>
#include <thread>

CacheZipImportDialog::CacheZipImportDialog(slime::cache::CacheStore* store, QWidget* parent)
    : QDialog(parent)
    , m_store(store)
{
    setWindowTitle(QStringLiteral("导入网盘缓存"));
    setAcceptDrops(true);
    resize(760, 540);
    setMinimumSize(620, 420);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* drop = new QLabel(QStringLiteral("把多个 zip 拖到这里\n支持批量导入；先预览，确认后才解压"), this);
    drop->setObjectName(QStringLiteral("cacheDropZone"));
    drop->setAlignment(Qt::AlignCenter);
    drop->setMinimumHeight(92);
    drop->setWordWrap(true);
    root->addWidget(drop);

    auto* tools = new QWidget(this);
    auto* toolsLay = new QHBoxLayout(tools);
    toolsLay->setContentsMargins(0, 0, 0, 0);
    auto* pick = new QPushButton(QStringLiteral("选择 zip"), tools);
    toolsLay->addWidget(pick);
    m_hint = new QLabel(QStringLiteral("尚未选择文件"), tools);
    m_hint->setObjectName(QStringLiteral("topSummary"));
    toolsLay->addWidget(m_hint, 1);
    root->addWidget(tools);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({ QStringLiteral("文件"), QStringLiteral("识别范围"),
                                         QStringLiteral("pack 数"), QStringLiteral("大小"),
                                         QStringLiteral("状态") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    root->addWidget(m_table, 1);

    m_summary = new QLabel(QStringLiteral("已选择 0 个文件"), this);
    m_summary->setObjectName(QStringLiteral("topSummary"));
    root->addWidget(m_summary);

    auto* footer = new QWidget(this);
    auto* footerLay = new QHBoxLayout(footer);
    footerLay->setContentsMargins(0, 0, 0, 0);
    m_cancel = new QPushButton(QStringLiteral("取消"), footer);
    m_start = new QPushButton(QStringLiteral("开始导入"), footer);
    m_start->setEnabled(false);
    footerLay->addStretch(1);
    footerLay->addWidget(m_cancel);
    footerLay->addWidget(m_start);
    root->addWidget(footer);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat(QStringLiteral("导入 %v / %m"));
    m_progress->setMinimumHeight(18);
    m_progress->setVisible(false);
    root->addWidget(m_progress);

    m_pick = pick;
    QObject::connect(m_pick, &QPushButton::clicked, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("选择缓存 zip"), QString(), QStringLiteral("缓存包 (*.zip)"));
        addPaths(files);
    });
    QObject::connect(m_cancel, &QPushButton::clicked, this, [this]() {
        if (!m_importing) reject();
    });
    QObject::connect(m_start, &QPushButton::clicked, this, [this]() { startImport(); });
}

CacheZipImportDialog::~CacheZipImportDialog()
{
    if (m_worker.joinable()) m_worker.join();
}

void CacheZipImportDialog::dragEnterEvent(QDragEnterEvent* ev)
{
    if (ev->mimeData()->hasUrls()) {
        for (const QUrl& u : ev->mimeData()->urls()) {
            if (u.isLocalFile() && u.toLocalFile().endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
                ev->acceptProposedAction();
                return;
            }
        }
    }
}

void CacheZipImportDialog::dropEvent(QDropEvent* ev)
{
    QStringList files;
    for (const QUrl& u : ev->mimeData()->urls()) {
        if (u.isLocalFile()) {
            const QString path = u.toLocalFile();
            if (path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) files << path;
        }
    }
    if (!files.isEmpty()) {
        addPaths(files);
        ev->acceptProposedAction();
    }
}

void CacheZipImportDialog::addPaths(const QStringList& paths)
{
    for (const QString& path : paths) {
        Item item;
        item.path = path;
        slime::cache::CacheZipImporter::inspect(path, item.info);
        m_items.push_back(std::move(item));
    }
    refresh();
}

void CacheZipImportDialog::refresh()
{
    m_table->setRowCount(0);
    int valid = 0;
    qint64 totalBytes = 0;
    int totalPacks = 0;
    for (const auto& item : m_items) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(QFileInfo(item.path).fileName()));
        m_table->setItem(row, 1, new QTableWidgetItem(item.info.ok ? item.info.rangeLabel
                                                                  : QStringLiteral("—")));
        m_table->setItem(row, 2, new QTableWidgetItem(item.info.ok ? QString::number(item.info.packCount)
                                                                  : QStringLiteral("—")));
        m_table->setItem(row, 3, new QTableWidgetItem(
            item.info.ok ? QStringLiteral("%1 MB").arg(item.info.bytes / (1024.0 * 1024.0), 0, 'f', 1)
                         : QStringLiteral("—")));
        auto* status = new QTableWidgetItem(
            item.info.ok ? QStringLiteral("待导入") : QStringLiteral("已拒绝：%1").arg(item.info.error));
        if (!item.info.ok) status->setForeground(QColor(176, 48, 32));
        m_table->setItem(row, 4, status);
        if (item.info.ok) {
            ++valid;
            totalBytes += item.info.bytes;
            totalPacks += item.info.packCount;
        }
    }
    m_start->setEnabled(valid > 0);
    m_hint->setText(valid ? QStringLiteral("%1 个文件可导入").arg(valid)
                          : QStringLiteral("尚未选择有效文件"));
    if (!m_importing && !m_hasResult && m_progress) {
        m_progress->setVisible(false);
        m_progress->setRange(0, 1);
        m_progress->setValue(0);
    }
    m_summary->setText(QStringLiteral("已选择 %1 个文件 · 有效 %2 个 · 预计 %3 个 pack · 合计 %4 MB")
                           .arg(m_items.size())
                           .arg(valid)
                           .arg(totalPacks)
                           .arg(totalBytes / (1024.0 * 1024.0), 0, 'f', 1));
}

void CacheZipImportDialog::startImport()
{
    if (m_importing) return;
    std::vector<Item> valid;
    for (const auto& item : m_items) if (item.info.ok) valid.push_back(item);
    if (valid.empty()) return;

    m_importing = true;
    m_hasResult = false;
    m_start->setEnabled(false);
    if (m_cancel) m_cancel->setEnabled(false);
    if (m_pick) m_pick->setEnabled(false);
    if (m_table) m_table->setEnabled(false);
    setWindowFlag(Qt::WindowCloseButtonHint, false);
    int totalPacks = 0;
    for (const auto& item : valid) totalPacks += item.info.packCount;
    if (m_progress) {
        m_progress->setVisible(true);
        m_progress->setRange(0, std::max(1, totalPacks));
        m_progress->setValue(0);
        m_progress->setVisible(true);
        m_progress->repaint();
    }
    m_hint->setText(QStringLiteral("后台导入中，请稍候…"));

    // 导入在后台线程执行；主线程只接收进度并刷新，避免大 zip 卡死界面。
    m_worker = std::thread([this, valid]() {
        int imported = 0, skipped = 0, failed = 0;
        int packDone = 0;
        QStringList errors;
        for (size_t i = 0; i < valid.size(); ++i) {
            int add = 0, skip = 0;
            QString err;
            const QString name = QFileInfo(valid[i].path).fileName();
            QMetaObject::invokeMethod(this, [this, i, name, total = valid.size()]() {
                if (m_hint) m_hint->setText(QStringLiteral("正在导入 %1 / %2：%3")
                                                .arg(static_cast<int>(i + 1)).arg(static_cast<int>(total)).arg(name));
            }, Qt::QueuedConnection);
            const int basePacks = packDone;
            const auto progress = [this, basePacks](int current, int, const QString& file) {
                QMetaObject::invokeMethod(this, [this, basePacks, current, file]() {
                    if (m_progress) m_progress->setValue(basePacks + current);
                    if (m_hint) m_hint->setText(QStringLiteral("正在导入：%1").arg(QFileInfo(file).fileName()));
                }, Qt::QueuedConnection);
            };
            if (slime::cache::CacheZipImporter::importZip(valid[i].path, m_store->root(), &err, &add, &skip, progress)) {
                imported += add;
                skipped += skip;
            } else {
                ++failed;
                errors << QStringLiteral("%1：%2").arg(name, err);
            }
            packDone += valid[i].info.packCount;
            QMetaObject::invokeMethod(this, [this, packDone]() {
                if (m_progress) m_progress->setValue(packDone);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, imported, skipped, failed, errors]() {
            m_importing = false;
            m_hasResult = true;
            m_start->setEnabled(true);
            if (m_cancel) m_cancel->setEnabled(true);
            if (m_pick) m_pick->setEnabled(true);
            if (m_table) m_table->setEnabled(true);
            if (m_progress) {
                m_progress->setValue(m_progress->maximum());
                m_progress->setVisible(true);
            }
            setWindowFlag(Qt::WindowCloseButtonHint, true);
            if (!errors.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("导入结果"),
                                     QStringLiteral("导入完成。\n新增 %1 个 pack，跳过 %2 个，失败 %3 个。\n\n%4")
                                         .arg(imported).arg(skipped).arg(failed).arg(errors.join(QChar('\n'))));
            } else {
                QMessageBox::information(this, QStringLiteral("导入结果"),
                                         QStringLiteral("导入完成。\n新增 %1 个 pack，跳过重复 %2 个。")
                                             .arg(imported).arg(skipped));
            }
            if (onImported) onImported();
            refresh();
        }, Qt::QueuedConnection);
    });
}

void CacheZipImportDialog::reject()
{
    if (m_importing) return; // 后台导入时禁止关闭，避免悬垂回调/UI线程 join 卡死
    QDialog::reject();
}

void CacheZipImportDialog::closeEvent(QCloseEvent* ev)
{
    if (m_importing) {
        ev->ignore();
        return;
    }
    QDialog::closeEvent(ev);
}

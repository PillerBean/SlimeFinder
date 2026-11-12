#include "gui/dialogs/CacheZipImportDialog.h"

#include "cache/CacheStore.h"
#include "i18n/UiText.h"

#include <QApplication>
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

#include <algorithm>

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
    auto* cancel = new QPushButton(QStringLiteral("取消"), footer);
    m_start = new QPushButton(QStringLiteral("开始导入"), footer);
    m_start->setEnabled(false);
    footerLay->addStretch(1);
    footerLay->addWidget(cancel);
    footerLay->addWidget(m_start);
    root->addWidget(footer);

    QObject::connect(pick, &QPushButton::clicked, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("选择缓存 zip"), QString(), QStringLiteral("缓存包 (*.zip)"));
        addPaths(files);
    });
    QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    QObject::connect(m_start, &QPushButton::clicked, this, [this]() { startImport(); });
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
    m_summary->setText(QStringLiteral("已选择 %1 个文件 · 有效 %2 个 · 预计 %3 个 pack · 合计 %4 MB")
                           .arg(m_items.size())
                           .arg(valid)
                           .arg(totalPacks)
                           .arg(totalBytes / (1024.0 * 1024.0), 0, 'f', 1));
}

void CacheZipImportDialog::startImport()
{
    std::vector<Item> valid;
    for (const auto& item : m_items) {
        if (item.info.ok) valid.push_back(item);
    }
    if (valid.empty()) return;

    QProgressDialog progress(QStringLiteral("正在校验、解压并合并缓存…"), QStringLiteral("取消"),
                             0, static_cast<int>(valid.size()), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    int imported = 0, skipped = 0, failed = 0;
    QStringList errors;
    for (int i = 0; i < static_cast<int>(valid.size()); ++i) {
        if (progress.wasCanceled()) break;
        progress.setValue(i);
        progress.setLabelText(QStringLiteral("正在导入：%1").arg(QFileInfo(valid[i].path).fileName()));
        QApplication::processEvents();
        int add = 0, skip = 0;
        QString err;
        if (slime::cache::CacheZipImporter::importZip(valid[i].path, m_store->root(), &err, &add, &skip)) {
            imported += add;
            skipped += skip;
        } else {
            ++failed;
            errors << QStringLiteral("%1：%2").arg(QFileInfo(valid[i].path).fileName(), err);
        }
    }
    progress.setValue(static_cast<int>(valid.size()));
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导入结果"),
                             QStringLiteral("导入完成。\n新增 %1 个 pack，跳过 %2 个，失败 %3 个。\n\n%4")
                                 .arg(imported).arg(skipped).arg(failed).arg(errors.join(QLatin1Char('\n'))));
    } else {
        QMessageBox::information(this, QStringLiteral("导入结果"),
                                 QStringLiteral("导入完成。\n新增 %1 个 pack，跳过重复 %2 个。")
                                     .arg(imported).arg(skipped));
    }
    if (onImported) onImported();
    refresh();
}

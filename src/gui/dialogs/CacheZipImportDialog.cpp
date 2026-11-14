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
    if (m_inspectWorker.joinable()) m_inspectWorker.join();
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
    if (m_importing || m_inspecting) return;
    QStringList added;
    for (const QString& path : paths) {
        Item item;
        item.path = path;
        m_items.push_back(std::move(item));
        added << path;
    }
    if (added.isEmpty()) return;
    if (m_inspectWorker.joinable()) m_inspectWorker.join();
    m_inspectDone = 0;
    m_inspectTotal = static_cast<int>(added.size());
    m_inspecting = true;
    if (m_pick) m_pick->setEnabled(false);
    if (m_start) m_start->setEnabled(false);
    if (m_cancel) m_cancel->setEnabled(false);
    if (m_progress) {
        m_progress->setVisible(true);
        m_progress->setFormat(QStringLiteral("检测 %v / %m"));
        m_progress->setRange(0, std::max(1, m_inspectTotal));
        m_progress->setValue(0);
    }
    if (m_hint) m_hint->setText(QStringLiteral("正在读取 zip 清单 0 / %1…").arg(m_inspectTotal));
    refresh();

    m_inspectWorker = std::thread([this, added]() {
        int done = 0;
        for (const QString& path : added) {
            slime::cache::ZipInspection info;
            slime::cache::CacheZipImporter::inspect(path, info);
            ++done;
            const QString name = QFileInfo(path).fileName();
            const int current = done;
            QMetaObject::invokeMethod(this, [this, path, info, current, name]() {
                for (auto& item : m_items) {
                    if (!item.inspected && item.path == path) {
                        item.info = info;
                        item.inspected = true;
                        break;
                    }
                }
                m_inspectDone = current;
                if (m_progress) m_progress->setValue(current);
                if (m_hint) m_hint->setText(QStringLiteral("正在读取 zip 清单 %1 / %2：%3")
                                                .arg(current).arg(m_inspectTotal).arg(name));
                refresh();
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this]() {
            m_inspecting = false;
            if (m_pick) m_pick->setEnabled(true);
            if (m_cancel) m_cancel->setEnabled(true);
            refresh();
            if (m_progress) {
                m_progress->setValue(m_progress->maximum());
                m_progress->setVisible(true);
            }
            if (m_hint) m_hint->setText(QStringLiteral("清单读取完成，共 %1 个 zip").arg(m_inspectTotal));
        }, Qt::QueuedConnection);
    });
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
        m_table->setItem(row, 1, new QTableWidgetItem(item.inspected && item.info.ok ? item.info.rangeLabel
                                                                                    : QStringLiteral("—")));
        m_table->setItem(row, 2, new QTableWidgetItem(item.inspected && item.info.ok ? QString::number(item.info.packCount)
                                                                                    : QStringLiteral("—")));
        m_table->setItem(row, 3, new QTableWidgetItem(
            item.info.ok ? QStringLiteral("%1 MB").arg(item.info.bytes / (1024.0 * 1024.0), 0, 'f', 1)
                         : QStringLiteral("—")));
        QString statusText;
        if (!item.inspected) statusText = QStringLiteral("检测中…");
        else if (item.info.ok) statusText = QStringLiteral("待导入");
        else statusText = QStringLiteral("已拒绝：%1").arg(item.info.error);
        auto* status = new QTableWidgetItem(statusText);
        if (item.inspected && !item.info.ok) status->setForeground(QColor(176, 48, 32));
        m_table->setItem(row, 4, status);
        if (item.inspected && item.info.ok) {
            ++valid;
            totalBytes += item.info.bytes;
            totalPacks += item.info.packCount;
        }
    }
    m_start->setEnabled(!m_inspecting && valid > 0);
    if (!m_inspecting) {
        m_hint->setText(valid ? QStringLiteral("%1 个文件可导入").arg(valid)
                              : QStringLiteral("尚未选择有效文件"));
    }
    if (!m_importing && !m_inspecting && !m_hasResult && m_progress) {
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
    if (m_importing || m_inspecting) return;
    std::vector<Item> valid;
    for (const auto& item : m_items) if (item.info.ok) valid.push_back(item);
    if (valid.empty()) return;

    m_importing = true;
    m_hasResult = false;
    m_start->setEnabled(false);
    if (m_cancel) m_cancel->setEnabled(false);
    if (m_pick) m_pick->setEnabled(false);
    if (m_table) m_table->setEnabled(false);
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
            QString result = QStringLiteral("导入完成：新增 %1 个 pack，跳过重复 %2 个，失败 %3 个。")
                                 .arg(imported).arg(skipped).arg(failed);
            if (!errors.isEmpty()) result += QStringLiteral("\n%1").arg(errors.join(QChar('\n')));
            // 先刷新本窗口，再写结果；否则 refresh() 会覆盖完成提示。
            refresh();
            if (m_hint) m_hint->setText(result);
            if (onImported) onImported();
        }, Qt::QueuedConnection);
    });
}

void CacheZipImportDialog::reject()
{
    if (m_importing || m_inspecting) return; // 后台导入/检测时禁止关闭
    QDialog::reject();
}

void CacheZipImportDialog::closeEvent(QCloseEvent* ev)
{
    if (m_importing || m_inspecting) {
        ev->ignore();
        return;
    }
    QDialog::closeEvent(ev);
}

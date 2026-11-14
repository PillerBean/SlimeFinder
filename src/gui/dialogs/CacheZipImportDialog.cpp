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
#include <QPair>
#include <QVector>
#include <QMetaObject>

#include <algorithm>
#include <thread>


namespace {
QString localizedZipError(const QString& message)
{
    static const QVector<QPair<QString, const char*>> map = {
        { QStringLiteral("无法读取 zip 条目"), "ui.cache.zip.err.readEntry" },
        { QStringLiteral("zip 条目过大"), "ui.cache.zip.err.entryTooLarge" },
        { QStringLiteral("zip 条目解压失败"), "ui.cache.zip.err.entryExtract" },
        { QStringLiteral("manifest.json 格式错误"), "ui.cache.zip.err.manifestFormat" },
        { QStringLiteral("manifest 格式版本不支持"), "ui.cache.zip.err.manifestVersion" },
        { QStringLiteral("manifest 没有 pack"), "ui.cache.zip.err.manifestNoPacks" },
        { QStringLiteral("manifest pack 字段非法"), "ui.cache.zip.err.manifestPackFields" },
        { QStringLiteral("manifest 重复 pack"), "ui.cache.zip.err.manifestDuplicatePack" },
        { QStringLiteral("manifest pack 数量不一致"), "ui.cache.zip.err.manifestPackCount" },
        { QStringLiteral("无法打开 zip"), "ui.cache.zip.err.openZip" },
        { QStringLiteral("zip 超过大小限制"), "ui.cache.zip.err.zipTooLarge" },
        { QStringLiteral("zip 为空"), "ui.cache.zip.err.zipEmpty" },
        { QStringLiteral("zip 结构损坏"), "ui.cache.zip.err.zipCorrupt" },
        { QStringLiteral("zip 条目数量非法"), "ui.cache.zip.err.entryCount" },
        { QStringLiteral("zip 条目损坏"), "ui.cache.zip.err.entryCorrupt" },
        { QStringLiteral("zip 包含非法路径"), "ui.cache.zip.err.illegalPath" },
        { QStringLiteral("zip 包含加密或不支持的条目"), "ui.cache.zip.err.encrypted" },
        { QStringLiteral("zip 包含符号链接"), "ui.cache.zip.err.symlink" },
        { QStringLiteral("zip 包含不允许的目录"), "ui.cache.zip.err.disallowedDir" },
        { QStringLiteral("zip 包含不允许的文件："), "ui.cache.zip.err.disallowedFile" },
        { QStringLiteral("zip 包含重复条目"), "ui.cache.zip.err.duplicateEntry" },
        { QStringLiteral("zip 解压体积异常"), "ui.cache.zip.err.extractSize" },
        { QStringLiteral("zip 缺少 manifest.json"), "ui.cache.zip.err.missingManifest" },
        { QStringLiteral("zip 包含未声明的缓存包"), "ui.cache.zip.err.undeclaredPack" },
        { QStringLiteral("manifest 引用的 pack 不在 zip 中"), "ui.cache.zip.err.missingPack" },
        { QStringLiteral("无法创建导入临时目录"), "ui.cache.zip.err.tempDir" },
        { QStringLiteral("缓存索引加载失败："), "ui.cache.zip.err.indexLoad" },
        { QStringLiteral("缓存包解压大小与 manifest 不一致："), "ui.cache.zip.err.packSizeMismatch" },
        { QStringLiteral("缓存包解压失败："), "ui.cache.zip.err.packExtract" },
        { QStringLiteral("缓存包大小校验失败："), "ui.cache.zip.err.packSizeCheck" },
        { QStringLiteral("缓存包 SHA256 校验失败："), "ui.cache.zip.err.packSha" },
        { QStringLiteral("缓存包文件名非法"), "ui.cache.zip.err.packName" },
        { QStringLiteral("无法写入导入临时文件"), "ui.cache.zip.err.tempWrite" },
        { QStringLiteral("缓存包格式非法："), "ui.cache.zip.err.packFormat" },
        { QStringLiteral("缓存包范围与 manifest 不一致："), "ui.cache.zip.err.packRange" },
        { QStringLiteral("同范围缓存已存在且内容不同，拒绝覆盖："), "ui.cache.zip.err.sameRangeConflict" },
        { QStringLiteral("无法写入下载缓存目录"), "ui.cache.zip.err.downloadedDir" },
        { QStringLiteral("缓存索引重建失败："), "ui.cache.zip.err.indexRebuild" },
    };
    for (const auto& [prefix, key] : map) {
        if (message == prefix) return slime::ui::t(key);
        if (message.startsWith(prefix)) {
            const QString detail = message.mid(prefix.size());
            return slime::ui::t(key).arg(detail);
        }
    }
    return message;
}
} // namespace

CacheZipImportDialog::CacheZipImportDialog(slime::cache::CacheStore* store, QWidget* parent)
    : QDialog(parent)
    , m_store(store)
{
    setWindowTitle(slime::ui::t("ui.cache.zip.import.title"));
    setAcceptDrops(true);
    resize(760, 540);
    setMinimumSize(620, 420);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* drop = new QLabel(slime::ui::t("ui.cache.zip.drop"), this);
    drop->setObjectName(QStringLiteral("cacheDropZone"));
    drop->setAlignment(Qt::AlignCenter);
    drop->setMinimumHeight(92);
    drop->setWordWrap(true);
    root->addWidget(drop);

    auto* tools = new QWidget(this);
    auto* toolsLay = new QHBoxLayout(tools);
    toolsLay->setContentsMargins(0, 0, 0, 0);
    auto* pick = new QPushButton(slime::ui::t("ui.cache.zip.pick"), tools);
    toolsLay->addWidget(pick);
    m_hint = new QLabel(slime::ui::t("ui.cache.zip.noFiles"), tools);
    m_hint->setObjectName(QStringLiteral("topSummary"));
    toolsLay->addWidget(m_hint, 1);
    root->addWidget(tools);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({ slime::ui::t("ui.cache.zip.column.file"), slime::ui::t("ui.cache.zip.column.range"),
                                         slime::ui::t("ui.cache.zip.column.packs"), slime::ui::t("ui.cache.zip.column.size"),
                                         slime::ui::t("ui.cache.zip.column.status") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    root->addWidget(m_table, 1);

    m_summary = new QLabel(slime::ui::t("ui.cache.zip.summaryInitial"), this);
    m_summary->setObjectName(QStringLiteral("topSummary"));
    root->addWidget(m_summary);

    auto* footer = new QWidget(this);
    auto* footerLay = new QHBoxLayout(footer);
    footerLay->setContentsMargins(0, 0, 0, 0);
    m_cancel = new QPushButton(slime::ui::t("ui.cache.zip.cancel"), footer);
    m_start = new QPushButton(slime::ui::t("ui.cache.zip.start"), footer);
    m_start->setEnabled(false);
    footerLay->addStretch(1);
    footerLay->addWidget(m_cancel);
    footerLay->addWidget(m_start);
    root->addWidget(footer);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat(slime::ui::t("ui.cache.zip.progress.import"));
    m_progress->setMinimumHeight(18);
    m_progress->setVisible(false);
    root->addWidget(m_progress);

    m_pick = pick;
    QObject::connect(m_pick, &QPushButton::clicked, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, slime::ui::t("ui.cache.zip.dialog.pickTitle"), QString(), slime::ui::t("ui.cache.zip.dialog.filter"));
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
        m_progress->setFormat(slime::ui::t("ui.cache.zip.progress.inspect"));
        m_progress->setRange(0, std::max(1, m_inspectTotal));
        m_progress->setValue(0);
    }
    if (m_hint) m_hint->setText(slime::ui::t("ui.cache.zip.inspect.start").arg(m_inspectTotal));
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
                if (m_hint) m_hint->setText(slime::ui::t("ui.cache.zip.inspect.progress")
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
            if (m_hint) m_hint->setText(slime::ui::t("ui.cache.zip.inspect.done").arg(m_inspectTotal));
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
        if (!item.inspected) statusText = slime::ui::t("ui.cache.zip.status.inspecting");
        else if (item.info.ok) statusText = slime::ui::t("ui.cache.zip.status.pending");
        else statusText = slime::ui::t("ui.cache.zip.status.rejected").arg(localizedZipError(item.info.error));
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
        m_hint->setText(valid ? slime::ui::t("ui.cache.zip.validCount").arg(valid)
                              : slime::ui::t("ui.cache.zip.noValid"));
    }
    if (!m_importing && !m_inspecting && !m_hasResult && m_progress) {
        m_progress->setVisible(false);
        m_progress->setRange(0, 1);
        m_progress->setValue(0);
    }
    m_summary->setText(slime::ui::t("ui.cache.zip.summary")
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
    m_hint->setText(slime::ui::t("ui.cache.zip.importing.wait"));

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
                if (m_hint) m_hint->setText(slime::ui::t("ui.cache.zip.importing.progress")
                                                .arg(static_cast<int>(i + 1)).arg(static_cast<int>(total)).arg(name));
            }, Qt::QueuedConnection);
            const int basePacks = packDone;
            const auto progress = [this, basePacks](int current, int, const QString& file) {
                QMetaObject::invokeMethod(this, [this, basePacks, current, file]() {
                    if (m_progress) m_progress->setValue(basePacks + current);
                    if (m_hint) m_hint->setText(slime::ui::t("ui.cache.zip.importing.file").arg(QFileInfo(file).fileName()));
                }, Qt::QueuedConnection);
            };
            if (slime::cache::CacheZipImporter::importZip(valid[i].path, m_store->root(), &err, &add, &skip, progress)) {
                imported += add;
                skipped += skip;
            } else {
                ++failed;
                errors << QStringLiteral("%1：%2").arg(name, localizedZipError(err));
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
            QString result = slime::ui::t("ui.cache.zip.result")
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

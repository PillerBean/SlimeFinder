#include "gui/dialogs/CacheDownloadDialog.h"
#include "gui/dialogs/CacheZipImportDialog.h"

#include "gui/widgets/CacheCoverageMap.h"
#include "i18n/UiText.h"

#include <QCheckBox>
#include <QButtonGroup>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace {
QString mbText(uint64_t bytes)
{
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
}
} // namespace

CacheDownloadDialog::CacheDownloadDialog(slime::cache::CacheStore* store, QWidget* parent)
    : QDialog(parent)
    , m_store(store)
    , m_client(new CacheDownloadClient(this))
{
    setWindowTitle(slime::ui::t("ui.cache.download.title"));
    resize(1000, 700);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto* note = new QLabel(slime::ui::t("ui.cache.download.dialog.note"), this);
    note->setObjectName(QStringLiteral("topSummary"));
    note->setWordWrap(true);
    root->addWidget(note);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("cacheDownloadTabs"));
    root->addWidget(tabs, 1);

    // 可下载页：manifest + pack 下载
    {
        auto* page = new QWidget(tabs);
        auto* lay = new QVBoxLayout(page);
        lay->setContentsMargins(10, 10, 10, 10);
        lay->setSpacing(8);
        auto* tools = new QWidget(page);
        auto* toolsLay = new QHBoxLayout(tools);
        toolsLay->setContentsMargins(0, 0, 0, 0);
        m_loadManifest = new QPushButton(slime::ui::t("ui.cache.download.fetch"), tools);
        m_availableStatus = new QLabel(tools);
        m_availableStatus->setObjectName(QStringLiteral("topSummary"));
        m_availableStatus->setWordWrap(true);
        toolsLay->addWidget(m_loadManifest);
        auto* rangeAll = new QPushButton(slime::ui::t("ui.cache.download.quick.all"), tools);
        auto* rangeLocal = new QPushButton(slime::ui::t("ui.cache.download.quick.local"), tools);
        auto* rangeLarge = new QPushButton(slime::ui::t("ui.cache.download.quick.large"), tools);
        for (QPushButton* b : { rangeAll, rangeLocal, rangeLarge }) {
            b->setCheckable(true);
            toolsLay->addWidget(b);
        }
        rangeAll->setChecked(true);
        auto* rangeGroup = new QButtonGroup(this);
        rangeGroup->setExclusive(true);
        rangeGroup->addButton(rangeAll, 0);
        rangeGroup->addButton(rangeLocal, 8192);
        rangeGroup->addButton(rangeLarge, 32768);
        m_availableSearch = new QLineEdit(tools);
        m_availableSearch->setPlaceholderText(slime::ui::t("ui.cache.download.search"));
        m_availableSearch->setMaximumWidth(180);
        toolsLay->addWidget(m_availableSearch);
        toolsLay->addWidget(m_availableStatus, 1);
        lay->addWidget(tools);

        m_availableTable = new QTableWidget(0, 5, page);
        m_availableTable->setHorizontalHeaderLabels(
            { QString(), slime::ui::t("ui.cache.pack.name"), slime::ui::t("ui.cache.pack.range"),
              slime::ui::t("ui.cache.pack.size"), slime::ui::t("ui.cache.pack.action") });
        m_availableTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_availableTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_availableTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        m_availableTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_availableTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        m_availableTable->verticalHeader()->setVisible(false);
        m_availableTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_availableTable->setSelectionMode(QAbstractItemView::SingleSelection);
        lay->addWidget(m_availableTable, 1);

        auto* bottom = new QWidget(page);
        auto* bottomLay = new QHBoxLayout(bottom);
        bottomLay->setContentsMargins(0, 0, 0, 0);
        auto* selectAll = new QCheckBox(slime::ui::t("ui.cache.pack.selectAll"), bottom);
        m_downloadSelected = new QPushButton(slime::ui::t("ui.cache.download.downloadSelected"), bottom);
        m_downloadSelected->setEnabled(false);
        bottomLay->addWidget(selectAll);
        bottomLay->addStretch(1);
        bottomLay->addWidget(m_downloadSelected);
        lay->addWidget(bottom);

        auto* progressRow = new QWidget(page);
        auto* progressLay = new QHBoxLayout(progressRow);
        progressLay->setContentsMargins(0, 0, 0, 0);
        progressLay->setSpacing(8);
        m_downloadProgressLabel = new QLabel(progressRow);
        m_downloadProgressLabel->setObjectName(QStringLiteral("topSummary"));
        m_downloadProgress = new QProgressBar(progressRow);
        m_downloadProgress->setRange(0, 1000);
        m_downloadProgress->setValue(0);
        m_downloadProgress->setTextVisible(false);
        progressLay->addWidget(m_downloadProgressLabel, 1);
        progressLay->addWidget(m_downloadProgress, 1);
        m_downloadProgress->setVisible(false);
        m_downloadProgressLabel->setVisible(false);
        lay->addWidget(progressRow);
        tabs->addTab(page, slime::ui::t("ui.cache.download.r2tab"));

        connect(m_loadManifest, &QPushButton::clicked, this, [this]() { loadAvailable(); });
        connect(rangeGroup, &QButtonGroup::idClicked, this, [this](int id) {
            m_availableRange = id;
            refreshAvailable();
        });
        connect(m_availableSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
            m_availableQuery = text.trimmed();
            refreshAvailable();
        });
        connect(selectAll, &QCheckBox::toggled, this, [this](bool on) {
            for (int r = 0; r < m_availableTable->rowCount(); ++r)
                if (auto* it = m_availableTable->item(r, 0))
                    it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            if (m_downloadSelected) m_downloadSelected->setEnabled(on && m_availableTable->rowCount() > 0);
        });
        connect(m_availableTable, &QTableWidget::itemChanged, this, [this]() {
            int n = 0;
            for (int r = 0; r < m_availableTable->rowCount(); ++r)
                if (auto* it = m_availableTable->item(r, 0); it && it->checkState() == Qt::Checked) ++n;
            if (m_downloadSelected) m_downloadSelected->setEnabled(n > 0);
        });
        connect(m_downloadSelected, &QPushButton::clicked, this, [this]() { downloadSelected(); });
        loadAvailable();
    }

    // 网盘手动页：只给 4 个范围链接 + 导入窗口，不做自动扫描/下载
    {
        auto* ndPage = new QWidget(tabs);
        auto* ndLay = new QVBoxLayout(ndPage);
        ndLay->setContentsMargins(10, 10, 10, 10);
        ndLay->setSpacing(8);

        auto* hint = new QLabel(slime::ui::t("ui.cache.download.netdisk.hint"), ndPage);
        hint->setWordWrap(true);
        hint->setObjectName(QStringLiteral("topSummary"));
        ndLay->addWidget(hint);

        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(8);
        // 网盘源已取消“附近”档；只保留局部/较大/全部。
        const char* keys[] = { "ui.cache.download.quick.local",
                               "ui.cache.download.quick.large",
                               "ui.cache.download.quick.all" };
        for (int i = 0; i < 3; ++i) {
            auto* box = new QGroupBox(slime::ui::t(keys[i]), ndPage);
            box->setProperty("card", true);
            auto* boxLay = new QVBoxLayout(box);
            boxLay->setContentsMargins(8, 6, 8, 8);
            auto* open = new QPushButton(slime::ui::t("ui.cache.download.openFolder"), box);
            const int rangeIndex = i; // 网盘源仅保留局部/较大/全部
            open->setEnabled(!CacheDownloadClient::netdiskRangeUrl(rangeIndex).trimmed().isEmpty());
            QObject::connect(open, &QPushButton::clicked, this, [rangeIndex]() {
                const QString url = CacheDownloadClient::netdiskRangeUrl(rangeIndex);
                if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
            });
            boxLay->addWidget(open);
            grid->addWidget(box, i / 2, i % 2);
        }
        ndLay->addLayout(grid);

        auto* importRow = new QWidget(ndPage);
        auto* importLay = new QHBoxLayout(importRow);
        importLay->setContentsMargins(0, 0, 0, 0);
        importLay->addWidget(new QLabel(slime::ui::t("ui.cache.download.netdisk.drop"), importRow), 1);
        auto* importBtn = new QPushButton(slime::ui::t("ui.cache.download.netdisk.import"), importRow);
        importLay->addWidget(importBtn);
        ndLay->addWidget(importRow);
        auto* note = new QLabel(slime::ui::t("ui.cache.download.netdisk.note"), ndPage);
        note->setWordWrap(true);
        note->setObjectName(QStringLiteral("topSummary"));
        ndLay->addWidget(note);
        ndLay->addStretch(1);

        QObject::connect(importBtn, &QPushButton::clicked, this, [this]() {
            CacheZipImportDialog dlg(m_store, this);
            dlg.onImported = [this]() {
                if (m_store) m_store->load();
                refresh();
                if (onChanged) onChanged();
            };
            dlg.exec();
            if (m_store) m_store->load();
            refresh();
        });
        tabs->addTab(ndPage, slime::ui::t("ui.cache.download.netdisktab"));
    }

    // 已下载页
    auto* page = new QWidget(tabs);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(8);

    auto* tools = new QWidget(page);
    auto* toolsLay = new QHBoxLayout(tools);
    toolsLay->setContentsMargins(0, 0, 0, 0);
    toolsLay->setSpacing(5);
    auto* all = new QPushButton(slime::ui::t("ui.cache.download.quick.all"), tools);
    all->setCheckable(true);
    all->setChecked(true);
    auto* near = new QPushButton(slime::ui::t("ui.cache.download.quick.near"), tools);
    near->setCheckable(true);
    auto* local = new QPushButton(slime::ui::t("ui.cache.download.quick.local"), tools);
    local->setCheckable(true);
    auto* large = new QPushButton(slime::ui::t("ui.cache.download.quick.large"), tools);
    large->setCheckable(true);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(all, 0);
    group->addButton(near, 2048);
    group->addButton(local, 8192);
    group->addButton(large, 32768);
    toolsLay->addWidget(all);
    toolsLay->addWidget(near);
    toolsLay->addWidget(local);
    toolsLay->addWidget(large);
    toolsLay->addSpacing(8);
    m_search = new QLineEdit(tools);
    m_search->setPlaceholderText(slime::ui::t("ui.cache.download.search"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(260);
    toolsLay->addWidget(m_search);
    toolsLay->addStretch(1);
    m_summary = new QLabel(tools);
    m_summary->setObjectName(QStringLiteral("topSummary"));
    toolsLay->addWidget(m_summary);
    lay->addWidget(tools);

    auto* body = new QHBoxLayout();
    body->setSpacing(8);
    auto* mapBox = new QGroupBox(slime::ui::t("ui.cache.map.title"), page);
    mapBox->setProperty("card", true);
    auto* mapLay = new QVBoxLayout(mapBox);
    m_map = new CacheCoverageMap(mapBox);
    m_map->setMinimumHeight(300);
    mapLay->addWidget(m_map, 1);
    auto* mapHint = new QLabel(slime::ui::t("ui.cache.map.hint"), mapBox);
    mapHint->setObjectName(QStringLiteral("topSummary"));
    mapHint->setWordWrap(true);
    mapLay->addWidget(mapHint);
    mapBox->setMinimumWidth(390);
    mapBox->setMaximumWidth(480);
    body->addWidget(mapBox);
    lay->addLayout(body, 1);

    m_table = new QTableWidget(0, 4, page);
    m_table->setHorizontalHeaderLabels({ QString(), slime::ui::t("ui.cache.pack.range"),
                                         slime::ui::t("ui.cache.pack.size"), slime::ui::t("ui.cache.pack.action") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_table, 2);
    mapBox->setMinimumHeight(300);

    auto* bottom = new QWidget(page);
    auto* bottomLay = new QHBoxLayout(bottom);
    bottomLay->setContentsMargins(0, 0, 0, 0);
    auto* selectAll = new QCheckBox(slime::ui::t("ui.cache.pack.selectAll"), bottom);
    m_deleteSelected = new QPushButton(slime::ui::t("ui.cache.pack.deleteSelected"), bottom);
    auto* close = new QPushButton(slime::ui::t("ui.cache.download.close"), bottom);
    bottomLay->addWidget(selectAll);
    bottomLay->addStretch(1);
    bottomLay->addWidget(m_deleteSelected);
    bottomLay->addWidget(close);
    lay->addWidget(bottom);
    tabs->addTab(page, slime::ui::t("ui.cache.download.downloaded"));

    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        m_rangeLimit = id;
        refresh();
    });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) { refresh(); });
    connect(selectAll, &QCheckBox::toggled, this, [this](bool on) {
        for (int r = 0; r < m_table->rowCount(); ++r) {
            if (auto* it = m_table->item(r, 0)) it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        }
        updateSelectionUi();
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this]() { updateSelectionUi(); });
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (auto* it = m_table->item(row, 0)) m_map->setSelected(it->data(Qt::UserRole).toString().toStdString());
    });
    if (m_map) {
        m_map->onSelected = [this](const std::string& file) {
            m_map->setSelected(file);
        };
    }
    connect(m_deleteSelected, &QPushButton::clicked, this, [this]() {
        if (!m_store) return;
        QStringList files;
        for (int r = 0; r < m_table->rowCount(); ++r) {
            auto* it = m_table->item(r, 0);
            if (it && it->checkState() == Qt::Checked) files << it->data(Qt::UserRole).toString();
        }
        std::string err;
        for (const QString& f : files) m_store->removePack(f.toStdString(), &err);
        refresh();
        if (onChanged) onChanged();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    refresh();
}

bool CacheDownloadDialog::matchesRange(const slime::cache::CachePackEntry& e) const
{
    if (m_rangeLimit <= 0) return true;
    return std::max({ std::abs(e.rect.minX), std::abs(e.rect.maxX),
                      std::abs(e.rect.minZ), std::abs(e.rect.maxZ) }) <= m_rangeLimit;
}

bool CacheDownloadDialog::matchesSearch(const slime::cache::CachePackEntry& e) const
{
    if (!m_search || m_search->text().trimmed().isEmpty()) return true;
    const QString q = m_search->text().trimmed();
    const QString range = QStringLiteral("%1..%2, %3..%4")
                              .arg(e.rect.minX).arg(e.rect.maxX).arg(e.rect.minZ).arg(e.rect.maxZ);
    return range.contains(q) || QString::fromStdString(e.file).contains(q, Qt::CaseInsensitive);
}

void CacheDownloadDialog::loadAvailable()
{
    if (!m_client || !m_availableStatus) return;
    m_populating = false;
    m_availablePacks.clear();
    if (m_availableTable) m_availableTable->setRowCount(0);
    const QString primary = CacheDownloadClient::primaryManifestUrl();
    const QString fallback = CacheDownloadClient::fallbackManifestUrl();
    if (primary.trimmed().isEmpty()) {
        m_availableStatus->setText(slime::ui::t("ui.cache.download.unconfigured"));
        return;
    }
    m_availableStatus->setText(slime::ui::t("ui.cache.download.loading"));
    const auto apply = [this](bool ok, const QString& err, const QVector<RemoteCachePack>& packs) {
        if (!m_availableStatus || !m_availableTable) return;
        if (!ok) {
            if (m_client && m_client->quotaWarning() == QStringLiteral("exhausted")) {
                m_availableStatus->setText(slime::ui::t("ui.cache.download.quota.exhausted"));
            } else {
                m_availableStatus->setText(err);
            }
            return;
        }
        m_availablePacks = packs;
        startAvailablePopulation();
        const QString quota = m_client ? m_client->quotaWarning() : QString();
        if (m_availableStatus) {
            if (quota == QStringLiteral("warn")) {
                m_availableStatus->setText(slime::ui::t("ui.cache.download.quota.warn"));
            } else if (quota == QStringLiteral("exhausted")) {
                m_availableStatus->setText(slime::ui::t("ui.cache.download.quota.exhausted"));
            }
        }
    };
    if (fallback.trimmed().isEmpty()) {
        m_client->fetchManifest(QUrl(primary), apply);
    } else {
        m_client->fetchManifest(QUrl(primary), [this, apply, fallback](bool ok, const QString& err,
                                                                       const QVector<RemoteCachePack>& packs) {
            if (ok) {
                apply(true, {}, packs);
                return;
            }
            m_client->fetchManifest(QUrl(fallback), [apply](bool ok2, const QString& err2,
                                                             const QVector<RemoteCachePack>& packs2) {
                apply(ok2, err2, packs2);
            });
        });
    }
}

void CacheDownloadDialog::startAvailablePopulation()
{
    if (!m_availableTable) return;
    m_availableTable->blockSignals(true);
    m_availableTable->setRowCount(0);
    m_availableTable->blockSignals(false);
    m_populating = true;
    m_populateIndex = 0;
    m_populateTotal = m_availablePacks.size();
    m_availableShown = 0;
    m_populateElapsed.start();
    if (m_downloadSelected) m_downloadSelected->setEnabled(false);
    if (m_availableStatus) {
        m_availableStatus->setText(QStringLiteral("清单已获取：0 / %1，正在生成列表…").arg(m_populateTotal));
    }
    QTimer::singleShot(0, this, [this]() { populateAvailableChunk(); });
}

void CacheDownloadDialog::appendAvailableRow(const RemoteCachePack& p, int sourceIndex)
{
    const int row = m_availableTable->rowCount();
    m_availableTable->insertRow(row);
    auto* check = new QTableWidgetItem();
    check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
    check->setCheckState(Qt::Unchecked);
    check->setData(Qt::UserRole, sourceIndex);
    m_availableTable->setItem(row, 0, check);
    m_availableTable->setItem(row, 1, new QTableWidgetItem(p.name));
    m_availableTable->setItem(row, 2, new QTableWidgetItem(QStringLiteral("%1..%2, %3..%4")
                                                              .arg(p.minX).arg(p.maxX).arg(p.minZ).arg(p.maxZ)));
    m_availableTable->setItem(row, 3, new QTableWidgetItem(mbText(static_cast<uint64_t>(p.bytes))));
    auto* dl = new QPushButton(slime::ui::t("ui.cache.download.download"), m_availableTable);
    dl->setObjectName(QStringLiteral("miniButton"));
    m_availableTable->setCellWidget(row, 4, dl);
    connect(dl, &QPushButton::clicked, this, [this, p]() {
        const QString name = QFileInfo(p.file).fileName();
        if (name.isEmpty() || name.contains(QStringLiteral("..")) || !m_client) return;
        QDir().mkpath(QString::fromStdWString((m_store->root() / "downloaded").wstring()));
        const auto dest = m_store->root() / "downloaded" / name.toStdWString();
        if (m_availableStatus) m_availableStatus->setText(
            slime::ui::t("ui.cache.download.downloading").arg(p.name));
        m_client->downloadPackWithFallback(p.url, p.fallbackUrl, p, QString::fromStdWString(dest.wstring()),
                               [this, p, dest](bool ok, const QString& err) {
            if (!ok) {
                if (m_availableStatus) m_availableStatus->setText(
                    slime::ui::t("ui.cache.download.failed").arg(p.name, err));
                return;
            }
            std::string addErr;
            if (!m_store->addPack(dest, slime::cache::CacheStore::kDownloadedDir, &addErr)) {
                if (m_availableStatus) m_availableStatus->setText(
                    slime::ui::t("ui.cache.download.failed").arg(p.name, QString::fromStdString(addErr)));
                return;
            }
            if (m_availableStatus) m_availableStatus->setText(
                slime::ui::t("ui.cache.download.downloaded").arg(p.name));
            refresh();
            if (onChanged) onChanged();
        });
    });
    ++m_availableShown;
}

void CacheDownloadDialog::populateAvailableChunk()
{
    if (!m_populating || !m_availableTable) return;
    constexpr int kChunk = 48;
    m_availableTable->blockSignals(true);
    const int end = std::min(m_populateTotal, m_populateIndex + kChunk);
    for (; m_populateIndex < end; ++m_populateIndex) {
        const RemoteCachePack& p = m_availablePacks[m_populateIndex];
        if (m_availableRange > 0 &&
            std::max({ std::abs(p.minX), std::abs(p.maxX), std::abs(p.minZ), std::abs(p.maxZ) }) > m_availableRange)
            continue;
        if (!m_availableQuery.isEmpty()) {
            const QString range = QStringLiteral("%1..%2, %3..%4").arg(p.minX).arg(p.maxX).arg(p.minZ).arg(p.maxZ);
            if (!p.name.contains(m_availableQuery, Qt::CaseInsensitive) && !range.contains(m_availableQuery))
                continue;
        }
        appendAvailableRow(p, m_populateIndex);
    }
    m_availableTable->blockSignals(false);
    if (m_populateIndex < m_populateTotal) {
        if (m_availableStatus) {
            const qint64 ms = std::max<qint64>(1, m_populateElapsed.elapsed());
            const double rate = m_populateIndex > 0 ? static_cast<double>(m_populateIndex) * 1000.0 / ms : 0.0;
            const int remain = rate > 0 ? static_cast<int>((m_populateTotal - m_populateIndex) / rate) : 0;
            m_availableStatus->setText(QStringLiteral("正在生成列表 %1 / %2，预计剩余 %3 秒")
                                           .arg(m_populateIndex).arg(m_populateTotal).arg(remain));
        }
        QTimer::singleShot(0, this, [this]() { populateAvailableChunk(); });
        return;
    }
    m_populating = false;
    if (m_availableStatus) {
        m_availableStatus->setText(QStringLiteral("清单获取完成：共 %1 个 pack，当前显示 %2 个")
                                       .arg(m_populateTotal).arg(m_availableShown));
    }
    if (m_downloadSelected) m_downloadSelected->setEnabled(m_availableShown > 0);
}

void CacheDownloadDialog::refreshAvailable()
{
    if (!m_availableTable) return;
    m_populating = false;
    m_availableTable->blockSignals(true);
    m_availableTable->setRowCount(0);
    m_availableShown = 0;
    for (int i = 0; i < m_availablePacks.size(); ++i) {
        const RemoteCachePack& p = m_availablePacks[i];
        if (m_availableRange > 0 &&
            std::max({ std::abs(p.minX), std::abs(p.maxX), std::abs(p.minZ), std::abs(p.maxZ) }) > m_availableRange)
            continue;
        if (!m_availableQuery.isEmpty()) {
            const QString range = QStringLiteral("%1..%2, %3..%4").arg(p.minX).arg(p.maxX).arg(p.minZ).arg(p.maxZ);
            if (!p.name.contains(m_availableQuery, Qt::CaseInsensitive) && !range.contains(m_availableQuery))
                continue;
        }
        appendAvailableRow(p, i);
    }
    m_availableTable->blockSignals(false);
    if (m_availableStatus) {
        m_availableStatus->setText(slime::ui::t("ui.cache.download.availableCount").arg(m_availableShown));
    }
    if (m_downloadSelected) m_downloadSelected->setEnabled(m_availableShown > 0);
}

void CacheDownloadDialog::downloadSelected()
{
    if (!m_client || !m_availableTable || m_downloadActive > 0) return;
    QVector<RemoteCachePack> chosen;
    for (int r = 0; r < m_availableTable->rowCount(); ++r) {
        if (auto* it = m_availableTable->item(r, 0); it && it->checkState() == Qt::Checked) {
            const int idx = it->data(Qt::UserRole).toInt();
            if (idx >= 0 && idx < m_availablePacks.size()) chosen.push_back(m_availablePacks[idx]);
        }
    }
    if (chosen.isEmpty()) return;
    m_downloadQueue = chosen;
    m_downloadTotal = chosen.size();
    m_downloadDone = 0;
    m_downloadFailed = 0;
    m_downloadActive = 0;
    m_downloadSelected->setEnabled(false);
    if (m_downloadProgress) {
        m_downloadProgress->setVisible(true);
        m_downloadProgress->setValue(0);
    }
    if (m_downloadProgressLabel) {
        m_downloadProgressLabel->setVisible(true);
        m_downloadProgressLabel->setText(
            slime::ui::t("ui.cache.download.progress").arg(0).arg(m_downloadTotal).arg(QStringLiteral("…")));
    }
    startNextDownloads();
}

void CacheDownloadDialog::startNextDownloads()
{
    constexpr int kParallel = 3; // Pages/R2 对 256 并发请求不友好；限制并发并保持反馈。
    while (m_downloadActive < kParallel && !m_downloadQueue.isEmpty()) {
        const RemoteCachePack p = m_downloadQueue.front();
        m_downloadQueue.pop_front();
        const QString name = QFileInfo(p.file).fileName();
        if (name.isEmpty() || name.contains(QStringLiteral(".."))) {
            ++m_downloadFailed;
            ++m_downloadDone;
            continue;
        }
        QDir().mkpath(QString::fromStdWString((m_store->root() / "downloaded").wstring()));
        const auto dest = m_store->root() / "downloaded" / name.toStdWString();
        ++m_downloadActive;
        if (m_downloadProgressLabel) {
            m_downloadProgressLabel->setText(
                slime::ui::t("ui.cache.download.progress")
                    .arg(m_downloadDone + 1).arg(m_downloadTotal).arg(name));
        }
        if (m_downloadProgress) {
            m_downloadProgress->setValue(
                m_downloadTotal > 0 ? (m_downloadDone * 1000 / m_downloadTotal) : 0);
        }
        m_client->downloadPackWithFallback(
            p.url, p.fallbackUrl, p, QString::fromStdWString(dest.wstring()),
            [this, name, dest](bool ok, const QString& err) {
                --m_downloadActive;
                if (!ok) {
                    ++m_downloadFailed;
                } else {
                    std::string addErr;
                    if (!m_store->addPack(dest, slime::cache::CacheStore::kDownloadedDir, &addErr)) {
                        ++m_downloadFailed;
                        if (m_availableStatus) m_availableStatus->setText(
                            slime::ui::t("ui.cache.download.failed").arg(name, QString::fromStdString(addErr)));
                    }
                }
                ++m_downloadDone;
                if (m_downloadProgress) {
                    m_downloadProgress->setValue(
                        m_downloadTotal > 0 ? (m_downloadDone * 1000 / m_downloadTotal) : 1000);
                }
                if (m_downloadProgressLabel) {
                    m_downloadProgressLabel->setText(
                        slime::ui::t("ui.cache.download.progress.done")
                            .arg(m_downloadDone - m_downloadFailed).arg(m_downloadFailed));
                }
                startNextDownloads();
            },
            [this, name](qint64 got, qint64 total) {
                if (m_downloadProgressLabel) {
                    const int pct = total > 0 ? static_cast<int>(got * 100 / total) : 0;
                    m_downloadProgressLabel->setText(
                        slime::ui::t("ui.cache.download.progress")
                            .arg(m_downloadDone + 1).arg(m_downloadTotal)
                            .arg(QStringLiteral("%1 (%2%)").arg(name).arg(pct)));
                }
            });
    }
    if (m_downloadActive == 0 && m_downloadQueue.isEmpty()) finishDownloads();
}

void CacheDownloadDialog::finishDownloads()
{
    if (m_downloadSelected) m_downloadSelected->setEnabled(m_availableTable && m_availableTable->rowCount() > 0);
    if (m_downloadProgress) m_downloadProgress->setValue(1000);
    if (m_downloadProgressLabel) {
        m_downloadProgressLabel->setText(
            slime::ui::t("ui.cache.download.progress.done")
                .arg(m_downloadDone - m_downloadFailed).arg(m_downloadFailed));
    }
    refresh();
    if (onChanged) onChanged();
}

void CacheDownloadDialog::refresh()
{
    if (!m_store) return;
    m_visible.clear();
    for (const auto& e : m_store->packs()) {
        if (e.source != slime::cache::CacheStore::kDownloadedDir) continue;
        if (matchesRange(e) && matchesSearch(e)) m_visible.push_back(e);
    }
    m_table->blockSignals(true);
    m_table->setRowCount(0);
    for (const auto& e : m_visible) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto* check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        check->setCheckState(Qt::Unchecked);
        check->setData(Qt::UserRole, QString::fromStdString(e.file));
        m_table->setItem(row, 0, check);
        m_table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("%1..%2, %3..%4")
                                                          .arg(e.rect.minX).arg(e.rect.maxX)
                                                          .arg(e.rect.minZ).arg(e.rect.maxZ)));
        m_table->setItem(row, 2, new QTableWidgetItem(mbText(e.compressedBytes)));
        auto* del = new QPushButton(slime::ui::t("ui.cache.pack.delete"), m_table);
        del->setObjectName(QStringLiteral("miniButton"));
        const std::string file = e.file;
        connect(del, &QPushButton::clicked, this, [this, file]() {
            std::string err;
            if (m_store) m_store->removePack(file, &err);
            refresh();
            if (onChanged) onChanged();
        });
        m_table->setCellWidget(row, 3, del);
    }
    m_table->blockSignals(false);
    m_table->resizeRowsToContents();
    m_summary->setText(QStringLiteral("%1 · %2").arg(m_visible.size()).arg(mbText(m_store->totalCompressedBytes())));
    m_map->setPacks(m_visible);
    updateSelectionUi();
}

void CacheDownloadDialog::updateSelectionUi()
{
    int n = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (auto* it = m_table->item(r, 0); it && it->checkState() == Qt::Checked) ++n;
    }
    m_deleteSelected->setEnabled(n > 0);
    m_deleteSelected->setText(n > 0
        ? QStringLiteral("%1 (%2)").arg(slime::ui::t("ui.cache.pack.deleteSelected")).arg(n)
        : slime::ui::t("ui.cache.pack.deleteSelected"));
}

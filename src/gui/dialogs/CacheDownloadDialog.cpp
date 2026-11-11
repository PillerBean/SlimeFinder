#include "gui/dialogs/CacheDownloadDialog.h"

#include "gui/widgets/CacheCoverageMap.h"
#include "i18n/UiText.h"

#include <QCheckBox>
#include <QButtonGroup>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString mbText(uint64_t bytes)
{
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
}
} // namespace

CacheDownloadDialog::CacheDownloadDialog(slime::cache::CacheStore* store, QWidget* parent)
    : QDialog(parent)
    , m_store(store)
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

    // 可下载页：等待官方缓存目录/下载器接入
    {
        auto* page = new QWidget(tabs);
        auto* lay = new QVBoxLayout(page);
        lay->setContentsMargins(10, 14, 10, 10);
        auto* empty = new QLabel(slime::ui::t("ui.cache.download.available.empty"), page);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignCenter);
        lay->addStretch(1);
        lay->addWidget(empty);
        lay->addStretch(1);
        tabs->addTab(page, slime::ui::t("ui.cache.download.available"));
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

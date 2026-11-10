// v3.5.2（任务 1 的界面）：挂机点（环形覆盖）对话框 —— 实现
#include "i18n/UiText.h"
#include "gui/dialogs/AfkDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cstdio>
#include <limits>

namespace {
slime::SlimeChunkPredicate makePredicate(int version, int64_t seed)
{
    const slime::Version v = (version == 1) ? slime::Version::Bedrock : slime::Version::Java;
    return [v, seed](int cx, int cz) { return slime::SlimeEngine::isSlimeChunk(v, seed, cx, cz); };
}
} // namespace

QString AfkDialog::commandFor(const slime::AfkCandidate& c)
{
    // 挂机点在地面以上即可；Y 用 100 只是占位，玩家可用 /tp 后自行下降
    return QStringLiteral("/tp @s %1 100 %2").arg(c.x).arg(c.z);
}

QString AfkDialog::cellTextFor(const slime::AfkCandidate& c)
{
    return slime::ui::t("ui.d5a72d5948cc")
        .arg(c.x)
        .arg(c.z)
        .arg(c.chunkX)
        .arg(c.chunkZ)
        .arg(static_cast<long long>(c.coveredBlocks))
        .arg(c.coveredChunks)
        .arg(QString::number(c.coveragePercent, 'f', 1));
}

AfkDialog::AfkDialog(const Context& ctx, QWidget* parent) : QDialog(parent), m_ctx(ctx)
{
    setWindowTitle(slime::ui::t("ui.b6de53b59039"));
    setMinimumSize(760, 460);

    auto* lay = new QVBoxLayout(this);

    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel(slime::ui::t("ui.c6a32c203a5a"), this));
    m_radius = new QSpinBox(this);
    m_radius->setRange(16, 512);
    m_radius->setSingleStep(16);
    m_radius->setValue(128);
    m_radius->setToolTip(slime::ui::t("ui.ed1b4230d88b"));
    top->addWidget(m_radius);
    top->addSpacing(12);
    top->addWidget(new QLabel(slime::ui::t("ui.0fbc1a0a534e"), this));
    m_topN = new QSpinBox(this);
    m_topN->setRange(1, 20);
    m_topN->setValue(5);
    top->addWidget(m_topN);
    top->addSpacing(12);
    top->addSpacing(12);
    top->addWidget(new QLabel(slime::ui::t("ui.3541a097fb69"), this));
    m_platform = new QComboBox(this);
    m_platform->addItem(slime::ui::t("ui.8accf0fe2eed"), 0);
    m_platform->addItem(slime::ui::t("ui.618fd37c0c01"), 1);
    top->addWidget(m_platform);
    top->addSpacing(12);
    top->addWidget(new QLabel(slime::ui::t("ui.257d31e37e28"), this));
    m_version = new QComboBox(this);
    for (const auto& v : slime::javaVersionOptions()) m_version->addItem(QString::fromUtf8(v.label), v.id);
    top->addWidget(m_version);
    auto* btnRe = new QPushButton(slime::ui::t("ui.3635d74e2884"), this);
    top->addWidget(btnRe);
    top->addStretch();
    lay->addLayout(top);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    lay->addWidget(m_summary);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({ QStringLiteral("#"), QStringLiteral("X"), QStringLiteral("Z"),
                                         slime::ui::t("ui.3da82d85c5de"), slime::ui::t("ui.6e1a9b47c22b"),
                                         slime::ui::t("ui.30c8ff494553"), slime::ui::t("ui.d5d366170935"),
                                         slime::ui::t("ui.d5f4497fa38f"), slime::ui::t("ui.977c87ae5b09") });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_table, 1);

    auto* bottom = new QHBoxLayout();
    auto* btnCopy = new QPushButton(slime::ui::t("ui.6ef6643d1733"), this);
    auto* btnCopyAll = new QPushButton(slime::ui::t("ui.b55b79e1c9e9"), this);
    auto* btnClose = new QPushButton(slime::ui::t("ui.6c14bd7f6f9e"), this);
    bottom->addWidget(btnCopy);
    bottom->addWidget(btnCopyAll);
    bottom->addStretch();
    bottom->addWidget(btnClose);
    lay->addLayout(bottom);

    connect(btnRe, &QPushButton::clicked, this, [this]() { recompute(); });
    connect(m_platform, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { recompute(); });
    connect(m_version, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { recompute(); });
    connect(m_radius, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { recompute(); });
    connect(m_topN, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { recompute(); });
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCopy, &QPushButton::clicked, this, [this]() {
        const int row = m_table->currentRow();
        if (row >= 0 && row < static_cast<int>(m_results.size()))
            QApplication::clipboard()->setText(commandFor(m_results[static_cast<size_t>(row)]));
    });
    connect(btnCopyAll, &QPushButton::clicked, this, [this]() {
        QStringList lines;
        for (const auto& c : m_results) {
            lines << QStringLiteral("%1   // %2").arg(commandFor(c), cellTextFor(c));
        }
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    });

    recompute();
}

void AfkDialog::recompute()
{
    m_results.clear();
    const int radius = m_radius ? m_radius->value() : 128;
    const int topN = m_topN ? m_topN->value() : 5;

    // v3.5.2（任务 3）：平台/版本 → 是否加权
    const int platform = m_platform ? m_platform->currentData().toInt() : 0;
    const int versionId = m_version ? m_version->currentData().toInt() : slime::defaultJavaVersionId();
    m_weighted = false;
    if (platform == 0 && m_biome.valid() == false) {
        m_biome.init(versionId, m_ctx.seed);
    } else if (platform == 0 && m_biome.valid()) {
        static int initedVersion = -1;
        static int64_t initedSeed = 0;
        if (initedVersion != versionId || initedSeed != m_ctx.seed) {
            m_biome.init(versionId, m_ctx.seed);
        }
        initedVersion = versionId;
        initedSeed = m_ctx.seed;
    }
    m_weighted = (platform == 0 && m_biome.valid());

    // 范围保护：窗口过大时在中心收窄；加权模式窗口上限更小（planner 限 1024 方块）
    int minX = m_ctx.minX, maxX = m_ctx.maxX, minZ = m_ctx.minZ, maxZ = m_ctx.maxZ;
    const int kMaxChunks = m_weighted ? 40 : 64;
    QString clampNote;
    if (maxX - minX + 1 > kMaxChunks || maxZ - minZ + 1 > kMaxChunks) {
        const int cx = (minX + maxX) / 2, cz = (minZ + maxZ) / 2;
        minX = cx - kMaxChunks / 2; maxX = minX + kMaxChunks - 1;
        minZ = cz - kMaxChunks / 2; maxZ = minZ + kMaxChunks - 1;
        clampNote = slime::ui::t("ui.5d620800ad53")
                        .arg(kMaxChunks)
                        .arg(m_weighted ? slime::ui::t("ui.ba3ffefed9ce") : QString());
    }

    slime::AfkPlanOptions o;
    o.area = slime::Rect{ minX, maxX, minZ, maxZ };
    o.radius = radius;
    o.topN = topN;
    o.minSeparation = 24;
    o.coarseStep = 4;
    if (m_weighted) {
        o.weightAt = [this](int bx, int bz) { return weightAtBlock(bx, bz); };
    }
    const auto pred = makePredicate(m_ctx.version, m_ctx.seed);
    m_results = slime::AfkPlanner::plan(o, pred);

    fillTable();

    if (m_summary) {
        const int64_t totalBlocks = slime::AfkPlanner::slimeBlocksInArea(o.area, pred);
        const int64_t blocks = static_cast<int64_t>(o.area.maxX - o.area.minX + 1) * (o.area.maxZ - o.area.minZ + 1) * 256;
        if (m_results.empty()) {
            m_summary->setText(slime::ui::t("ui.a3dc656d9638")
                                   .arg(m_ctx.source, clampNote));
        } else {
            const QString mode = m_weighted
                                     ? slime::ui::t("ui.33f83b437991")
                                           .arg(QString::fromUtf8(slime::BiomeMap::labelForVersionId(versionId)))
                                     : slime::ui::t("ui.c004ab47a6a1");
            m_summary->setText(slime::ui::t("ui.92d2032a130f")
                                   .arg(m_ctx.source, clampNote)
                                   .arg(blocks > 0 ? QString::number(double(totalBlocks) * 100.0 / double(blocks), 'f', 1)
                                                   : QStringLiteral("-"))
                                   .arg(cellTextFor(m_results.front()))
                                   .arg(mode));
        }
    }
    std::fprintf(stderr, "[挂机点] R=%d 候选=%d 区域=[%d..%d]x[%d..%d] 结果=%zu\n", radius, topN,
                 o.area.minX, o.area.maxX, o.area.minZ, o.area.maxZ, m_results.size());
}

// v3.5.2（任务 3）：方块 → 群系权重（1:16 采样并缓存，避免逐块查询）
double AfkDialog::weightAtBlock(int blockX, int blockZ)
{
    static int64_t lastKey = (std::numeric_limits<int64_t>::min)();
    static double lastVal = 1.0;
    const int64_t key = (static_cast<int64_t>(blockX >> 4) << 32) ^ static_cast<uint32_t>(blockZ >> 4);
    if (key == lastKey) return lastVal;
    const int id = m_biome.biomeAt(blockX, blockZ, 16);
    lastVal = slime::spawnWeight(id, slime::Platform::Java);
    lastKey = key;
    return lastVal;
}

void AfkDialog::fillTable()
{
    if (!m_table) return;
    m_table->setRowCount(static_cast<int>(m_results.size()));
    for (int i = 0; i < static_cast<int>(m_results.size()); ++i) {
        const auto& c = m_results[static_cast<size_t>(i)];
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(c.x)));
        m_table->setItem(i, 2, new QTableWidgetItem(QString::number(c.z)));
        m_table->setItem(i, 3, new QTableWidgetItem(QStringLiteral("%1,%2").arg(c.chunkX).arg(c.chunkZ)));
        m_table->setItem(i, 4, new QTableWidgetItem(QString::number(static_cast<long long>(c.coveredBlocks))));
        m_table->setItem(i, 5, new QTableWidgetItem(QString::number(c.coveredChunks)));
        m_table->setItem(i, 6, new QTableWidgetItem(QStringLiteral("%1%").arg(QString::number(c.coveragePercent, 'f', 1))));
        m_table->setItem(i, 7, new QTableWidgetItem(QString::number(c.coveredWeight, 'f', 0)));
        m_table->setItem(i, 8, new QTableWidgetItem(QString::number(c.avgWeight, 'f', 2)));
        if (c.onChunkCorner) {
            m_table->item(i, 1)->setToolTip(slime::ui::t("ui.c327477ff7e5"));
        }
    }
    if (!m_results.empty()) m_table->selectRow(0);
}

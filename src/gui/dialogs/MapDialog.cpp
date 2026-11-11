// v3.5.2（任务 2）：地图预览对话框 —— 实现（含群系彩色底图与版本选择）
#include "i18n/UiText.h"
#include "gui/dialogs/MapDialog.h"

#include "gui/widgets/MapPreviewWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cstdio>

MapDialog::MapDialog(const Context& ctx, QWidget* parent) : QDialog(parent), m_ctx(ctx)
{
    setWindowTitle(slime::ui::t("ui.5392e685cc44"));
    setMinimumSize(1040, 700);

    auto* lay = new QVBoxLayout(this);

    auto* bar = new QHBoxLayout();
    auto* btnIn = new QPushButton(slime::ui::t("ui.d7f48a059cf3"), this);
    auto* btnOut = new QPushButton(slime::ui::t("ui.11f8516f82b1"), this);
    auto* btnFit = new QPushButton(slime::ui::t("ui.e9562f6fa7af"), this);
    auto* btnReset = new QPushButton(slime::ui::t("ui.52a5c38f0efc"), this);
    m_cbBiome = new QCheckBox(slime::ui::t("ui.af0edd97079c"), this);
    m_cbBiome->setChecked(true);
    m_cbSlime = new QCheckBox(slime::ui::t("ui.836b19da6804"), this);
    m_cbSlime->setChecked(true);
    m_cbResults = new QCheckBox(slime::ui::t("ui.2f17ec14763f"), this);
    m_cbResults->setChecked(true);
    m_cbAfk = new QCheckBox(slime::ui::t("ui.469105ed1a86"), this);
    m_cbAfk->setChecked(true);
    bar->addWidget(btnIn);
    bar->addWidget(btnOut);
    bar->addWidget(btnFit);
    bar->addWidget(btnReset);
    bar->addSpacing(12);
    bar->addWidget(m_cbBiome);
    bar->addWidget(m_cbSlime);
    bar->addWidget(m_cbResults);
    bar->addWidget(m_cbAfk);
    lay->addLayout(bar);

    auto* bar2 = new QHBoxLayout();
    bar2->addWidget(new QLabel(slime::ui::t("ui.82879877a77e"), this));
    m_versionCombo = new QComboBox(this);
    for (const auto& v : slime::javaVersionOptions())
        m_versionCombo->addItem(QString::fromUtf8(v.label), v.id);
    m_versionCombo->setCurrentIndex(0);
    bar2->addWidget(m_versionCombo);
    bar2->addSpacing(12);
    bar2->addWidget(new QLabel(slime::ui::t("ui.42668c3e0788"), this));
    m_scaleCombo = new QComboBox(this);
    m_scaleCombo->addItem(slime::ui::t("ui.451a3a0adaef"), 1);
    m_scaleCombo->addItem(slime::ui::t("ui.8ca46f1229b0"), 4);
    m_scaleCombo->addItem(slime::ui::t("ui.8eb7f59f4e6b"), 16);
    m_scaleCombo->addItem(slime::ui::t("ui.92fe3e55916c"), 64);
    m_scaleCombo->setCurrentIndex(1);
    bar2->addWidget(m_scaleCombo);
    bar2->addSpacing(12);
    bar2->addWidget(new QLabel(slime::ui::t("ui.54636b347288"), this));
    m_radius = new QSpinBox(this);
    m_radius->setRange(16, 512);
    m_radius->setSingleStep(16);
    m_radius->setValue(128);
    bar2->addWidget(m_radius);
    auto* btnAfk = new QPushButton(slime::ui::t("ui.edf45feb592e"), this);
    btnAfk->setToolTip(slime::ui::t("ui.8eae02fe83a9"));
    bar2->addWidget(btnAfk);
    bar2->addStretch();
    lay->addLayout(bar2);

    m_map = new MapPreviewWidget(this);
    {
        MapPreviewWidget::Layer l;
        l.version = m_ctx.version;
        l.seed = m_ctx.seed;
        l.afkRadius = m_radius->value();
        l.biomeScale = m_scaleCombo->currentData().toInt();
        m_map->setLayer(l);
    }
    lay->addWidget(m_map, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch();
    auto* btnClose = new QPushButton(slime::ui::t("ui.6c14bd7f6f9e"), this);
    bottom->addWidget(btnClose);
    lay->addLayout(bottom);

    connect(btnIn, &QPushButton::clicked, this, [this]() {
        m_map->zoomAt(1.25, m_map->rect().center());
        refreshStatus();
    });
    connect(btnOut, &QPushButton::clicked, this, [this]() {
        m_map->zoomAt(0.8, m_map->rect().center());
        refreshStatus();
    });
    connect(btnFit, &QPushButton::clicked, this, [this]() {
        m_map->fitToContent();
        refreshStatus();
    });
    connect(btnReset, &QPushButton::clicked, this, [this]() {
        m_map->resetView();
        refreshStatus();
    });
    connect(btnAfk, &QPushButton::clicked, this, [this]() { computeAfk(); });
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    const auto applyLayer = [this]() {
        auto l = m_map->layer();
        l.showBiome = m_cbBiome->isChecked();
        l.showSlime = m_cbSlime->isChecked();
        l.showResults = m_cbResults->isChecked();
        l.showAfk = m_cbAfk->isChecked();
        l.biomeScale = m_scaleCombo->currentData().toInt();
        l.afkRadius = m_radius->value();
        m_map->setLayer(l);
        refreshStatus();
    };
    connect(m_cbBiome, &QCheckBox::toggled, this, [applyLayer](bool) { applyLayer(); });
    connect(m_cbSlime, &QCheckBox::toggled, this, [applyLayer](bool) { applyLayer(); });
    connect(m_cbResults, &QCheckBox::toggled, this, [applyLayer](bool) { applyLayer(); });
    connect(m_cbAfk, &QCheckBox::toggled, this, [applyLayer](bool) { applyLayer(); });
    connect(m_scaleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [applyLayer](int) { applyLayer(); });
    connect(m_radius, QOverload<int>::of(&QSpinBox::valueChanged), this, [applyLayer](int) { applyLayer(); });
    connect(m_versionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { initBiomeSource(); refreshStatus(); });

    // 初始视图：搜索范围中心
    const double cx = (m_ctx.minX + m_ctx.maxX + 1) * 16.0 / 2.0;
    const double cz = (m_ctx.minZ + m_ctx.maxZ + 1) * 16.0 / 2.0;
    const double spanBlocks = (std::max)(16.0, static_cast<double>((std::max)(m_ctx.maxX - m_ctx.minX + 1,
                                                                            m_ctx.maxZ - m_ctx.minZ + 1)) * 16.0);
    double scale = 640.0 / spanBlocks;
    scale = (std::max)(0.5, (std::min)(8.0, scale));
    m_map->setView(cx, cz, scale);

    initBiomeSource();
    refreshStatus();
    std::fprintf(stderr, "[地图预览] 打开：%s 版本=%s 种子=%lld 中心=(%.0f,%.0f) 缩放=%.2f px/格 群系=%d\n",
                 m_ctx.source.toUtf8().constData(), m_ctx.version == 1 ? "基岩版" : "Java",
                 static_cast<long long>(m_ctx.seed), cx, cz, m_map->pxPerBlock(),
                 m_map->layer().biomeAvailable ? 1 : 0);
}

void MapDialog::initBiomeSource()
{
    if (!m_map) return;
    // v3.5.21：基岩版同样可以上色 —— 同一 64 位种子下 Java 与基岩版群系一致（1.18+）
    const int versionId = m_versionCombo ? m_versionCombo->currentData().toInt() : slime::defaultJavaVersionId();
    m_map->setBiomeSource(versionId, m_ctx.seed);
    m_map->setBiomeParityNote(m_ctx.version == 1);
}

void MapDialog::setResults(const std::vector<slime::MatchResult>& r)
{
    if (m_map) m_map->setResults(r);
    refreshStatus();
}

void MapDialog::setAfkCandidates(const std::vector<slime::AfkCandidate>& c)
{
    m_afk = c;
    if (m_map) m_map->setAfkCandidates(c);
    refreshStatus();
}

void MapDialog::computeAfk()
{
    slime::AfkPlanOptions o;
    o.area = slime::Rect{ m_ctx.minX, m_ctx.maxX, m_ctx.minZ, m_ctx.maxZ };
    o.radius = m_radius ? m_radius->value() : 128;
    o.topN = 5;
    o.minSeparation = 24;
    o.coarseStep = 4;
    const slime::Version v = (m_ctx.version == 1) ? slime::Version::Bedrock : slime::Version::Java;
    const auto pred = [v, seed = m_ctx.seed](int cx, int cz) {
        return slime::SlimeEngine::isSlimeChunk(v, seed, cx, cz);
    };
    std::vector<slime::AfkCandidate> res = slime::AfkPlanner::plan(o, pred);
    setAfkCandidates(res);
    if (!res.empty()) m_map->fitToContent();
    std::fprintf(stderr, "[地图预览] 计算挂机点：R=%d 结果=%zu\n", o.radius, res.size());
}

void MapDialog::refreshStatus()
{
    if (!m_status || !m_map) return;
    const QRectF vis = m_map->visibleBlockRect();
    QString s = slime::ui::t("ui.ced733299632")
                    .arg(m_ctx.source)
                    .arg((std::max)(1, static_cast<int>(vis.width() / 16.0)))
                    .arg((std::max)(1, static_cast<int>(vis.height() / 16.0)))
                    .arg(QString::number(m_map->pxPerBlock(), 'f', 2));
    if (m_map->layer().biomeAvailable) {
        s += slime::ui::t("ui.b53afcd34b22")
                 .arg(QString::fromUtf8(slime::BiomeMap::labelForVersionId(m_versionCombo->currentData().toInt())))
                 .arg(m_scaleCombo->currentData().toInt());
    } else {
        s += slime::ui::t("ui.4a55f14a024d").arg(m_ctx.version == 1 ? slime::ui::t("ui.618fd37c0c01") : slime::ui::t("ui.4b33c0ba63ba"));
    }
    if (!m_afk.empty()) {
        const auto& b = m_afk.front();
        s += slime::ui::t("ui.237f2fa41a80")
                 .arg(b.x)
                 .arg(b.z)
                 .arg(static_cast<long long>(b.coveredBlocks))
                 .arg(b.coveredChunks);
    }
    s += slime::ui::t("ui.8f25595b3b1f");
    m_status->setText(s);
}

int MapDialog::currentVersionId() const
{
    return m_versionCombo ? m_versionCombo->currentData().toInt() : slime::defaultJavaVersionId();
}

bool MapDialog::biomeAvailable() const
{
    return m_map && m_map->layer().biomeAvailable;
}

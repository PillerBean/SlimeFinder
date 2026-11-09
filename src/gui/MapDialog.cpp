// v3.5.2（任务 2）：地图预览对话框 —— 实现（含群系彩色底图与版本选择）
#include "gui/MapDialog.h"

#include "gui/MapPreviewWidget.h"

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
    setWindowTitle(QStringLiteral("地图预览（群系 / 史莱姆区块 / 搜索结果 / 挂机点）"));
    setMinimumSize(1040, 700);

    auto* lay = new QVBoxLayout(this);

    auto* bar = new QHBoxLayout();
    auto* btnIn = new QPushButton(QStringLiteral("放大"), this);
    auto* btnOut = new QPushButton(QStringLiteral("缩小"), this);
    auto* btnFit = new QPushButton(QStringLiteral("适应内容"), this);
    auto* btnReset = new QPushButton(QStringLiteral("复位"), this);
    m_cbBiome = new QCheckBox(QStringLiteral("群系颜色"), this);
    m_cbBiome->setChecked(true);
    m_cbSlime = new QCheckBox(QStringLiteral("史莱姆区块"), this);
    m_cbSlime->setChecked(true);
    m_cbResults = new QCheckBox(QStringLiteral("搜索结果"), this);
    m_cbResults->setChecked(true);
    m_cbAfk = new QCheckBox(QStringLiteral("挂机点"), this);
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
    bar2->addWidget(new QLabel(QStringLiteral("群系版本（Java）："), this));
    m_versionCombo = new QComboBox(this);
    for (const auto& v : slime::javaVersionOptions())
        m_versionCombo->addItem(QString::fromUtf8(v.label), v.id);
    m_versionCombo->setCurrentIndex(0);
    bar2->addWidget(m_versionCombo);
    bar2->addSpacing(12);
    bar2->addWidget(new QLabel(QStringLiteral("群系精度："), this));
    m_scaleCombo = new QComboBox(this);
    m_scaleCombo->addItem(QStringLiteral("1:1（最细，慢）"), 1);
    m_scaleCombo->addItem(QStringLiteral("1:4（推荐）"), 4);
    m_scaleCombo->addItem(QStringLiteral("1:16（快）"), 16);
    m_scaleCombo->addItem(QStringLiteral("1:64（最粗）"), 64);
    m_scaleCombo->setCurrentIndex(1);
    bar2->addWidget(m_scaleCombo);
    bar2->addSpacing(12);
    bar2->addWidget(new QLabel(QStringLiteral("半径 R："), this));
    m_radius = new QSpinBox(this);
    m_radius->setRange(16, 512);
    m_radius->setSingleStep(16);
    m_radius->setValue(128);
    bar2->addWidget(m_radius);
    auto* btnAfk = new QPushButton(QStringLiteral("计算挂机点"), this);
    btnAfk->setToolTip(QStringLiteral("按 1/16 区块精度算挂机点并画到地图上（同「挂机点」对话框）"));
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
    auto* btnClose = new QPushButton(QStringLiteral("关闭"), this);
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
    QString s = QStringLiteral("来源：%1｜可见 %2×%3 区块｜缩放 %4 px/格")
                    .arg(m_ctx.source)
                    .arg((std::max)(1, static_cast<int>(vis.width() / 16.0)))
                    .arg((std::max)(1, static_cast<int>(vis.height() / 16.0)))
                    .arg(QString::number(m_map->pxPerBlock(), 'f', 2));
    if (m_map->layer().biomeAvailable) {
        s += QStringLiteral("｜群系 Java %1（1:%2）")
                 .arg(QString::fromUtf8(slime::BiomeMap::labelForVersionId(m_versionCombo->currentData().toInt())))
                 .arg(m_scaleCombo->currentData().toInt());
    } else {
        s += QStringLiteral("｜群系：不可用（%1）").arg(m_ctx.version == 1 ? QStringLiteral("基岩版") : QStringLiteral("该版本"));
    }
    if (!m_afk.empty()) {
        const auto& b = m_afk.front();
        s += QStringLiteral("｜最优挂机点 (%1, %2)：覆盖 %3 格 / %4 区块")
                 .arg(b.x)
                 .arg(b.z)
                 .arg(static_cast<long long>(b.coveredBlocks))
                 .arg(b.coveredChunks);
    }
    s += QStringLiteral("｜滚轮缩放 · 拖拽平移 · 双击复位");
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

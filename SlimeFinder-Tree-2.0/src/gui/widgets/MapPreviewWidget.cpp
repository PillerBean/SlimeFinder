// v3.5.3（地图优化）：地图预览控件 —— 实现
#include "i18n/UiText.h"
#include "gui/widgets/MapPreviewWidget.h"

#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <QCoreApplication>

namespace {

// v3.6.12：地图底图缓存目录（程序目录下的 cache/maps）
static std::filesystem::path mapCacheDir()
{
    const std::filesystem::path base(QCoreApplication::applicationDirPath().toStdWString());
    return base / L"cache" / L"maps";
}

constexpr double kMinScale = 0.5;
constexpr double kMaxScale = 8.0;
constexpr int kChunk = 16;
constexpr int kMaxBiomeCells = 400000;   // 单次采样上限
constexpr double kCacheMargin = 0.25;    // 缓存余量（相对视口）
} // namespace

MapPreviewWidget::MapPreviewWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(480, 320);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true); // v3.5.5：悬停显示群系
    setToolTip(slime::ui::t("ui.41322a26b66a"));
    m_resampleTimer = new QTimer(this);
    m_resampleTimer->setSingleShot(true);
    m_resampleTimer->setInterval(120);
    connect(m_resampleTimer, &QTimer::timeout, this, [this]() {
        m_resamplePending = false;
        ensureBiomeImage();
        if (!layerCoversView()) buildLayerPixmap();   // v3.5.7：静止后重建一次合成底图
        update();
    });
}

void MapPreviewWidget::setLayer(const Layer& l)
{
    const bool detailChanged = (m_layer.biomeScale != l.biomeScale) || (m_layer.showBiome != l.showBiome) ||
                               (m_layer.autoDetail != l.autoDetail);
    m_layer = l;
    if (detailChanged) invalidateBiomeCache();
    invalidateLayer();   // v3.5.7：史莱姆/网格/群系显示开关变化都要重画底图
    m_slimeCountCache = -1;
    update();
}

void MapPreviewWidget::centerOnChunk(int chunkX, int chunkZ)
{
    // 区块中心 ≈ (chunkX+0.5)*16，(chunkZ+0.5)*16
    m_center = QPointF((chunkX + 0.5) * kChunk, (chunkZ + 0.5) * kChunk);
    invalidateLayer();
    scheduleResample();
    update();
}

void MapPreviewWidget::setResults(const std::vector<slime::MatchResult>& r)
{
    m_results = r;
    update();
}

void MapPreviewWidget::setAfkCandidates(const std::vector<slime::AfkCandidate>& c)
{
    const bool sameSize = (m_afk.size() == c.size());
    m_afk = c;
    // v3.5.10：新一批候选默认不画环（点击环心/结果行才显示）
    if (!sameSize || m_focusedAfk >= static_cast<int>(m_afk.size())) m_focusedAfk = -1;
    update();
}

void MapPreviewWidget::setFocusedAfk(int index)
{
    const int n = static_cast<int>(m_afk.size());
    const int v = (index >= 0 && index < n) ? index : -1;
    if (v == m_focusedAfk) return;
    m_focusedAfk = v;
    update();
}

int MapPreviewWidget::afkIndexAt(const QPoint& px) const
{
    if (!m_layer.showAfk) return -1;
    for (size_t i = 0; i < m_afk.size(); ++i) {
        const QPointF ctr = blockToPx(QPointF(m_afk[i].x + 0.5, m_afk[i].z + 0.5));
        if (QLineF(ctr, QPointF(px)).length() <= 11.0) return static_cast<int>(i);
    }
    return -1;
}

bool MapPreviewWidget::setBiomeSource(int versionId, int64_t seed)
{
    m_biome = std::make_unique<slime::BiomeMap>();
    m_biomeVersionId = versionId;
    m_biomeSeed = seed;
    const bool ok = m_biome->init(versionId, seed);
    m_layer.biomeAvailable = ok;
    invalidateBiomeCache();
    invalidateLayer();   // v3.5.22：切版本/换种子后必须重画合成底图（此前会沿用旧图，缩放才刷新）
    update();
    std::fprintf(stderr, "[地图] 群系源：version=%d seed=%lld → %s\n", versionId,
                 static_cast<long long>(seed),
                 ok ? (m_biomeParity ? "可用（基岩版：按 Java 同种子近似；两版非逐像素一致）" : "可用") : "不可用（版本不支持）");
    return ok;
}

int MapPreviewWidget::biomeScaleForScale(double pxPerBlock) const
{
    // v3.5.17（用户要求）：只保留「精度最高的两档」—— 1:1 与 1:4，不再出现 1:16/1:64。
    //   缩小到整片区域时仍然只有这两档；采样量由 ensureBiomeImage() 的格数上限兜底
    //   （超限就只采视口、再超限就不画群系，不会退回更粗的精度）。
    return (pxPerBlock >= 4.0) ? 1 : 4;
}

int MapPreviewWidget::effectiveBiomeScale() const
{
    if (!m_layer.showBiome || !m_layer.biomeAvailable) return 0;
    if (!m_layer.autoDetail) return m_layer.biomeScale;
    return biomeScaleForScale(m_scale);
}

void MapPreviewWidget::setView(double centerXBlock, double centerZBlock, double pxPerBlock)
{
    const bool scaleChanged = std::abs(m_scale - pxPerBlock) > 1e-12;
    m_center = QPointF(centerXBlock, centerZBlock);
    m_scale = pxPerBlock;
    clampScale();
    // v3.5.7：只有缩放变化才让缓存失效；纯平移靠缓存余量覆盖（不再重建）
    if (scaleChanged) invalidateLayer();
    m_slimeCountCache = -1;
    scheduleResample();
    update();
}

void MapPreviewWidget::clampScale()
{
    m_scale = (std::max)(kMinScale, (std::min)(kMaxScale, m_scale));
}

QPointF MapPreviewWidget::blockToPx(const QPointF& b) const
{
    return QPointF((b.x() - m_center.x()) * m_scale + width() / 2.0,
                   (b.y() - m_center.y()) * m_scale + height() / 2.0);
}

QPointF MapPreviewWidget::pxToBlock(const QPointF& p) const
{
    return QPointF((p.x() - width() / 2.0) / m_scale + m_center.x(),
                   (p.y() - height() / 2.0) / m_scale + m_center.y());
}

QRectF MapPreviewWidget::visibleBlockRect() const
{
    const QPointF tl = pxToBlock(QPointF(0, 0));
    const QPointF br = pxToBlock(QPointF(width(), height()));
    return QRectF(tl, br).normalized();
}

bool MapPreviewWidget::predicate(int chunkX, int chunkZ) const
{
    const slime::Version v = (m_layer.version == 1) ? slime::Version::Bedrock : slime::Version::Java;
    return slime::SlimeEngine::isSlimeChunk(v, m_layer.seed, chunkX, chunkZ);
}

int MapPreviewWidget::visibleSlimeChunkCount() const
{
    // v3.5.3：缓存（同一视图不重复算；缩放变化才失效）
    const int keyCx = static_cast<int>(m_center.x());
    const int keyCz = static_cast<int>(m_center.y());
    static int lastCx = 0x7fffffff, lastCz = 0x7fffffff;
    static double lastScale = -1.0;
    static int lastVal = -1;
    if (lastVal >= 0 && lastCx == keyCx && lastCz == keyCz && std::abs(lastScale - m_scale) < 1e-9)
        return lastVal;
    const QRectF r = visibleBlockRect();
    const int c0x = static_cast<int>(std::floor(r.left() / kChunk));
    const int c1x = static_cast<int>(std::floor(r.right() / kChunk));
    const int c0z = static_cast<int>(std::floor(r.top() / kChunk));
    const int c1z = static_cast<int>(std::floor(r.bottom() / kChunk));
    int n = 0;
    for (int cz = c0z; cz <= c1z; ++cz)
        for (int cx = c0x; cx <= c1x; ++cx)
            if (predicate(cx, cz)) ++n;
    m_slimeCountCache = n;
    m_slimeCountScale = m_scale;
    lastCx = keyCx;
    lastCz = keyCz;
    lastScale = m_scale;
    lastVal = n;
    return n;
}

void MapPreviewWidget::zoomAt(double factor, const QPoint& atPx)
{
    const QPointF before = pxToBlock(QPointF(atPx));
    m_scale *= factor;
    clampScale();
    const QPointF after = pxToBlock(QPointF(atPx));
    m_center += before - after;
    m_slimeCountCache = -1;
    scheduleResample();
    update();
}

void MapPreviewWidget::resetView()
{
    m_center = QPointF(0.0, 0.0);
    m_scale = 2.0;
    m_slimeCountCache = -1;
    if (!m_results.empty() || !m_afk.empty()) {
        fitToContent();
    } else {
        scheduleResample();
        update();
    }
}

void MapPreviewWidget::fitToContent()
{
    double minX = 1e18, maxX = -1e18, minZ = 1e18, maxZ = -1e18;
    for (const auto& r : m_results) {
        minX = (std::min)(minX, static_cast<double>(r.minX * kChunk));
        maxX = (std::max)(maxX, static_cast<double>((r.maxX + 1) * kChunk));
        minZ = (std::min)(minZ, static_cast<double>(r.minZ * kChunk));
        maxZ = (std::max)(maxZ, static_cast<double>((r.maxZ + 1) * kChunk));
    }
    for (const auto& c : m_afk) {
        const double r = m_layer.afkRadius;
        minX = (std::min)(minX, c.x - r);
        maxX = (std::max)(maxX, c.x + r);
        minZ = (std::min)(minZ, c.z - r);
        maxZ = (std::max)(maxZ, c.z + r);
    }
    if (maxX <= minX || maxZ <= minZ) {
        m_center = QPointF(0.0, 0.0);
        m_scale = 2.0;
        m_slimeCountCache = -1;
        scheduleResample();
        update();
        return;
    }
    m_center = QPointF((minX + maxX) / 2.0, (minZ + maxZ) / 2.0);
    const double padPx = 24.0;
    const double sx = (width() - 2 * padPx) / (maxX - minX);
    const double sz = (height() - 2 * padPx) / (maxZ - minZ);
    m_scale = (std::min)(sx, sz);
    clampScale();
    m_slimeCountCache = -1;
    scheduleResample();
    update();
}

void MapPreviewWidget::scheduleResample()
{
    if (m_resampleTimer && !m_resampleTimer->isActive()) {
        m_resampleTimer->start();
        m_resamplePending = true;
    }
}

void MapPreviewWidget::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    m_slimeCountCache = -1;
    scheduleResample();
}

// v3.6.13（用户目标：来回拖动地图不要重新加载）：
//   **内存分块缓存**取代「一张整图 + 落盘 PNG」。
//   把群系采样切成 128×128 格的小块（tile），按 (精度, tileX, tileZ) 常驻内存；
//   拖动时只补「新进入视口」的块，已经看过的块直接复用 —— 来回拖不重新采样。
//   每块 128×128 RGB32 ≈ 64 KB，最多保留 256 块（≈16 MB），超出按最久未用淘汰。
static constexpr int kTileCells = 128;
static constexpr int kTileBudget = 256;

namespace {
inline quint64 tileKey(int detail, int tx, int tz)
{
    return (static_cast<quint64>(detail) << 60) ^ (static_cast<quint64>(static_cast<quint32>(tx)) << 30) ^
           static_cast<quint64>(static_cast<quint32>(tz));
}
} // namespace

void MapPreviewWidget::invalidateBiomeCache()
{
    m_tiles.clear();
    m_tileClock = 0;
}

void MapPreviewWidget::ensureBiomeImage()
{
    if (!m_layer.showBiome || !m_layer.biomeAvailable || !m_biome || !m_biome->valid()) return;
    // v3.6.15（用户反馈：拖动卡顿 / 不要预加载周围）：
    //   拖动过程中**不生成任何新分块** —— 只画已经缓存过的块，
    //   松手后再补当前视口缺的块。这样既不会边拖边算（卡顿），也不会顺手把周围一圈采出来。
    if (m_dragging) return;
    const int detail = effectiveBiomeScale();
    if (detail <= 0) return;
    if (m_tileDetail != detail) {          // 精度切换 ⇒ 旧块不再可用
        m_tiles.clear();
        m_tileDetail = detail;
    }

    // 视口覆盖的块范围（各向外多取 1 块，拖动时更顺）
    const QRectF vis = visibleBlockRect();
    const int tileBlocks = kTileCells * detail;
    // 用 floorDiv 处理负坐标
    const auto floorDiv = [](int a, int b) { return (a >= 0) ? (a / b) : -(((-a) + b - 1) / b); };
    // v3.6.15：只取「视图真正覆盖到的块」，不再向四周预取一块
    const int tx0 = floorDiv(static_cast<int>(std::floor(vis.left())), tileBlocks);
    const int tx1 = floorDiv(static_cast<int>(std::ceil(vis.right()) - 1), tileBlocks);
    const int tz0 = floorDiv(static_cast<int>(std::floor(vis.top())), tileBlocks);
    const int tz1 = floorDiv(static_cast<int>(std::ceil(vis.bottom()) - 1), tileBlocks);

    QElapsedTimer t;
    t.start();
    int generated = 0;
    for (int tz = tz0; tz <= tz1; ++tz) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const quint64 key = tileKey(detail, tx, tz);
            auto it = m_tiles.find(key);
            if (it != m_tiles.end()) {
                it->stamp = ++m_tileClock;   // 触碰 = 最近使用
                continue;
            }
            const int blockX0 = tx * tileBlocks;
            const int blockZ0 = tz * tileBlocks;
            std::vector<int> ids;
            if (!m_biome->genArea(detail, blockX0, blockZ0, kTileCells, kTileCells, ids)) continue;
            BiomeTile tile;
            tile.img = QImage(kTileCells, kTileCells, QImage::Format_RGB32);
            for (int z = 0; z < kTileCells; ++z) {
                auto* line = reinterpret_cast<QRgb*>(tile.img.scanLine(z));
                for (int x = 0; x < kTileCells; ++x) {
                    const uint32_t rgb = slime::BiomeMap::colorFor(ids[static_cast<size_t>(z) * kTileCells + x]);
                    // 底图略降饱和，让绿色史莱姆区块更跳
                    int r = static_cast<int>((rgb >> 16) & 0xFF), g = static_cast<int>((rgb >> 8) & 0xFF),
                        b = static_cast<int>(rgb & 0xFF);
                    const int gray = (r * 30 + g * 59 + b * 11) / 100;
                    r = r * 82 / 100 + gray * 18 / 100;
                    g = g * 82 / 100 + gray * 18 / 100;
                    b = b * 82 / 100 + gray * 18 / 100;
                    line[x] = qRgb(r, g, b);
                }
            }
            tile.x0 = blockX0;
            tile.z0 = blockZ0;
            tile.stamp = ++m_tileClock;
            m_tiles.insert(key, tile);
            ++generated;
            ++m_mapTilesBuilt;
        }
    }
    if (generated > 0) {
        m_lastSampleMs = t.nsecsElapsed() / 1e6;
        m_lastSampleCells = generated * kTileCells * kTileCells;
        ++m_sampleCount;
        std::fprintf(stderr, "[地图] 群系分块：新采 %d 块（共缓存 %d 块，%dx%d 格/块）= %.1f ms\n", generated,
                     m_tiles.size(), kTileCells, kTileCells, m_lastSampleMs);
    }
    // 预算淘汰（最久未用）
    if (m_tiles.size() > kTileBudget) {
        QList<quint64> keys = m_tiles.keys();
        std::sort(keys.begin(), keys.end(), [this](quint64 a, quint64 b) {
            return m_tiles.value(a).stamp < m_tiles.value(b).stamp;
        });
        const int drop = m_tiles.size() - kTileBudget;
        for (int i = 0; i < drop; ++i) m_tiles.remove(keys[static_cast<int>(i)]);
    }
}

// v3.6.13：把当前内存里的所有分块拼成一张图 —— 只给自检/截图用，不参与绘制路径
const QImage& MapPreviewWidget::lastBiomeImage() const
{
    if (m_tiles.isEmpty()) {
        m_diagImage = QImage();
        return m_diagImage;
    }
    int x0 = INT_MAX, z0 = INT_MAX, x1 = INT_MIN, z1 = INT_MIN;
    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it) {
        const BiomeTile& t = it.value();
        x0 = (std::min)(x0, t.x0);
        z0 = (std::min)(z0, t.z0);
        x1 = (std::max)(x1, t.x0 + 128);
        z1 = (std::max)(z1, t.z0 + 128);
    }
    const int w = x1 - x0, h = z1 - z0;
    if (w <= 0 || h <= 0) {
        m_diagImage = QImage();
        return m_diagImage;
    }
    m_diagImage = QImage(w, h, QImage::Format_RGB32);
    m_diagImage.fill(Qt::black);
    QPainter pp(&m_diagImage);
    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it) {
        const BiomeTile& t = it.value();
        pp.drawImage(QPoint(t.x0 - x0, t.z0 - z0), t.img);
    }
    return m_diagImage;
}

void MapPreviewWidget::refreshNow()
{
    if (m_resampleTimer) m_resampleTimer->stop();
    m_resamplePending = false;
    ensureBiomeImage();
    update();
}

// v3.5.5：鼠标处群系名（按 16 格缓存，避免每次移动都查询）
QString MapPreviewWidget::hoverBiomeName()
{
    if (!m_biome || !m_biome->valid() || !m_hoverValid) return QString();
    // v3.5.6：按当前显示精度查询（显示 1:1 时按 1:1 查，避免「放大后群系对不上」）
    const int q = effectiveBiomeScale();
    const int scale = (q == 1) ? 1 : 4;
    const int key = scale * 1000003 + (m_hoverBlockX / scale) * 100003 + (m_hoverBlockZ / scale);
    if (key != m_hoverCellKey) {
        m_hoverCellKey = key;
        m_hoverBiome = m_biome->biomeAt(m_hoverBlockX, m_hoverBlockZ, scale);
    }
    if (m_hoverBiome < 0) return slime::ui::t("ui.d9c32a4c3dda");
    const QString name = QString::fromUtf8(slime::BiomeMap::nameFor(m_hoverBiome));
    // v3.6.7：表里没有的 ID 不再显示裸 "unknown"
    if (name.isEmpty() || name == QLatin1String("unknown"))
        return slime::ui::t("ui.640ed42a696e").arg(m_hoverBiome);
    return name;
}

// v3.5.5：边界坐标刻度（上边 X 轴、左边 Z 轴；步长 1/2/5×10^n，约每 90px 一格）
void MapPreviewWidget::drawAxisLabels(QPainter& p)
{
    const QRectF vis = visibleBlockRect();
    p.save();
    p.setPen(QColor(255, 255, 255, 210));
    QFont f = p.font();
    f.setPointSizeF((std::max)(7.0, f.pointSizeF() - 1.5));
    p.setFont(f);
    double step = std::pow(10.0, std::floor(std::log10((std::max)(1.0, 90.0 / m_scale))));
    const double cand[] = { 1.0, 2.0, 5.0, 10.0 };
    for (double k : cand) {
        if (step * k * m_scale >= 70.0) {
            step = step * k;
            break;
        }
    }
    const auto labelOf = [&](double v) {
        return step >= 1000.0 ? QStringLiteral("%1k").arg(v / 1000.0, 0, 'g', 4)
                              : QStringLiteral("%1").arg(static_cast<long long>(v));
    };
    const double x0 = std::floor(vis.left() / step) * step;
    for (double x = x0; x <= vis.right(); x += step) {
        const double px = blockToPx(QPointF(x, 0)).x();
        if (px < 2 || px > width() - 2) continue;
        p.drawLine(QPointF(px, 0), QPointF(px, 5));
        p.drawText(QPointF(px + 3, 14), labelOf(x));
    }
    const double z0 = std::floor(vis.top() / step) * step;
    for (double z = z0; z <= vis.bottom(); z += step) {
        const double py = blockToPx(QPointF(0, z)).y();
        if (py < 12 || py > height() - 2) continue;
        p.drawLine(QPointF(0, py), QPointF(5, py));
        p.drawText(QPointF(7, py - 2), labelOf(z));
    }
    p.restore();
}

void MapPreviewWidget::drawBiomeLayer(QPainter& p)
{
    if (!m_layer.showBiome || !m_layer.biomeAvailable) return;
    ensureBiomeImage();
    if (m_tiles.isEmpty()) return;
    const int detail = m_tileDetail;
    if (detail <= 0) return;
    const int tileBlocks = kTileCells * detail;
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it) {
        const BiomeTile& tile = it.value();
        const QRectF r(blockToPx(QPointF(tile.x0, tile.z0)), blockToPx(QPointF(tile.x0 + tileBlocks, tile.z0 + tileBlocks)));
        if (!r.intersects(QRectF(rect()))) continue;   // 屏幕外的块不画
        p.drawImage(r, tile.img);
    }
}

void MapPreviewWidget::drawLayerPixmap(QPainter& p, const QPixmap& pm, int x0, int z0, int wBlocks, int hBlocks)
{
    if (pm.isNull() || wBlocks <= 0 || hBlocks <= 0) return;
    const QPointF tl = blockToPx(QPointF(x0, z0));
    const QPointF br = blockToPx(QPointF(x0 + wBlocks, z0 + hBlocks));
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawPixmap(QRectF(tl, br), pm, QRectF(pm.rect()));
}

// v3.5.7：底图三层（群系 / 史莱姆区块 / 网格）—— 既可画到控件，也可画进缓存 pixmap
void MapPreviewWidget::drawBaseLayers(QPainter& p, const QRectF& area)
{
    const bool dark = palette().base().color().lightness() < 128;
    drawBiomeLayer(p);

    // v3.5.22：按「要画的方块矩形」决定区块范围。此前固定用视口 ——
    //   而合成底图带 25% 余量，余量区域就没画史莱姆区块，平移后边缘出现空洞。
    const QRectF vis = area.isNull() ? visibleBlockRect() : area;
    const int c0x = static_cast<int>(std::floor(vis.left() / kChunk));
    const int c1x = static_cast<int>(std::floor(vis.right() / kChunk)) + 1;
    const int c0z = static_cast<int>(std::floor(vis.top() / kChunk));
    const int c1z = static_cast<int>(std::floor(vis.bottom() / kChunk)) + 1;

    if (m_layer.showSlime) {
        const int alpha = (std::max)(60, (std::min)(230, m_layer.slimeAlpha));
        const QColor fill(36, 214, 120, alpha);
        const QColor edge(16, 150, 78, (std::min)(255, alpha + 60));
        const double cell = kChunk * m_scale;
        for (int cz = c0z; cz <= c1z; ++cz) {
            for (int cx = c0x; cx <= c1x; ++cx) {
                if (!predicate(cx, cz)) continue;
                const QPointF tl = blockToPx(QPointF(cx * kChunk, cz * kChunk));
                const QPointF br = blockToPx(QPointF((cx + 1) * kChunk, (cz + 1) * kChunk));
                const QRectF rc(tl, br);
                p.fillRect(rc, fill);
                if (cell >= 10.0) {
                    p.setPen(QPen(edge, 1.5));
                    p.drawRect(rc.adjusted(0.5, 0.5, -0.5, -0.5));
                }
            }
        }
    }

    const double cell = kChunk * m_scale;
    // v3.6.15（用户反馈：拖动卡顿）：拖动中只画「群系 + 史莱姆区块」，
    //   网格线/环/结果/悬停这些装饰层等松手后再补 —— 每帧少几千次 drawLine。
    if (m_dragging) return;
    if (cell >= 6.0) {
        const QColor grid = dark ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 18);
        const QColor gridMajor = dark ? QColor(255, 255, 255, 46) : QColor(0, 0, 0, 46);
        for (int cx = c0x; cx <= c1x; ++cx) {
            const double x = blockToPx(QPointF(cx * kChunk, 0)).x();
            p.setPen(QPen((cx % 8 == 0) ? gridMajor : grid, 1));
            p.drawLine(QPointF(x, 0), QPointF(x, height()));
        }
        for (int cz = c0z; cz <= c1z; ++cz) {
            const double y = blockToPx(QPointF(0, cz * kChunk)).y();
            p.setPen(QPen((cz % 8 == 0) ? gridMajor : grid, 1));
            p.drawLine(QPointF(0, y), QPointF(width(), y));
        }
    }
}

void MapPreviewWidget::invalidateLayer()
{
    m_layerValid = false;
}

bool MapPreviewWidget::layerCoversView() const
{
    if (!m_layerValid || m_layerPixmap.isNull()) return false;
    if (std::abs(m_layerScale - m_scale) > 1e-9) return false;
    const QRectF vis = visibleBlockRect();
    return vis.left() >= m_layerX0 && vis.top() >= m_layerZ0 &&
           vis.right() <= m_layerX0 + m_layerW && vis.bottom() <= m_layerZ0 + m_layerH;
}

// v3.5.7：把底图渲染进 QPixmap（覆盖视口 + 25% 余量，平移时零重算）
void MapPreviewWidget::buildLayerPixmap()
{
    QElapsedTimer t;
    t.start();
    ensureBiomeImage();
    const QRectF vis = visibleBlockRect();
    const double marginX = vis.width() * kCacheMargin;
    const double marginZ = vis.height() * kCacheMargin;
    int x0 = static_cast<int>(std::floor(vis.left() - marginX));
    int z0 = static_cast<int>(std::floor(vis.top() - marginZ));
    int x1 = static_cast<int>(std::ceil(vis.right() + marginX));
    int z1 = static_cast<int>(std::ceil(vis.bottom() + marginZ));
    // 像素上限：单张 ≤ 4096×4096（避免 4K 屏 + 大余量爆内存）
    const double pxW = (x1 - x0) * m_scale;
    const double pxH = (z1 - z0) * m_scale;
    if (pxW > 4096.0 || pxH > 4096.0) {
        x0 = static_cast<int>(std::floor(vis.left()));
        z0 = static_cast<int>(std::floor(vis.top()));
        x1 = static_cast<int>(std::ceil(vis.right()));
        z1 = static_cast<int>(std::ceil(vis.bottom()));
    }
    const int wpx = (std::max)(1, static_cast<int>(std::ceil((x1 - x0) * m_scale)));
    const int hpx = (std::max)(1, static_cast<int>(std::ceil((z1 - z0) * m_scale)));
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(static_cast<int>(wpx * dpr), static_cast<int>(hpx * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(palette().base().color());
    {
        QPainter pp(&pm);
        pp.setRenderHint(QPainter::SmoothPixmapTransform, false);
        // 把「控件坐标」平移到 pixmap 坐标：pixmap 原点 = 当前视图下 (x0,z0) 的控件像素位置
        const QPointF origin = blockToPx(QPointF(x0, z0));
        pp.translate(-origin);
        drawBaseLayers(pp, QRectF(x0, z0, x1 - x0, z1 - z0));
    }
    m_layerPixmap = pm;
    m_layerX0 = x0;
    m_layerZ0 = z0;
    m_layerW = x1 - x0;
    m_layerH = z1 - z0;
    m_layerScale = m_scale;
    m_layerValid = true;
    m_lastBuildMs = t.nsecsElapsed() / 1000000;
    ++m_buildCount;
    if (qEnvironmentVariableIsSet("SLIME_MAP_PROFILE")) {
        std::fprintf(stderr, "[地图] 底图重建 %dx%d px = %lld ms（第 %d 次）\n", wpx, hpx,
                     static_cast<long long>(m_lastBuildMs), m_buildCount);
    }
}

void MapPreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QColor bg = palette().base().color();
    const bool dark = bg.lightness() < 128;
    p.fillRect(rect(), bg);
    const QRectF vis = visibleBlockRect();

    // 1) 底图：v3.6.14 一律实时绘制（群系走内存分块，史莱姆区块/网格/环/结果本来就是现算）
    //    ⇒ 平移多少都只补新分块，不存在「整张重建」的停顿。
    drawBaseLayers(p);

    // v3.5.28：占位提示（未填种子时）
    if (!m_placeholder.isEmpty()) {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.2);
        p.setFont(f);
        p.setPen(dark ? QColor(200, 200, 200) : QColor(90, 90, 90));
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
        return;
    }

    // 4) 搜索结果（环形模式下改用候选圆点表示，不画方块，避免杂乱）
    if (m_layer.showResults && m_afk.empty()) {
        p.setPen(QPen(QColor(236, 120, 26), 2));
        p.setBrush(QColor(236, 120, 26, 30));
        for (const auto& r : m_results) {
            const QPointF tl = blockToPx(QPointF(r.minX * kChunk, r.minZ * kChunk));
            const QPointF br = blockToPx(QPointF((r.maxX + 1) * kChunk, (r.maxZ + 1) * kChunk));
            p.drawRect(QRectF(tl, br));
        }
        p.setBrush(Qt::NoBrush);
    }

    // 5) 挂机点候选：v3.5.10 只给「选中」的候选画环，其余只留小圆点（避免多环叠加显得杂乱）
    if (m_layer.showAfk) {
        const QColor ringCol(26, 96, 196);
        const QColor ringLight(150, 200, 255);
        for (size_t i = 0; i < m_afk.size(); ++i) {
            const auto& c = m_afk[i];
            const QPointF ctr = blockToPx(QPointF(c.x + 0.5, c.z + 0.5));
            const bool focused = (static_cast<int>(i) == m_focusedAfk);
            if (!focused) {
                // 未选中：只标出环心（小红点），点击它可切换
                p.setPen(QPen(QColor(255, 255, 255, 150), 1.0));
                p.setBrush(QColor(206, 74, 66, 190));
                p.drawEllipse(ctr, 3.4, 3.4);
                p.setBrush(Qt::NoBrush);
                continue;
            }
            const double rad = m_layer.afkRadius * m_scale;
            const double radIn = m_layer.afkInnerRadius * m_scale;
            // 方案 A：细环双层描边（深色主环 + 浅色内描边），环带不填充 —— 不遮挡地图
            if (rad > 2.0 && rad < 20000.0) {
                p.setPen(QPen(ringCol, 3.6));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(ctr, rad, rad);
                p.setPen(QPen(ringLight, 1.0));
                p.drawEllipse(ctr, rad - 1.8, rad - 1.8);
            }
            // 环形 = 内圈（过近不刷）
            if (radIn > 2.0 && radIn < rad) {
                p.setPen(QPen(ringCol, 2.6));
                p.drawEllipse(ctr, radIn, radIn);
                p.setPen(QPen(ringLight, 1.0));
                p.drawEllipse(ctr, radIn - 1.4, radIn - 1.4);
            }
            // 环心：红色圆点（白色描边 + 柔光）
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 84));
            p.drawEllipse(ctr, 8.0, 8.0);
            p.setBrush(QColor(226, 58, 58));
            p.setPen(QPen(QColor(255, 255, 255), 2.0));
            p.drawEllipse(ctr, 4.6, 4.6);
            p.setBrush(Qt::NoBrush);
            // 标签 + 环带内史莱姆区块描边高亮
            p.setPen(QPen(ringCol, 1));
            const QString tag = m_layer.afkInnerRadius > 0
                                    ? slime::ui::t("ui.38461719f80b").arg(m_layer.afkInnerRadius).arg(m_layer.afkRadius)
                                    : QStringLiteral("R%1").arg(m_layer.afkRadius);
            p.drawText(ctr + QPointF(11, -11), tag);
                if (m_layer.afkInnerRadius > 0 && m_layer.showSlime) {
                    const int c0x = static_cast<int>((c.x - m_layer.afkRadius) / kChunk) - 1;
                    const int c1x = static_cast<int>((c.x + m_layer.afkRadius) / kChunk) + 1;
                    const int c0z = static_cast<int>((c.z - m_layer.afkRadius) / kChunk) - 1;
                    const int c1z = static_cast<int>((c.z + m_layer.afkRadius) / kChunk) + 1;
                    p.setPen(QPen(QColor(64, 255, 170, 220), 2.0));
                    p.setBrush(Qt::NoBrush);
                    const double in2 = static_cast<double>(m_layer.afkInnerRadius) * m_layer.afkInnerRadius;
                    const double out2 = static_cast<double>(m_layer.afkRadius) * m_layer.afkRadius;
                    for (int cz = c0z; cz <= c1z; ++cz) {
                        for (int cx = c0x; cx <= c1x; ++cx) {
                            if (!predicate(cx, cz)) continue;
                            const double bx = cx * 16.0, bz = cz * 16.0;
                            const double nx = (std::max)(bx, (std::min)(static_cast<double>(c.x), bx + 16.0));
                            const double nz = (std::max)(bz, (std::min)(static_cast<double>(c.z), bz + 16.0));
                            const double d2min = (nx - c.x) * (nx - c.x) + (nz - c.z) * (nz - c.z);
                            const double fx = (std::abs(bx - c.x) > std::abs(bx + 16.0 - c.x)) ? bx : bx + 16.0;
                            const double fz = (std::abs(bz - c.z) > std::abs(bz + 16.0 - c.z)) ? bz : bz + 16.0;
                            const double d2max = (fx - c.x) * (fx - c.x) + (fz - c.z) * (fz - c.z);
                            if (d2min > out2 || d2max < in2) continue;   // 不与环带相交
                            const QPointF tl = blockToPx(QPointF(bx, bz));
                            const QPointF br = blockToPx(QPointF(bx + 16.0, bz + 16.0));
                            p.drawRect(QRectF(tl, br).adjusted(1, 1, -1, -1));
                        }
                    }
                }
        }
    }

    // 5.5) v3.5.5：边界坐标刻度
    drawAxisLabels(p);

    // 6) 顶部信息条
    {
        const int nSlime = m_layer.showSlime ? visibleSlimeChunkCount() : 0;
        QString info = slime::ui::t("ui.7e44bfef8822")
                           .arg(static_cast<int>(m_center.x()))
                           .arg(static_cast<int>(m_center.y()))
                           .arg(QString::number(m_scale, 'f', 2))
                           .arg((std::max)(1, static_cast<int>(vis.width() / kChunk)))
                           .arg((std::max)(1, static_cast<int>(vis.height() / kChunk)));
        if (m_layer.showSlime) info += slime::ui::t("ui.e5c043489a23").arg(nSlime);
        if (m_hoverValid) {
            info += m_layer.biomeAvailable
                        ? slime::ui::t("ui.4a91204ff743").arg(m_hoverBlockX).arg(m_hoverBlockZ).arg(hoverBiomeName())
                        : slime::ui::t("ui.9d9295cec6de").arg(m_hoverBlockX).arg(m_hoverBlockZ);
        }
        const int eff = effectiveBiomeScale();
        if (eff > 0) info += slime::ui::t("ui.ae31190413fc").arg(eff).arg(m_layer.autoDetail ? slime::ui::t("ui.f08b84225ba0") : QString());
        // v3.6.11（用户要求：基岩版群系按社区方案近似，但要标清楚）
        if (eff > 0 && m_biomeParity) info += slime::ui::t("ui.8dd8df68d584");
        const QRectF box(8, 8, p.fontMetrics().horizontalAdvance(info) + 18, 24);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, dark ? 130 : 110));
        p.drawRoundedRect(box, 6, 6);
        p.setPen(QColor(255, 255, 255));
        p.drawText(box.adjusted(9, 0, -9, 0), Qt::AlignVCenter | Qt::AlignLeft, info);
    }

    // 7) 比例尺
    {
        const double targetPx = 120.0;
        double blocks = targetPx / m_scale;
        const double step = std::pow(10.0, std::floor(std::log10((std::max)(1.0, blocks))));
        blocks = std::ceil(blocks / step) * step;
        const double px = blocks * m_scale;
        const QPointF a(14, height() - 22), b(14 + px, height() - 22);
        p.setPen(QPen(QColor(255, 255, 255), 2));
        p.drawLine(a, b);
        p.drawLine(a, a + QPointF(0, -5));
        p.drawLine(b, b + QPointF(0, -5));
        p.setPen(QColor(255, 255, 255));
        p.drawText(QPointF(14, height() - 28), slime::ui::t("ui.24e51c8eb8e0").arg(static_cast<long long>(blocks)));
    }
}

void MapPreviewWidget::wheelEvent(QWheelEvent* ev)
{
    const double steps = ev->angleDelta().y() / 120.0;
    if (steps != 0.0) zoomAt(std::pow(1.2, steps), ev->position().toPoint());
    ev->accept();
}

void MapPreviewWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        // v3.5.10：点到某个候选的环心 → 只显示它的环（不启动拖动）
        const int hit = afkIndexAt(ev->pos());
        if (hit >= 0) {
            setFocusedAfk(hit);
            if (m_afkFocusHandler) m_afkFocusHandler(hit);
            ev->accept();
            return;
        }
        m_dragging = true;
        m_lastMouse = ev->pos();
        setCursor(Qt::ClosedHandCursor);
        ev->accept();
        return;
    }
    QWidget::mousePressEvent(ev);
}

void MapPreviewWidget::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_dragging) {
        const QPoint d = ev->pos() - m_lastMouse;
        m_lastMouse = ev->pos();
        m_center -= QPointF(d.x() / m_scale, d.y() / m_scale);
        m_slimeCountCache = -1;
        // v3.5.23（用户反馈：拖动不够流畅）：拖动中**不重采样、不重建底图**，
        //   只用已缓存的 pixmap 平移贴图；松手后统一补一次高质量重建。
        update();
        ev->accept();
        return;
    }
    // v3.5.10：悬停在环心上时给「可点击」的手型光标
    setCursor(afkIndexAt(ev->pos()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    // v3.5.5：悬停位置（非拖拽时）用于顶部信息条显示群系
    const QPointF b = pxToBlock(QPointF(ev->pos()));
    const int bx = static_cast<int>(std::floor(b.x()));
    const int bz = static_cast<int>(std::floor(b.y()));
    if (!m_hoverValid || bx != m_hoverBlockX || bz != m_hoverBlockZ) {
        m_hoverValid = true;
        m_hoverBlockX = bx;
        m_hoverBlockZ = bz;
        update();
    }
    QWidget::mouseMoveEvent(ev);
}

void MapPreviewWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        unsetCursor();
        scheduleResample();   // v3.5.23：拖动结束再补一次高质量重建
        ev->accept();
        return;
    }
    QWidget::mouseReleaseEvent(ev);
}

void MapPreviewWidget::mouseDoubleClickEvent(QMouseEvent* ev)
{
    resetView();
    ev->accept();
}

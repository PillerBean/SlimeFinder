// v3.5.3（地图优化）：地图预览控件
//
// 本版改动（针对「低版本群系帧率更高」的实测线索）：
//   1) 自适应精度：按缩放自动选 1:1 / 1:4 / 1:16 / 1:64（放大更精细），可关闭后手动指定；
//   2) 采样去抖 + 覆盖缓存：拖动/缩放先用已有底图平移显示，静止 120ms 后才重采样；
//      缓存带 25% 余量，小幅平移直接复用（旧版 1.18+ 每次都会整屏重采 ⇒ 卡）；
//   3) 史莱姆区块更醒目：加浓填充 + 描边，底图略降饱和；
//   4) 每帧不再重复统计史莱姆区块数（改为随填充循环一次算完并缓存）。
#pragma once

#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QTimer>
#include <QWidget>
#include <memory>
#include <functional>
#include <vector>

#include "core/AfkPlanner.h"
#include "core/BiomeMap.h"
#include "core/SlimeEngine.h"

class MapPreviewWidget : public QWidget {
public:
    struct Layer {
        int version = 0;     // 0 = Java, 1 = Bedrock
        int64_t seed = 0;
        bool showSlime = true;
        bool showResults = true;
        bool showAfk = true;
        bool showBiome = true;
        bool autoDetail = true;  // v3.5.3：自适应精度（关闭后用 biomeScale 手动值）
        int biomeScale = 16;     // 手动精度（autoDetail=false 时生效）
        bool biomeAvailable = false;
        int afkRadius = 128;
        int afkInnerRadius = 0;   // v3.5.8：环形内半径（0 = 整圆）
        int slimeAlpha = 150;    // v3.5.3：史莱姆区块不透明度（0~255），默认更醒目
    };

    explicit MapPreviewWidget(QWidget* parent = nullptr);

    void setLayer(const Layer& l);
    Layer layer() const noexcept { return m_layer; }

    // v3.5.22：把视图中心移到指定区块（搜索结果自动定位用）
    void centerOnChunk(int chunkX, int chunkZ);
    void setResults(const std::vector<slime::MatchResult>& r);
    void setAfkCandidates(const std::vector<slime::AfkCandidate>& c);
    // v3.5.10：只给「选中」的候选画环（点环心/结果行切换），其余只留小圆点
    void setFocusedAfk(int index);
    int focusedAfk() const noexcept { return m_focusedAfk; }
    void setAfkFocusHandler(std::function<void(int)> fn) { m_afkFocusHandler = std::move(fn); }
    // v3.5.28：无种子/无数据时的居中占位提示（空串=不显示）
    void setPlaceholderText(const QString& t) { if (m_placeholder == t) return; m_placeholder = t; update(); }
    QString placeholderText() const { return m_placeholder; }
    bool setBiomeSource(int versionId, int64_t seed);
    // v3.5.21：标记当前群系是否来自「基岩版按 Java 同种子推导」（1.18+ 两版群系一致）
    void setBiomeParityNote(bool bedrockParity) { m_biomeParity = bedrockParity; }
    bool biomeParityNote() const noexcept { return m_biomeParity; }
    int mapCacheHits() const noexcept { return m_tiles.size(); }   // v3.6.13：当前缓存块数
    int tilesBuilt() const noexcept { return static_cast<int>(m_mapTilesBuilt); }   // v3.6.13：累计新建块数

    void setView(double centerXBlock, double centerZBlock, double pxPerBlock);
    QPointF centerBlock() const noexcept { return m_center; }
    double pxPerBlock() const noexcept { return m_scale; }

    void zoomAt(double factor, const QPoint& atPx);
    void resetView();
    void fitToContent();
    // v3.5.4：立即重采底图（跳转/切参数时用，避免远距离跳转出现空白帧）
    void refreshNow();
    QRectF visibleBlockRect() const;
    int visibleSlimeChunkCount() const;

    // v3.5.3：当前生效的群系精度（自适应时为自动计算值；1:0 表示未启用）
    int effectiveBiomeScale() const;
    // v3.5.15：给定缩放下的群系精度（自检用；与 effectiveBiomeScale 同一套规则，但不看图层开关）
    int biomeScaleForScale(double pxPerBlock) const;
    // 统计：最近一次群系采样的耗时与单元格数（自检/排查帧率用）
    double lastSampleMs() const noexcept { return m_lastSampleMs; }
    int lastSampleCells() const noexcept { return m_lastSampleCells; }
    int sampleCount() const noexcept { return m_sampleCount; }

    const QImage& lastBiomeImage() const;   // v3.6.13：把内存分块拼成一张图（自检/截图用）
    // v3.5.7：渲染统计（自检/性能排查用）
    qint64 lastLayerBuildMs() const noexcept { return m_lastBuildMs; }
    int layerBuildCount() const noexcept { return m_buildCount; }

protected:
    void paintEvent(QPaintEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

private:
    QPointF blockToPx(const QPointF& b) const;
    QPointF pxToBlock(const QPointF& p) const;
    bool predicate(int chunkX, int chunkZ) const;
    void clampScale();
    void drawBiomeLayer(QPainter& p);
    void drawBaseLayers(QPainter& p, const QRectF& area = QRectF());   // v3.5.7：群系+史莱姆+网格（可画进缓存）
    bool layerCoversView() const;
    void buildLayerPixmap();
    void invalidateLayer();
    void drawAxisLabels(QPainter& p);   // v3.5.5：边界坐标刻度
    QString hoverBiomeName();           // v3.5.5：鼠标处群系名（带缓存）
    void invalidateBiomeCache();
    void scheduleResample();     // 去抖：静止后重采样
    void ensureBiomeImage();     // 覆盖缓存 + 余量采样

    Layer m_layer;
    QPointF m_center{ 0.0, 0.0 };
    double m_scale = 2.0;
    std::vector<slime::MatchResult> m_results;
    std::vector<slime::AfkCandidate> m_afk;
    int m_focusedAfk = -1;   // v3.5.10：当前选中的候选（-1 = 不画任何环）
    std::function<void(int)> m_afkFocusHandler;
    int afkIndexAt(const QPoint& px) const;   // v3.5.10：命中测试（点环心）
    bool m_dragging = false;
    QPoint m_lastMouse;
    // v3.5.5：鼠标悬停位置（方块坐标）+ 该处群系名缓存
    bool m_hoverValid = false;
    int m_hoverBlockX = 0;
    int m_hoverBlockZ = 0;
    int m_hoverCellKey = 0;
    QString m_placeholder;   // v3.5.28：居中占位提示
    bool m_biomeParity = false;   // v3.5.21：基岩版群系（按 Java 同种子推导）
    int m_hoverBiome = -1;

    // v3.6.13：内存分块缓存（128x128 格/块，按最近使用淘汰）
    struct BiomeTile { QImage img; int x0 = 0; int z0 = 0; qint64 stamp = 0; };
    QHash<quint64, BiomeTile> m_tiles;
    qint64 m_tileClock = 0;
    int m_tileDetail = 0;
    qint64 m_mapTilesBuilt = 0;

    std::unique_ptr<slime::BiomeMap> m_biome;
    int m_biomeVersionId = 0;   // v3.6.12：底图缓存 key
    int64_t m_biomeSeed = 0;
    int m_mapCacheHits() const noexcept { return m_tiles.size(); }   // v3.6.13：当前缓存块数
    mutable QImage m_diagImage;   // v3.6.13：分块拼图缓存（自检用）
    // v3.5.7：合成底图缓存（群系 + 史莱姆区块 + 网格），平移时直接贴图
    QPixmap m_layerPixmap;
    int m_layerX0 = 0, m_layerZ0 = 0, m_layerW = 0, m_layerH = 0; // 覆盖的方块矩形
    double m_layerScale = 0.0;                                    // 生成时的 px/格
    bool m_layerValid = false;
    qint64 m_lastBuildMs = 0;
    int m_buildCount = 0;
    QPixmap m_stalePixmap;   // 旧图（拖动/缩放时的过渡画面）
    double m_staleScale = 0.0;
    int m_staleX0 = 0, m_staleZ0 = 0, m_staleW = 0, m_staleH = 0;
    // v3.5.16：把缓存的底图 pixmap 拉伸到对应方块矩形（缩放时与网格/叠加层保持同步）
    void drawLayerPixmap(QPainter& p, const QPixmap& pm, int x0, int z0, int wBlocks, int hBlocks);
    // 缓存覆盖的方块矩形（含余量）与采样精度
    int m_cacheX0 = 0, m_cacheZ0 = 0, m_cacheW = 0, m_cacheH = 0, m_cacheDetail = 0;
    bool m_resamplePending = false;
    QTimer* m_resampleTimer = nullptr;
    double m_lastSampleMs = 0.0;
    int m_lastSampleCells = 0;
    int m_sampleCount = 0;
    // 史莱姆区块统计缓存
    mutable int m_slimeCountCache = -1;
    mutable double m_slimeCountScale = -1.0;
};

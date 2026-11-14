#include "gui/widgets/CacheCoverageMap.h"

#include "i18n/UiText.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPen>

#include <algorithm>
#include <cmath>

CacheCoverageMap::CacheCoverageMap(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(230);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void CacheCoverageMap::setPacks(const std::vector<slime::cache::CachePackEntry>& packs)
{
    m_packs = packs;
    if (!m_selected.empty()) {
        const bool exists = std::any_of(m_packs.begin(), m_packs.end(),
                                       [&](const auto& e) { return e.file == m_selected; });
        if (!exists) m_selected.clear();
    }
    update();
}

void CacheCoverageMap::setSelected(const std::string& file)
{
    if (m_selected == file) return;
    m_selected = file;
    update();
}

CacheCoverageMap::View CacheCoverageMap::view() const
{
    if (m_packs.empty()) return View{};
    double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;
    for (const auto& e : m_packs) {
        x0 = std::min(x0, static_cast<double>(e.rect.minX));
        x1 = std::max(x1, static_cast<double>(e.rect.maxX) + 1);
        z0 = std::min(z0, static_cast<double>(e.rect.minZ));
        z1 = std::max(z1, static_cast<double>(e.rect.maxZ) + 1);
    }
    const double padX = std::max(256.0, (x1 - x0) * 0.10);
    const double padZ = std::max(256.0, (z1 - z0) * 0.10);
    return View{ x0 - padX, x1 + padX, z0 - padZ, z1 + padZ };
}

QPointF CacheCoverageMap::toScreen(double x, double z) const
{
    const View v = view();
    const double w = std::max(1, width());
    const double h = std::max(1, height());
    return QPointF((x - v.x0) / (v.x1 - v.x0) * w,
                   h - (z - v.z0) / (v.z1 - v.z0) * h);
}

void CacheCoverageMap::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QPalette& pal = palette();
    const QColor bg = pal.color(QPalette::Base);
    const QColor grid = pal.color(QPalette::Midlight);
    const QColor text = pal.color(QPalette::Text);

    p.fillRect(rect(), bg);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(grid, 1));
    for (int i = 1; i < 10; ++i) {
        const int x = width() * i / 10;
        const int y = height() * i / 10;
        p.drawLine(x, 0, x, height());
        p.drawLine(0, y, width(), y);
    }

    const View v = view();
    // 原点轴
    const QPointF origin = toScreen(0, 0);
    p.setPen(QPen(grid, 1, Qt::DashLine));
    p.drawLine(QPointF(0, origin.y()), QPointF(width(), origin.y()));
    p.drawLine(QPointF(origin.x(), 0), QPointF(origin.x(), height()));

    if (m_packs.empty()) {
        p.setPen(text);
        p.drawText(rect(), Qt::AlignCenter, slime::ui::t("ui.cache.map.noCoverage"));
        return;
    }

    for (const auto& e : m_packs) {
        const QPointF a = toScreen(e.rect.minX, e.rect.minZ);
        const QPointF b = toScreen(static_cast<double>(e.rect.maxX) + 1,
                                   static_cast<double>(e.rect.maxZ) + 1);
        const QRectF r(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                       QPointF(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
        const bool local = e.source == "local";
        const bool selected = !m_selected.empty() && e.file == m_selected;
        QColor fill = local ? QColor(103, 179, 123) : QColor(91, 145, 200);
        fill.setAlpha(selected ? 150 : 75);
        p.fillRect(r, fill);
        p.setPen(QPen(selected ? QColor(216, 163, 62) : fill.darker(110), selected ? 2 : 1));
        p.drawRect(r);
        if (r.width() > 88 && r.height() > 22) {
            p.setPen(text);
            QFont f = font();
            f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.5));
            p.setFont(f);
            p.drawText(r.adjusted(5, 3, -4, -3), Qt::AlignLeft | Qt::AlignTop,
                       (local ? slime::ui::t("ui.cache.map.local") : slime::ui::t("ui.cache.map.download")) +
                           QString::number(static_cast<double>(e.compressedBytes) / (1024.0 * 1024.0), 'f', 1) +
                           QStringLiteral(" MB"));
        }
    }

    p.setPen(text);
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.5));
    p.setFont(f);
    p.drawText(QRect(6, height() - 18, width() - 12, 16), Qt::AlignLeft,
               QStringLiteral("x %1 … %2 ｜ z %3 … %4")
                   .arg(static_cast<qint64>(std::llround(v.x0)))
                   .arg(static_cast<qint64>(std::llround(v.x1)))
                   .arg(static_cast<qint64>(std::llround(v.z0)))
                   .arg(static_cast<qint64>(std::llround(v.z1))));
}

void CacheCoverageMap::mousePressEvent(QMouseEvent* ev)
{
    if (m_packs.empty()) return;
    const View v = view();
    const double x = v.x0 + (ev->position().x() / std::max(1, width())) * (v.x1 - v.x0);
    const double z = v.z0 + ((height() - ev->position().y()) / std::max(1, height())) * (v.z1 - v.z0);
    for (auto it = m_packs.rbegin(); it != m_packs.rend(); ++it) {
        if (x >= it->rect.minX && x <= it->rect.maxX && z >= it->rect.minZ && z <= it->rect.maxZ) {
            setSelected(it->file);
            if (onSelected) onSelected(it->file);
            return;
        }
    }
    setSelected(std::string());
    if (onSelected) onSelected(std::string());
}

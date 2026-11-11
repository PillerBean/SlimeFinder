// CacheCoverageMap.h - v3.12.3：缓存页覆盖地图（本地/下载 pack 矩形）
#pragma once

#include <QWidget>

#include "cache/CacheStore.h"

#include <functional>
#include <string>
#include <vector>

class QMouseEvent;

class CacheCoverageMap : public QWidget {
public:
    explicit CacheCoverageMap(QWidget* parent = nullptr);

    void setPacks(const std::vector<slime::cache::CachePackEntry>& packs);
    void setSelected(const std::string& file);
    std::string selected() const { return m_selected; }
    std::function<void(const std::string&)> onSelected;

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;

private:
    struct View {
        double x0 = -4096, x1 = 4096, z0 = -4096, z1 = 4096;
    };
    View view() const;
    QPointF toScreen(double x, double z) const;

    std::vector<slime::cache::CachePackEntry> m_packs;
    std::string m_selected;
};

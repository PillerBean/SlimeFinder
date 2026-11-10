// v3.5.2（任务 2）：地图预览对话框（群系彩色底图 + 版本/精度选择）
#pragma once

#include <QDialog>
#include <QString>
#include <vector>

#include "core/AfkPlanner.h"
#include "core/BiomeMap.h"
#include "core/SlimeEngine.h"

class QLabel;
class QSpinBox;
class QCheckBox;
class QComboBox;
class MapPreviewWidget;

class MapDialog : public QDialog {
public:
    struct Context {
        int version = 0;     // 0 = Java, 1 = Bedrock
        int64_t seed = 0;
        int minX = 0, maxX = 0, minZ = 0, maxZ = 0; // 搜索范围（区块）
        QString source;
    };

    explicit MapDialog(const Context& ctx, QWidget* parent = nullptr);

    MapPreviewWidget* map() const noexcept { return m_map; }
    void setResults(const std::vector<slime::MatchResult>& r);
    void setAfkCandidates(const std::vector<slime::AfkCandidate>& c);
    const std::vector<slime::AfkCandidate>& afk() const noexcept { return m_afk; }
    void computeAfk();
    int currentVersionId() const;
    bool biomeAvailable() const;

private:
    void refreshStatus();
    void initBiomeSource();

    Context m_ctx;
    MapPreviewWidget* m_map = nullptr;
    QLabel* m_status = nullptr;
    QSpinBox* m_radius = nullptr;
    QCheckBox* m_cbBiome = nullptr;
    QCheckBox* m_cbSlime = nullptr;
    QCheckBox* m_cbResults = nullptr;
    QCheckBox* m_cbAfk = nullptr;
    QComboBox* m_versionCombo = nullptr;
    QComboBox* m_scaleCombo = nullptr;
    std::vector<slime::AfkCandidate> m_afk;
};

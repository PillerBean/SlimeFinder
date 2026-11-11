// v3.5.2（任务 1 的界面）：挂机点（环形覆盖）对话框
//
// 输入：游戏版本 / 世界种子 / 候选范围（区块）
// 输出：按「半径 R 内覆盖的史莱姆格数」排序的挂机点，可一键复制 /tp 命令。
#pragma once

#include <QDialog>
#include <QString>
#include <vector>

#include "core/AfkPlanner.h"
#include "core/BiomeMap.h"
#include "core/SpawnWeights.h"

class QLabel;
class QSpinBox;
class QComboBox;
class QTableWidget;

class AfkDialog : public QDialog {
public:
    struct Context {
        int version = 0;      // 0 = Java, 1 = Bedrock（与 slime::Version 对应）
        int64_t seed = 0;     // 仅 Java 使用
        int minX = 0, maxX = 0, minZ = 0, maxZ = 0; // 候选范围（区块，含边界）
        QString source;       // 说明：范围从哪里来（当前结果 / 搜索范围）
    };

    explicit AfkDialog(const Context& ctx, QWidget* parent = nullptr);

    // 重新计算（半径/候选数改变时调用）
    void recompute();

    const std::vector<slime::AfkCandidate>& results() const noexcept { return m_results; }
    QTableWidget* table() const noexcept { return m_table; }

    // 供复制/自检使用：/tp 命令与一行摘要文本
    static QString commandFor(const slime::AfkCandidate& c);
    static QString cellTextFor(const slime::AfkCandidate& c);

private:
    void fillTable();

    Context m_ctx;
    QSpinBox* m_radius = nullptr;
    QComboBox* m_platform = nullptr;   // v3.5.2（任务 3）：Java / 基岩
    QComboBox* m_version = nullptr;    // v3.5.2（任务 3）：群系版本（Java）
    slime::BiomeMap m_biome;
    bool m_weighted = false;
    double weightAtBlock(int blockX, int blockZ);
    QSpinBox* m_topN = nullptr;
    QLabel* m_summary = nullptr;
    QTableWidget* m_table = nullptr;
    std::vector<slime::AfkCandidate> m_results;
};
